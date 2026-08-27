#ifndef OS_KERNEL_FS_CACHE_H
#define OS_KERNEL_FS_CACHE_H

#include <stdint.h>
#include "../block/block.h"

#define BLOCK_CACHE_ENTRIES 16U
#define BLOCK_CACHE_SECTOR_MAX 512U

typedef struct {
    block_registry_t *registry;
    uint32_t device;
    uint64_t sector;
    uint32_t sector_size;
    uint64_t age;
    uint8_t valid;
    uint8_t data[BLOCK_CACHE_SECTOR_MAX];
} block_cache_entry_t;

typedef struct {
    spinlock_t lock;
    uint64_t clock;
    block_cache_entry_t entries[BLOCK_CACHE_ENTRIES];
} block_cache_t;

void block_cache_initialize(block_cache_t *cache);
int block_cache_read(block_cache_t *cache, block_registry_t *registry,
                     uint32_t device, uint64_t sector, void *buffer,
                     uint32_t size);
int block_cache_write(block_cache_t *cache, block_registry_t *registry,
                      uint32_t device, uint64_t sector, const void *buffer,
                      uint32_t size);
int block_cache_flush(block_cache_t *cache, block_registry_t *registry,
                      uint32_t device);
void block_cache_invalidate(block_cache_t *cache, block_registry_t *registry,
                            uint32_t device, uint64_t sector);
void block_cache_invalidate_device(block_cache_t *cache,
                                   block_registry_t *registry,
                                   uint32_t device);

#endif
