#include "slab.h"

int slab_cache_initialize(slab_cache_t *cache, void *storage,
                          uint32_t object_size, uint32_t capacity,
                          uint8_t *used) {
    if (!cache || !storage || !used || object_size == 0 || capacity == 0)
        return 0;
    cache->storage = (uint8_t *)storage;
    cache->used = used;
    cache->object_size = object_size;
    cache->capacity = capacity;
    cache->available = capacity;
    for (uint32_t i = 0; i < capacity; ++i) used[i] = 0;
    return 1;
}

void *slab_cache_allocate(slab_cache_t *cache) {
    if (!cache || cache->available == 0) return 0;
    for (uint32_t i = 0; i < cache->capacity; ++i) {
        if (cache->used[i]) continue;
        cache->used[i] = 1;
        --cache->available;
        return cache->storage + (uint64_t)i * cache->object_size;
    }
    return 0;
}

int slab_cache_free(slab_cache_t *cache, void *object) {
    if (!cache || !object) return 0;
    uintptr_t start = (uintptr_t)cache->storage;
    uintptr_t address = (uintptr_t)object;
    uint64_t span = (uint64_t)cache->object_size * cache->capacity;
    if (address < start || address - start >= span ||
        (address - start) % cache->object_size != 0) return 0;
    uint32_t index = (uint32_t)((address - start) / cache->object_size);
    if (!cache->used[index]) return 0;
    cache->used[index] = 0;
    ++cache->available;
    return 1;
}

uint32_t slab_cache_available(const slab_cache_t *cache) {
    return cache ? cache->available : 0;
}
