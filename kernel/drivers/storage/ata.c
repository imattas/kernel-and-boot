#include "storage.h"
#include "../../device/device.h"
#include "../../core/printk/serial.h"
#include "../../core/sync/spinlock.h"

static uint16_t io_base = 0x1f0;
static uint16_t control_base = 0x3f6;
static uint64_t sector_count;
static int lba48_supported;
static device_driver_t ata_driver;
static spinlock_t ata_lock;

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port));
}

static int wait_status(uint8_t mask, uint8_t expected) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        uint8_t status = in8(io_base + 7);
        if (status == 0xff) return 0;
        if ((status & mask) == expected) return 1;
        if ((status & 1) != 0) return 0;
    }
    return 0;
}

static int identify(void) {
    out8(control_base, 0x04);
    out8(control_base, 0x00);
    out8(io_base + 6, 0xa0);
    out8(io_base + 2, 0);
    out8(io_base + 3, 0);
    out8(io_base + 4, 0);
    out8(io_base + 5, 0);
    out8(io_base + 7, 0xec);
    uint8_t status = in8(io_base + 7);
    if (status == 0 || status == 0xff) return 0;
    if (!wait_status(0x88, 0x08)) return 0;
    uint16_t identify_data[256];
    for (uint32_t i = 0; i < 256; ++i) identify_data[i] = in16(io_base);
    /* Word zero distinguishes ATA disks from ATAPI devices. */
    if ((identify_data[0] & 0x8000U) != 0 ||
        (identify_data[49] & (1U << 9)) == 0) return 0;
    uint64_t lba48 = (uint64_t)identify_data[100] |
                     ((uint64_t)identify_data[101] << 16) |
                     ((uint64_t)identify_data[102] << 32) |
                     ((uint64_t)identify_data[103] << 48);
    lba48_supported = (identify_data[83] & (1U << 10)) != 0;
    sector_count = lba48_supported && lba48 != 0
        ? lba48
        : (uint64_t)identify_data[60] | ((uint64_t)identify_data[61] << 16);
    return sector_count != 0;
}

static void ata_issue(uint64_t lba, uint32_t count, uint8_t command) {
    if (lba48_supported) {
        out8(io_base + 6, 0xe0);
        out8(io_base + 2, (uint8_t)(count >> 8));
        out8(io_base + 3, (uint8_t)(lba >> 24));
        out8(io_base + 4, (uint8_t)(lba >> 32));
        out8(io_base + 5, (uint8_t)(lba >> 40));
        out8(io_base + 2, (uint8_t)count);
        out8(io_base + 3, (uint8_t)lba);
        out8(io_base + 4, (uint8_t)(lba >> 8));
        out8(io_base + 5, (uint8_t)(lba >> 16));
        out8(io_base + 7, command);
        return;
    }
    out8(io_base + 6, (uint8_t)(0xe0 | ((lba >> 24) & 0x0f)));
    out8(io_base + 2, (uint8_t)count);
    out8(io_base + 3, (uint8_t)lba);
    out8(io_base + 4, (uint8_t)(lba >> 8));
    out8(io_base + 5, (uint8_t)(lba >> 16));
    out8(io_base + 7, command);
}

static int ata_read_locked(uint64_t lba, uint32_t count, void *buffer) {
    if (!buffer || (!lba48_supported && lba > 0x0fffffffULL) || count == 0 || count > 256 ||
        lba >= sector_count || count > sector_count - lba) return 0;
    uint8_t *output = (uint8_t *)buffer;
    ata_issue(lba, count, lba48_supported ? 0x24 : 0x20);
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (!wait_status(0x88, 0x08)) return 0;
        for (uint32_t word = 0; word < 256; ++word) {
            uint16_t value = in16(io_base);
            output[sector * 512 + word * 2] = (uint8_t)value;
            output[sector * 512 + word * 2 + 1] = (uint8_t)(value >> 8);
        }
    }
    return wait_status(0x81, 0);
}

static int ata_write_locked(uint64_t lba, uint32_t count, const void *buffer) {
    if ((!lba48_supported && lba > 0x0fffffffULL) || count == 0 || count > 256 ||
        lba >= sector_count || count > sector_count - lba || !buffer) return 0;
    const uint8_t *input = (const uint8_t *)buffer;
    ata_issue(lba, count, lba48_supported ? 0x34 : 0x30);
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (!wait_status(0x88, 0x08)) return 0;
        for (uint32_t word = 0; word < 256; ++word)
            out16(io_base, (uint16_t)input[sector * 512 + word * 2] |
                  ((uint16_t)input[sector * 512 + word * 2 + 1] << 8));
    }
    if (!wait_status(0x81, 0)) return 0;
    /* Do not report completion until the device has committed its write cache. */
    out8(io_base + 7, lba48_supported ? 0xea : 0xe7);
    return wait_status(0x80, 0);
}

static int ata_read(uint64_t lba, uint32_t count, void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ata_lock);
    int result = ata_read_locked(lba, count, buffer);
    spinlock_unlock_irqrestore(&ata_lock, flags);
    return result;
}

static int ata_write(uint64_t lba, uint32_t count, const void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&ata_lock);
    int result = ata_write_locked(lba, count, buffer);
    spinlock_unlock_irqrestore(&ata_lock, flags);
    return result;
}

static int ata_match(const device_t *device) {
    return device && device->bus == DEVICE_BUS_PCI &&
           device->class_code == 0x01 && device->subclass == 0x01;
}

static int ata_probe(device_t *device) {
    if (!device) return 0;
    int claimed_io = device->resources[0].size != 0;
    int claimed_control = device->resources[1].size != 0;
    if ((claimed_io && ((device->resources[0].flags & 1U) == 0 ||
                        device->resources[0].size < 8U ||
                        (device->resources[0].address & 3U) != 0 ||
                        device->resources[0].address > 0xffffU)) ||
        (claimed_control && ((device->resources[1].flags & 1U) == 0 ||
                              device->resources[1].size < 4U ||
                              (device->resources[1].address & 3U) != 0 ||
                              device->resources[1].address > 0xfffdU)))
        return 0;
    if ((claimed_io && !device_claim_resource(device, 0, &ata_driver)) ||
        (claimed_control && !device_claim_resource(device, 1, &ata_driver))) {
        if (claimed_io) device_release_resource(device, 0, &ata_driver);
        if (claimed_control) device_release_resource(device, 1, &ata_driver);
        return 0;
    }
    if ((device->resources[0].flags & 1u) != 0 &&
        device->resources[0].address != 0)
        io_base = (uint16_t)(device->resources[0].address & 0xfffcu);
    if ((device->resources[1].flags & 1u) != 0 &&
        device->resources[1].address != 0)
        control_base = (uint16_t)((device->resources[1].address + 2) & 0xfffcu);
    uint64_t flags = spinlock_lock_irqsave(&ata_lock);
    int identified = identify();
    spinlock_unlock_irqrestore(&ata_lock, flags);
    if (!identified) {
        if (claimed_io) device_release_resource(device, 0, &ata_driver);
        if (claimed_control) device_release_resource(device, 1, &ata_driver);
        return 0;
    }
    storage_device_t discovered = {0};
    discovered.name = "ata0";
    discovered.block_size = 512;
    discovered.block_count = sector_count;
    discovered.read = ata_read;
    discovered.write = ata_write;
    if (!storage_register(&discovered)) {
        if (claimed_io) device_release_resource(device, 0, &ata_driver);
        if (claimed_control) device_release_resource(device, 1, &ata_driver);
        return 0;
    }
    return 1;
}

int ata_initialize(void) {
    lba48_supported = 0;
    spinlock_init(&ata_lock);
    ata_driver.name = "ata-pio";
    ata_driver.bus = DEVICE_BUS_PCI;
    ata_driver.match = ata_match;
    ata_driver.probe = ata_probe;
    if (!device_driver_register(&ata_driver)) {
        return 0;
    }
    if (!device_bind_drivers()) {
        return 0;
    }
    serial_write("storage devices=");
    serial_write_hex(storage_device_count());
    serial_write("\r\n");
    return storage_device_count() != 0;
}

int ata_read_boot_sector(void *buffer) { return ata_read(0, 1, buffer); }
int ata_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    return ata_read(lba, count, buffer);
}
int ata_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    return ata_write(lba, count, buffer);
}
