#ifndef OS_KERNEL_FS_BLOCK_H
#define OS_KERNEL_FS_BLOCK_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

#define BLOCK_MAX_DEVICES 16U

typedef int (*block_read_fn)(void *context, uint64_t sector,
                             uint32_t count, void *buffer);
typedef int (*block_write_fn)(void *context, uint64_t sector,
                              uint32_t count, const void *buffer);
typedef int (*block_flush_fn)(void *context);

typedef struct {
    char name[32];
    uint64_t sector_count;
    uint32_t sector_size;
    void *context;
    block_read_fn read;
    block_write_fn write;
    block_flush_fn flush;
    uint8_t registered;
} block_device_t;

typedef struct {
    spinlock_t lock;
    block_device_t devices[BLOCK_MAX_DEVICES];
} block_registry_t;

void block_registry_initialize(block_registry_t *registry);
int block_registry_register(block_registry_t *registry,
                            const block_device_t *device);
block_device_t *block_registry_at(block_registry_t *registry, uint32_t index);
int block_registry_read(block_registry_t *registry, uint32_t index,
                        uint64_t sector, uint32_t count, void *buffer);
int block_registry_write(block_registry_t *registry, uint32_t index,
                         uint64_t sector, uint32_t count, const void *buffer);
int block_registry_flush(block_registry_t *registry, uint32_t index);

#endif
