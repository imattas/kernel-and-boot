#include "btrfs.h"
#include "deflate.h"
#include "lzo.h"
#include "zstd.h"
#include "../../drivers/storage/storage.h"
#include "../../mm/heap/heap.h"

#define BTRFS_SECTOR_SIZE 512U
#define BTRFS_SUPERBLOCK_SECTOR 128U
#define BTRFS_CHUNK_ITEM_TYPE 0x21U
#define BTRFS_ROOT_ITEM_TYPE 132U
#define BTRFS_FS_TREE_OBJECTID 5ULL
#define BTRFS_EXTENT_DATA_TYPE 108U
#define BTRFS_INODE_ITEM_TYPE 1U
#define BTRFS_DIR_ITEM_TYPE 84U
#define BTRFS_EXTENT_CSUM_OBJECTID (UINT64_MAX - 9ULL)
#define BTRFS_EXTENT_CSUM_TYPE 128U
#define BTRFS_CSUM_TREE_OBJECTID 7ULL
#define BTRFS_BLOCK_GROUP_RAID0 8ULL
#define BTRFS_BLOCK_GROUP_RAID1 2ULL
#define BTRFS_BLOCK_GROUP_DUP 16ULL
#define BTRFS_BLOCK_GROUP_RAID10 64ULL
#define BTRFS_BLOCK_GROUP_RAID5 128ULL
#define BTRFS_BLOCK_GROUP_RAID6 256ULL
#define BTRFS_BLOCK_GROUP_RAID1C3 512ULL
#define BTRFS_BLOCK_GROUP_RAID1C4 1024ULL

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static void store32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void store64(uint8_t *p, uint64_t value) {
    store32(p, (uint32_t)value);
    store32(p + 4, (uint32_t)(value >> 32));
}

static int btrfs_map_at(const btrfs_fs_t *fs, uint64_t logical, uint32_t bytes,
                        uint8_t mirror, uint32_t *device, uint64_t *physical) {
    if (!fs || !device || !physical || !bytes) return 0;
    for (uint32_t i = 0; i < fs->chunk_count; ++i) {
        const btrfs_chunk_t *chunk = &fs->chunks[i];
        if (logical >= chunk->logical && logical - chunk->logical < chunk->length &&
            bytes <= chunk->length - (logical - chunk->logical)) {
            uint64_t delta = logical - chunk->logical;
            uint32_t mapped_device = mirror && chunk->mirror_physical ?
                                     chunk->mirror_device : chunk->device;
            uint64_t base = mirror && chunk->mirror_physical ?
                            chunk->mirror_physical : chunk->physical;
            if (base > UINT64_MAX - delta) return 0;
            *device = mapped_device;
            *physical = base + delta;
            return 1;
        }
    }
    return 0;
}

static int btrfs_map(const btrfs_fs_t *fs, uint64_t logical, uint32_t bytes,
                     uint32_t *device, uint64_t *physical) {
    return btrfs_map_at(fs, logical, bytes, 0, device, physical);
}

static uint32_t crc32c(const uint8_t *data, uint32_t length) {
    uint32_t crc = ~0U;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0x82f63b78U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static int btrfs_register_device(btrfs_fs_t *fs, uint64_t devid, uint32_t device) {
    for (uint32_t i = 0; i < fs->device_count; ++i)
        if (fs->devices[i].devid == devid) return fs->devices[i].storage_device == device;
    if (fs->device_count >= BTRFS_MAX_DEVICES) return 0;
    fs->devices[fs->device_count++] = (btrfs_device_t){devid, device};
    return 1;
}

static int btrfs_resolve_device(btrfs_fs_t *fs, uint64_t devid, uint32_t *device) {
    if (!fs || !device) return 0;
    for (uint32_t i = 0; i < fs->device_count; ++i)
        if (fs->devices[i].devid == devid) { *device = fs->devices[i].storage_device; return 1; }
    uint8_t sb[4096];
    for (uint32_t candidate = 0; candidate < storage_device_count(); ++candidate) {
        const storage_device_t *storage = storage_device_at(candidate);
        if (!storage || candidate == fs->device || storage->block_size != BTRFS_SECTOR_SIZE ||
            storage->block_count < 136U || !storage_read(candidate, BTRFS_SUPERBLOCK_SECTOR, 8, sb) ||
            le64(&sb[0x40]) != 0x4d5f53665248425fULL || le64(&sb[0x100]) != devid)
            continue;
        uint8_t same_fsid = 1;
        for (uint32_t i = 0; i < sizeof(fs->fsid); ++i)
            if (sb[0x20 + i] != fs->fsid[i]) same_fsid = 0;
        uint32_t checksum = le32(sb);
        sb[0] = sb[1] = sb[2] = sb[3] = 0;
        if (!same_fsid || crc32c(&sb[32], sizeof(sb) - 32U) != checksum) continue;
        if (!btrfs_register_device(fs, devid, candidate)) return 0;
        *device = candidate;
        return 1;
    }
    return 0;
}

static uint32_t name_hash(const char *name, uint32_t length) {
    uint32_t crc = ~1U;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= (uint8_t)name[i];
        for (uint32_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0x82f63b78U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static int btrfs_read_node(const btrfs_fs_t *fs, uint64_t bytenr, uint8_t *node) {
    uint32_t device; uint64_t physical;
    if (!fs || !fs->mounted || !node || bytenr % fs->sector_size != 0 ||
        bytenr > fs->total_bytes - fs->node_size ||
        fs->node_size / BTRFS_SECTOR_SIZE == 0 ||
        !btrfs_map(fs, bytenr, fs->node_size, &device, &physical)) return 0;
    uint8_t primary_read = storage_read(device, physical / BTRFS_SECTOR_SIZE,
                                        fs->node_size / BTRFS_SECTOR_SIZE, node);
    uint32_t checksum = 0; uint8_t valid = 0;
    if (primary_read) {
        checksum = le32(node); node[0] = node[1] = node[2] = node[3] = 0;
        valid = crc32c(&node[32], fs->node_size - 32U) == checksum;
        for (uint32_t i = 0; i < sizeof(fs->fsid); ++i)
            if (node[32 + i] != fs->fsid[i]) valid = 0;
        if (le64(&node[48]) != bytenr || node[100] > 8) valid = 0;
    }
    if (valid) return 1;
    if (!btrfs_map_at(fs, bytenr, fs->node_size, 1, &device, &physical) ||
        physical % BTRFS_SECTOR_SIZE != 0 ||
        !storage_read(device, physical / BTRFS_SECTOR_SIZE,
                      fs->node_size / BTRFS_SECTOR_SIZE, node)) return 0;
    checksum = le32(node); node[0] = node[1] = node[2] = node[3] = 0;
    if (crc32c(&node[32], fs->node_size - 32U) != checksum) return 0;
    for (uint32_t i = 0; i < sizeof(fs->fsid); ++i)
        if (node[32 + i] != fs->fsid[i]) return 0;
    return le64(&node[48]) == bytenr && node[100] <= 8;
}

static int btrfs_add_chunk(btrfs_fs_t *fs, uint64_t logical, uint64_t length,
                           uint64_t devid, uint64_t mirror_devid,
                           uint64_t physical, uint64_t mirror_physical) {
    uint32_t device = 0, mirror_device = 0;
    if (!btrfs_resolve_device(fs, devid, &device) ||
        (mirror_physical && !btrfs_resolve_device(fs, mirror_devid, &mirror_device))) return 0;
    const storage_device_t *primary = storage_device_at(device);
    const storage_device_t *mirror = mirror_physical ? storage_device_at(mirror_device) : 0;
    if (!length || length > fs->total_bytes || logical > fs->total_bytes - length ||
        !primary || length > primary->block_count * (uint64_t)BTRFS_SECTOR_SIZE ||
        physical > primary->block_count * (uint64_t)BTRFS_SECTOR_SIZE - length ||
        (mirror_physical && (!mirror || length > mirror->block_count *
         (uint64_t)BTRFS_SECTOR_SIZE || mirror_physical > mirror->block_count *
         (uint64_t)BTRFS_SECTOR_SIZE - length)))
        return 0;
    for (uint32_t i = 0; i < fs->chunk_count; ++i)
        if (fs->chunks[i].logical == logical && fs->chunks[i].length == length &&
            fs->chunks[i].device == device && fs->chunks[i].physical == physical &&
            fs->chunks[i].mirror_physical == mirror_physical) return 1;
    for (uint32_t i = 0; i < fs->chunk_count; ++i) {
        const btrfs_chunk_t *existing = &fs->chunks[i];
        if (logical < existing->logical + existing->length &&
            existing->logical < logical + length) return 0;
    }
    if (fs->chunk_count >= BTRFS_MAX_SYSTEM_CHUNKS) return 0;
    fs->chunks[fs->chunk_count++] =
        (btrfs_chunk_t){logical, length, device, mirror_physical ? mirror_device : 0,
                        physical, mirror_physical};
    return 1;
}

static int btrfs_chunk_first_stripe(const uint8_t *chunk, uint16_t stripes,
                                    uint64_t *devid, uint64_t *mirror_devid,
                                    uint64_t *physical, uint64_t *mirror_physical) {
    if (!chunk || !devid || !mirror_devid || !physical || !mirror_physical || !stripes) return 0;
    uint64_t flags = le64(&chunk[24]);
    if (
        (flags & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10 |
                  BTRFS_BLOCK_GROUP_RAID5 | BTRFS_BLOCK_GROUP_RAID6 |
                  BTRFS_BLOCK_GROUP_RAID1C3 | BTRFS_BLOCK_GROUP_RAID1C4)) != 0)
        return 0;
    if (stripes > 1 && (flags & (BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_DUP)) == 0)
        return 0;
    *devid = le64(&chunk[48]);
    if (*devid == 0) *devid = 1;
    if (stripes > 2) return 0;
    *physical = le64(&chunk[56]);
    *mirror_devid = stripes > 1 ? le64(&chunk[80]) : 0;
    if (*mirror_devid == 0) *mirror_devid = *devid;
    *mirror_physical = stripes > 1 ? le64(&chunk[88]) : 0;
    return 1;
}

static int btrfs_collect_chunks(btrfs_fs_t *fs, uint64_t bytenr, uint8_t depth) {
    uint8_t node[65536];
    if (depth > 8 || fs->node_size > sizeof(node) || !btrfs_read_node(fs, bytenr, node)) return 0;
    uint32_t count = le32(&node[96]);
    if (node[100] == 0) {
        if (count > (fs->node_size - 101U) / 25U) return 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *item = &node[101U + i * 25U];
            if (le64(item) != 1 || item[8] != BTRFS_CHUNK_ITEM_TYPE) continue;
            uint32_t offset = le32(&item[17]), size = le32(&item[21]);
            if (size < 80U || offset > fs->node_size || size > fs->node_size - offset) return 0;
            const uint8_t *chunk = &node[offset];
            uint64_t length = le64(chunk);
            uint16_t stripes = le16(&chunk[44]);
            uint64_t devid = 0, mirror_devid = 0, physical = 0, mirror_physical = 0;
            if (!btrfs_chunk_first_stripe(chunk, stripes, &devid, &mirror_devid,
                                          &physical, &mirror_physical) ||
                size < 48U + (uint32_t)stripes * 32U ||
                !btrfs_add_chunk(fs, le64(&item[9]), length, devid, mirror_devid,
                                 physical, mirror_physical)) return 0;
        }
        return 1;
    }
    if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!btrfs_collect_chunks(fs, le64(&node[118U + i * 33U]), (uint8_t)(depth + 1U))) return 0;
    return 1;
}

static int key_compare(const uint8_t *key, uint64_t objectid, uint8_t type,
                       uint64_t offset) {
    uint64_t key_objectid = le64(key), key_offset = le64(&key[9]);
    if (key_objectid != objectid) return key_objectid < objectid ? -1 : 1;
    if (key[8] != type) return key[8] < type ? -1 : 1;
    if (key_offset != offset) return key_offset < offset ? -1 : 1;
    return 0;
}

int btrfs_mount(btrfs_fs_t *fs, uint32_t device) {
    if (!fs || !storage_device_at(device) ||
        storage_device_at(device)->block_size != BTRFS_SECTOR_SIZE) return 0;
    uint8_t sb[4096];
    const uint64_t mirrors[] = {64ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL,
                                256ULL * 1024ULL * 1024ULL * 1024ULL};
    uint8_t valid = 0;
    for (uint32_t i = 0; i < sizeof(mirrors) / sizeof(mirrors[0]); ++i) {
        uint64_t lba = mirrors[i] / BTRFS_SECTOR_SIZE;
        if (mirrors[i] % BTRFS_SECTOR_SIZE != 0 || storage_device_at(device)->block_count < 8U ||
            lba > storage_device_at(device)->block_count - 8U ||
            !storage_read(device, lba, 8, sb) || le64(&sb[0x40]) != 0x4d5f53665248425fULL ||
            le16(&sb[0xc0]) != 0) continue;
        uint32_t checksum = le32(sb);
        sb[0] = sb[1] = sb[2] = sb[3] = 0;
        if (crc32c(&sb[32], sizeof(sb) - 32U) != checksum) continue;
        valid = 1;
        break;
    }
    if (!valid) return 0;
    uint32_t sector_size = le32(&sb[0x90]);
    uint32_t node_size = le32(&sb[0x94]);
    uint64_t total_bytes = le64(&sb[0x70]);
    uint64_t root = le64(&sb[0x50]);
    uint64_t chunk_root = le64(&sb[0x58]);
    uint64_t device_blocks = storage_device_at(device)->block_count;
    if (sector_size < 512 || sector_size > 4096 || (sector_size & (sector_size - 1U)) != 0 ||
        node_size < sector_size || node_size > 65536 || (node_size & (node_size - 1U)) != 0 ||
        total_bytes < node_size || root < sector_size || chunk_root < sector_size ||
        root >= total_bytes || chunk_root >= total_bytes ||
        device_blocks > UINT64_MAX / BTRFS_SECTOR_SIZE ||
        total_bytes > device_blocks * (uint64_t)BTRFS_SECTOR_SIZE ||
        (root % sector_size) != 0 || (chunk_root % sector_size) != 0) return 0;
    fs->device = device; fs->sector_size = sector_size; fs->node_size = node_size;
    fs->total_bytes = total_bytes; fs->root_bytenr = root; fs->chunk_count = 0;
    fs->chunk_root_bytenr = chunk_root; fs->mounted = 1; fs->device_count = 0;
    for (uint32_t i = 0; i < sizeof(fs->fsid); ++i) fs->fsid[i] = sb[0x20 + i];
    fs->primary_devid = le64(&sb[0x100]);
    if (fs->primary_devid == 0) fs->primary_devid = 1;
    if (!btrfs_register_device(fs, fs->primary_devid, device)) { fs->mounted = 0; return 0; }
    uint32_t array_size = le32(&sb[0xa0]);
    uint32_t position = 0;
    if (array_size > 2048U || 0x2c0U + array_size > sizeof(sb)) { fs->mounted = 0; return 0; }
    while (position + 17U + 44U + 32U <= array_size) {
        const uint8_t *key = &sb[0x2c0U + position];
        uint64_t logical = le64(&key[9]);
        uint64_t length = le64(&key[17]);
        uint32_t num_stripes = le16(&key[17 + 44]);
        uint64_t devid = 0, mirror_devid = 0, physical = 0, mirror_physical = 0;
        if (le64(key) != 1 || key[8] != BTRFS_CHUNK_ITEM_TYPE || !length || !num_stripes ||
            fs->chunk_count >= BTRFS_MAX_SYSTEM_CHUNKS || logical > total_bytes - length) {
            fs->mounted = 0; return 0;
        }
        if (position + 17U + 44U + num_stripes * 32U > array_size ||
            !btrfs_chunk_first_stripe(&key[17], num_stripes, &devid, &mirror_devid,
                                      &physical, &mirror_physical)) {
            fs->mounted = 0; return 0;
        }
        if (!btrfs_add_chunk(fs, logical, length, devid, mirror_devid,
                             physical, mirror_physical)) { fs->mounted = 0; return 0; }
        position += 17U + 44U + num_stripes * 32U;
    }
    if (position != array_size || fs->chunk_count == 0 ||
        !btrfs_collect_chunks(fs, fs->chunk_root_bytenr, 0)) { fs->mounted = 0; return 0; }
    return 1;
}

static int btrfs_read_item_full(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t objectid,
                                uint8_t type, uint64_t offset, void *buffer,
                                uint32_t capacity, uint32_t *data_size_out) {
    uint8_t node[65536];
    if (!fs || !buffer || !capacity || !data_size_out || fs->node_size > sizeof(node) ||
        !btrfs_read_node(fs, tree_bytenr, node)) return 0;
    for (;;) {
        uint32_t count = le32(&node[96]);
        if (node[100] == 0) {
            if (count > (fs->node_size - 101U) / 25U) return 0;
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t *item = &node[101U + i * 25U];
                if (key_compare(item, objectid, type, offset) != 0) continue;
                uint32_t data_offset = le32(&item[17]);
                uint32_t data_size = le32(&item[21]);
                if (data_offset > fs->node_size || data_size > fs->node_size - data_offset ||
                    data_size > capacity) return 0;
                for (uint32_t j = 0; j < data_size; ++j) ((uint8_t *)buffer)[j] = node[data_offset + j];
                *data_size_out = data_size;
                return 1;
            }
            return 0;
        }
        if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U) return 0;
        const uint8_t *selected = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *item = &node[101U + i * 33U];
            if (key_compare(item, objectid, type, offset) > 0) break;
            selected = item;
        }
        if (!selected || !btrfs_read_node(fs, le64(&selected[17]), node)) return 0;
    }
}

int btrfs_read_item(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t objectid,
                    uint8_t type, uint64_t offset, void *buffer, uint32_t size) {
    uint8_t item[4096];
    uint32_t data_size = 0;
    if (!size || !btrfs_read_item_full(fs, tree_bytenr, objectid, type, offset,
                                       item, sizeof(item), &data_size) || data_size < size)
        return 0;
    for (uint32_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = item[i];
    return 1;
}

int btrfs_resolve_filesystem_tree(btrfs_fs_t *fs) {
    uint8_t root_item[256];
    if (!fs || !fs->mounted ||
        !btrfs_read_item(fs, fs->root_bytenr, BTRFS_FS_TREE_OBJECTID,
                         BTRFS_ROOT_ITEM_TYPE, BTRFS_FS_TREE_OBJECTID,
                         root_item, 184U)) return 0;
    uint64_t root = le64(&root_item[176]);
    if (root < fs->sector_size || root >= fs->total_bytes || root % fs->sector_size != 0) return 0;
    fs->fs_root_bytenr = root;
    if (!btrfs_read_item(fs, fs->root_bytenr, BTRFS_CSUM_TREE_OBJECTID,
                         BTRFS_ROOT_ITEM_TYPE, BTRFS_CSUM_TREE_OBJECTID,
                         root_item, 184U)) return 0;
    uint64_t csum_root = le64(&root_item[176]);
    if (csum_root < fs->sector_size || csum_root >= fs->total_bytes ||
        csum_root % fs->sector_size != 0) return 0;
    fs->csum_root_bytenr = csum_root;
    return 1;
}

int btrfs_inode_stat(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                     uint64_t *size, uint32_t *mode) {
    uint8_t item[160];
    if (!fs || !size || !mode ||
        !btrfs_read_item(fs, tree_bytenr, inode, BTRFS_INODE_ITEM_TYPE, 0, item, sizeof(item))) return 0;
    *size = le64(&item[16]); *mode = le32(&item[96]);
    return 1;
}

int btrfs_lookup_dir(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t directory,
                     const char *name, uint64_t *inode_number) {
    uint8_t item[4096]; uint32_t item_size = 0, position = 0, length = 0;
    if (!fs || !name || !name[0] || !inode_number) return 0;
    while (name[length] && length < 256U) ++length;
    if (length == 0 || length == 256U) return 0;
    uint32_t hash = name_hash(name, length);
    if (!btrfs_read_item_full(fs, tree_bytenr, directory, BTRFS_DIR_ITEM_TYPE,
                              hash, item, sizeof(item), &item_size)) return 0;
    while (position + 30U <= item_size) {
        uint16_t data_length = le16(&item[position + 25U]);
        uint16_t name_length = le16(&item[position + 27U]);
        uint32_t entry_size = 30U + (uint32_t)data_length + (uint32_t)name_length;
        if (!entry_size || entry_size > item_size - position ||
            data_length != 0) return 0;
        if (name_length == length) {
            uint8_t match = 1;
            for (uint32_t i = 0; i < length; ++i)
                if (item[position + 30U + i] != (uint8_t)name[i]) match = 0;
            if (match) {
                *inode_number = le64(&item[position]);
                return *inode_number != 0;
            }
        }
        position += entry_size;
    }
    return 0;
}

static int btrfs_find_data_csum(btrfs_fs_t *fs, uint64_t bytenr, uint64_t logical,
                                uint8_t depth, uint32_t *checksum) {
    uint8_t node[65536];
    if (!fs || !checksum || depth > 8 || fs->node_size > sizeof(node) ||
        !btrfs_read_node(fs, bytenr, node)) return 0;
    uint32_t count = le32(&node[96]);
    if (node[100] == 0) {
        if (count > (fs->node_size - 101U) / 25U) return 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *item = &node[101U + i * 25U];
            if (le64(item) != BTRFS_EXTENT_CSUM_OBJECTID || item[8] != BTRFS_EXTENT_CSUM_TYPE)
                continue;
            uint64_t start = le64(&item[9]);
            uint32_t offset = le32(&item[17]), size = le32(&item[21]);
            if (start > logical || logical - start > UINT64_MAX / sizeof(uint32_t) ||
                (logical - start) % fs->sector_size != 0 || size % sizeof(uint32_t) != 0 ||
                offset > fs->node_size || size > fs->node_size - offset) continue;
            uint64_t index = (logical - start) / fs->sector_size;
            if (index < size / sizeof(uint32_t)) {
                *checksum = le32(&node[offset + index * sizeof(uint32_t)]);
                return 1;
            }
        }
        return 0;
    }
    if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (btrfs_find_data_csum(fs, le64(&node[118U + i * 33U]), logical,
                                 (uint8_t)(depth + 1U), checksum)) return 1;
    return 0;
}

static int btrfs_validate_data(const btrfs_fs_t *fs, uint64_t logical,
                               const uint8_t *data, uint32_t bytes) {
    if (!fs || !data || !bytes || logical % fs->sector_size != 0 ||
        bytes % fs->sector_size != 0) return 0;
    for (uint32_t offset = 0; offset < bytes; offset += fs->sector_size) {
        uint32_t expected = 0;
        if (logical > UINT64_MAX - offset ||
            !btrfs_find_data_csum((btrfs_fs_t *)fs, fs->csum_root_bytenr,
                                  logical + offset, 0, &expected) ||
            crc32c(data + offset, fs->sector_size) != expected) return 0;
    }
    return 1;
}

static int btrfs_write_node(btrfs_fs_t *fs, uint64_t bytenr, uint8_t *node) {
    uint32_t device = 0; uint64_t physical = 0;
    if (!fs || !node || !btrfs_map(fs, bytenr, fs->node_size, &device, &physical) ||
        physical % BTRFS_SECTOR_SIZE != 0) return 0;
    store32(node, crc32c(&node[32], fs->node_size - 32U));
    if (!storage_write(device, physical / BTRFS_SECTOR_SIZE,
                       fs->node_size / BTRFS_SECTOR_SIZE, node)) return 0;
    uint32_t mirror_device = 0; uint64_t mirror_physical = 0;
    if (btrfs_map_at(fs, bytenr, fs->node_size, 1, &mirror_device,
                     &mirror_physical) &&
        (mirror_device != device || mirror_physical != physical)) {
        if (mirror_physical % BTRFS_SECTOR_SIZE != 0 ||
            !storage_write(mirror_device, mirror_physical / BTRFS_SECTOR_SIZE,
                           fs->node_size / BTRFS_SECTOR_SIZE, node)) return 0;
    }
    return 1;
}

static int btrfs_update_data_csum(btrfs_fs_t *fs, uint64_t bytenr,
                                  uint64_t logical, uint32_t checksum,
                                  uint8_t depth) {
    uint8_t node[65536];
    if (!fs || depth > 8 || fs->node_size > sizeof(node) ||
        !btrfs_read_node(fs, bytenr, node)) return 0;
    uint32_t count = le32(&node[96]);
    if (node[100] == 0) {
        if (count > (fs->node_size - 101U) / 25U) return 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t *item = &node[101U + i * 25U];
            if (le64(item) != BTRFS_EXTENT_CSUM_OBJECTID ||
                item[8] != BTRFS_EXTENT_CSUM_TYPE) continue;
            uint64_t start = le64(&item[9]);
            uint32_t offset = le32(&item[17]), size = le32(&item[21]);
            if (start > logical || logical - start > UINT64_MAX / sizeof(uint32_t) ||
                (logical - start) % fs->sector_size != 0 ||
                size % sizeof(uint32_t) != 0 || offset > fs->node_size ||
                size > fs->node_size - offset) continue;
            uint64_t index = (logical - start) / fs->sector_size;
            if (index >= size / sizeof(uint32_t)) continue;
            store32(&node[offset + index * sizeof(uint32_t)], checksum);
            return btrfs_write_node(fs, bytenr, node);
        }
        return 0;
    }
    if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (btrfs_update_data_csum(fs, le64(&node[118U + i * 33U]), logical,
                                   checksum, (uint8_t)(depth + 1U))) return 1;
    return 0;
}

static int btrfs_update_inode_size(btrfs_fs_t *fs, uint64_t bytenr,
                                   uint64_t inode, uint64_t size,
                                   uint8_t depth) {
    uint8_t node[65536];
    if (!fs || depth > 8 || fs->node_size > sizeof(node) ||
        !btrfs_read_node(fs, bytenr, node)) return 0;
    uint32_t count = le32(&node[96]);
    if (node[100] == 0) {
        if (count > (fs->node_size - 101U) / 25U) return 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t *item = &node[101U + i * 25U];
            if (key_compare(item, inode, BTRFS_INODE_ITEM_TYPE, 0) != 0) continue;
            uint32_t data_offset = le32(&item[17]);
            uint32_t data_size = le32(&item[21]);
            if (data_offset > fs->node_size || data_size < 24U ||
                data_size > fs->node_size - data_offset) return 0;
            store64(&node[data_offset + 16U], size);
            return btrfs_write_node(fs, bytenr, node);
        }
        return 0;
    }
    if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (btrfs_update_inode_size(fs, le64(&node[118U + i * 33U]),
                                    inode, size, (uint8_t)(depth + 1U))) return 1;
    return 0;
}

static int btrfs_update_inline_item(btrfs_fs_t *fs, uint64_t bytenr,
                                    uint64_t inode, uint64_t extent_offset,
                                    uint32_t relative, const uint8_t *source,
                                    uint32_t size, uint8_t depth) {
    uint8_t node[65536];
    if (!fs || !source || depth > 8 || fs->node_size > sizeof(node) ||
        !btrfs_read_node(fs, bytenr, node)) return 0;
    uint32_t count = le32(&node[96]);
    if (node[100] == 0) {
        if (count > (fs->node_size - 101U) / 25U) return 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t *item = &node[101U + i * 25U];
            if (key_compare(item, inode, BTRFS_EXTENT_DATA_TYPE,
                            extent_offset) != 0) continue;
            uint32_t data_offset = le32(&item[17]), data_size = le32(&item[21]);
            if (data_offset > fs->node_size || data_size > fs->node_size - data_offset ||
                data_size < 53U || relative > data_size - 53U ||
                size > data_size - 53U - relative) return 0;
            for (uint32_t j = 0; j < size; ++j)
                node[data_offset + 53U + relative + j] = source[j];
            return btrfs_write_node(fs, bytenr, node);
        }
        return 0;
    }
    if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U)
        return 0;
    const uint8_t *selected = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *item = &node[101U + i * 33U];
        if (key_compare(item, inode, BTRFS_EXTENT_DATA_TYPE, extent_offset) > 0)
            break;
        selected = item;
    }
    return selected && btrfs_update_inline_item(fs, le64(&selected[17]), inode,
                                                 extent_offset, relative, source,
                                                 size, (uint8_t)(depth + 1U));
}

static int btrfs_read_checked(btrfs_fs_t *fs, uint64_t logical, uint32_t bytes,
                              uint8_t *data) {
    uint32_t device = 0; uint64_t physical = 0;
    if (!fs || !data || !bytes || !btrfs_map(fs, logical, bytes, &device, &physical) ||
        physical % BTRFS_SECTOR_SIZE != 0) return 0;
    if (storage_read(device, physical / BTRFS_SECTOR_SIZE,
                     bytes / BTRFS_SECTOR_SIZE, data) &&
        btrfs_validate_data(fs, logical, data, bytes)) return 1;
    if (!btrfs_map_at(fs, logical, bytes, 1, &device, &physical) ||
        physical % BTRFS_SECTOR_SIZE != 0 ||
        !storage_read(device, physical / BTRFS_SECTOR_SIZE,
                      bytes / BTRFS_SECTOR_SIZE, data)) return 0;
    return btrfs_validate_data(fs, logical, data, bytes);
}

static int btrfs_lzo_inflate(const uint8_t *compressed, uint32_t compressed_size,
                             uint32_t start_offset, uint32_t sector_size,
                             uint8_t *decoded, uint32_t decoded_capacity,
                             uint32_t *decoded_size) {
    uint8_t *segment;
    uint32_t total, position = 4, output = 0;
    if (!compressed || compressed_size < 4 || !decoded || !decoded_size ||
        !sector_size || sector_size > 65536U || compressed_size > 65536U)
        return 0;
    total = le32(compressed);
    if (total < 4 || total > compressed_size) return 0;
    segment = (uint8_t *)kmalloc(sector_size);
    if (!segment) return 0;
    while (position < total) {
        uint32_t offset = (start_offset + position) % sector_size;
        uint32_t left = sector_size - offset;
        uint32_t length, produced = 0;
        if (left < 4) {
            while (position < total && left--) {
                if (compressed[position++] != 0) { kfree(segment); return 0; }
            }
            continue;
        }
        if (position > total - 4) { kfree(segment); return 0; }
        length = le32(&compressed[position]); position += 4;
        if (!length || length > total - position ||
            output > decoded_capacity || sector_size > decoded_capacity - output ||
            !btrfs_lzo1x_decompress(&compressed[position], length, segment,
                                     sector_size, &produced) || produced > sector_size ||
            produced > decoded_capacity - output)
            { kfree(segment); return 0; }
        for (uint32_t i = 0; i < produced; ++i) decoded[output + i] = segment[i];
        output += produced; position += length;
    }
    kfree(segment);
    *decoded_size = output;
    return position == total;
}

int btrfs_read_extent_data(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                           uint64_t extent_offset, uint64_t file_offset,
                           void *buffer, uint32_t size) {
    uint8_t item[4096], data[4096];
    if (!fs || !buffer || !size || fs->node_size > sizeof(item) ||
        file_offset < extent_offset ||
        !btrfs_read_item(fs, tree_bytenr, inode, BTRFS_EXTENT_DATA_TYPE,
                         extent_offset, item, 53U)) return 0;
    uint8_t extent_type = item[20];
    uint64_t relative = file_offset - extent_offset;
    uint64_t available;
    if (item[17] != 0 || item[18] != 0) return 0;
    if (extent_type == 0) {
        uint64_t ram_bytes = le64(&item[8]);
        uint32_t inline_size = 53U;
        if (ram_bytes < relative || relative > UINT32_MAX - inline_size ||
            size > ram_bytes - relative || inline_size > sizeof(item) ||
            size > sizeof(item) - inline_size - (uint32_t)relative ||
            !btrfs_read_item(fs, tree_bytenr, inode, BTRFS_EXTENT_DATA_TYPE,
                             extent_offset, item, inline_size + (uint32_t)relative + size)) return 0;
        for (uint32_t i = 0; i < size; ++i)
            ((uint8_t *)buffer)[i] = item[inline_size + (uint32_t)relative + i];
        return 1;
    }
    if (item[16] == 1) {
        uint64_t disk_bytenr = le64(&item[21]);
        uint64_t disk_size = le64(&item[29]);
        uint64_t data_offset = le64(&item[37]);
        uint64_t data_size = le64(&item[45]);
        if (extent_type != 1 || data_size == 0 || data_size > 65536U || disk_size == 0 ||
            disk_size > 65536U || data_offset > disk_size || relative > data_size ||
            size > data_size - relative || relative > disk_size - data_offset ||
            file_offset > UINT64_MAX - disk_bytenr - data_offset) return 0;
        uint64_t logical = disk_bytenr + data_offset;
        uint64_t sector_logical = logical - logical % fs->sector_size;
        uint32_t in_sector = (uint32_t)(logical - sector_logical);
        uint32_t transfer = in_sector + (uint32_t)disk_size;
        uint32_t covered = (transfer + fs->sector_size - 1U) / fs->sector_size * fs->sector_size;
        if (transfer < disk_size || covered < transfer || covered > 65536U) return 0;
        uint8_t *compressed = (uint8_t *)kmalloc(covered);
        uint8_t *decoded = (uint8_t *)kmalloc((uint32_t)data_size);
        uint32_t decoded_size = 0; int result = 0;
        if (compressed && decoded && btrfs_read_checked(fs, sector_logical, covered, compressed) &&
            btrfs_zlib_inflate(compressed + in_sector, (uint32_t)disk_size, decoded,
                               (uint32_t)data_size, &decoded_size) &&
            relative <= decoded_size && size <= decoded_size - relative) {
            for (uint32_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = decoded[relative + i];
            result = 1;
        }
        kfree(decoded); kfree(compressed);
        return result;
    }
    if (item[16] == 2) {
        uint64_t disk_bytenr = le64(&item[21]);
        uint64_t disk_size = le64(&item[29]);
        uint64_t data_offset = le64(&item[37]);
        uint64_t data_size = le64(&item[45]);
        if (extent_type != 1 || data_size == 0 || data_size > 65536U || disk_size == 0 ||
            disk_size > 65536U || data_offset > disk_size || relative > data_size ||
            size > data_size - relative || file_offset > UINT64_MAX - disk_bytenr - data_offset)
            return 0;
        uint64_t logical = disk_bytenr + data_offset;
        uint64_t sector_logical = logical - logical % fs->sector_size;
        uint32_t in_sector = (uint32_t)(logical - sector_logical);
        uint32_t transfer = in_sector + (uint32_t)disk_size;
        uint32_t covered = (transfer + fs->sector_size - 1U) / fs->sector_size * fs->sector_size;
        uint8_t *compressed = 0, *decoded = 0;
        uint32_t decoded_size = 0; int result = 0;
        if (transfer < disk_size || covered < transfer || covered > 65536U) return 0;
        compressed = (uint8_t *)kmalloc(covered);
        decoded = (uint8_t *)kmalloc((uint32_t)data_size);
        if (compressed && decoded && btrfs_read_checked(fs, sector_logical, covered, compressed) &&
            btrfs_lzo_inflate(compressed + in_sector, (uint32_t)disk_size, in_sector,
                              fs->sector_size, decoded, (uint32_t)data_size, &decoded_size) &&
            relative <= decoded_size && size <= decoded_size - relative) {
            for (uint32_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = decoded[relative + i];
            result = 1;
        }
        kfree(decoded); kfree(compressed);
        return result;
    }
    if (item[16] == 3) {
        uint64_t disk_bytenr = le64(&item[21]);
        uint64_t disk_size = le64(&item[29]);
        uint64_t data_offset = le64(&item[37]);
        uint64_t data_size = le64(&item[45]);
        if (extent_type != 1 || data_size == 0 || data_size > 65536U || disk_size == 0 ||
            disk_size > 65536U || data_offset > disk_size || relative > data_size ||
            size > data_size - relative || file_offset > UINT64_MAX - disk_bytenr - data_offset)
            return 0;
        uint64_t logical = disk_bytenr + data_offset;
        uint64_t sector_logical = logical - logical % fs->sector_size;
        uint32_t in_sector = (uint32_t)(logical - sector_logical);
        uint32_t transfer = in_sector + (uint32_t)disk_size;
        uint32_t covered = (transfer + fs->sector_size - 1U) / fs->sector_size * fs->sector_size;
        uint8_t *compressed = 0, *decoded = 0;
        uint32_t decoded_size = 0; int result = 0;
        if (transfer < disk_size || covered < transfer || covered > 65536U) return 0;
        compressed = (uint8_t *)kmalloc(covered);
        decoded = (uint8_t *)kmalloc((uint32_t)data_size);
        if (compressed && decoded && btrfs_read_checked(fs, sector_logical, covered, compressed) &&
            btrfs_zstd_decompress(compressed + in_sector, (uint32_t)disk_size, decoded,
                                  (uint32_t)data_size, &decoded_size) &&
            relative <= decoded_size && size <= decoded_size - relative) {
            for (uint32_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = decoded[relative + i];
            result = 1;
        }
        kfree(decoded); kfree(compressed);
        return result;
    }
    if (item[16] != 0) return 0;
    if (extent_type != 1) return 0;
    uint64_t disk_bytenr = le64(&item[21]);
    uint64_t disk_size = le64(&item[29]);
    uint64_t data_offset = le64(&item[37]);
    uint64_t data_size = le64(&item[45]);
    if (relative > data_size || size > data_size - relative ||
        data_offset > disk_size || relative > disk_size - data_offset ||
        size > disk_size - data_offset - relative ||
        file_offset > UINT64_MAX - disk_bytenr - data_offset) return 0;
    uint64_t logical = disk_bytenr + data_offset + relative;
    uint64_t sector_logical = logical - logical % fs->sector_size;
    uint32_t in_sector = (uint32_t)(logical - sector_logical);
    uint32_t transfer = in_sector + size;
    uint32_t device = 0; uint64_t physical = 0;
    uint32_t covered = (transfer + fs->sector_size - 1U) / fs->sector_size * fs->sector_size;
    if (transfer < size || covered < transfer || covered > sizeof(data) ||
        !btrfs_map(fs, sector_logical, covered, &device, &physical) ||
        physical % BTRFS_SECTOR_SIZE != 0) return 0;
    uint8_t read_ok = storage_read(device, physical / BTRFS_SECTOR_SIZE,
                                   covered / BTRFS_SECTOR_SIZE, data);
    if (!read_ok || !btrfs_validate_data(fs, sector_logical, data, covered)) {
        if (!btrfs_map_at(fs, sector_logical, covered, 1, &device, &physical) ||
            physical % BTRFS_SECTOR_SIZE != 0 ||
            !storage_read(device, physical / BTRFS_SECTOR_SIZE,
                          covered / BTRFS_SECTOR_SIZE, data) ||
            !btrfs_validate_data(fs, sector_logical, data, covered)) return 0;
    }
    available = size;
    for (uint32_t i = 0; i < available; ++i) ((uint8_t *)buffer)[i] = data[in_sector + i];
    return 1;
}

static int btrfs_scan_extents(btrfs_fs_t *fs, uint64_t bytenr, uint64_t inode,
                              uint64_t file_offset, uint8_t next, uint8_t depth,
                              uint64_t *result, uint8_t *found) {
    uint8_t node[65536];
    if (!fs || !result || !found || depth > 8 || fs->node_size > sizeof(node) ||
        !btrfs_read_node(fs, bytenr, node)) return 0;
    uint32_t count = le32(&node[96]);
    if (node[100] == 0) {
        if (count > (fs->node_size - 101U) / 25U) return 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *item = &node[101U + i * 25U];
            uint64_t key_offset = le64(&item[9]);
            if (le64(item) != inode || item[8] != BTRFS_EXTENT_DATA_TYPE) continue;
            if ((!next && key_offset <= file_offset && (!*found || key_offset > *result)) ||
                (next && key_offset > file_offset && (!*found || key_offset < *result))) {
                *result = key_offset; *found = 1;
            }
        }
        return 1;
    }
    if (node[100] > 8 || count == 0 || count > (fs->node_size - 101U) / 33U) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!btrfs_scan_extents(fs, le64(&node[101U + i * 33U + 17U]), inode,
                                file_offset, next, (uint8_t)(depth + 1U), result, found)) return 0;
    return 1;
}

static int btrfs_find_extent_start(btrfs_fs_t *fs, uint64_t tree_bytenr,
                                   uint64_t inode, uint64_t file_offset,
                                   uint64_t *extent_start) {
    uint8_t found = 0;
    return btrfs_scan_extents(fs, tree_bytenr, inode, file_offset, 0, 0,
                              extent_start, &found) && found;
}

static int btrfs_find_extent_after(btrfs_fs_t *fs, uint64_t tree_bytenr,
                                   uint64_t inode, uint64_t file_offset,
                                   uint64_t *extent_start) {
    uint8_t found = 0;
    return btrfs_scan_extents(fs, tree_bytenr, inode, file_offset, 1, 0,
                              extent_start, &found) && found;
}

int btrfs_read_file(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                    uint64_t offset, void *buffer, uint32_t size) {
    uint64_t file_size = 0; uint32_t mode = 0;
    if (!fs || !buffer || !size || !btrfs_inode_stat(fs, tree_bytenr, inode,
                                                     &file_size, &mode) ||
        (mode & 0170000U) != 0100000U || offset > file_size ||
        size > file_size - offset) return 0;
    uint8_t extent[53]; uint8_t *destination = buffer; uint32_t remaining = size;
    while (remaining) {
        uint64_t start = 0;
        if (!btrfs_find_extent_start(fs, tree_bytenr, inode, offset, &start)) {
            uint64_t next = 0;
            uint64_t hole = remaining;
            if (btrfs_find_extent_after(fs, tree_bytenr, inode, offset, &next) && next - offset < hole)
                hole = next - offset;
            for (uint64_t i = 0; i < hole; ++i) destination[i] = 0;
            offset += hole; destination += hole; remaining -= (uint32_t)hole;
            if (hole == 0) return 0;
            continue;
        }
        if (!btrfs_read_item(fs, tree_bytenr, inode, BTRFS_EXTENT_DATA_TYPE,
                             start, extent, sizeof(extent))) return 0;
        uint64_t relative = offset - start;
        uint64_t length = extent[20] == 0 ? le64(&extent[8]) : le64(&extent[45]);
        if (relative >= length) {
            uint64_t next = 0;
            if (!btrfs_find_extent_after(fs, tree_bytenr, inode, offset, &next)) return 0;
            uint64_t hole = next - offset; if (hole > remaining) hole = remaining;
            for (uint64_t i = 0; i < hole; ++i) destination[i] = 0;
            offset += hole; destination += hole; remaining -= (uint32_t)hole;
            continue;
        }
        uint64_t chunk = length - relative; if (chunk > remaining) chunk = remaining;
        if (chunk > UINT32_MAX || !btrfs_read_extent_data(fs, tree_bytenr, inode,
                                                           start, offset, destination,
                                                           (uint32_t)chunk)) return 0;
        offset += chunk; destination += chunk; remaining -= (uint32_t)chunk;
    }
    return 1;
}

int btrfs_write_file(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                     uint64_t offset, const void *buffer, uint32_t size) {
    uint64_t file_size = 0; uint32_t mode = 0;
    uint8_t extent[53], data[4096], original[4096];
    if (!fs || !fs->mounted || !buffer || !size || fs->sector_size > sizeof(data) ||
        !btrfs_inode_stat(fs, tree_bytenr, inode, &file_size, &mode) ||
        (mode & 0170000U) != 0100000U || offset > file_size ||
        size > file_size - offset) return 0;
    const uint8_t *source = (const uint8_t *)buffer;
    uint32_t remaining = size;
    while (remaining) {
        uint64_t start = 0;
        if (!btrfs_find_extent_start(fs, tree_bytenr, inode, offset, &start) ||
            !btrfs_read_item(fs, tree_bytenr, inode, BTRFS_EXTENT_DATA_TYPE,
                             start, extent, sizeof(extent)) || extent[16] != 0 ||
            extent[17] != 0 || extent[18] != 0) return 0;
        uint64_t relative = offset - start;
        if (extent[20] == 0) {
            uint64_t inline_size = le64(&extent[8]);
            if (relative > UINT32_MAX || relative > inline_size ||
                remaining > inline_size - relative ||
                !btrfs_update_inline_item(fs, tree_bytenr, inode, start,
                                          (uint32_t)relative, source, remaining, 0))
                return 0;
            return 1;
        }
        if (extent[20] != 1) return 0;
        uint64_t disk_bytenr = le64(&extent[21]);
        uint64_t disk_size = le64(&extent[29]);
        uint64_t data_offset = le64(&extent[37]);
        uint64_t data_size = le64(&extent[45]);
        if (!data_size || data_offset > disk_size || relative > data_size ||
            remaining > data_size - relative || data_offset > disk_size - relative ||
            offset > UINT64_MAX - disk_bytenr - data_offset) return 0;
        uint64_t logical = disk_bytenr + data_offset + relative;
        uint64_t sector_logical = logical - logical % fs->sector_size;
        uint32_t in_sector = (uint32_t)(logical - sector_logical);
        uint32_t chunk = fs->sector_size - in_sector;
        if (chunk > remaining) chunk = remaining;
        if (sector_logical < disk_bytenr + data_offset ||
            sector_logical > UINT64_MAX - fs->sector_size ||
            sector_logical >= disk_bytenr + data_offset + disk_size ||
            !btrfs_read_checked(fs, sector_logical, fs->sector_size, data)) return 0;
        for (uint32_t i = 0; i < fs->sector_size; ++i) original[i] = data[i];
        for (uint32_t i = 0; i < chunk; ++i) data[in_sector + i] = source[i];
        uint32_t device = 0; uint64_t physical = 0;
        if (!btrfs_map(fs, sector_logical, fs->sector_size, &device, &physical) ||
            physical % BTRFS_SECTOR_SIZE != 0 ||
            !storage_write(device, physical / BTRFS_SECTOR_SIZE,
                           fs->sector_size / BTRFS_SECTOR_SIZE, data)) return 0;
        uint32_t mirror_device = 0; uint64_t mirror_physical = 0;
        if (btrfs_map_at(fs, sector_logical, fs->sector_size, 1,
                         &mirror_device, &mirror_physical) &&
            (mirror_device != device || mirror_physical != physical) &&
            (mirror_physical % BTRFS_SECTOR_SIZE != 0 ||
             !storage_write(mirror_device, mirror_physical / BTRFS_SECTOR_SIZE,
                            fs->sector_size / BTRFS_SECTOR_SIZE, data))) {
            (void)storage_write(device, physical / BTRFS_SECTOR_SIZE,
                                fs->sector_size / BTRFS_SECTOR_SIZE, original);
            return 0;
        }
        if (fs->csum_root_bytenr &&
            !btrfs_update_data_csum(fs, fs->csum_root_bytenr, sector_logical,
                                    crc32c(data, fs->sector_size), 0)) {
            (void)storage_write(device, physical / BTRFS_SECTOR_SIZE,
                                fs->sector_size / BTRFS_SECTOR_SIZE, original);
            if (mirror_physical != physical)
                (void)storage_write(mirror_device, mirror_physical / BTRFS_SECTOR_SIZE,
                                    fs->sector_size / BTRFS_SECTOR_SIZE, original);
            return 0;
        }
        source += chunk; remaining -= chunk; offset += chunk;
    }
    return 1;
}

int btrfs_truncate_file(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                        uint64_t size) {
    uint64_t old_size = 0; uint32_t mode = 0;
    uint8_t extent[53];
    if (!fs || !fs->mounted ||
        !btrfs_inode_stat(fs, tree_bytenr, inode, &old_size, &mode) ||
        (mode & 0170000U) != 0100000U || size >= old_size ||
        !btrfs_read_item(fs, tree_bytenr, inode, BTRFS_EXTENT_DATA_TYPE, 0,
                         extent, sizeof(extent)) || extent[16] != 0 ||
        extent[17] != 0 || extent[18] != 0 || extent[20] != 0)
        return 0;
    return btrfs_update_inode_size(fs, tree_bytenr, inode, size, 0);
}
