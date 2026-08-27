#include "btrfs.h"
#include "../../drivers/storage/storage.h"

#define BTRFS_SECTOR_SIZE 512U
#define BTRFS_SUPERBLOCK_SECTOR 128U
#define BTRFS_CHUNK_ITEM_TYPE 0x21U
#define BTRFS_ROOT_ITEM_TYPE 132U
#define BTRFS_FS_TREE_OBJECTID 5ULL
#define BTRFS_EXTENT_DATA_TYPE 108U
#define BTRFS_INODE_ITEM_TYPE 1U
#define BTRFS_DIR_ITEM_TYPE 84U

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static int btrfs_map(const btrfs_fs_t *fs, uint64_t logical, uint32_t bytes,
                     uint64_t *physical) {
    if (!fs || !physical || !bytes) return 0;
    for (uint32_t i = 0; i < fs->chunk_count; ++i) {
        const btrfs_chunk_t *chunk = &fs->chunks[i];
        if (logical >= chunk->logical && logical - chunk->logical <= chunk->length &&
            bytes <= chunk->length - (logical - chunk->logical)) {
            uint64_t delta = logical - chunk->logical;
            if (chunk->physical > UINT64_MAX - delta) return 0;
            *physical = chunk->physical + delta;
            return 1;
        }
    }
    return 0;
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
    uint64_t physical;
    if (!fs || !fs->mounted || !node || bytenr % fs->sector_size != 0 ||
        bytenr > fs->total_bytes - fs->node_size ||
        fs->node_size / BTRFS_SECTOR_SIZE == 0 ||
        !btrfs_map(fs, bytenr, fs->node_size, &physical) ||
        !storage_read(fs->device, physical / BTRFS_SECTOR_SIZE,
                      fs->node_size / BTRFS_SECTOR_SIZE, node)) return 0;
    uint32_t checksum = le32(node); node[0] = node[1] = node[2] = node[3] = 0;
    if (crc32c(&node[32], fs->node_size - 32U) != checksum) return 0;
    for (uint32_t i = 0; i < sizeof(fs->fsid); ++i)
        if (node[32 + i] != fs->fsid[i]) return 0;
    return le64(&node[48]) == bytenr && node[100] <= 8;
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
    if (!storage_read(device, BTRFS_SUPERBLOCK_SECTOR, 8, sb) ||
        le64(&sb[0x40]) != 0x4d5f53665248425fULL || le16(&sb[0xc0]) != 0) return 0;
    uint32_t checksum = le32(sb);
    sb[0] = sb[1] = sb[2] = sb[3] = 0;
    if (crc32c(&sb[32], sizeof(sb) - 32U) != checksum) return 0;
    uint32_t sector_size = le32(&sb[0x90]);
    uint32_t node_size = le32(&sb[0x94]);
    uint64_t total_bytes = le64(&sb[0x70]);
    uint64_t root = le64(&sb[0x50]);
    uint64_t chunk_root = le64(&sb[0x58]);
    if (sector_size < 512 || sector_size > 4096 || (sector_size & (sector_size - 1U)) != 0 ||
        node_size < sector_size || node_size > 65536 || (node_size & (node_size - 1U)) != 0 ||
        total_bytes < node_size || root < sector_size || chunk_root < sector_size ||
        root >= total_bytes || chunk_root >= total_bytes ||
        total_bytes > storage_device_at(device)->block_count * (uint64_t)BTRFS_SECTOR_SIZE ||
        (root % sector_size) != 0 || (chunk_root % sector_size) != 0) return 0;
    fs->device = device; fs->sector_size = sector_size; fs->node_size = node_size;
    fs->total_bytes = total_bytes; fs->root_bytenr = root; fs->chunk_count = 0;
    fs->chunk_root_bytenr = chunk_root; fs->mounted = 1;
    for (uint32_t i = 0; i < sizeof(fs->fsid); ++i) fs->fsid[i] = sb[0x20 + i];
    uint32_t array_size = le32(&sb[0xa0]);
    uint32_t position = 0;
    if (array_size > 2048U || 0x2c0U + array_size > sizeof(sb)) { fs->mounted = 0; return 0; }
    while (position + 17U + 44U + 32U <= array_size) {
        const uint8_t *key = &sb[0x2c0U + position];
        uint64_t logical = le64(&key[9]);
        uint64_t length = le64(&key[17]);
        uint32_t num_stripes = le16(&key[17 + 44]);
        if (le64(key) != 1 || key[8] != BTRFS_CHUNK_ITEM_TYPE || !length || num_stripes != 1 ||
            fs->chunk_count >= BTRFS_MAX_SYSTEM_CHUNKS || logical > total_bytes - length) {
            fs->mounted = 0; return 0;
        }
        fs->chunks[fs->chunk_count].logical = logical;
        fs->chunks[fs->chunk_count].length = length;
        fs->chunks[fs->chunk_count].physical = le64(&key[17 + 44 + 8]);
        if (fs->chunks[fs->chunk_count].physical >
            storage_device_at(device)->block_count * (uint64_t)BTRFS_SECTOR_SIZE - length) {
            fs->mounted = 0; return 0;
        }
        ++fs->chunk_count;
        position += 17U + 44U + num_stripes * 32U;
    }
    if (position != array_size || fs->chunk_count == 0) { fs->mounted = 0; return 0; }
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
    if (item[16] != 0 || item[17] != 0 || item[18] != 0) return 0;
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
    uint64_t sector_logical = logical - logical % BTRFS_SECTOR_SIZE;
    uint32_t in_sector = (uint32_t)(logical - sector_logical);
    uint32_t transfer = in_sector + size;
    uint64_t physical = 0;
    if (transfer < size || transfer > sizeof(data) ||
        !btrfs_map(fs, sector_logical, transfer, &physical) ||
        physical % BTRFS_SECTOR_SIZE != 0 ||
        !storage_read(fs->device, physical / BTRFS_SECTOR_SIZE,
                      (transfer + BTRFS_SECTOR_SIZE - 1U) / BTRFS_SECTOR_SIZE, data)) return 0;
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
