#ifndef OS_KERNEL_FS_STORAGE_BLOCK_H
#define OS_KERNEL_FS_STORAGE_BLOCK_H

#include "block.h"
#include "../../drivers/storage/storage.h"

typedef struct {
    uint32_t storage_device;
} storage_block_context_t;

int storage_block_bind(block_device_t *block, storage_block_context_t *context,
                       const char *name, uint32_t storage_device);

#endif
