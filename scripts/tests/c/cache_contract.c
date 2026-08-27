#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../kernel/fs/cache/cache.h"

static uint8_t disk[32U * 512U];
static uint32_t reads;
static uint32_t writes;
static uint32_t flushes;

static int disk_read(void *context, uint64_t sector, uint32_t count,
                     void *buffer) {
    (void)context;
    if (!buffer || count == 0 || sector >= 32U || count > 32U - sector) return 0;
    memcpy(buffer, &disk[sector * 512U], count * 512U);
    ++reads;
    return 1;
}

static int disk_write(void *context, uint64_t sector, uint32_t count,
                      const void *buffer) {
    (void)context;
    if (!buffer || count == 0 || sector >= 32U || count > 32U - sector) return 0;
    memcpy(&disk[sector * 512U], buffer, count * 512U);
    ++writes;
    return 1;
}

static int disk_flush(void *context) {
    (void)context;
    ++flushes;
    return 1;
}

int main(void) {
    block_registry_t registry;
    block_cache_t cache;
    block_device_t device = {
        .name = "ram-cache", .sector_count = 32, .sector_size = 512,
        .read = disk_read, .write = disk_write, .flush = disk_flush
    };
    uint8_t buffer[512];
    uint8_t value[512];
    memset(disk, 0, sizeof(disk));
    memset(value, 0x5a, sizeof(value));
    block_registry_initialize(&registry);
    assert(block_registry_register(&registry, &device));
    block_cache_initialize(&cache);
    assert(block_registry_flush(&registry, 0) && flushes == 1);
    assert(block_cache_flush(&cache, &registry, 0) && flushes == 2);

    assert(block_cache_read(&cache, &registry, 0, 0, buffer, sizeof(buffer)));
    assert(reads == 1);
    assert(block_cache_read(&cache, &registry, 0, 0, buffer, sizeof(buffer)));
    assert(reads == 1);
    assert(block_cache_write(&cache, &registry, 0, 0, value, sizeof(value)));
    assert(writes == 1);
    memset(buffer, 0, sizeof(buffer));
    assert(block_cache_read(&cache, &registry, 0, 0, buffer, sizeof(buffer)));
    assert(memcmp(buffer, value, sizeof(buffer)) == 0 && reads == 1);

    memset(disk, 0xa5, sizeof(buffer));
    block_cache_invalidate(&cache, &registry, 0, 0);
    assert(block_cache_read(&cache, &registry, 0, 0, buffer, sizeof(buffer)));
    assert(buffer[0] == 0xa5 && reads == 2);

    for (uint32_t sector = 1; sector <= BLOCK_CACHE_ENTRIES; ++sector)
        assert(block_cache_read(&cache, &registry, 0, sector, buffer,
                                sizeof(buffer)));
    uint32_t reads_before = reads;
    assert(block_cache_read(&cache, &registry, 0, 0, buffer, sizeof(buffer)));
    assert(reads > reads_before);
    block_cache_invalidate_device(&cache, &registry, 0);
    assert(block_cache_read(&cache, &registry, 0, 1, buffer, sizeof(buffer)));
    assert(reads > reads_before + 1U);
    return 0;
}
