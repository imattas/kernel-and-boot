#include "block.h"

static uint32_t name_length(const char *name) {
    uint32_t length = 0;
    while (name && name[length] != '\0') ++length;
    return length;
}

void block_registry_initialize(block_registry_t *registry) {
    if (!registry) return;
    spinlock_init(&registry->lock);
    for (uint32_t i = 0; i < BLOCK_MAX_DEVICES; ++i)
        registry->devices[i].registered = 0;
}

int block_registry_register(block_registry_t *registry,
                            const block_device_t *device) {
    uint32_t length = device ? name_length(device->name) : 0;
    if (!registry || !device || length == 0 || length >= 32 ||
        device->sector_count == 0 || device->sector_size == 0 ||
        !device->read || !device->write)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&registry->lock);
    for (uint32_t i = 0; i < BLOCK_MAX_DEVICES; ++i) {
        if (registry->devices[i].registered &&
            name_length(registry->devices[i].name) == length) {
            uint32_t j = 0;
            while (j < length &&
                   registry->devices[i].name[j] == device->name[j]) ++j;
            if (j == length) {
                spinlock_unlock_irqrestore(&registry->lock, flags);
                return 0;
            }
        }
        if (registry->devices[i].registered) continue;
        registry->devices[i] = *device;
        registry->devices[i].registered = 1;
        spinlock_unlock_irqrestore(&registry->lock, flags);
        return 1;
    }
    spinlock_unlock_irqrestore(&registry->lock, flags);
    return 0;
}

block_device_t *block_registry_at(block_registry_t *registry, uint32_t index) {
    if (!registry || index >= BLOCK_MAX_DEVICES) return 0;
    uint64_t flags = spinlock_lock_irqsave(&registry->lock);
    block_device_t *device = registry->devices[index].registered ?
        &registry->devices[index] : 0;
    spinlock_unlock_irqrestore(&registry->lock, flags);
    return device;
}

static int valid_request(const block_device_t *device, uint64_t sector,
                         uint32_t count, const void *buffer) {
    return device && count != 0 && buffer && sector < device->sector_count &&
           (uint64_t)count <= device->sector_count - sector;
}

int block_registry_read(block_registry_t *registry, uint32_t index,
                        uint64_t sector, uint32_t count, void *buffer) {
    if (!registry || index >= BLOCK_MAX_DEVICES) return 0;
    uint64_t flags = spinlock_lock_irqsave(&registry->lock);
    block_device_t *device = &registry->devices[index];
    int valid = device->registered && valid_request(device, sector, count, buffer);
    block_read_fn read = valid ? device->read : 0;
    void *context = valid ? device->context : 0;
    spinlock_unlock_irqrestore(&registry->lock, flags);
    return read ? read(context, sector, count, buffer) : 0;
}

int block_registry_write(block_registry_t *registry, uint32_t index,
                         uint64_t sector, uint32_t count, const void *buffer) {
    if (!registry || index >= BLOCK_MAX_DEVICES) return 0;
    uint64_t flags = spinlock_lock_irqsave(&registry->lock);
    block_device_t *device = &registry->devices[index];
    int valid = device->registered && valid_request(device, sector, count, buffer);
    block_write_fn write = valid ? device->write : 0;
    void *context = valid ? device->context : 0;
    spinlock_unlock_irqrestore(&registry->lock, flags);
    return write ? write(context, sector, count, buffer) : 0;
}
