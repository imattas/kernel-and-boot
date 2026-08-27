#include "nvme.h"
#include "../../device/device.h"
#include "../pci/pci.h"
#include "../../mm/physical/frame.h"
#include "../../core/sync/spinlock.h"
#include "../../arch/x86_64/cpu/tables.h"

#define NVME_BAR_INDEX 0
#define NVME_CAP 0x00
#define NVME_CC 0x14
#define NVME_CSTS 0x1c
#define NVME_AQA 0x24
#define NVME_ASQ 0x28
#define NVME_ACQ 0x30
#define NVME_CC_EN (1U << 0)
#define NVME_CC_IOSQES 6
#define NVME_CC_IOCQES 4
#define NVME_CSTS_RDY (1U << 0)
#define NVME_CAP_MQES_MASK 0xffffU
#define NVME_CAP_DSTRD_SHIFT 32
#define NVME_ADMIN_SQ_DOORBELL 0x1000
#define NVME_TIMEOUT_ATTEMPTS 10000000U
#define NVME_IRQ_VECTOR 0x53

extern void arch_nvme_irq_stub(void);

static uint32_t controllers;
static volatile uint32_t *active_regs;
static uint64_t active_asq;
static uint64_t active_acq;
static uint16_t active_queue_entries;
static uint16_t active_sq_tail;
static uint16_t active_cq_head;
static uint8_t active_cq_phase;
static uint16_t active_command_id;
static uint32_t active_doorbell_stride;
static uint64_t active_io_sq;
static uint64_t active_io_cq;
static uint16_t active_io_sq_tail;
static uint16_t active_io_cq_head;
static uint8_t active_io_cq_phase;
static uint8_t active_io_ready;
static spinlock_t nvme_lock;
static device_driver_t nvme_driver;
static int nvme_irq_enabled;
static volatile uint32_t nvme_interrupts;

static int nvme_submit_admin_words(uint8_t opcode, uint32_t namespace_id,
                                   uint64_t prp1, const uint32_t words[6],
                                   uint32_t *result);
static int nvme_initialize_io(void);

static int nvme_match(const device_t *device) {
    return device && device->bus == DEVICE_BUS_PCI &&
           device->class_code == 0x01 && device->subclass == 0x08;
}

static int nvme_wait_ready(volatile uint32_t *regs, uint32_t expected) {
    for (uint32_t i = 0; i < NVME_TIMEOUT_ATTEMPTS; ++i)
        if ((regs[NVME_CSTS / 4] & NVME_CSTS_RDY) == expected) return 1;
    return 0;
}

static void nvme_abort_controller(void) {
    if (!active_regs) return;
    active_regs[NVME_CC / 4] &= ~NVME_CC_EN;
    (void)nvme_wait_ready(active_regs, 0);
    active_io_ready = 0;
}

static int nvme_probe(device_t *device) {
    if (!device || device->resources[NVME_BAR_INDEX].size < 0x1000 ||
        device->resources[NVME_BAR_INDEX].address == 0 ||
        device->resources[NVME_BAR_INDEX].address >= 0x100000000ULL ||
        (device->resources[NVME_BAR_INDEX].flags & 1U) != 0 ||
        !device_claim_resource(device, NVME_BAR_INDEX, &nvme_driver)) return 0;
    uint64_t asq = 0;
    uint64_t acq = 0;
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)
        device->resources[NVME_BAR_INDEX].address;
    uint32_t cc = regs[NVME_CC / 4] & ~NVME_CC_EN;
    regs[NVME_CC / 4] = cc;
    if (!nvme_wait_ready(regs, 0)) goto fail;
    uint64_t cap = ((uint64_t)regs[1] << 32) | regs[0];
    uint32_t queue_entries = (uint32_t)(cap & NVME_CAP_MQES_MASK) + 1U;
    if (queue_entries > 64) queue_entries = 64;
    if (queue_entries == 0) goto fail;
    asq = physical_alloc_frame();
    acq = physical_alloc_frame();
    if (!asq || !acq) goto fail;
    regs[NVME_AQA / 4] = (queue_entries - 1U) |
                         ((queue_entries - 1U) << 16);
    ((volatile uint32_t *)regs)[NVME_ASQ / 4] = (uint32_t)asq;
    ((volatile uint32_t *)regs)[(NVME_ASQ / 4) + 1] = 0;
    ((volatile uint32_t *)regs)[NVME_ACQ / 4] = (uint32_t)acq;
    ((volatile uint32_t *)regs)[(NVME_ACQ / 4) + 1] = 0;
    regs[NVME_CC / 4] = cc | NVME_CC_EN |
        (NVME_CC_IOCQES << 20) | (NVME_CC_IOSQES << 16);
    if (!nvme_wait_ready(regs, NVME_CSTS_RDY)) goto fail;
    active_regs = regs;
    active_asq = asq;
    active_acq = acq;
    active_queue_entries = (uint16_t)queue_entries;
    active_sq_tail = 0;
    active_cq_head = 0;
    active_cq_phase = 1;
    active_command_id = 0;
    active_doorbell_stride = 4U << ((cap >> NVME_CAP_DSTRD_SHIFT) & 0xfU);
    active_io_sq = 0;
    active_io_cq = 0;
    active_io_ready = 0;
    arch_set_interrupt_gate(NVME_IRQ_VECTOR, arch_nvme_irq_stub);
    nvme_irq_enabled = pci_enable_msi(device, NVME_IRQ_VECTOR);
    if (!nvme_irq_enabled)
        nvme_irq_enabled = pci_enable_legacy_irq(device, NVME_IRQ_VECTOR);
    if (!nvme_initialize_io()) goto fail;
    ++controllers;
    return 1;
fail:
    if (asq) physical_free_frame(asq);
    if (acq) physical_free_frame(acq);
    active_regs = 0; active_asq = 0; active_acq = 0;
    active_io_sq = 0; active_io_cq = 0; active_io_ready = 0;
    nvme_irq_enabled = 0;
    device_release_resource(device, NVME_BAR_INDEX, &nvme_driver);
    return 0;
}

typedef struct {
    uint32_t command;
    uint32_t namespace_id;
    uint32_t reserved[2];
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t command_dwords[6];
} __attribute__((packed)) nvme_command_t;

typedef struct {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed)) nvme_completion_t;

static int nvme_submit_admin_words(uint8_t opcode, uint32_t namespace_id,
                                   uint64_t prp1, const uint32_t words[6],
                                   uint32_t *result) {
    if (!active_regs || !active_asq || !active_acq || !active_queue_entries ||
        !result) return 0;
    uint64_t flags = spinlock_lock_irqsave(&nvme_lock);
    volatile nvme_command_t *sq = (volatile nvme_command_t *)(uintptr_t)active_asq;
    volatile nvme_completion_t *cq =
        (volatile nvme_completion_t *)(uintptr_t)active_acq;
    nvme_command_t *command = (nvme_command_t *)&sq[active_sq_tail];
    for (uint32_t i = 0; i < sizeof(*command) / sizeof(uint32_t); ++i)
        ((volatile uint32_t *)command)[i] = 0;
    uint16_t command_id = active_command_id++;
    command->command = opcode | ((uint32_t)command_id << 16);
    command->namespace_id = namespace_id;
    command->prp1 = prp1;
    for (uint32_t i = 0; i < 6; ++i)
        command->command_dwords[i] = words ? words[i] : 0;
    active_sq_tail = (uint16_t)((active_sq_tail + 1U) % active_queue_entries);
    volatile uint32_t *sq_tail_doorbell =
        (volatile uint32_t *)((uintptr_t)active_regs + NVME_ADMIN_SQ_DOORBELL);
    *sq_tail_doorbell = active_sq_tail;
    for (uint32_t wait = 0; wait < NVME_TIMEOUT_ATTEMPTS; ++wait) {
        volatile nvme_completion_t *completion = &cq[active_cq_head];
        uint16_t status = completion->status;
        if ((status & 1U) != active_cq_phase) continue;
        if (completion->command_id != command_id) continue;
        *result = completion->result;
        int success = (status >> 1) == 0;
        ++active_cq_head;
        if (active_cq_head == active_queue_entries) {
            active_cq_head = 0;
            active_cq_phase ^= 1U;
        }
        volatile uint32_t *cq_head_doorbell =
            (volatile uint32_t *)((uintptr_t)active_regs +
                                  NVME_ADMIN_SQ_DOORBELL + active_doorbell_stride);
        *cq_head_doorbell = active_cq_head;
        spinlock_unlock_irqrestore(&nvme_lock, flags);
        return success;
    }
    nvme_abort_controller();
    spinlock_unlock_irqrestore(&nvme_lock, flags);
    return 0;
}

int nvme_admin_command(uint8_t opcode, uint32_t namespace_id,
                       uint64_t prp1, uint32_t *result) {
    return nvme_submit_admin_words(opcode, namespace_id, prp1, 0, result);
}

int nvme_identify_controller(void *buffer) {
    if (!buffer) return 0;
    uint64_t frame = physical_alloc_frame();
    if (!frame) return 0;
    uint32_t result = 0;
    const uint32_t words[6] = {1, 0, 0, 0, 0, 0};
    int success = nvme_submit_admin_words(0x06, 0, frame, words, &result);
    if (success) {
        const uint8_t *source = (const uint8_t *)(uintptr_t)frame;
        uint8_t *destination = (uint8_t *)buffer;
        for (uint32_t i = 0; i < 4096; ++i) destination[i] = source[i];
    }
    physical_free_frame(frame);
    return success;
}

int nvme_identify_namespace(void *buffer) {
    if (!buffer) return 0;
    uint64_t frame = physical_alloc_frame();
    if (!frame) return 0;
    uint32_t result = 0;
    const uint32_t words[6] = {0, 0, 0, 0, 0, 0};
    int success = nvme_submit_admin_words(0x06, 1, frame, words, &result);
    if (success) {
        const uint8_t *source = (const uint8_t *)(uintptr_t)frame;
        uint8_t *destination = (uint8_t *)buffer;
        for (uint32_t i = 0; i < 4096; ++i) destination[i] = source[i];
    }
    physical_free_frame(frame);
    return success;
}

static int nvme_initialize_io(void) {
    if (!active_regs || !active_queue_entries) return 0;
    uint64_t io_sq = physical_alloc_frame();
    uint64_t io_cq = physical_alloc_frame();
    if (!io_sq || !io_cq) {
        if (io_sq) physical_free_frame(io_sq);
        if (io_cq) physical_free_frame(io_cq);
        return 0;
    }
    for (uint32_t i = 0; i < 4096 / 4; ++i) {
        ((uint32_t *)(uintptr_t)io_sq)[i] = 0;
        ((uint32_t *)(uintptr_t)io_cq)[i] = 0;
    }
    uint32_t create_cq[6] = {1U | ((uint32_t)(active_queue_entries - 1U) << 16),
                             1U, 0, 0, 0, 0};
    uint32_t result = 0;
    if (!nvme_submit_admin_words(0x05, 0, io_cq, create_cq, &result)) goto fail;
    uint32_t create_sq[6] = {1U | ((uint32_t)(active_queue_entries - 1U) << 16),
                             1U | (1U << 16), 0, 0, 0, 0};
    if (!nvme_submit_admin_words(0x01, 0, io_sq, create_sq, &result)) goto fail;
    active_io_sq = io_sq;
    active_io_cq = io_cq;
    active_io_sq_tail = 0;
    active_io_cq_head = 0;
    active_io_cq_phase = 1;
    active_io_ready = 1;
    return 1;
fail:
    physical_free_frame(io_sq);
    physical_free_frame(io_cq);
    return 0;
}

static int nvme_io(uint64_t lba, void *buffer, uint32_t count, int write) {
    if (!active_io_ready || !buffer || count == 0 || count > 8) return 0;
    uint64_t flags = spinlock_lock_irqsave(&nvme_lock);
    uint64_t data_frame = physical_alloc_frame();
    if (!data_frame) {
        spinlock_unlock_irqrestore(&nvme_lock, flags);
        return 0;
    }
    volatile nvme_command_t *sq =
        (volatile nvme_command_t *)(uintptr_t)active_io_sq;
    volatile nvme_completion_t *cq =
        (volatile nvme_completion_t *)(uintptr_t)active_io_cq;
    volatile nvme_command_t *command = &sq[active_io_sq_tail];
    for (uint32_t i = 0; i < sizeof(*command) / sizeof(uint32_t); ++i)
        ((volatile uint32_t *)command)[i] = 0;
    uint16_t command_id = active_command_id++;
    command->command = 0x02U | ((uint32_t)command_id << 16);
    command->namespace_id = 1;
    command->prp1 = data_frame;
    if (write) {
        const uint8_t *source = (const uint8_t *)buffer;
        uint8_t *destination = (uint8_t *)(uintptr_t)data_frame;
        for (uint32_t i = 0; i < count * 512U; ++i) destination[i] = source[i];
    }
    command->command = (write ? 0x01U : 0x02U) |
                       ((uint32_t)command_id << 16);
    command->command_dwords[0] = (uint32_t)lba;
    command->command_dwords[1] = (uint32_t)(lba >> 32);
    command->command_dwords[2] = count - 1U;
    active_io_sq_tail = (uint16_t)((active_io_sq_tail + 1U) % active_queue_entries);
    volatile uint32_t *sq_doorbell =
        (volatile uint32_t *)((uintptr_t)active_regs +
                              NVME_ADMIN_SQ_DOORBELL + 2U * active_doorbell_stride);
    *sq_doorbell = active_io_sq_tail;
    int success = 0;
    int completed = 0;
    for (uint32_t wait = 0; wait < NVME_TIMEOUT_ATTEMPTS; ++wait) {
        volatile nvme_completion_t *completion = &cq[active_io_cq_head];
        uint16_t status = completion->status;
        if ((status & 1U) != active_io_cq_phase ||
            completion->command_id != command_id) continue;
        success = (status >> 1) == 0;
        completed = 1;
        ++active_io_cq_head;
        if (active_io_cq_head == active_queue_entries) {
            active_io_cq_head = 0;
            active_io_cq_phase ^= 1U;
        }
        volatile uint32_t *cq_doorbell =
            (volatile uint32_t *)((uintptr_t)active_regs +
                                  NVME_ADMIN_SQ_DOORBELL + 3U * active_doorbell_stride);
        *cq_doorbell = active_io_cq_head;
        break;
    }
    if (success && !write) {
        const uint8_t *source = (const uint8_t *)(uintptr_t)data_frame;
        uint8_t *destination = (uint8_t *)buffer;
        for (uint32_t i = 0; i < count * 512U; ++i) destination[i] = source[i];
    }
    if (!completed) nvme_abort_controller();
    physical_free_frame(data_frame);
    spinlock_unlock_irqrestore(&nvme_lock, flags);
    return success;
}

int nvme_read_sector(uint64_t lba, void *buffer) {
    return nvme_io(lba, buffer, 1, 0);
}

int nvme_write_sector(uint64_t lba, const void *buffer) {
    return nvme_io(lba, (void *)buffer, 1, 1);
}

int nvme_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    return nvme_io(lba, buffer, count, 0);
}

int nvme_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    return nvme_io(lba, (void *)buffer, count, 1);
}

int nvme_initialize(void) {
    controllers = 0;
    active_regs = 0;
    active_asq = 0;
    active_acq = 0;
    active_queue_entries = 0;
    nvme_irq_enabled = 0;
    nvme_interrupts = 0;
    spinlock_init(&nvme_lock);
    nvme_driver.name = "nvme";
    nvme_driver.bus = DEVICE_BUS_PCI;
    nvme_driver.match = nvme_match;
    nvme_driver.probe = nvme_probe;
    return device_driver_register(&nvme_driver) && device_bind_drivers();
}

uint32_t nvme_controller_count(void) { return controllers; }
int nvme_interrupt_enabled(void) { return nvme_irq_enabled; }
uint32_t nvme_interrupt_count(void) { return nvme_interrupts; }
void nvme_interrupt_handler(void) { ++nvme_interrupts; }
