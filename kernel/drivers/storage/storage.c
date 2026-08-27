#include "storage.h"

static storage_device_t devices[STORAGE_DEVICE_CAPACITY];
static uint32_t device_count;

void storage_initialize(void) { device_count = 0; }

int storage_register(const storage_device_t *device) {
    if (!device || !device->name || !device->read || !device->write || device->block_size == 0 ||
        device->block_count == 0 || device_count >= STORAGE_DEVICE_CAPACITY)
        return 0;
    devices[device_count++] = *device;
    return 1;
}

uint32_t storage_device_count(void) { return device_count; }

const storage_device_t *storage_device_at(uint32_t index) {
    return index < device_count ? &devices[index] : 0;
}

int storage_read(uint32_t device, uint64_t lba, uint32_t count, void *buffer) {
    if (device >= device_count || !buffer || count == 0 ||
        lba >= devices[device].block_count ||
        count > devices[device].block_count - lba)
        return 0;
    return devices[device].read(lba, count, buffer);
}

int storage_write(uint32_t device, uint64_t lba, uint32_t count,
                  const void *buffer) {
    if (device >= device_count || !buffer || count == 0 ||
        lba >= devices[device].block_count ||
        count > devices[device].block_count - lba)
        return 0;
    return devices[device].write(lba, count, buffer);
}
