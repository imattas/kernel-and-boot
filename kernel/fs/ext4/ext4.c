#include "ext4.h"
#include "../../drivers/storage/storage.h"

#define EXT4_SECTOR_SIZE 512U
#define EXT4_MAGIC 0xef53U
#define EXT4_EXTENTS_FL 0x00080000U
#define EXT4_EXTENT_MAGIC 0xf30aU
#define EXT4_INCOMPAT_FILETYPE 0x0002U
#define EXT4_INCOMPAT_EXTENTS 0x0040U
#define EXT4_INCOMPAT_64BIT 0x0080U

static uint16_t load16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t load32(const uint8_t *p) { return (uint32_t)load16(p) | ((uint32_t)load16(p + 2) << 16); }
static void store32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void store16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static uint64_t inode_size_value(const uint8_t *inode) {
    return (uint64_t)load32(&inode[4]) | ((uint64_t)load32(&inode[108]) << 32);
}

static int extent_data_block(const ext4_fs_t *fs, const uint8_t *header,
                             uint32_t logical, uint64_t *physical,
                             uint8_t *hole, uint8_t depth, uint8_t root);

static int read_block(const ext4_fs_t *fs, uint64_t block, void *buffer) {
    if (!fs || !buffer || block >= fs->block_count || fs->block_size > 4096U) return 0;
    return storage_read(fs->device, block * (fs->block_size / EXT4_SECTOR_SIZE),
                        fs->block_size / EXT4_SECTOR_SIZE, buffer);
}

static int write_block(const ext4_fs_t *fs, uint64_t block, const void *buffer) {
    uint32_t sectors = fs ? fs->block_size / EXT4_SECTOR_SIZE : 0;
    return fs && buffer && sectors != 0 && block < fs->block_count &&
           block <= UINT64_MAX / sectors &&
           storage_write(fs->device, block * sectors, sectors, buffer);
}

int ext4_mount(ext4_fs_t *fs, uint32_t device) {
    if (!fs || !storage_device_at(device) ||
        storage_device_at(device)->block_size != EXT4_SECTOR_SIZE) return 0;
    uint8_t sb[1024];
    if (!storage_read(device, 2, 2, sb) || load16(&sb[56]) != EXT4_MAGIC) return 0;
    uint32_t log_block_size = load32(&sb[24]);
    uint32_t incompat = load32(&sb[96]);
    uint64_t block_count = load32(&sb[4]);
    uint32_t blocks_per_group = load32(&sb[32]);
    uint32_t inodes_per_group = load32(&sb[40]);
    uint32_t inode_size = load16(&sb[88]);
    uint32_t descriptor_size = load16(&sb[254]);
    if (log_block_size > 2) return 0;
    uint32_t block_size = 1024U << log_block_size;
    uint64_t device_blocks = storage_device_at(device)->block_count;
    uint32_t sectors_per_block = block_size / EXT4_SECTOR_SIZE;
    if (block_size < 1024 || block_size > 4096 ||
        (incompat & ~(EXT4_INCOMPAT_FILETYPE | EXT4_INCOMPAT_EXTENTS |
                      EXT4_INCOMPAT_64BIT)) != 0 ||
        block_count == 0 || blocks_per_group == 0 || inodes_per_group == 0 ||
        inode_size < 128 || inode_size > block_size ||
        (block_size % inode_size) != 0 || descriptor_size < 32 ||
        descriptor_size > block_size ||
        ((incompat & EXT4_INCOMPAT_64BIT) && descriptor_size < 64U) ||
        sectors_per_block == 0 || block_count > device_blocks / sectors_per_block)
        return 0;
    if (incompat & EXT4_INCOMPAT_64BIT) block_count |= (uint64_t)load32(&sb[336]) << 32;
    if (block_count > device_blocks / sectors_per_block) return 0;
    uint64_t groups_count = block_count / blocks_per_group +
                             (block_count % blocks_per_group != 0);
    if (!groups_count || groups_count > 0xffffffffULL) return 0;
    uint8_t group[4096];
    ext4_fs_t probe = {.device = device, .block_size = block_size, .block_count = block_count};
    uint64_t descriptor_block = block_size == 1024 ? 2 : 1;
    if (!read_block(&probe, descriptor_block, group)) return 0;
    uint64_t inode_table = load32(&group[8]);
    if ((incompat & EXT4_INCOMPAT_64BIT) != 0) inode_table |= (uint64_t)load32(&group[40]) << 32;
    uint64_t inode_table_blocks = (inodes_per_group * (uint64_t)inode_size + block_size - 1U) / block_size;
    if (inode_table >= block_count || inode_table_blocks > block_count - inode_table) return 0;
    fs->device = device; fs->block_size = block_size; fs->inode_size = inode_size;
    fs->inodes_per_group = inodes_per_group; fs->descriptor_size = descriptor_size;
    fs->descriptor_block = descriptor_block; fs->groups_count = groups_count;
    fs->block_count = block_count; fs->has_64bit = (uint8_t)((incompat & EXT4_INCOMPAT_64BIT) != 0);
    fs->mounted = 1;
    return 1;
}

static int read_inode(const ext4_fs_t *fs, uint32_t number, uint8_t *inode) {
    if (!fs || !fs->mounted || number == 0 || !inode) return 0;
    uint64_t group_number = ((uint64_t)number - 1U) / fs->inodes_per_group;
    uint32_t group_index = (uint32_t)(((uint64_t)number - 1U) % fs->inodes_per_group);
    if (group_number >= fs->groups_count) return 0;
    uint8_t descriptor[64];
    uint64_t descriptor_block = fs->descriptor_block +
                                (group_number * fs->descriptor_size) / fs->block_size;
    uint32_t descriptor_offset = (uint32_t)((group_number * fs->descriptor_size) % fs->block_size);
    uint8_t descriptor_data[4096];
    if (descriptor_offset + fs->descriptor_size > fs->block_size ||
        !read_block(fs, descriptor_block, descriptor_data)) return 0;
    for (uint32_t i = 0; i < fs->descriptor_size && i < sizeof(descriptor); ++i)
        descriptor[i] = descriptor_data[descriptor_offset + i];
    uint64_t inode_table = load32(&descriptor[8]);
    if (fs->has_64bit) inode_table |= (uint64_t)load32(&descriptor[40]) << 32;
    uint64_t byte_offset = (uint64_t)group_index * fs->inode_size;
    uint64_t block = inode_table + byte_offset / fs->block_size;
    uint32_t offset = (uint32_t)(byte_offset % fs->block_size);
    if (offset + fs->inode_size > fs->block_size) return 0;
    uint8_t data[4096];
    if (!read_block(fs, block, data)) return 0;
    for (uint32_t i = 0; i < fs->inode_size; ++i) inode[i] = data[offset + i];
    return 1;
}

static int write_inode_size(const ext4_fs_t *fs, uint32_t number,
                            uint64_t size) {
    if (!fs || !fs->mounted || number == 0) return 0;
    uint64_t group_number = ((uint64_t)number - 1U) / fs->inodes_per_group;
    uint32_t group_index = (uint32_t)(((uint64_t)number - 1U) % fs->inodes_per_group);
    if (group_number >= fs->groups_count) return 0;
    uint8_t descriptor[64], descriptor_data[4096], data[4096];
    uint64_t descriptor_block = fs->descriptor_block +
        (group_number * fs->descriptor_size) / fs->block_size;
    uint32_t descriptor_offset = (uint32_t)((group_number * fs->descriptor_size) % fs->block_size);
    if (descriptor_offset + fs->descriptor_size > fs->block_size ||
        !read_block(fs, descriptor_block, descriptor_data)) return 0;
    for (uint32_t i = 0; i < fs->descriptor_size && i < sizeof(descriptor); ++i)
        descriptor[i] = descriptor_data[descriptor_offset + i];
    uint64_t inode_table = load32(&descriptor[8]);
    if (fs->has_64bit) inode_table |= (uint64_t)load32(&descriptor[40]) << 32;
    uint64_t byte_offset = (uint64_t)group_index * fs->inode_size;
    uint64_t block = inode_table + byte_offset / fs->block_size;
    uint32_t offset = (uint32_t)(byte_offset % fs->block_size);
    if (offset + fs->inode_size > fs->block_size || !read_block(fs, block, data)) return 0;
    store32(&data[offset + 4], (uint32_t)size);
    store32(&data[offset + 108], (uint32_t)(size >> 32));
    return write_block(fs, block, data);
}

static int write_inode(const ext4_fs_t *fs, uint32_t number,
                       const uint8_t *inode) {
    uint64_t group = ((uint64_t)number - 1U) / fs->inodes_per_group;
    uint32_t index = (uint32_t)(((uint64_t)number - 1U) % fs->inodes_per_group);
    uint8_t descriptor[64], descriptor_data[4096], data[4096];
    uint64_t descriptor_block = fs->descriptor_block +
        (group * fs->descriptor_size) / fs->block_size;
    uint32_t descriptor_offset = (uint32_t)((group * fs->descriptor_size) % fs->block_size);
    if (!fs || !inode || group >= fs->groups_count ||
        descriptor_offset + fs->descriptor_size > fs->block_size ||
        !read_block(fs, descriptor_block, descriptor_data)) return 0;
    for (uint32_t i = 0; i < fs->descriptor_size && i < sizeof(descriptor); ++i)
        descriptor[i] = descriptor_data[descriptor_offset + i];
    uint64_t table = load32(&descriptor[8]);
    if (fs->has_64bit) table |= (uint64_t)load32(&descriptor[40]) << 32;
    uint64_t byte_offset = (uint64_t)index * fs->inode_size;
    uint64_t block = table + byte_offset / fs->block_size;
    uint32_t offset = (uint32_t)(byte_offset % fs->block_size);
    if (offset + fs->inode_size > fs->block_size || !read_block(fs, block, data)) return 0;
    for (uint32_t i = 0; i < fs->inode_size; ++i) data[offset + i] = inode[i];
    return write_block(fs, block, data);
}

static int group_bitmap(const ext4_fs_t *fs, uint64_t group, uint64_t *bitmap,
                        uint64_t *first, uint64_t *count) {
    uint8_t descriptor[64], data[4096];
    uint64_t descriptor_block = fs->descriptor_block +
        (group * fs->descriptor_size) / fs->block_size;
    uint32_t offset = (uint32_t)((group * fs->descriptor_size) % fs->block_size);
    if (!fs || !bitmap || !first || !count || group >= fs->groups_count ||
        offset + fs->descriptor_size > fs->block_size ||
        !read_block(fs, descriptor_block, data)) return 0;
    for (uint32_t i = 0; i < fs->descriptor_size && i < sizeof(descriptor); ++i)
        descriptor[i] = data[offset + i];
    *bitmap = load32(descriptor);
    if (fs->has_64bit) *bitmap |= (uint64_t)load32(&descriptor[32]) << 32;
    *first = group * (fs->block_count / fs->groups_count);
    *count = fs->block_count / fs->groups_count;
    if (group + 1U == fs->groups_count) *count = fs->block_count - *first;
    return *bitmap < fs->block_count && *count != 0;
}

static int ext4_set_block_used(ext4_fs_t *fs, uint64_t block, int used) {
    uint64_t group_span = fs->block_count / fs->groups_count;
    uint64_t group = block / group_span;
    uint64_t bitmap, first, count;
    uint8_t data[4096];
    if (!fs || !fs->mounted || group >= fs->groups_count ||
        !group_bitmap(fs, group, &bitmap, &first, &count) || block < first ||
        block - first >= count || !read_block(fs, bitmap, data)) return 0;
    uint64_t bit = block - first;
    uint32_t byte = (uint32_t)(bit / 8U);
    if ((uint64_t)byte >= fs->block_size) return 0;
    if (used) data[byte] |= (uint8_t)(1U << (bit & 7U));
    else data[byte] &= (uint8_t)~(1U << (bit & 7U));
    return write_block(fs, bitmap, data);
}

static int ext4_alloc_block(ext4_fs_t *fs, uint64_t *block) {
    uint64_t group_span = fs->block_count / fs->groups_count;
    if (!fs || !block || group_span == 0) return 0;
    for (uint64_t group = 0; group < fs->groups_count; ++group) {
        uint64_t bitmap, first, count;
        uint8_t data[4096];
        if (!group_bitmap(fs, group, &bitmap, &first, &count) || !read_block(fs, bitmap, data)) continue;
        for (uint64_t bit = 0; bit < count; ++bit) {
            uint64_t candidate = first + bit;
            if ((data[bit / 8U] & (uint8_t)(1U << (bit & 7U))) != 0) continue;
            if (!ext4_set_block_used(fs, candidate, 1)) return 0;
            if (!write_block(fs, candidate, (uint8_t[4096]){0})) {
                (void)ext4_set_block_used(fs, candidate, 0); return 0;
            }
            *block = candidate; return 1;
        }
    }
    return 0;
}

static int ext4_trim_tree(ext4_fs_t *fs, uint32_t block, uint32_t depth,
                          uint64_t keep, uint8_t *empty) {
    uint8_t table[4096];
    uint64_t pointers = fs->block_size / 4U;
    if (!fs || !block || !empty || depth == 0 ||
        depth > 3 || !read_block(fs, block, table)) return 0;
    uint64_t span = 1;
    for (uint32_t level = 1; level < depth; ++level) {
        if (span > UINT64_MAX / pointers) return 0;
        span *= pointers;
    }
    int changed = 0;
    uint8_t has_child = 0;
    for (uint64_t index = 0; index < pointers; ++index) {
        uint32_t child = load32(&table[index * 4U]);
        if (!child) continue;
        uint64_t begin = index * span;
        uint64_t child_keep = keep > begin ? keep - begin : 0;
        if (child_keep > span) child_keep = span;
        uint8_t child_empty = 0;
        if (depth == 1) {
            if (child_keep == 0) {
                if (!ext4_set_block_used(fs, child, 0)) return 0;
                store32(&table[index * 4U], 0);
                changed = 1;
            } else {
                has_child = 1;
            }
        } else {
            if (!ext4_trim_tree(fs, child, depth - 1U, child_keep,
                                &child_empty)) return 0;
            if (child_empty) {
                store32(&table[index * 4U], 0);
                changed = 1;
            } else {
                has_child = 1;
            }
        }
    }
    if (changed && !write_block(fs, block, table)) return 0;
    if (!has_child) {
        if (!ext4_set_block_used(fs, block, 0)) return 0;
        *empty = 1;
    } else {
        *empty = 0;
    }
    return 1;
}

static int ext4_resize_blocks(ext4_fs_t *fs, uint32_t inode_number,
                              uint8_t *inode, uint64_t new_size) {
    uint64_t old_size = inode_size_value(inode);
    uint64_t old_blocks = (old_size + fs->block_size - 1U) / fs->block_size;
    uint64_t new_blocks = (new_size + fs->block_size - 1U) / fs->block_size;
    uint64_t pointers = fs->block_size / 4U;
    uint64_t double_limit = 12U + pointers + pointers * pointers;
    uint64_t triple_limit = double_limit + pointers * pointers * pointers;
    if (new_blocks > triple_limit ||
        (new_size > UINT32_MAX && !fs->has_64bit)) return 0;
    uint32_t indirect = load32(&inode[88]);
    uint32_t double_indirect = load32(&inode[92]);
    uint32_t triple_indirect = load32(&inode[96]);
    uint8_t table[4096];
    if (new_blocks > old_blocks) {
        for (uint64_t i = old_blocks; i < new_blocks; ++i) {
            uint64_t block;
            if (i >= 12U && indirect == 0) {
                if (!ext4_alloc_block(fs, &block) || !write_block(fs, block,
                    (uint8_t[4096]){0})) return 0;
                indirect = (uint32_t)block; store32(&inode[88], indirect);
            }
            if (i < 12U) {
                if (!ext4_alloc_block(fs, &block)) return 0;
                store32(&inode[40 + i * 4U], (uint32_t)block);
            } else if (i < 12U + pointers) {
                if (!read_block(fs, indirect, table)) return 0;
                uint64_t table_index = i - 12U;
                if (load32(&table[table_index * 4U]) != 0) return 0;
                if (!ext4_alloc_block(fs, &block)) return 0;
                store32(&table[table_index * 4U], (uint32_t)block);
                if (!write_block(fs, indirect, table)) return 0;
            } else if (i < double_limit) {
                uint64_t relative = i - 12U - pointers;
                uint32_t first_index = (uint32_t)(relative / pointers);
                uint32_t second_index = (uint32_t)(relative % pointers);
                if (double_indirect == 0) {
                    if (!ext4_alloc_block(fs, &block) || !write_block(fs, block,
                        (uint8_t[4096]){0})) return 0;
                    double_indirect = (uint32_t)block; store32(&inode[92], double_indirect);
                }
                if (!read_block(fs, double_indirect, table)) return 0;
                uint32_t first = load32(&table[first_index * 4U]);
                if (first == 0) {
                    if (!ext4_alloc_block(fs, &block) || !write_block(fs, block,
                        (uint8_t[4096]){0})) return 0;
                    first = (uint32_t)block; store32(&table[first_index * 4U], first);
                    if (!write_block(fs, double_indirect, table)) return 0;
                }
                if (!read_block(fs, first, table)) return 0;
                if (load32(&table[second_index * 4U]) != 0) return 0;
                if (!ext4_alloc_block(fs, &block)) return 0;
                store32(&table[second_index * 4U], (uint32_t)block);
                if (!write_block(fs, first, table)) return 0;
            } else {
                uint64_t relative = i - double_limit;
                uint64_t span = pointers * pointers;
                uint32_t first_index = (uint32_t)(relative / span);
                uint32_t second_index = (uint32_t)((relative / pointers) % pointers);
                uint32_t third_index = (uint32_t)(relative % pointers);
                if (triple_indirect == 0) {
                    if (!ext4_alloc_block(fs, &block) || !write_block(fs, block,
                        (uint8_t[4096]){0})) return 0;
                    triple_indirect = (uint32_t)block; store32(&inode[96], triple_indirect);
                }
                if (!read_block(fs, triple_indirect, table)) return 0;
                uint32_t first = load32(&table[first_index * 4U]);
                if (first == 0) {
                    if (!ext4_alloc_block(fs, &block) || !write_block(fs, block,
                        (uint8_t[4096]){0})) return 0;
                    first = (uint32_t)block; store32(&table[first_index * 4U], first);
                    if (!write_block(fs, triple_indirect, table)) return 0;
                }
                if (!read_block(fs, first, table)) return 0;
                uint32_t second = load32(&table[second_index * 4U]);
                if (second == 0) {
                    if (!ext4_alloc_block(fs, &block) || !write_block(fs, block,
                        (uint8_t[4096]){0})) return 0;
                    second = (uint32_t)block; store32(&table[second_index * 4U], second);
                    if (!write_block(fs, first, table)) return 0;
                }
                if (!read_block(fs, second, table)) return 0;
                if (load32(&table[third_index * 4U]) != 0) return 0;
                if (!ext4_alloc_block(fs, &block)) return 0;
                store32(&table[third_index * 4U], (uint32_t)block);
                if (!write_block(fs, second, table)) return 0;
            }
        }
    } else if (new_blocks < old_blocks) {
        for (uint64_t i = new_blocks; i < 12U && i < old_blocks; ++i) {
            uint32_t block = load32(&inode[40U + i * 4U]);
            if (block && !ext4_set_block_used(fs, block, 0)) return 0;
            store32(&inode[40U + i * 4U], 0);
        }
        uint8_t empty = 0;
        uint64_t indirect_keep = new_blocks > 12U ? new_blocks - 12U : 0;
        if (indirect && !ext4_trim_tree(fs, indirect, 1U,
                                        indirect_keep > pointers ? pointers : indirect_keep,
                                        &empty)) return 0;
        if (empty) store32(&inode[88], 0);
        uint64_t double_start = 12U + pointers;
        uint64_t double_keep = new_blocks > double_start ? new_blocks - double_start : 0;
        if (double_indirect && !ext4_trim_tree(fs, double_indirect, 2U,
                                                double_keep > pointers * pointers ?
                                                pointers * pointers : double_keep,
                                                &empty)) return 0;
        if (empty) store32(&inode[92], 0);
        uint64_t triple_keep = new_blocks > double_limit ?
                               new_blocks - double_limit : 0;
        if (triple_indirect && !ext4_trim_tree(fs, triple_indirect, 3U,
                                                triple_keep > pointers * pointers * pointers ?
                                                pointers * pointers * pointers : triple_keep,
                                                &empty)) return 0;
        if (empty) store32(&inode[96], 0);
    }
    store32(&inode[4], (uint32_t)new_size);
    store32(&inode[108], (uint32_t)(new_size >> 32));
    return write_inode(fs, inode_number, inode);
}

static int ext4_resize_extent_file(ext4_fs_t *fs, uint32_t inode_number,
                                    uint8_t *inode, uint64_t new_size) {
    uint64_t old_size = inode_size_value(inode);
    uint64_t old_blocks = (old_size + fs->block_size - 1U) / fs->block_size;
    uint64_t new_blocks = (new_size + fs->block_size - 1U) / fs->block_size;
    uint64_t delta = new_blocks - old_blocks;
    uint8_t leaf_data[4096];
    uint8_t *header = &inode[40], *records = &inode[52];
    uint64_t leaf_block = 0;
    uint32_t record_capacity = 0;
    uint16_t entries = load16(&header[2]), maximum = load16(&header[4]);
    uint16_t depth = load16(&header[6]);
    uint16_t root_entries = entries, root_maximum = maximum;
    uint32_t allocated[1024];
    uint32_t allocated_count = 0;
    if (new_size <= old_size) return 1;
    if (delta == 0 || delta > 1024U || load16(header) != EXT4_EXTENT_MAGIC ||
        entries > maximum || maximum > 4U ||
        12U + (uint32_t)maximum * 12U > 60U ||
        (new_size > UINT32_MAX && !fs->has_64bit)) return 0;
    if (depth == 0) {
        record_capacity = maximum;
    } else if (depth == 1 && entries != 0) {
        uint8_t *index = &inode[52];
        leaf_block = (uint64_t)load32(&index[4]) |
                     ((uint64_t)load16(&index[10]) << 32);
        if (leaf_block >= fs->block_count || !read_block(fs, leaf_block, leaf_data))
            return 0;
        header = leaf_data;
        records = &leaf_data[12];
        entries = load16(&header[2]);
        maximum = load16(&header[4]);
        if (load16(header) != EXT4_EXTENT_MAGIC || load16(&header[6]) != 0 ||
            entries > maximum || maximum > (fs->block_size - 12U) / 12U)
            return 0;
        record_capacity = maximum;
    } else {
        return 0;
    }
    for (uint64_t logical = old_blocks; logical < new_blocks; ++logical) {
        if (logical > UINT32_MAX) goto fail;
        uint64_t physical;
        if (!ext4_alloc_block(fs, &physical) || physical > 0xffffffffffffULL)
            goto fail;
        allocated[allocated_count++] = (uint32_t)physical;
        if (entries != 0) {
            uint8_t *extent = &records[(uint32_t)(entries - 1U) * 12U];
            uint32_t first = load32(extent);
            uint16_t raw_length = load16(&extent[4]);
            uint32_t length = raw_length & 0x7fffU;
            uint64_t start = ((uint64_t)load16(&extent[6]) << 32) |
                             load32(&extent[8]);
            if ((raw_length & 0x8000U) == 0 && length < 0x7fffU &&
                (uint64_t)first + length == logical && start + length == physical) {
                store16(&extent[4], (uint16_t)(length + 1U));
                continue;
            }
        }
        if (entries >= record_capacity) {
            if (depth != 1 || root_entries >= root_maximum) goto fail;
            uint64_t new_leaf;
            if (!ext4_alloc_block(fs, &new_leaf)) goto fail;
            if (new_leaf > UINT32_MAX) {
                if (!ext4_set_block_used(fs, new_leaf, 0)) return 0;
                goto fail;
            }
            allocated[allocated_count++] = (uint32_t)new_leaf;
            for (uint32_t byte = 0; byte < fs->block_size; ++byte)
                leaf_data[byte] = 0;
            store16(&leaf_data[0], EXT4_EXTENT_MAGIC);
            store16(&leaf_data[2], 0);
            store16(&leaf_data[4], (uint16_t)((fs->block_size - 12U) / 12U));
            store16(&leaf_data[6], 0);
            uint8_t *index = &inode[52U + (uint32_t)root_entries * 12U];
            store32(index, (uint32_t)logical);
            store32(&index[4], (uint32_t)new_leaf);
            store16(&index[8], 0);
            store16(&index[10], (uint16_t)(new_leaf >> 32));
            ++root_entries;
            store16(&inode[42], root_entries);
            leaf_block = new_leaf;
            header = leaf_data;
            records = &leaf_data[12];
            entries = 0;
            maximum = load16(&header[4]);
            record_capacity = maximum;
        }
        uint8_t *extent = &records[(uint32_t)entries * 12U];
        store32(extent, (uint32_t)logical);
        store16(&extent[4], 1);
        store16(&extent[6], (uint16_t)(physical >> 32));
        store32(&extent[8], (uint32_t)physical);
        ++entries;
        store16(&header[2], entries);
    }
    if (depth == 1 && !write_block(fs, leaf_block, leaf_data)) goto fail;
    store32(&inode[4], (uint32_t)new_size);
    store32(&inode[108], (uint32_t)(new_size >> 32));
    if (write_inode(fs, inode_number, inode)) return 1;
fail:
    while (allocated_count != 0)
        if (!ext4_set_block_used(fs, allocated[--allocated_count], 0)) return 0;
    return 0;
}

static int ext4_shrink_extent_file(ext4_fs_t *fs, uint32_t inode_number,
                                   uint8_t *inode, uint64_t new_size) {
    uint64_t old_size = inode_size_value(inode);
    uint64_t old_blocks = (old_size + fs->block_size - 1U) / fs->block_size;
    uint64_t new_blocks = (new_size + fs->block_size - 1U) / fs->block_size;
    uint64_t released = 0;
    uint8_t leaf_data[4096];
    uint8_t *header = &inode[40], *records = &inode[52];
    uint16_t entries = load16(&header[2]), maximum = load16(&header[4]);
    uint16_t depth = load16(&header[6]);
    if (new_size >= old_size) return 0;
    if (old_blocks - new_blocks > 1024U || load16(header) != EXT4_EXTENT_MAGIC ||
        entries > maximum || maximum > 4U ||
        12U + (uint32_t)maximum * 12U > 60U) return 0;
    uint16_t root_entries = entries;
    if (depth == 1 && (root_entries == 0 || root_entries > maximum)) {
        return 0;
    } else if (depth != 0 && depth != 1) {
        return 0;
    }
    if (new_size % fs->block_size != 0 && new_blocks != 0) {
        uint64_t logical = new_blocks - 1U, physical = 0;
        uint8_t hole = 0, found = 0;
        for (uint32_t i = 0; i < entries; ++i)
            if (extent_data_block(fs, &inode[40], (uint32_t)logical, &physical,
                                  &hole, (uint8_t)depth, 1)) { found = 1; break; }
        if (found && !hole) {
            uint8_t block[4096];
            if (!read_block(fs, physical, block)) return 0;
            for (uint32_t i = (uint32_t)(new_size % fs->block_size);
                 i < fs->block_size; ++i) block[i] = 0;
            if (!write_block(fs, physical, block)) return 0;
        }
    }
    if (depth == 1) {
        for (uint16_t leaf_index = root_entries; leaf_index != 0;) {
            --leaf_index;
            uint8_t *index_record = &inode[52U + (uint32_t)leaf_index * 12U];
            uint64_t block = (uint64_t)load32(&index_record[4]) |
                             ((uint64_t)load16(&index_record[10]) << 32);
            if (block >= fs->block_count || !read_block(fs, block, leaf_data)) return 0;
            header = leaf_data;
            records = &leaf_data[12];
            entries = load16(&header[2]);
            maximum = load16(&header[4]);
            if (load16(header) != EXT4_EXTENT_MAGIC || load16(&header[6]) != 0 ||
                entries > maximum || maximum > (fs->block_size - 12U) / 12U)
                return 0;
            for (uint32_t extent_index = 0; extent_index < entries;) {
                uint8_t *extent = &records[extent_index * 12U];
                uint32_t first = load32(extent);
                uint32_t raw_length = load16(&extent[4]);
                uint32_t length = raw_length & 0x7fffU;
                uint64_t physical = ((uint64_t)load16(&extent[6]) << 32) |
                                    load32(&extent[8]);
                uint64_t keep = new_blocks > first ? new_blocks - first : 0;
                if (keep > length) keep = length;
                uint32_t release = length - (uint32_t)keep;
                if (released > 1024U - release) return 0;
                for (uint32_t block_index = 0; block_index < release; ++block_index)
                    if (!ext4_set_block_used(fs, physical + keep + block_index, 0)) return 0;
                released += release;
                if (keep == 0) {
                    for (uint32_t move = extent_index; move + 1U < entries; ++move)
                        for (uint32_t byte = 0; byte < 12U; ++byte)
                            records[move * 12U + byte] = records[(move + 1U) * 12U + byte];
                    --entries;
                    continue;
                }
                if (release) store16(&extent[4], (uint16_t)keep | (raw_length & 0x8000U));
                ++extent_index;
            }
            store16(&header[2], entries);
            if (entries == 0) {
                if (!ext4_set_block_used(fs, block, 0)) return 0;
                for (uint16_t move = leaf_index; move + 1U < root_entries; ++move)
                    for (uint32_t byte = 0; byte < 12U; ++byte)
                        inode[52U + (uint32_t)move * 12U + byte] =
                            inode[52U + (uint32_t)(move + 1U) * 12U + byte];
                --root_entries;
            } else if (!write_block(fs, block, leaf_data)) {
                return 0;
            }
        }
        store16(&inode[42], root_entries);
    } else {
        for (uint32_t index = 0; index < entries;) {
        uint8_t *extent = &records[index * 12U];
        uint32_t first = load32(extent);
        uint32_t raw_length = load16(&extent[4]);
        uint32_t length = raw_length & 0x7fffU;
        uint64_t physical = ((uint64_t)load16(&extent[6]) << 32) |
                            load32(&extent[8]);
        uint64_t keep = new_blocks > first ? new_blocks - first : 0;
        if (keep > length) keep = length;
        uint32_t release = length - (uint32_t)keep;
        if (released > 1024U - release) return 0;
        for (uint32_t block = 0; block < release; ++block)
            if (!ext4_set_block_used(fs, physical + keep + block, 0)) return 0;
        released += release;
        if (keep == 0) {
            for (uint32_t move = index; move + 1U < entries; ++move)
                for (uint32_t byte = 0; byte < 12U; ++byte)
                    records[move * 12U + byte] =
                        records[(move + 1U) * 12U + byte];
            --entries;
            continue;
        }
        if (release) store16(&extent[4], (uint16_t)keep |
                                      (raw_length & 0x8000U));
        ++index;
        }
        store16(&header[2], entries);
    }
    if (depth == 1 && root_entries == 0) {
        store16(&inode[42], 0);
    } else if (depth == 1 && root_entries != 0) {
        store16(&inode[42], root_entries);
    } else {
        store16(&header[2], entries);
    }
    store32(&inode[4], (uint32_t)new_size);
    store32(&inode[108], (uint32_t)(new_size >> 32));
    return write_inode(fs, inode_number, inode);
}

static int extent_data_block(const ext4_fs_t *fs, const uint8_t *header,
                             uint32_t logical, uint64_t *physical, uint8_t *hole,
                             uint8_t depth, uint8_t root) {
    if (!fs || !header || !physical || !hole || depth > 5 || load16(header) != EXT4_EXTENT_MAGIC)
        return 0;
    uint16_t entries = load16(header + 2), maximum = load16(header + 4);
    uint16_t node_depth = load16(header + 6);
    uint32_t header_bytes = root ? 60U : fs->block_size;
    if (node_depth != depth || entries > maximum || 12U + (uint32_t)entries * 12U > header_bytes)
        return 0;
    if (depth == 0) {
        for (uint16_t i = 0; i < entries; ++i) {
            const uint8_t *extent = header + 12U + i * 12U;
            uint32_t first = load32(extent);
            uint16_t raw_length = load16(extent + 4);
            uint16_t length = raw_length & 0x7fffU;
            if (!length || logical < first || logical - first >= length) continue;
            uint64_t start = ((uint64_t)load16(extent + 6) << 32) | load32(extent + 8);
            if (start > UINT64_MAX - (logical - first)) return 0;
            *physical = start + logical - first;
            *hole = (uint8_t)((raw_length & 0x8000U) != 0);
            return *hole || *physical < fs->block_count;
        }
        return 0;
    }
    const uint8_t *selected = 0;
    for (uint16_t i = 0; i < entries; ++i) {
        const uint8_t *index = header + 12U + i * 12U;
        if (load32(index) > logical) break;
        selected = index;
    }
    if (!selected) return 0;
    uint64_t child = load32(selected + 4) | ((uint64_t)load16(selected + 10) << 32);
    uint8_t block[4096];
    return read_block(fs, child, block) && extent_data_block(fs, block, logical,
                                                               physical, hole, (uint8_t)(depth - 1U), 0);
}

static int inode_data_block(const ext4_fs_t *fs, const uint8_t *inode,
                            uint32_t logical, uint64_t *physical, uint8_t *hole) {
    if (!fs || !inode || !physical || !hole) return 0;
    *hole = 0;
    if ((load32(&inode[32]) & EXT4_EXTENTS_FL) != 0) {
        const uint8_t *header = &inode[40];
        return extent_data_block(fs, header, logical, physical, hole, load16(header + 6), 1);
    }
    uint64_t index = logical;
    if (index < 12U) {
        *physical = load32(&inode[40 + (uint32_t)index * 4U]);
        *hole = (uint8_t)(*physical == 0);
        return *hole || *physical < fs->block_count;
    }
    index -= 12U;
    uint64_t pointers = fs->block_size / 4U;
    uint32_t indirect = load32(&inode[40 + 12U * 4U]);
    uint8_t block[4096];
    if (index < pointers) {
        if (!read_block(fs, indirect, block)) return 0;
        *physical = load32(&block[index * 4U]);
        *hole = (uint8_t)(*physical == 0);
        return *hole || *physical < fs->block_count;
    }
    index -= pointers;
    uint64_t double_count = pointers * pointers;
    uint32_t double_indirect = load32(&inode[40 + 13U * 4U]);
    if (index < double_count) {
        if (!read_block(fs, double_indirect, block)) return 0;
        uint32_t first = load32(&block[(index / pointers) * 4U]);
        if (!read_block(fs, first, block)) return 0;
        *physical = load32(&block[(index % pointers) * 4U]);
        *hole = (uint8_t)(*physical == 0);
        return *hole || *physical < fs->block_count;
    }
    index -= double_count;
    uint64_t triple_count = double_count * pointers;
    uint32_t triple_indirect = load32(&inode[40 + 14U * 4U]);
    if (index >= triple_count || !read_block(fs, triple_indirect, block)) return 0;
    uint32_t first = load32(&block[(index / double_count) * 4U]);
    if (!read_block(fs, first, block)) return 0;
    uint32_t second = load32(&block[((index / pointers) % pointers) * 4U]);
    if (!read_block(fs, second, block)) return 0;
    *physical = load32(&block[(index % pointers) * 4U]);
    *hole = (uint8_t)(*physical == 0);
    return *hole || *physical < fs->block_count;
}

int ext4_lookup(ext4_fs_t *fs, uint32_t directory_inode, const char *name,
                uint32_t *inode_number) {
    if (!fs || !fs->mounted || !name || !inode_number || name[0] == 0) return 0;
    uint8_t inode[4096], block[4096];
    if (!read_inode(fs, directory_inode, inode) || (load16(inode) & 0xf000U) != 0x4000U) return 0;
    uint64_t directory_size = inode_size_value(inode);
    uint64_t block_count = directory_size / fs->block_size +
                           (directory_size % fs->block_size != 0);
    if (block_count > fs->block_count || block_count > UINT32_MAX) return 0;
    for (uint32_t logical = 0; logical < block_count; ++logical) {
        uint64_t physical = 0;
        uint8_t hole = 0;
        if (!inode_data_block(fs, inode, logical, &physical, &hole) || hole ||
            !read_block(fs, physical, block)) return 0;
        uint32_t offset = 0;
        while (offset + 8 <= fs->block_size) {
            uint32_t child = load32(&block[offset]); uint16_t record = load16(&block[offset + 4]);
            uint8_t name_length = block[offset + 6];
            if (record < 8 || (record & 3U) != 0 || offset + record > fs->block_size ||
                name_length > record - 8U) return 0;
            if (child != 0 && name_length == 0) return 0;
            int equal = child != 0 && name_length != 0;
            for (uint32_t i = 0; equal && (name[i] || i < name_length); ++i)
                if (i >= name_length || name[i] == 0 || name[i] != (char)block[offset + 8 + i]) equal = 0;
            if (equal && name[name_length] == 0) { *inode_number = child; return 1; }
            offset += record;
            if (record == 0) return 0;
        }
    }
    return 0;
}

int ext4_inode_size(ext4_fs_t *fs, uint32_t inode_number, uint64_t *size) {
    uint8_t inode[4096];
    if (!size || !read_inode(fs, inode_number, inode)) return 0;
    if ((load16(inode) & 0xf000U) != 0x8000U) return 0;
    *size = inode_size_value(inode);
    return 1;
}

int ext4_inode_mode(ext4_fs_t *fs, uint32_t inode_number, uint32_t *mode) {
    uint8_t inode[4096];
    if (!fs || !mode || !read_inode(fs, inode_number, inode)) return 0;
    *mode = load16(inode);
    return 1;
}

int ext4_read_file(ext4_fs_t *fs, uint32_t inode_number, uint64_t offset,
                   void *buffer, uint32_t size) {
    if (!fs || !buffer || size == 0) return 0;
    uint8_t inode[4096];
    if (!read_inode(fs, inode_number, inode)) return 0;
    uint64_t file_size = inode_size_value(inode);
    if (offset > file_size || size > file_size - offset) return 0;
    uint8_t block[4096]; uint8_t *destination = buffer;
    uint32_t in_block = (uint32_t)(offset % fs->block_size); uint64_t logical = offset / fs->block_size;
    uint32_t remaining = size;
    while (remaining) {
        uint64_t physical = 0;
        uint8_t hole = 0;
        if (!inode_data_block(fs, inode, (uint32_t)logical, &physical, &hole)) return 0;
        uint32_t chunk = fs->block_size - in_block; if (chunk > remaining) chunk = remaining;
        if (hole) {
            for (uint32_t i = 0; i < chunk; ++i) destination[i] = 0;
        } else {
            if (!read_block(fs, physical, block)) return 0;
            for (uint32_t i = 0; i < chunk; ++i) destination[i] = block[in_block + i];
        }
        destination += chunk; remaining -= chunk; ++logical; in_block = 0;
    }
    return 1;
}

int ext4_write_file(ext4_fs_t *fs, uint32_t inode_number, uint64_t offset,
                    const void *buffer, uint32_t size) {
    if (!fs || !fs->mounted || !buffer || size == 0) return 0;
    uint8_t inode[4096];
    if (!read_inode(fs, inode_number, inode)) return 0;
    uint64_t file_size = inode_size_value(inode);
    if (offset > UINT64_MAX - size || offset > file_size) return 0;
    uint64_t end = offset + size;
    if (end > file_size && (load32(&inode[32]) & EXT4_EXTENTS_FL) != 0 &&
        !ext4_resize_extent_file(fs, inode_number, inode, end)) return 0;
    if (end > file_size && (load32(&inode[32]) & EXT4_EXTENTS_FL) == 0 &&
        !ext4_resize_blocks(fs, inode_number, inode, end)) return 0;
    file_size = end > file_size ? end : file_size;
    uint8_t block[4096];
    const uint8_t *source = (const uint8_t *)buffer;
    uint64_t logical = offset / fs->block_size;
    uint32_t in_block = (uint32_t)(offset % fs->block_size);
    uint32_t remaining = size;
    while (remaining != 0) {
        if (logical > UINT32_MAX) return 0;
        uint64_t physical = 0;
        uint8_t hole = 0;
        if (!inode_data_block(fs, inode, (uint32_t)logical, &physical, &hole) ||
            hole || !read_block(fs, physical, block)) {
            return 0;
        }
        uint32_t chunk = fs->block_size - in_block;
        if (chunk > remaining) chunk = remaining;
        for (uint32_t i = 0; i < chunk; ++i)
            block[in_block + i] = source[i];
        if (!write_block(fs, physical, block)) return 0;
        source += chunk; remaining -= chunk; ++logical; in_block = 0;
    }
    return 1;
}

int ext4_truncate_file(ext4_fs_t *fs, uint32_t inode_number, uint64_t size) {
    if (!fs || !fs->mounted) return 0;
    uint8_t inode[4096];
    if (!read_inode(fs, inode_number, inode) ||
        (load16(inode) & 0xf000U) != 0x8000U ||
        size > inode_size_value(inode)) return 0;
    if ((load32(&inode[32]) & EXT4_EXTENTS_FL) != 0) {
        if (size == inode_size_value(inode)) return 1;
        if (size < inode_size_value(inode))
            return ext4_shrink_extent_file(fs, inode_number, inode, size);
        return write_inode_size(fs, inode_number, size);
    }
    return ext4_resize_blocks(fs, inode_number, inode, size);
}
