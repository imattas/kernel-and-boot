#include "ahci.h"
#include "../../device/device.h"
#include "../../mm/physical/frame.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../pci/pci.h"
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
#define AHCI_SIGNATURE_SATA 0x00000101U
#define AHCI_IRQ_VECTOR 0x54
#define AHCI_GHC_IE (1U << 1)
#define AHCI_PORT_IS_ERROR_MASK ((1U << 4) | (1U << 5) | (1U << 7) | \
                                 (1U << 22) | (1U << 23) | (1U << 24) | \
                                 (1U << 25) | (1U << 26) | (1U << 27) | \
                                 (1U << 28) | (1U << 30) | (1U << 31))
#define AHCI_PORT_IRQ_MASK (0x0000001fU | AHCI_PORT_IS_ERROR_MASK)

extern void arch_ahci_irq_stub(void);

static uint32_t controllers;
static uint32_t ports;
static uint32_t ready_ports;
static volatile uint32_t *active_port;
static uint64_t active_command_list;
static uint64_t active_command_table;
static uint64_t active_data;
static device_driver_t ahci_driver;
static volatile uint32_t *active_abar;
static uint32_t active_port_number;
static int ahci_irq_enabled;
static volatile uint32_t ahci_interrupts;
static spinlock_t ahci_lock;

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

static int ahci_command_ok(uint32_t task_file, uint32_t transferred,
                           uint32_t expected) {
    uint32_t interrupt_status = active_port[AHCI_PORT_IS / 4];
    uint32_t serial_error = active_port[AHCI_PORT_SERR / 4];
    if (interrupt_status & AHCI_PORT_IS_ERROR_MASK)
        active_port[AHCI_PORT_IS / 4] =
            interrupt_status & AHCI_PORT_IS_ERROR_MASK;
    if (serial_error) active_port[AHCI_PORT_SERR / 4] = serial_error;
    return (task_file & 0x09U) == 0 && transferred == expected &&
           (interrupt_status & AHCI_PORT_IS_ERROR_MASK) == 0 &&
           serial_error == 0;
}

static int ahci_match(const device_t *device) {
    return device && device->bus == DEVICE_BUS_PCI &&
           device->class_code == PCI_CLASS_MASS_STORAGE &&
           device->subclass == PCI_SUBCLASS_SATA;
}

static int ahci_probe(device_t *device) {
    if (!device || device->resources[AHCI_BAR_INDEX].size == 0 ||
        device->resources[AHCI_BAR_INDEX].address == 0 ||
        device->resources[AHCI_BAR_INDEX].address >= 0x100000000ULL ||
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
    active_port = 0; active_command_list = 0; active_command_table = 0; active_data = 0;
    for (uint32_t port = 0; port < 32; ++port) {
        if ((implemented_ports & (1U << port)) == 0) continue;
        volatile uint32_t *regs = abar + (AHCI_PORT_BASE + port * AHCI_PORT_STRIDE) / 4;
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
        if (ready_ports != 0) continue;
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
        active_port = regs;
        active_abar = abar;
        active_port_number = port;
        active_command_list = command_list;
        ++ready_ports;
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
    spinlock_init(&ahci_lock);
    active_abar = 0;
    active_port_number = 0;
    ahci_irq_enabled = 0;
    ahci_interrupts = 0;
    ahci_driver.name = "ahci";
    ahci_driver.bus = DEVICE_BUS_PCI;
    ahci_driver.match = ahci_match;
    ahci_driver.probe = ahci_probe;
    if (!device_driver_register(&ahci_driver) || !device_bind_drivers()) return 0;
    return 1;
}

uint32_t ahci_controller_count(void) { return controllers; }
uint32_t ahci_port_mask(void) { return ports; }
uint32_t ahci_ready_port_count(void) { return ready_ports; }
int ahci_interrupt_enabled(void) { return ahci_irq_enabled; }
uint32_t ahci_interrupt_count(void) { return ahci_interrupts; }
void ahci_interrupt_handler(void) {
    if (!active_port) return;
    uint32_t status = active_port[AHCI_PORT_IS / 4];
    if (status == 0) return;
    ++ahci_interrupts;
    active_port[AHCI_PORT_IS / 4] = status;
    if (active_abar) active_abar[2] = 1U << active_port_number;
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
} __attribute__((packed)) ahci_command_table_t;

static int ahci_identify_locked(uint16_t *words) {
    if (!active_port || !active_command_list || !words || active_data) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
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
    table->data_base = (uint32_t)active_data;
    table->data_base_high = 0;
    table->byte_count = 511U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc, 512);
            break;
        }
    }
    if (completed) {
        const uint16_t *source = (const uint16_t *)(uintptr_t)active_data;
        for (uint32_t i = 0; i < 256; ++i) words[i] = source[i];
    }
    if (!completed && !ahci_stop_engine()) return 0;
    physical_free_frame(active_command_table);
    physical_free_frame(active_data);
    active_command_table = 0; active_data = 0;
    return completed;
}

static int ahci_read_sector_locked(uint64_t lba, void *buffer) {
    if (!active_port || !active_command_list || !buffer || active_data) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
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
    table->command_fis.command = 0xc8; table->command_fis.device = 0x40;
    table->command_fis.lba0 = (uint8_t)lba; table->command_fis.lba1 = (uint8_t)(lba >> 8);
    table->command_fis.lba2 = (uint8_t)(lba >> 16); table->command_fis.lba3 = (uint8_t)(lba >> 24);
    table->command_fis.lba4 = (uint8_t)(lba >> 32); table->command_fis.lba5 = (uint8_t)(lba >> 40);
    table->command_fis.count_low = 1; table->command_fis.count_high = 0;
    table->data_base = (uint32_t)active_data; table->data_base_high = 0;
    table->byte_count = 511U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX; active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
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
    if (!completed && !ahci_stop_engine()) return 0;
    physical_free_frame(active_command_table); physical_free_frame(active_data);
    active_command_table = 0; active_data = 0;
    return completed;
}

static int ahci_write_sector_locked(uint64_t lba, const void *buffer) {
    if (!active_port || !active_command_list || !buffer || active_data) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
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
    table->command_fis.command = 0xca; table->command_fis.device = 0x40;
    table->command_fis.lba0 = (uint8_t)lba; table->command_fis.lba1 = (uint8_t)(lba >> 8);
    table->command_fis.lba2 = (uint8_t)(lba >> 16); table->command_fis.lba3 = (uint8_t)(lba >> 24);
    table->command_fis.lba4 = (uint8_t)(lba >> 32); table->command_fis.lba5 = (uint8_t)(lba >> 40);
    table->command_fis.count_low = 1; table->command_fis.count_high = 0;
    table->data_base = (uint32_t)active_data; table->data_base_high = 0;
    table->byte_count = 511U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX; active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        uint32_t status = active_port[AHCI_PORT_TFD / 4];
        if ((active_port[AHCI_PORT_CI / 4] & 1U) == 0) {
            completed = ahci_command_ok(status, header[0].prdbc, 512); break;
        }
    }
    if (!completed && !ahci_stop_engine()) return 0;
    physical_free_frame(active_command_table); physical_free_frame(active_data);
    active_command_table = 0; active_data = 0;
    return completed;
}

static int ahci_io_sectors(uint64_t lba, uint32_t count, void *buffer, int write) {
    if (!active_port || !active_command_list || !buffer || active_data ||
        count == 0 || count > 8) return 0;
    active_command_table = physical_alloc_frame();
    active_data = physical_alloc_frame();
    if (!active_command_table || !active_data) {
        if (active_command_table) physical_free_frame(active_command_table);
        if (active_data) physical_free_frame(active_data);
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
    header[0].prdt_length = 1; header[0].prdbc = 0;
    header[0].ctba = (uint32_t)active_command_table; header[0].ctbau = 0;
    ahci_command_table_t *table =
        (ahci_command_table_t *)(uintptr_t)active_command_table;
    table->command_fis.fis_type = 0x27; table->command_fis.flags = 0x80;
    table->command_fis.command = write ? 0xca : 0xc8;
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
    table->byte_count = count * 512U - 1U | (1U << 31);
    active_port[AHCI_PORT_IS / 4] = UINT32_MAX;
    active_port[AHCI_PORT_CI / 4] = 1;
    int completed = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
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
    if (!completed && !ahci_stop_engine()) return 0;
    physical_free_frame(active_command_table); physical_free_frame(active_data);
    active_command_table = 0; active_data = 0;
    return completed;
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
    int result = ahci_write_sector_locked(lba, buffer);
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_io_sectors(lba, count, buffer, 0);
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ahci_lock);
    int result = ahci_io_sectors(lba, count, (void *)buffer, 1);
    spinlock_unlock_irqrestore(&ahci_lock, flags);
    return result;
}
