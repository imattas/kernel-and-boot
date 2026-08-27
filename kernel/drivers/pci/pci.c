#include "pci.h"
#include "../../device/device.h"
#include "../../core/printk/serial.h"
#include "../../arch/x86_64/interrupts/apic.h"

#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA 0xcfc
#define PCI_HEADER_TYPE 0x0e
#define PCI_SECONDARY_BUS 0x19

static uint32_t discovered;
static uint32_t resources;
static uint64_t next_mmio_address;
static volatile uint8_t scanned_buses[256];

static void out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t config_read(uint8_t bus, uint8_t slot, uint8_t function,
                            uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) | ((uint32_t)function << 8) |
                       (offset & 0xfcu);
    out32(PCI_CONFIG_ADDRESS, address);
    return in32(PCI_CONFIG_DATA);
}

static void config_write(uint8_t bus, uint8_t slot, uint8_t function,
                         uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) | ((uint32_t)function << 8) |
                       (offset & 0xfcu);
    out32(PCI_CONFIG_ADDRESS, address);
    out32(PCI_CONFIG_DATA, value);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset) {
    return config_read(bus, slot, function, offset);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value) {
    config_write(bus, slot, function, offset, value);
}

static uint16_t config_read16(uint8_t bus, uint8_t slot, uint8_t function,
                              uint8_t offset) {
    uint32_t value = config_read(bus, slot, function, offset);
    return (uint16_t)(value >> ((offset & 2u) * 8u));
}

static uint8_t config_read8(uint8_t bus, uint8_t slot, uint8_t function,
                            uint8_t offset) {
    uint32_t value = config_read(bus, slot, function, offset);
    return (uint8_t)(value >> ((offset & 3u) * 8u));
}

static void scan_bus(uint8_t bus);

int pci_enable_msi(const device_t *device, uint8_t vector) {
    if (!device || vector < 0x20 || vector > 0xfe) return 0;
    uint8_t capability = config_read8(device->bus_number, device->slot,
                                       device->function, 0x34) & 0xfcu;
    for (uint32_t step = 0; capability >= 0x40 && step < 48; ++step) {
        uint32_t header = config_read(device->bus_number, device->slot,
                                      device->function, capability);
        if ((header & 0xffU) == 0x05U) {
            uint32_t control = config_read(device->bus_number, device->slot,
                                            device->function,
                                            (uint8_t)(capability + 2));
            uint16_t message_control = (uint16_t)(control >> 16);
            int address_64 = (message_control & (1U << 7)) != 0;
            uint8_t data_offset = (uint8_t)(capability + (address_64 ? 12 : 8));
            config_write(device->bus_number, device->slot, device->function,
                         (uint8_t)(capability + 2),
                         (control & 0x0000ffffU) | ((uint32_t)(message_control &
                         (uint16_t)~0x0070U) << 16));
            config_write(device->bus_number, device->slot, device->function,
                         (uint8_t)(capability + 4), 0xfee00000U);
            if (address_64)
                config_write(device->bus_number, device->slot, device->function,
                             (uint8_t)(capability + 8), 0);
            config_write(device->bus_number, device->slot, device->function,
                         data_offset, vector);
            config_write(device->bus_number, device->slot, device->function,
                         (uint8_t)(capability + 2),
                         (control & 0x0000ffffU) |
                         ((uint32_t)((message_control & (uint16_t)~0x0070U) |
                         0x0001U) << 16));
            return 1;
        }
        capability = (uint8_t)(header >> 8) & 0xfcu;
    }
    return 0;
}

int pci_enable_legacy_irq(const device_t *device, uint8_t vector) {
    return device && device->irq_line < 16 &&
           arch_ioapic_route_irq(device->irq_line, vector);
}

static void enable_device(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t command_status = config_read(bus, slot, function, 0x04);
    uint16_t command = (uint16_t)command_status;
    command = (uint16_t)(command | 0x0007U);
    config_write(bus, slot, function, 0x04,
                 (command_status & 0xffff0000U) | command);
}

static uint64_t bar_size(uint32_t mask, uint64_t high_mask, int wide) {
    uint64_t combined = (uint64_t)(mask & ~0xfu) |
                        (wide ? (uint64_t)high_mask << 32 : 0);
    if (combined == 0) return 0;
    uint64_t size = (~combined) + 1;
    return size == 0 ? 0 : size;
}

static void read_resources(device_t *device, uint8_t header_type) {
    uint32_t command_status = config_read(device->bus_number, device->slot,
                                          device->function, 0x04);
    uint16_t command = (uint16_t)command_status;
    command = (uint16_t)(command & (uint16_t)~0x0007U);
    config_write(device->bus_number, device->slot, device->function, 0x04,
                 (command_status & 0xffff0000U) | command);
    uint8_t limit = (header_type & 0x7fu) == 1u ? 2u : 6u;
    for (uint8_t bar = 0; bar < limit; ++bar) {
        uint8_t offset = (uint8_t)(0x10 + bar * 4);
        uint32_t original = config_read(device->bus_number, device->slot,
                                        device->function, offset);
        if (original == 0) continue;
        uint32_t original_high = 0;
        int wide = (original & 1u) == 0 && (original & 6u) == 4u && bar < 5;
        if (wide) original_high = config_read(device->bus_number, device->slot,
                                               device->function,
                                               (uint8_t)(offset + 4));
        config_write(device->bus_number, device->slot, device->function,
                     offset, UINT32_MAX);
        uint32_t mask = config_read(device->bus_number, device->slot,
                                    device->function, offset);
        uint32_t high_mask = 0;
        if (wide) {
            config_write(device->bus_number, device->slot, device->function,
                         (uint8_t)(offset + 4), UINT32_MAX);
            high_mask = config_read(device->bus_number, device->slot,
                                    device->function, (uint8_t)(offset + 4));
            config_write(device->bus_number, device->slot, device->function,
                         (uint8_t)(offset + 4), original_high);
        }
        config_write(device->bus_number, device->slot, device->function,
                     offset, original);
        device->resources[bar].flags = original & 0xfu;
        device->resources[bar].address = (uint64_t)(original & ~0xfu) |
                                          (wide ? (uint64_t)original_high << 32 : 0);
        device->resources[bar].size = (original & 1u) != 0
            ? bar_size(mask & ~0x3u, 0, 0)
            : bar_size(mask, high_mask, wide);
        if ((original & 1u) == 0 &&
            (device->resources[bar].address == 0 ||
             device->resources[bar].address >= 0x100000000ULL) &&
            device->resources[bar].size != 0 &&
            device->resources[bar].size <= 0x10000000ULL) {
            uint64_t alignment = device->resources[bar].size;
            uint64_t address = (next_mmio_address + alignment - 1) &
                               ~(alignment - 1);
            if (address < 0x100000000ULL &&
                device->resources[bar].size <= 0x100000000ULL - address) {
                config_write(device->bus_number, device->slot,
                             device->function, offset,
                             (uint32_t)address | (original & 0xfu));
                if (wide) config_write(device->bus_number, device->slot,
                                       device->function, (uint8_t)(offset + 4), 0);
                device->resources[bar].address = address;
                next_mmio_address = address + device->resources[bar].size;
            }
        }
        if (device->resources[bar].size != 0) ++resources;
        if (wide) ++bar;
    }
    config_write(device->bus_number, device->slot, device->function, 0x04,
                 command_status);
}

static void scan_function(uint8_t bus, uint8_t slot, uint8_t function) {
    uint16_t vendor = config_read16(bus, slot, function, 0x00);
    if (vendor == 0xffffu) return;
    device_t device = {
        .bus = DEVICE_BUS_PCI,
        .bus_number = bus,
        .slot = slot,
        .function = function,
        .vendor_id = vendor,
        .device_id = config_read16(bus, slot, function, 0x02),
        .class_code = config_read8(bus, slot, function, 0x0b),
        .subclass = config_read8(bus, slot, function, 0x0a),
        .programming_interface = config_read8(bus, slot, function, 0x09),
        .irq_line = config_read8(bus, slot, function, 0x3c),
        .irq_pin = config_read8(bus, slot, function, 0x3d)
    };
    uint8_t header = config_read8(bus, slot, function, PCI_HEADER_TYPE);
    read_resources(&device, header);
    enable_device(bus, slot, function);
    if (device_register(&device)) ++discovered;

    if ((header & 0x7fu) == 1u && device.class_code == 0x06u &&
        device.subclass == 0x04u) {
        uint8_t secondary = config_read8(bus, slot, function,
                                         PCI_SECONDARY_BUS);
        if (secondary != 0 && secondary != 0xff && secondary != bus)
            scan_bus(secondary);
    }
}

static void scan_slot(uint8_t bus, uint8_t slot) {
    scan_function(bus, slot, 0);
    if ((config_read8(bus, slot, 0, PCI_HEADER_TYPE) & 0x80u) != 0) {
        for (uint8_t function = 1; function < 8; ++function)
            scan_function(bus, slot, function);
    }
}

static void scan_bus(uint8_t bus) {
    if (scanned_buses[bus]) return;
    scanned_buses[bus] = 1;
    for (uint8_t slot = 0; slot < 32; ++slot) scan_slot(bus, slot);
}

int pci_initialize(void) {
    discovered = 0;
    resources = 0;
    next_mmio_address = 0xd0000000ULL;
    for (uint32_t i = 0; i < sizeof(scanned_buses); ++i) scanned_buses[i] = 0;
    scan_bus(0);
    serial_write("pci devices=");
    serial_write_hex(discovered);
    serial_write(" resources=");
    serial_write_hex(resources);
    serial_write("\r\n");
    return 1;
}

uint32_t pci_device_count(void) { return discovered; }
uint32_t pci_resource_count(void) { return resources; }
