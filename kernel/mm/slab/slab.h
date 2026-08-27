#ifndef OS_KERNEL_MM_SLAB_H
#define OS_KERNEL_MM_SLAB_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

typedef struct {
    uint8_t *storage;
    uint8_t *used;
    uint32_t object_size;
    uint32_t capacity;
    uint32_t available;
    spinlock_t lock;
} slab_cache_t;

int slab_cache_initialize(slab_cache_t *cache, void *storage,
                          uint32_t object_size, uint32_t capacity,
                          uint8_t *used);
void *slab_cache_allocate(slab_cache_t *cache);
int slab_cache_free(slab_cache_t *cache, void *object);
uint32_t slab_cache_available(const slab_cache_t *cache);

#endif
