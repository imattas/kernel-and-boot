#include "storage.h"
#include "../../core/sync/spinlock.h"

static storage_device_t devices[STORAGE_DEVICE_CAPACITY];
static uint32_t device_count;
static spinlock_t storage_lock;

void storage_initialize(void) {
    spinlock_init(&storage_lock);
    device_count = 0;
}

static int same_name(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

int storage_register(const storage_device_t *device) {
    if (!device || !device->name || !device->name[0] || !device->read ||
        !device->write || device->block_size == 0 || device->block_count == 0 ||
        device->block_count > UINT64_MAX / device->block_size)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    if (device_count >= STORAGE_DEVICE_CAPACITY) {
        spinlock_unlock_irqrestore(&storage_lock, flags);
        return 0;
    }
    for (uint32_t i = 0; i < device_count; ++i)
        if (same_name(devices[i].name, device->name)) {
            spinlock_unlock_irqrestore(&storage_lock, flags);
            return 0;
        }
    devices[device_count++] = *device;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return 1;
}

uint32_t storage_device_count(void) {
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    uint32_t count = device_count;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return count;
}

const storage_device_t *storage_device_at(uint32_t index) {
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    const storage_device_t *device = index < device_count ? &devices[index] : 0;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return device;
}

int storage_read(uint32_t device, uint64_t lba, uint32_t count, void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    if (device >= device_count || !buffer || count == 0 ||
        lba >= devices[device].block_count ||
        count > devices[device].block_count - lba) {
        spinlock_unlock_irqrestore(&storage_lock, flags);
        return 0;
    }
    storage_read_fn read = devices[device].read;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return read(lba, count, buffer);
}

int storage_write(uint32_t device, uint64_t lba, uint32_t count,
                  const void *buffer) {
    uint64_t flags = spinlock_lock_irqsave(&storage_lock);
    if (device >= device_count || !buffer || count == 0 ||
        lba >= devices[device].block_count ||
        count > devices[device].block_count - lba) {
        spinlock_unlock_irqrestore(&storage_lock, flags);
        return 0;
    }
    storage_write_fn write = devices[device].write;
    spinlock_unlock_irqrestore(&storage_lock, flags);
    return write(lba, count, buffer);
}
