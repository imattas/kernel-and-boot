#include "storage_block.h"

static int storage_block_read(void *opaque, uint64_t sector, uint32_t count,
                              void *buffer) {
    storage_block_context_t *context = (storage_block_context_t *)opaque;
    return context && storage_read(context->storage_device, sector, count,
                                   buffer);
}

static int storage_block_write(void *opaque, uint64_t sector, uint32_t count,
                               const void *buffer) {
    storage_block_context_t *context = (storage_block_context_t *)opaque;
    return context && storage_write(context->storage_device, sector, count,
                                    buffer);
}

static int storage_block_flush(void *opaque) {
    storage_block_context_t *context = (storage_block_context_t *)opaque;
    return context && storage_flush(context->storage_device);
}

int storage_block_bind(block_device_t *block, storage_block_context_t *context,
                       const char *name, uint32_t storage_device) {
    if (!block || !context || !name || !name[0]) return 0;
    const storage_device_t *source = storage_device_at(storage_device);
    if (!source || source->block_size == 0 || source->block_size > UINT32_MAX)
        return 0;
    uint32_t length = 0;
    while (length < sizeof(block->name) - 1U && name[length] != 0) ++length;
    if (name[length] != 0) return 0;
    for (uint32_t i = 0; i <= length; ++i) block->name[i] = name[i];
    context->storage_device = storage_device;
    block->sector_count = source->block_count;
    block->sector_size = source->block_size;
    block->context = context;
    block->read = storage_block_read;
    block->write = storage_block_write;
    block->flush = source->flush ? storage_block_flush : 0;
    block->registered = 0;
    return 1;
}
