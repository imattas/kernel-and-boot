#include "xfs.h"
#include "../../drivers/storage/storage.h"

#define XFS_SECTOR_SIZE 512U
#define XFS_SB_MAGIC 0x58465342U
#define XFS_SB_VERSION4 4U
#define XFS_INODE_MAGIC 0x494eU
#define XFS_FORMAT_LOCAL 1U
#define XFS_FORMAT_EXTENTS 2U
#define XFS_CORE_V2_SIZE 100U

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}
static void store_be64(uint8_t *p, uint64_t value) {
    p[0] = (uint8_t)(value >> 56); p[1] = (uint8_t)(value >> 48);
    p[2] = (uint8_t)(value >> 40); p[3] = (uint8_t)(value >> 32);
    p[4] = (uint8_t)(value >> 24); p[5] = (uint8_t)(value >> 16);
    p[6] = (uint8_t)(value >> 8); p[7] = (uint8_t)value;
}
static void store_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static void xfs_store_extent(uint8_t *record, uint64_t logical,
                             uint64_t physical, uint64_t length,
                             uint8_t unwritten) {
    uint64_t high = (logical << 9) | (unwritten ? (1ULL << 63) : 0);
    high |= physical >> 43;
    uint64_t low = (physical << 21) | length;
    store_be64(record, high);
    store_be64(record + 8, low);
}

static int xfs_split_unwritten_extent(uint8_t *data, uint32_t core,
                                      uint32_t *extent_count,
                                      uint32_t capacity, uint32_t index,
                                      uint64_t logical, uint64_t physical,
                                      uint64_t length) {
    if (!data || !extent_count || index >= *extent_count || length == 0)
        return 0;
    uint64_t record_high = be64(&data[core + index * 16U]);
    uint64_t start = (record_high & 0x7fffffffffffffffULL) >> 9;
    uint64_t original_length = be64(&data[core + index * 16U + 8U]) &
                               0x1fffffULL;
    if (!original_length || logical < start ||
        logical - start >= original_length) return 0;
    uint64_t prefix_length = logical - start;
    uint64_t suffix_length = original_length - prefix_length - 1U;
    if (original_length == 1) {
        uint64_t high = be64(&data[core + index * 16U]);
        store_be64(&data[core + index * 16U], high & 0x7fffffffffffffffULL);
        return 1;
    }
    if (*extent_count > capacity - 2U || physical < prefix_length ||
        physical > UINT64_MAX - original_length + prefix_length + 1U)
        return 0;
    uint8_t records[4096];
    uint32_t output = 0;
    for (uint32_t i = 0; i < *extent_count; ++i) {
        if (i != index) {
            for (uint32_t byte = 0; byte < 16; ++byte)
                records[output * 16U + byte] = data[core + i * 16U + byte];
            ++output;
            continue;
        }
        if (prefix_length)
            xfs_store_extent(&records[output++ * 16U], start,
                             physical - prefix_length, prefix_length, 1);
        xfs_store_extent(&records[output++ * 16U], logical, physical, 1, 0);
        if (suffix_length)
            xfs_store_extent(&records[output++ * 16U], logical + 1U,
                             physical + 1U, suffix_length, 1);
    }
    for (uint32_t i = 0; i < output * 16U; ++i)
        data[core + i] = records[i];
    *extent_count = output;
    return 1;
}

int xfs_mount(xfs_fs_t *fs, uint32_t device) {
    if (!fs || !storage_device_at(device) ||
        storage_device_at(device)->block_size != XFS_SECTOR_SIZE) return 0;
    uint8_t sb[XFS_SECTOR_SIZE];
    if (!storage_read(device, 0, 1, sb) || be32(&sb[0]) != XFS_SB_MAGIC) return 0;
    uint32_t block_size = be32(&sb[4]);
    uint64_t blocks = be64(&sb[8]);
    uint32_t ag_blocks = be32(&sb[84]);
    uint32_t ag_count = be32(&sb[88]);
    uint32_t inode_size = be16(&sb[104]);
    uint32_t sector_size = be16(&sb[102]);
    uint8_t block_log = sb[108], inode_log = sb[110], inopblock_log = sb[111];
    uint32_t version = be16(&sb[100]) & 0x000fU;
    uint32_t inopblock = be16(&sb[106]);
    if (sector_size != XFS_SECTOR_SIZE || block_size < 512 || block_size > 4096 ||
        (block_size & (block_size - 1U)) != 0 ||
        blocks == 0 || ag_blocks == 0 || ag_count == 0 || inode_size < 256 ||
        inode_size > block_size || (inode_size & (inode_size - 1U)) != 0 ||
        inopblock != block_size / inode_size || version != XFS_SB_VERSION4 ||
        block_log < 9 || block_log > 12 || inode_log < 8 || inode_log > 12 ||
        inopblock_log > 4 || (1U << block_log) != block_size ||
        (1U << inode_log) != inode_size || (1U << inopblock_log) != (block_size / inode_size) ||
        (uint64_t)ag_blocks * ag_count < blocks ||
        blocks > storage_device_at(device)->block_count / (block_size / XFS_SECTOR_SIZE)) return 0;
    fs->device = device; fs->block_size = block_size; fs->inode_size = inode_size;
    fs->ag_count = ag_count; fs->ag_blocks = ag_blocks; fs->block_count = blocks;
    fs->ag_block_log = sb[112]; fs->inode_per_block_log = inopblock_log;
    fs->root_inode = be64(&sb[56]); fs->mounted = 1;
    return fs->root_inode != 0;
}

static int xfs_read_block(const xfs_fs_t *fs, uint64_t block, void *buffer) {
    if (!fs || !fs->mounted || !buffer || block >= fs->block_count ||
        fs->block_size / XFS_SECTOR_SIZE == 0) return 0;
    return storage_read(fs->device, block * (fs->block_size / XFS_SECTOR_SIZE),
                        fs->block_size / XFS_SECTOR_SIZE, buffer);
}

static int xfs_write_block(const xfs_fs_t *fs, uint64_t block, const void *buffer) {
    uint32_t sectors = fs ? fs->block_size / XFS_SECTOR_SIZE : 0;
    return fs && buffer && sectors != 0 && block < fs->block_count &&
           block <= UINT64_MAX / sectors &&
           storage_write(fs->device, block * sectors, sectors, buffer);
}

static int xfs_read_inode(const xfs_fs_t *fs, uint64_t inode, uint8_t *data) {
    uint64_t agno, agino_mask, agino;
    uint64_t block;
    uint32_t offset;
    uint8_t block_data[4096];
    if (!fs || !data) return 0;
    agno = inode >> (fs->ag_block_log + fs->inode_per_block_log);
    agino_mask = (1ULL << (fs->ag_block_log + fs->inode_per_block_log)) - 1ULL;
    agino = inode & agino_mask;
    if (agno >= fs->ag_count || agino >= (uint64_t)fs->ag_blocks *
        (1U << fs->inode_per_block_log)) return 0;
    block = agno * fs->ag_blocks + (agino >> fs->inode_per_block_log);
    offset = (uint32_t)(agino & ((1U << fs->inode_per_block_log) - 1U)) * fs->inode_size;
    if (offset + fs->inode_size > fs->block_size || !xfs_read_block(fs, block, block_data)) return 0;
    for (uint32_t i = 0; i < fs->inode_size; ++i) data[i] = block_data[offset + i];
    return be16(data) == XFS_INODE_MAGIC && data[4] <= 2;
}

static int xfs_extent(const uint8_t *record, uint64_t logical,
                      uint64_t *physical, uint64_t *length, uint8_t *unwritten) {
    uint64_t high = be64(record), low = be64(record + 8);
    uint64_t start = (high & 0x7fffffffffffffffULL) >> 9;
    uint64_t block = ((high & 0x1ffULL) << 43) | (low >> 21);
    uint64_t count = low & 0x1fffffULL;
    if (!count || logical < start || logical - start >= count) return 0;
    *physical = block + logical - start; *length = count - (logical - start);
    *unwritten = (uint8_t)(high >> 63);
    return 1;
}

static int xfs_write_inode(const xfs_fs_t *fs, uint64_t inode,
                           const uint8_t *data) {
    if (!fs || !data) return 0;
    uint64_t agno = inode >> (fs->ag_block_log + fs->inode_per_block_log);
    uint64_t agino_mask = (1ULL << (fs->ag_block_log + fs->inode_per_block_log)) - 1ULL;
    uint64_t agino = inode & agino_mask;
    if (agno >= fs->ag_count ||
        agino >= (uint64_t)fs->ag_blocks * (1U << fs->inode_per_block_log)) return 0;
    uint64_t block = agno * fs->ag_blocks + (agino >> fs->inode_per_block_log);
    uint32_t offset = (uint32_t)(agino & ((1U << fs->inode_per_block_log) - 1U)) * fs->inode_size;
    if (offset + fs->inode_size > fs->block_size) return 0;
    uint8_t block_data[4096];
    if (!xfs_read_block(fs, block, block_data)) return 0;
    for (uint32_t i = 0; i < fs->inode_size; ++i) block_data[offset + i] = data[i];
    return xfs_write_block(fs, block, block_data);
}

int xfs_inode_size(xfs_fs_t *fs, uint64_t inode, uint64_t *size) {
    uint8_t data[4096];
    if (!size || !xfs_read_inode(fs, inode, data) ||
        (be16(&data[2]) & 0xf000U) != 0x8000U) return 0;
    *size = be64(&data[56]);
    return 1;
}

int xfs_lookup(xfs_fs_t *fs, uint64_t directory_inode, const char *name,
               uint64_t *inode_number) {
    uint8_t data[4096];
    if (!fs || !name || !inode_number || !name[0] || !xfs_read_inode(fs, directory_inode, data) ||
        (be16(&data[2]) & 0xf000U) != 0x4000U || data[5] != XFS_FORMAT_LOCAL) return 0;
    uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
    uint64_t directory_size = be64(&data[56]);
    if (directory_size < 6 || directory_size > fs->inode_size - core) return 0;
    const uint8_t *local = &data[core]; uint32_t count = local[0];
    uint8_t wide = local[1] != 0; uint32_t position = 2U + (wide ? 8U : 4U);
    for (uint32_t i = 0; i < count; ++i) {
        if (position + 3U > directory_size) return 0;
        uint8_t length = local[position]; position += 3U;
        uint32_t inode_bytes = wide ? 8U : 4U;
        if (position + inode_bytes + length > directory_size) return 0;
        uint64_t child = wide ? be64(&local[position]) : be32(&local[position]);
        position += inode_bytes;
        uint32_t j = 0;
        while (j < length && name[j] && name[j] == (char)local[position + j]) ++j;
        if (j == length && name[j] == 0) { *inode_number = child; return child != 0; }
        position += length;
    }
    return 0;
}

int xfs_read_file(xfs_fs_t *fs, uint64_t inode, uint64_t offset,
                  void *buffer, uint32_t size) {
    uint8_t data[4096], block[4096];
    if (!fs || !buffer || !size || !xfs_read_inode(fs, inode, data) ||
        (be16(&data[2]) & 0xf000U) != 0x8000U) return 0;
    uint64_t file_size = be64(&data[56]);
    if (offset > file_size || size > file_size - offset) return 0;
    if (data[5] == XFS_FORMAT_LOCAL) {
        uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
        if (offset > fs->inode_size - core || size > fs->inode_size - core - offset) return 0;
        for (uint32_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = data[core + offset + i];
        return 1;
    }
    if (data[5] != XFS_FORMAT_EXTENTS) return 0;
    uint32_t extent_count = be32(&data[76]);
    uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
    if (extent_count == 0 || extent_count > (fs->inode_size - core) / 16U) return 0;
    uint8_t *destination = buffer; uint32_t remaining = size;
    uint64_t logical = offset / fs->block_size; uint32_t in_block = (uint32_t)(offset % fs->block_size);
    while (remaining) {
        uint64_t physical = 0, extent_length = 0; uint8_t unwritten = 0; int found = 0;
        for (uint32_t i = 0; i < extent_count; ++i)
            if (xfs_extent(&data[core + i * 16U], logical, &physical, &extent_length,
                           &unwritten)) { found = 1; break; }
        uint32_t chunk = fs->block_size - in_block; if (chunk > remaining) chunk = remaining;
        if (!found || unwritten) {
            for (uint32_t i = 0; i < chunk; ++i) destination[i] = 0;
        } else {
            if (physical >= fs->block_count || !xfs_read_block(fs, physical, block)) return 0;
            for (uint32_t i = 0; i < chunk; ++i) destination[i] = block[in_block + i];
        }
        destination += chunk; remaining -= chunk; ++logical; in_block = 0;
    }
    return 1;
}

int xfs_write_file(xfs_fs_t *fs, uint64_t inode, uint64_t offset,
                   const void *buffer, uint32_t size) {
    uint8_t data[4096], block[4096];
    if (!fs || !fs->mounted || !buffer || size == 0 ||
        !xfs_read_inode(fs, inode, data)) return 0;
    uint32_t core = fs->inode_size == 256 ? 100U : 176U;
    uint64_t file_size = be64(&data[56]);
    if (offset > file_size || offset > UINT64_MAX - size ||
        core > fs->inode_size || (be16(&data[2]) & 0xf000U) != 0x8000U)
        return 0;
    if (data[5] == XFS_FORMAT_LOCAL) {
        uint64_t end = offset + size;
        if (end > fs->inode_size - core) return 0;
        for (uint32_t i = 0; i < size; ++i)
            data[core + (uint32_t)offset + i] = ((const uint8_t *)buffer)[i];
        if (end > file_size) store_be64(&data[56], end);
        return xfs_write_inode(fs, inode, data);
    }
    if (data[5] != XFS_FORMAT_EXTENTS) return 0;
    uint32_t extent_count = be32(&data[76]);
    if (extent_count == 0 || extent_count > (fs->inode_size - core) / 16U)
        return 0;
    uint64_t end = offset + size;
    int grew = end > file_size;
    if (grew) {
        uint64_t physical = 0, extent_length = 0;
        uint8_t unwritten = 0;
        if (!xfs_extent(&data[core], (end - 1U) / fs->block_size,
                        &physical, &extent_length, &unwritten)) return 0;
    }
    uint64_t logical = offset / fs->block_size;
    uint32_t in_block = (uint32_t)(offset % fs->block_size);
    uint32_t remaining = size;
    const uint8_t *source = (const uint8_t *)buffer;
    while (remaining) {
        uint64_t physical = 0, extent_length = 0;
        uint8_t unwritten = 0; int found = 0;
        for (uint32_t i = 0; i < extent_count; ++i)
            if (xfs_extent(&data[core + i * 16U], logical, &physical,
                           &extent_length, &unwritten)) { found = 1; break; }
        uint32_t chunk = fs->block_size - in_block;
        if (chunk > remaining) chunk = remaining;
        if (!found || physical >= fs->block_count) return 0;
        if (unwritten) {
            for (uint32_t i = 0; i < fs->block_size; ++i) block[i] = 0;
        } else if (!xfs_read_block(fs, physical, block)) return 0;
        for (uint32_t i = 0; i < chunk; ++i)
            block[in_block + i] = source[i];
        if (!xfs_write_block(fs, physical, block)) return 0;
        if (unwritten) {
            uint32_t record_index = 0;
            int record_found = 0;
            for (; record_index < extent_count; ++record_index) {
                uint64_t candidate = 0, candidate_length = 0;
                uint8_t candidate_unwritten = 0;
                if (xfs_extent(&data[core + record_index * 16U], logical,
                               &candidate, &candidate_length,
                               &candidate_unwritten) &&
                    candidate == physical && candidate_unwritten) {
                    record_found = 1;
                    break;
                }
            }
            if (!record_found || !xfs_split_unwritten_extent(
                    data, core, &extent_count,
                    (fs->inode_size - core) / 16U, record_index, logical,
                    physical, extent_length)) return 0;
            store_be32(&data[76], extent_count);
        }
        source += chunk; remaining -= chunk; ++logical; in_block = 0;
    }
    if (grew) {
        store_be64(&data[56], end);
        return xfs_write_inode(fs, inode, data);
    }
    return 1;
}

int xfs_truncate_file(xfs_fs_t *fs, uint64_t inode, uint64_t size) {
    uint8_t data[4096];
    if (!fs || !fs->mounted || !xfs_read_inode(fs, inode, data) ||
        (be16(&data[2]) & 0xf000U) != 0x8000U || size > be64(&data[56])) return 0;
    if (data[5] == XFS_FORMAT_LOCAL) {
        uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
        if (size > fs->inode_size - core) return 0;
        for (uint64_t i = size; i < be64(&data[56]); ++i) data[core + i] = 0;
    }
    store_be64(&data[56], size);
    return xfs_write_inode(fs, inode, data);
}
