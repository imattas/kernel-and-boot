#include "ahci.h"
#include "../../device/device.h"
#include "../../mm/physical/frame.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../pci/pci.h"
#include "../storage/storage.h"
#include "../../core/sync/spinlock.h"

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_SATA 0x06
#define AHCI_BAR_INDEX 5
#define AHCI_GHC_OFFSET 0x04
#define AHCI_PI_OFFSET 0x0c
#define AHCI_GHC_AE (1U << 31)
#define AHCI_PORT_BASE 0x100
#define AHCI_PORT_STRIDE 0x80
#define AHCI_PORT_CLB 0x00
#define AHCI_PORT_FB 0x08
#define AHCI_PORT_CMD 0x18
#define AHCI_PORT_SSTS 0x28
#define AHCI_PORT_SERR 0x30
#define AHCI_PORT_CI 0x38
#define AHCI_CMD_ST (1U << 0)
#define AHCI_CMD_FRE (1U << 4)
#define AHCI_CMD_CR (1U << 15)
#define AHCI_CMD_FR (1U << 14)
#define AHCI_PORT_TFD 0x20
#define AHCI_PORT_SIG 0x24
#define AHCI_PORT_IS 0x10
#define AHCI_PORT_IE 0x14
#define AHCI_PORT_PRDBC 0x04
#define AHCI_ATA_STATUS_BSY 0x80U
#define AHCI_ATA_STATUS_DRQ 0x08U
#define AHCI_ATA_STATUS_ERR 0x01U
#define AHCI_SIGNATURE_SATA 0x00000101U
#define AHCI_ATA_READ_DMA_EXT 0x25U
#define AHCI_ATA_WRITE_DMA_EXT 0x35U
#define AHCI_ATA_FLUSH_CACHE 0xe7U
#define AHCI_ATA_FLUSH_CACHE_EXT 0xeaU
#define AHCI_MAX_LBA 0x0000ffffffffffffULL
#define AHCI_LEGACY_MAX_LBA 0x0fffffffULL
#define AHCI_IRQ_VECTOR 0x54
#define AHCI_GHC_IE (1U << 1)
#define AHCI_PORT_IS_ERROR_MASK ((1U << 4) | (1U << 5) | (1U << 7) | \
                                 (1U << 22) | (1U << 23) | (1U << 24) | \
                                 (1U << 25) | (1U << 26) | (1U << 27) | \
                                 (1U << 28) | (1U << 30) | (1U << 31))
#define AHCI_PORT_IRQ_MASK (0x0000001fU | AHCI_PORT_IS_ERROR_MASK)
#define AHCI_PORT_REGISTER_SIZE 0x40U

extern void arch_ahci_irq_stub(void);

static void ahci_dma_write_barrier(void) {
    __asm__ volatile ("sfence" ::: "memory");
}

static void ahci_dma_read_barrier(void) {
    __asm__ volatile ("lfence" ::: "memory");
}

static uint32_t controllers;
static uint32_t ports;
static uint32_t ready_ports;
static uint32_t ready_port_mask;
typedef struct {
    volatile uint32_t *regs;
    uint64_t command_list;
    uint64_t fis;
    uint64_t sector_count;
    int lba48;
    int identified;
} ahci_port_state_t;
static ahci_port_state_t port_state[32];
static volatile uint32_t *active_port;
static uint64_t active_command_list;
static uint64_t active_command_table;
static uint64_t active_data;
static uint32_t active_data_pages;
static uint32_t ahci_last_prdt_length;
static device_driver_t ahci_driver;
static volatile uint32_t *active_abar;
static uint32_t active_port_number;
static int ahci_irq_enabled;
static volatile uint32_t ahci_interrupts;
static volatile uint32_t ahci_pending_port_status[32];
static spinlock_t ahci_lock;
static int ahci_io_disabled;
static volatile uint32_t ahci_errors;
static volatile uint32_t ahci_last_task_file;
static volatile uint32_t ahci_last_interrupt_status_value;
static volatile uint32_t ahci_last_serial_error_value;
static uint64_t active_sector_count;
static int active_lba48;
static int ahci_storage_registered;
static int ahci_identify_ready_ports_locked(void);

static void ahci_select_port_locked(uint32_t port) {
    active_port = port_state[port].regs;
    active_port_number = port;
    active_command_list = port_state[port].command_list;
    active_sector_count = port_state[port].sector_count;
    active_lba48 = port_state[port].lba48;
}

static int ahci_lba_valid(uint64_t lba, uint32_t count) {
    uint64_t maximum = active_lba48 ? AHCI_MAX_LBA : AHCI_LEGACY_MAX_LBA;
    return active_sector_count != 0 && lba <= maximum && count != 0 &&
           lba < active_sector_count &&
           (uint64_t)count <= active_sector_count - lba;
}

static int ahci_stop_engine(void) {
    if (!active_port) return 0;
    active_port[AHCI_PORT_CMD / 4] &= ~(AHCI_CMD_ST | AHCI_CMD_FRE);
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if ((active_port[AHCI_PORT_CMD / 4] & (AHCI_CMD_CR | AHCI_CMD_FR)) == 0) {
            active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
            active_port[AHCI_PORT_SERR / 4] = UINT32_MAX;
            active_port[AHCI_PORT_CMD / 4] |= AHCI_CMD_FRE | AHCI_CMD_ST;
            return 1;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

static void ahci_release_command_buffers(void) {
    if (active_command_table) physical_free_frame(active_command_table);
    if (active_data && active_data_pages)
        physical_free_frames(active_data, active_data_pages);
    active_command_table = 0;
    active_data = 0;
    active_data_pages = 0;
}

static int ahci_finish_command(int completed) {
    if (!completed && !ahci_stop_engine()) {
        /* A wedged HBA may still fetch these buffers; retain them safely. */
        ahci_io_disabled = 1;
        if (active_port) active_port[AHCI_PORT_IE / 4] = 0;
        return 0;
    }
    ahci_release_command_buffers();
    return completed;
}

static int ahci_command_ok(uint32_t task_file, uint32_t transferred,
                           uint32_t expected) {
    uint32_t interrupt_status = active_port[AHCI_PORT_IS / 4];
    interrupt_status |= __atomic_exchange_n(
        &ahci_pending_port_status[active_port_number], 0U, __ATOMIC_ACQUIRE);
    uint32_t serial_error = active_port[AHCI_PORT_SERR / 4];
    __atomic_store_n(&ahci_last_task_file, task_file, __ATOMIC_RELEASE);
    __atomic_store_n(&ahci_last_interrupt_status_value, interrupt_status,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&ahci_last_serial_error_value, serial_error,
                     __ATOMIC_RELEASE);
    if (interrupt_status & AHCI_PORT_IS_ERROR_MASK)
        active_port[AHCI_PORT_IS / 4] =
            interrupt_status & AHCI_PORT_IS_ERROR_MASK;
    if (serial_error) active_port[AHCI_PORT_SERR / 4] = serial_error;
    int success = (task_file & (AHCI_ATA_STATUS_BSY |
                                AHCI_ATA_STATUS_DRQ |
                                AHCI_ATA_STATUS_ERR)) == 0 &&
           transferred == expected &&
           (interrupt_status & AHCI_PORT_IS_ERROR_MASK) == 0 &&
           serial_error == 0;
    if (!success) __atomic_fetch_add(&ahci_errors, 1U, __ATOMIC_RELAXED);
    return success;
}

static int ahci_match(const device_t *device) {
    return device && device->bus == DEVICE_BUS_PCI &&
           device->class_code == PCI_CLASS_MASS_STORAGE &&
           device->subclass == PCI_SUBCLASS_SATA;
}

static int ahci_probe(device_t *device) {
    if (!device || device->resources[AHCI_BAR_INDEX].size < AHCI_PORT_BASE ||
        device->resources[AHCI_BAR_INDEX].address == 0 ||
        device->resources[AHCI_BAR_INDEX].address >= 0x100000000ULL ||
        (device->resources[AHCI_BAR_INDEX].flags & 1U) != 0 ||
        !device_claim_resource(device, AHCI_BAR_INDEX, &ahci_driver)) return 0;
    volatile uint32_t *abar = (volatile uint32_t *)(uintptr_t)
        device->resources[AHCI_BAR_INDEX].address;
    abar[AHCI_GHC_OFFSET / sizeof(uint32_t)] |= AHCI_GHC_AE;
    uint32_t implemented_ports = abar[AHCI_PI_OFFSET / sizeof(uint32_t)];
    if (implemented_ports == 0) {
        device_release_resource(device, AHCI_BAR_INDEX, &ahci_driver);
        return 0;
    }
    ready_ports = 0;
    ready_port_mask = 0;
    for (uint32_t port = 0; port < 32; ++port)
        port_state[port] = (ahci_port_state_t){0};
    for (uint32_t port = 0; port < 32; ++port)
        ahci_pending_port_status[port] = 0;
    active_port = 0; active_command_list = 0; active_command_table = 0;
    active_data = 0; active_data_pages = 0; ahci_last_prdt_length = 0;
    ahci_io_disabled = 0;
    ahci_errors = 0;
    ahci_last_task_file = 0;
    ahci_last_interrupt_status_value = 0;
    ahci_last_serial_error_value = 0;
    ahci_storage_registered = 0;
    for (uint32_t port = 0; port < 32; ++port) {
        if ((implemented_ports & (1U << port)) == 0) continue;
        uint64_t port_offset = AHCI_PORT_BASE + (uint64_t)port * AHCI_PORT_STRIDE;
        if (port_offset > device->resources[AHCI_BAR_INDEX].size ||
            device->resources[AHCI_BAR_INDEX].size - port_offset <
                AHCI_PORT_REGISTER_SIZE) continue;
        volatile uint32_t *regs = abar + port_offset / 4;
        uint32_t ssts = regs[AHCI_PORT_SSTS / 4];
        if ((ssts & 0x0fU) != 3 || ((ssts >> 8) & 0x0fU) != 1) continue;
        if (regs[AHCI_PORT_SIG / 4] != AHCI_SIGNATURE_SATA) continue;
        regs[AHCI_PORT_CMD / 4] &= ~(AHCI_CMD_ST | AHCI_CMD_FRE);
        int stopped = 0;
        for (uint32_t wait = 0; wait < 100000; ++wait)
            if ((regs[AHCI_PORT_CMD / 4] & (AHCI_CMD_CR | AHCI_CMD_FR)) == 0) {
                stopped = 1; break;
            }
        if (!stopped) continue;
        uint64_t command_list = physical_alloc_frame();
        uint64_t fis = physical_alloc_frame();
        if (!command_list || !fis) {
            if (command_list) physical_free_frame(command_list);
            if (fis) physical_free_frame(fis);
            continue;
        }
        for (uint32_t word = 0; word < 1024; ++word) {
            ((uint32_t *)(uintptr_t)command_list)[word] = 0;
            ((uint32_t *)(uintptr_t)fis)[word] = 0;
        }
        regs[AHCI_PORT_CLB / 4] = (uint32_t)command_list;
        regs[(AHCI_PORT_CLB / 4) + 1] = 0;
        regs[AHCI_PORT_FB / 4] = (uint32_t)fis;
        regs[(AHCI_PORT_FB / 4) + 1] = 0;
        regs[AHCI_PORT_SERR / 4] = UINT32_MAX;
        regs[AHCI_PORT_IS / 4] = UINT32_MAX;
        regs[AHCI_PORT_IE / 4] = AHCI_PORT_IRQ_MASK;
        regs[AHCI_PORT_CI / 4] = 0;
        regs[AHCI_PORT_CMD / 4] |= AHCI_CMD_FRE | AHCI_CMD_ST;
        port_state[port].regs = regs;
        port_state[port].command_list = command_list;
        port_state[port].fis = fis;
        ready_port_mask |= 1U << port;
        ++ready_ports;
        if (active_port) continue;
        active_port = regs;
        active_abar = abar;
        active_port_number = port;
        active_command_list = command_list;
    }
    if (ready_ports == 0) {
        active_port = 0;
        active_command_list = 0;
        device_release_resource(device, AHCI_BAR_INDEX, &ahci_driver);
        return 0;
    }
    ++controllers;
    ports |= implemented_ports;
    arch_set_interrupt_gate(AHCI_IRQ_VECTOR, arch_ahci_irq_stub);
    ahci_irq_enabled = pci_enable_msix(device, AHCI_IRQ_VECTOR);
    if (!ahci_irq_enabled) ahci_irq_enabled = pci_enable_msi(device, AHCI_IRQ_VECTOR);
    if (!ahci_irq_enabled)
        ahci_irq_enabled = pci_enable_legacy_irq(device, AHCI_IRQ_VECTOR);
    if (ahci_irq_enabled) abar[AHCI_GHC_OFFSET / 4] |= AHCI_GHC_IE;
    return 1;
}

int ahci_initialize(void) {
    controllers = 0;
    ports = 0;
    ready_ports = 0;
    ready_port_mask = 0;
    for (uint32_t port = 0; port < 32; ++port)
        port_state[port] = (ahci_port_state_t){0};
    spinlock_init(&ahci_lock);
    active_abar = 0;
    active_port_number = 0;
    ahci_irq_enabled = 0;
    ahci_interrupts = 0;
    ahci_io_disabled = 0;
    active_sector_count = 0;
    active_lba48 = 0;
    ahci_driver.name = "ahci";
    ahci_driver.bus = DEVICE_BUS_PCI;
    ahci_driver.match = ahci_match;
    ahci_driver.probe = ahci_probe;
    if (!device_driver_register(&ahci_driver) || !device_bind_drivers()) return 0;
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int identified = ahci_identify_ready_ports_locked();
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return identified;
}

uint32_t ahci_controller_count(void) { return controllers; }
uint32_t ahci_port_mask(void) { return ports; }
uint32_t ahci_ready_port_count(void) { return ready_ports; }
uint32_t ahci_ready_port_mask(void) { return ready_port_mask; }
int ahci_interrupt_enabled(void) { return ahci_irq_enabled; }
uint32_t ahci_interrupt_count(void) { return ahci_interrupts; }
void ahci_interrupt_handler(void) {
    if (!active_abar) return;
    uint32_t handled = 0;
    for (uint32_t port = 0; port < 32; ++port) {
        if ((ready_port_mask & (1U << port)) == 0 || !port_state[port].regs)
            continue;
        uint32_t status = port_state[port].regs[AHCI_PORT_IS / 4];
        if (status == 0) continue;
        __atomic_fetch_or(&ahci_pending_port_status[port], status,
                          __ATOMIC_RELEASE);
        __atomic_fetch_add(&ahci_interrupts, 1U, __ATOMIC_RELAXED);
        port_state[port].regs[AHCI_PORT_IS / 4] = status;
        handled |= 1U << port;
    }
    if (handled) active_abar[2] = handled;
}

typedef struct {
    uint16_t flags, prdt_length;
    uint32_t prdbc, ctba, ctbau, reserved[4];
} __attribute__((packed)) ahci_command_header_t;

typedef struct {
    uint8_t fis_type, flags, command, feature_low;
    uint8_t lba0, lba1, lba2, device;
    uint8_t lba3, lba4, lba5, feature_high;
    uint8_t count_low, count_high, icc, control;
    uint8_t reserved[4];
} __attribute__((packed)) ahci_register_fis_t;

typedef struct {
    ahci_register_fis_t command_fis;
    uint8_t command_fis_padding[44];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    uint32_t data_base, data_base_high, reserved2, byte_count;
    uint32_t data_base2, data_base_high2, reserved3, byte_count2;
} __attribute__((packed)) ahci_command_table_t;

static int ahci_identify_locked(uint16_t *words) {
    if (ahci_io_disabled || !active_port || !active_command_list || !words || active_data) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
    active_data_pages = 1;
    if (!active_command_table || !active_data) {
        if (active_command_table) physical_free_frame(active_command_table);
        if (active_data) physical_free_frame(active_data);
        active_command_table = 0; active_data = 0; return 0;
    }
    for (uint32_t i = 0; i < 1024; ++i) {
        ((uint32_t *)(uintptr_t)active_command_table)[i] = 0;
        ((uint32_t *)(uintptr_t)active_data)[i] = 0;
    }
    ahci_command_header_t *header = (ahci_command_header_t *)(uintptr_t)active_command_list;
    header[0].flags = 5U;
    header[0].prdt_length = 1;
    header[0].prdbc = 0;
    header[0].ctba = (uint32_t)active_command_table;
    header[0].ctbau = 0;
    ahci_command_table_t *table = (ahci_command_table_t *)(uintptr_t)active_command_table;
    table->command_fis.fis_type = 0x27;
    table->command_fis.flags = 0x80;
    table->command_fis.command = 0xec;
    table->command_fis.device = 0x40;
    table->data_base = (uint32_t)active_data;
    table->data_base_high = 0;
    table->byte_count = 511U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    ahci_dma_write_barrier();
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        ahci_dma_read_barrier();
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc, 512);
            break;
        }
    }
    if (completed) {
        const uint16_t *source = (const uint16_t *)(uintptr_t)active_data;
        for (uint32_t i = 0; i < 256; ++i) words[i] = source[i];
        active_lba48 = (words[83] & (1U << 10)) != 0;
        if (active_lba48)
            active_sector_count = (uint64_t)words[100] |
                ((uint64_t)words[101] << 16) |
                ((uint64_t)words[102] << 32) |
                ((uint64_t)words[103] << 48);
        else
            active_sector_count = (uint64_t)words[60] |
                ((uint64_t)words[61] << 16);
        if (active_sector_count == 0) completed = 0;
    }
    int result = ahci_finish_command(completed);
    if (!result) {
        active_sector_count = 0;
        active_lba48 = 0;
    }
    return result;
}

static int ahci_identify_ready_ports_locked(void) {
    uint16_t words[256];
    uint32_t identified_mask = 0;
    for (uint32_t port = 0; port < 32; ++port) {
        if ((ready_port_mask & (1U << port)) == 0) continue;
        ahci_select_port_locked(port);
        if (!ahci_identify_locked(words)) {
            port_state[port].identified = 0;
            continue;
        }
        port_state[port].lba48 = active_lba48;
        port_state[port].sector_count = active_sector_count;
        port_state[port].identified = 1;
        identified_mask |= 1U << port;
    }
    ready_port_mask = identified_mask;
    ready_ports = 0;
    for (uint32_t port = 0; port < 32; ++port)
        if (identified_mask & (1U << port)) ++ready_ports;
    for (uint32_t port = 0; port < 32; ++port)
        if (identified_mask & (1U << port)) {
            ahci_select_port_locked(port);
            break;
        }
    return ready_ports != 0;
}

static int ahci_flush_locked(void) {
    if (ahci_io_disabled || !active_port || !active_command_list ||
        active_data || !active_sector_count) return 0;
    active_command_table = physical_alloc_frame();
    if (!active_command_table) return 0;
    active_data_pages = 0;
    for (uint32_t i = 0; i < 1024; ++i)
        ((uint32_t *)(uintptr_t)active_command_table)[i] = 0;
    ahci_command_header_t *header =
        (ahci_command_header_t *)(uintptr_t)active_command_list;
    header[0].flags = 5U;
    header[0].prdt_length = 0;
    header[0].prdbc = 0;
    header[0].ctba = (uint32_t)active_command_table;
    header[0].ctbau = 0;
    ahci_command_table_t *table =
        (ahci_command_table_t *)(uintptr_t)active_command_table;
    table->command_fis.fis_type = 0x27;
    table->command_fis.flags = 0x80;
    table->command_fis.command = active_lba48 ?
        AHCI_ATA_FLUSH_CACHE_EXT : AHCI_ATA_FLUSH_CACHE;
    table->command_fis.device = 0x40;
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    ahci_dma_write_barrier();
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        ahci_dma_read_barrier();
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc, 0);
            break;
        }
    }
    return ahci_finish_command(completed);
}

static int ahci_read_sector_locked(uint64_t lba, void *buffer) {
    if (ahci_io_disabled || !ahci_lba_valid(lba, 1) || !active_port ||
        !active_command_list || !buffer || active_data) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
    active_data_pages = 1;
    if (!active_command_table || !active_data) {
        if (active_command_table) physical_free_frame(active_command_table);
        if (active_data) physical_free_frame(active_data);
        active_command_table = 0; active_data = 0; return 0;
    }
    for (uint32_t i = 0; i < 1024; ++i) {
        ((uint32_t *)(uintptr_t)active_command_table)[i] = 0;
        ((uint32_t *)(uintptr_t)active_data)[i] = 0;
    }
    ahci_command_header_t *header = (ahci_command_header_t *)(uintptr_t)active_command_list;
    header[0].flags = 5; header[0].prdt_length = 1; header[0].prdbc = 0;
    header[0].ctba = (uint32_t)active_command_table; header[0].ctbau = 0;
    ahci_command_table_t *table = (ahci_command_table_t *)(uintptr_t)active_command_table;
    table->command_fis.fis_type = 0x27; table->command_fis.flags = 0x80;
    table->command_fis.command = active_lba48 ? AHCI_ATA_READ_DMA_EXT : 0xc8U;
    table->command_fis.device = 0x40;
    table->command_fis.lba0 = (uint8_t)lba; table->command_fis.lba1 = (uint8_t)(lba >> 8);
    table->command_fis.lba2 = (uint8_t)(lba >> 16); table->command_fis.lba3 = (uint8_t)(lba >> 24);
    table->command_fis.lba4 = (uint8_t)(lba >> 32); table->command_fis.lba5 = (uint8_t)(lba >> 40);
    table->command_fis.count_low = 1; table->command_fis.count_high = 0;
    table->data_base = (uint32_t)active_data; table->data_base_high = 0;
    table->byte_count = 511U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    ahci_dma_write_barrier();
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        ahci_dma_read_barrier();
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc, 512); break;
        }
    }
    if (completed) {
        const uint8_t *source = (const uint8_t *)(uintptr_t)active_data;
        uint8_t *destination = (uint8_t *)buffer;
        for (uint32_t i = 0; i < 512; ++i) destination[i] = source[i];
    }
    return ahci_finish_command(completed);
}

static int ahci_write_sector_locked(uint64_t lba, const void *buffer) {
    if (ahci_io_disabled || !ahci_lba_valid(lba, 1) || !active_port ||
        !active_command_list || !buffer || active_data) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
    active_data_pages = 1;
    if (!active_command_table || !active_data) {
        if (active_command_table) physical_free_frame(active_command_table);
        if (active_data) physical_free_frame(active_data);
        active_command_table = 0; active_data = 0; return 0;
    }
    for (uint32_t i = 0; i < 1024; ++i) {
        ((uint32_t *)(uintptr_t)active_command_table)[i] = 0;
        ((uint32_t *)(uintptr_t)active_data)[i] = 0;
    }
    const uint8_t *source = (const uint8_t *)buffer;
    uint8_t *destination = (uint8_t *)(uintptr_t)active_data;
    for (uint32_t i = 0; i < 512; ++i) destination[i] = source[i];
    ahci_command_header_t *header = (ahci_command_header_t *)(uintptr_t)active_command_list;
    header[0].flags = 5U | (1U << 6);
    header[0].prdt_length = 1; header[0].prdbc = 0;
    header[0].ctba = (uint32_t)active_command_table; header[0].ctbau = 0;
    ahci_command_table_t *table = (ahci_command_table_t *)(uintptr_t)active_command_table;
    table->command_fis.fis_type = 0x27; table->command_fis.flags = 0x80;
    table->command_fis.command = active_lba48 ? AHCI_ATA_WRITE_DMA_EXT : 0xcaU;
    table->command_fis.device = 0x40;
    table->command_fis.lba0 = (uint8_t)lba; table->command_fis.lba1 = (uint8_t)(lba >> 8);
    table->command_fis.lba2 = (uint8_t)(lba >> 16); table->command_fis.lba3 = (uint8_t)(lba >> 24);
    table->command_fis.lba4 = (uint8_t)(lba >> 32); table->command_fis.lba5 = (uint8_t)(lba >> 40);
    table->command_fis.count_low = 1; table->command_fis.count_high = 0;
    table->data_base = (uint32_t)active_data; table->data_base_high = 0;
    table->byte_count = 511U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    ahci_dma_write_barrier();
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        ahci_dma_read_barrier();
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc, 512); break;
        }
    }
    return ahci_finish_command(completed);
}

static int ahci_io_sectors(uint64_t lba, uint32_t count, void *buffer, int write) {
    if (ahci_io_disabled || !ahci_lba_valid(lba, count) || !active_port ||
        !active_command_list || !buffer || active_data || count == 0 ||
        count > 16) return 0;
    active_command_table = physical_alloc_frame();
    active_data_pages = count > 8 ? 2U : 1U;
    active_data = physical_alloc_frames(active_data_pages);
    if (!active_command_table || !active_data) {
        if (active_command_table) physical_free_frame(active_command_table);
        if (active_data) physical_free_frames(active_data, active_data_pages);
        active_command_table = 0; active_data = 0;
        return 0;
    }
    for (uint32_t i = 0; i < 1024; ++i) {
        ((uint32_t *)(uintptr_t)active_command_table)[i] = 0;
        ((uint32_t *)(uintptr_t)active_data)[i] = 0;
    }
    uint8_t *dma = (uint8_t *)(uintptr_t)active_data;
    if (write) {
        const uint8_t *source = (const uint8_t *)buffer;
        for (uint32_t i = 0; i < count * 512U; ++i) dma[i] = source[i];
    }
    ahci_command_header_t *header =
        (ahci_command_header_t *)(uintptr_t)active_command_list;
    header[0].flags = 5U | (write ? (1U << 6) : 0U);
    header[0].prdt_length = (uint16_t)active_data_pages; header[0].prdbc = 0;
    ahci_last_prdt_length = active_data_pages;
    header[0].ctba = (uint32_t)active_command_table; header[0].ctbau = 0;
    ahci_command_table_t *table =
        (ahci_command_table_t *)(uintptr_t)active_command_table;
    table->command_fis.fis_type = 0x27; table->command_fis.flags = 0x80;
    table->command_fis.command = write ?
        (active_lba48 ? AHCI_ATA_WRITE_DMA_EXT : 0xcaU) :
        (active_lba48 ? AHCI_ATA_READ_DMA_EXT : 0xc8U);
    table->command_fis.device = 0x40;
    table->command_fis.lba0 = (uint8_t)lba;
    table->command_fis.lba1 = (uint8_t)(lba >> 8);
    table->command_fis.lba2 = (uint8_t)(lba >> 16);
    table->command_fis.lba3 = (uint8_t)(lba >> 24);
    table->command_fis.lba4 = (uint8_t)(lba >> 32);
    table->command_fis.lba5 = (uint8_t)(lba >> 40);
    table->command_fis.count_low = (uint8_t)count;
    table->command_fis.count_high = (uint8_t)(count >> 8);
    table->data_base = (uint32_t)active_data; table->data_base_high = 0;
    table->byte_count = (active_data_pages > 1U ? 4095U : count * 512U - 1U) |
                        (1U << 31);
    if (active_data_pages > 1U) {
        table->data_base2 = (uint32_t)(active_data + 4096U);
        table->data_base_high2 = 0;
        table->byte_count2 = (count * 512U - 4096U - 1U) | (1U << 31);
    }
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    ahci_dma_write_barrier();
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        ahci_dma_read_barrier();
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc,
                                        count * 512U);
            break;
        }
    }
    if (completed && !write)
        for (uint32_t i = 0; i < count * 512U; ++i)
            ((uint8_t *)buffer)[i] = dma[i];
    return ahci_finish_command(completed);
}

static int ahci_io_range_locked(uint64_t lba, uint32_t count, void *buffer,
                                int write) {
    if (!ahci_lba_valid(lba, count) || !buffer) return 0;
    uint32_t completed = 0;
    while (completed < count) {
        uint32_t chunk = count - completed;
        if (chunk > 16U) chunk = 16U;
        if (!ahci_io_sectors(lba + completed, chunk,
                             (uint8_t *)buffer + (uint64_t)completed * 512U,
                             write)) return 0;
        completed += chunk;
    }
    return 1;
}

static int ahci_storage_read_context(uint64_t lba, uint32_t count,
                                     void *buffer, void *context) {
    uint32_t port = (uint32_t)(uintptr_t)context;
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = port < 32 && port_state[port].identified;
    if (result) {
        ahci_select_port_locked(port);
        result = ahci_io_range_locked(lba, count, buffer, 0);
    }
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

static int ahci_storage_write_context(uint64_t lba, uint32_t count,
                                      const void *buffer, void *context) {
    uint32_t port = (uint32_t)(uintptr_t)context;
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = port < 32 && port_state[port].identified;
    if (result) {
        ahci_select_port_locked(port);
        result = ahci_io_range_locked(lba, count, (void *)buffer, 1) &&
                 ahci_flush_locked();
    }
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

static int ahci_storage_flush_context(void *context) {
    uint32_t port = (uint32_t)(uintptr_t)context;
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = port < 32 && port_state[port].identified;
    if (result) {
        ahci_select_port_locked(port);
        result = ahci_flush_locked();
    }
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_register_storage_devices(void) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    if (ahci_storage_registered) {
        spinlock_unlock_irqrestore(&ahci_lock, flags);
        return 0;
    }
    static char names[32][8];
    uint32_t ordinal = 0;
    for (uint32_t port = 0; port < 32; ++port) {
        if (!port_state[port].identified) continue;
        names[ordinal][0] = 'a'; names[ordinal][1] = 'h';
        names[ordinal][2] = 'c'; names[ordinal][3] = 'i';
        names[ordinal][4] = (char)('0' + (ordinal % 10U));
        uint32_t digits = ordinal / 10U;
        if (digits != 0) {
            names[ordinal][4] = (char)('0' + digits);
            names[ordinal][5] = (char)('0' + (ordinal % 10U));
            names[ordinal][6] = '\0';
        } else {
            names[ordinal][5] = '\0';
        }
        storage_device_t device = {
            .name = names[ordinal],
            .block_size = 512,
            .block_count = port_state[port].sector_count,
            .context = (void *)(uintptr_t)port,
            .read_context = ahci_storage_read_context,
            .write_context = ahci_storage_write_context,
            .flush = ahci_storage_flush_context
        };
        if (!storage_register(&device)) {
            /* The storage registry has no rollback operation.  Freeze the
               publication state after a partial batch so a retry cannot
               duplicate the devices already accepted. */
            ahci_storage_registered = ordinal != 0;
            spinlock_unlock_irqrestore(&ahci_lock, flags);
            return 0;
        }
        ++ordinal;
    }
    ahci_storage_registered = ordinal != 0;
    int result = ahci_storage_registered;
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_identify(uint16_t *words) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_identify_locked(words);
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_read_sector(uint64_t lba, void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_read_sector_locked(lba, buffer);
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_write_sector(uint64_t lba, const void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_write_sector_locked(lba, buffer) && ahci_flush_locked();
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_io_range_locked(lba, count, buffer, 0);
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_io_range_locked(lba, count, (void *)buffer, 1) &&
                 ahci_flush_locked();
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

uint32_t ahci_last_io_prdt_length(void) { return ahci_last_prdt_length; }
uint32_t ahci_error_count(void) {
    return __atomic_load_n(&ahci_errors, __ATOMIC_ACQUIRE);
}
uint32_t ahci_last_task_file_status(void) {
    return __atomic_load_n(&ahci_last_task_file, __ATOMIC_ACQUIRE);
}
uint32_t ahci_last_interrupt_status(void) {
    return __atomic_load_n(&ahci_last_interrupt_status_value, __ATOMIC_ACQUIRE);
}
uint32_t ahci_last_serial_error(void) {
    return __atomic_load_n(&ahci_last_serial_error_value, __ATOMIC_ACQUIRE);
}
