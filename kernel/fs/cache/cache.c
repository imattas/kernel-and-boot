#include "cache.h"

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) destination[i] = source[i];
}

static block_cache_entry_t *find_entry(block_cache_t *cache,
                                       block_registry_t *registry,
                                       uint32_t device, uint64_t sector) {
    for (uint32_t i = 0; i < BLOCK_CACHE_ENTRIES; ++i) {
        block_cache_entry_t *entry = &cache->entries[i];
        if (entry->valid && entry->registry == registry &&
            entry->device == device && entry->sector == sector) return entry;
    }
    return 0;
}

static block_cache_entry_t *choose_entry(block_cache_t *cache) {
    block_cache_entry_t *chosen = &cache->entries[0];
    for (uint32_t i = 0; i < BLOCK_CACHE_ENTRIES; ++i) {
        block_cache_entry_t *entry = &cache->entries[i];
        if (!entry->valid) return entry;
        if (entry->age < chosen->age) chosen = entry;
    }
    return chosen;
}

void block_cache_initialize(block_cache_t *cache) {
    if (!cache) return;
    spinlock_init(&cache->lock);
    cache->clock = 0;
    for (uint32_t i = 0; i < BLOCK_CACHE_ENTRIES; ++i)
        cache->entries[i].valid = 0;
}

int block_cache_read(block_cache_t *cache, block_registry_t *registry,
                     uint32_t device, uint64_t sector, void *buffer,
                     uint32_t size) {
    if (!cache || !registry || !buffer || size == 0 ||
        size > BLOCK_CACHE_SECTOR_MAX) return 0;
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    block_device_t *descriptor = block_registry_at(registry, device);
    if (!descriptor || descriptor->sector_size != size) {
        spinlock_unlock_irqrestore(&cache->lock, flags);
        return 0;
    }
    block_cache_entry_t *entry = find_entry(cache, registry, device, sector);
    if (entry) {
        entry->age = ++cache->clock;
        copy_bytes((uint8_t *)buffer, entry->data, size);
        spinlock_unlock_irqrestore(&cache->lock, flags);
        return 1;
    }
    spinlock_unlock_irqrestore(&cache->lock, flags);

    uint8_t fetched[BLOCK_CACHE_SECTOR_MAX];
    if (!block_registry_read(registry, device, sector, 1, fetched)) return 0;
    flags = spinlock_lock_irqsave(&cache->lock);
    entry = find_entry(cache, registry, device, sector);
    if (!entry) {
        entry = choose_entry(cache);
        entry->registry = registry;
        entry->device = device;
        entry->sector = sector;
        entry->sector_size = size;
        entry->valid = 1;
        copy_bytes(entry->data, fetched, size);
    }
    entry->age = ++cache->clock;
    copy_bytes((uint8_t *)buffer, entry->data, size);
    spinlock_unlock_irqrestore(&cache->lock, flags);
    return 1;
}

int block_cache_write(block_cache_t *cache, block_registry_t *registry,
                      uint32_t device, uint64_t sector, const void *buffer,
                      uint32_t size) {
    if (!cache || !registry || !buffer || size == 0 ||
        size > BLOCK_CACHE_SECTOR_MAX) return 0;
    uint8_t pending[BLOCK_CACHE_SECTOR_MAX];
    copy_bytes(pending, (const uint8_t *)buffer, size);
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    block_device_t *descriptor = block_registry_at(registry, device);
    if (!descriptor || descriptor->sector_size != size) {
        spinlock_unlock_irqrestore(&cache->lock, flags);
        return 0;
    }
    spinlock_unlock_irqrestore(&cache->lock, flags);
    if (!block_registry_write(registry, device, sector, 1, pending)) return 0;
    flags = spinlock_lock_irqsave(&cache->lock);
    block_cache_entry_t *entry = find_entry(cache, registry, device, sector);
    if (!entry) entry = choose_entry(cache);
    entry->registry = registry;
    entry->device = device;
    entry->sector = sector;
    entry->sector_size = size;
    entry->valid = 1;
    entry->age = ++cache->clock;
    copy_bytes(entry->data, pending, size);
    spinlock_unlock_irqrestore(&cache->lock, flags);
    return 1;
}

void block_cache_invalidate(block_cache_t *cache, block_registry_t *registry,
                            uint32_t device, uint64_t sector) {
    if (!cache || !registry) return;
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    block_cache_entry_t *entry = find_entry(cache, registry, device, sector);
    if (entry) entry->valid = 0;
    spinlock_unlock_irqrestore(&cache->lock, flags);
}

void block_cache_invalidate_device(block_cache_t *cache,
                                   block_registry_t *registry,
                                   uint32_t device) {
    if (!cache || !registry) return;
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    for (uint32_t i = 0; i < BLOCK_CACHE_ENTRIES; ++i) {
        block_cache_entry_t *entry = &cache->entries[i];
        if (entry->valid && entry->registry == registry &&
            entry->device == device) entry->valid = 0;
    }
    spinlock_unlock_irqrestore(&cache->lock, flags);
}
