#include "xfs.h"
#include "../../drivers/storage/storage.h"

#define XFS_SECTOR_SIZE 512U
#define XFS_SB_MAGIC 0x58465342U
#define XFS_SB_VERSION4 4U
#define XFS_INODE_MAGIC 0x494eU
#define XFS_FORMAT_LOCAL 1U
#define XFS_FORMAT_EXTENTS 2U
#define XFS_CORE_V2_SIZE 100U
#define XFS_AGF_MAGIC 0x58414746U
#define XFS_BNO_MAGIC 0x58414254U
#define XFS_BNO_MAGIC_V3 0x58414233U
#define XFS_BNO_MAGIC_REAL 0x41425442U

static uint32_t be32(const uint8_t *p);
static uint16_t be16(const uint8_t *p);
static void store_be16(uint8_t *p, uint16_t value);
static void store_be32(uint8_t *p, uint32_t value);

typedef struct {
    uint32_t records;
    uint32_t count_offset;
    uint8_t real_header;
} xfs_bno_layout_t;

typedef struct {
    uint32_t bno_root;
    uint32_t cnt_root;
    uint32_t bno_level;
    uint32_t cnt_level;
    uint32_t free_blocks;
    uint32_t longest;
    uint8_t authentic;
} xfs_agf_view_t;

/* Legacy contract images predate the separate CNT root.  Authentic AGF
 * records use the upstream field order: BNO/CNT roots at 16/20, BNO/CNT
 * levels at 28/32, and free/longest at 52/56. */
static int xfs_agf_view(const uint8_t *agf, xfs_agf_view_t *view) {
    if (!agf || !view || be32(agf) != XFS_AGF_MAGIC) return 0;
    if (be32(&agf[20]) != 0U) {
        view->bno_root = be32(&agf[16]);
        view->cnt_root = be32(&agf[20]);
        view->bno_level = be32(&agf[28]);
        view->cnt_level = be32(&agf[32]);
        view->free_blocks = be32(&agf[52]);
        view->longest = be32(&agf[56]);
        view->authentic = 1;
    } else {
        view->bno_root = be32(&agf[16]);
        view->cnt_root = 0;
        view->bno_level = be32(&agf[4]) == 1U ? be32(&agf[28]) : be32(&agf[24]);
        view->cnt_level = 0;
        view->free_blocks = be32(&agf[40]);
        view->longest = be32(&agf[44]);
        view->authentic = 0;
    }
    return view->bno_root != 0U && view->bno_level != 0U;
}

static int xfs_bno_layout(const uint8_t *tree, uint32_t block_size,
                          xfs_bno_layout_t *layout) {
    if (!tree || !layout || block_size < 24U) return 0;
    if (be32(tree) == XFS_BNO_MAGIC_REAL) {
        layout->records = be16(&tree[6]);
        layout->count_offset = 6;
        layout->real_header = 1;
    } else if (be32(tree) == XFS_BNO_MAGIC ||
               be32(tree) == XFS_BNO_MAGIC_V3) {
        layout->records = be32(&tree[8]);
        layout->count_offset = 8;
        layout->real_header = 0;
    } else return 0;
    return layout->records <= (block_size - 16U) / 8U &&
           (layout->real_header ? be16(&tree[4]) == 0 : be32(&tree[4]) == 0);
}

static void xfs_bno_store_records(uint8_t *tree,
                                  const xfs_bno_layout_t *layout,
                                  uint32_t records) {
    if (layout->real_header) store_be16(&tree[layout->count_offset],
                                        (uint16_t)records);
    else store_be32(&tree[layout->count_offset], records);
}

static int xfs_read_block(const xfs_fs_t *fs, uint64_t block, void *buffer);
static int xfs_write_block(const xfs_fs_t *fs, uint64_t block, const void *buffer);
static int xfs_validate_auth_cnt(const xfs_fs_t *fs, uint64_t ag_base,
                                 const xfs_agf_view_t *view);
static int xfs_allocate_real_bno(xfs_fs_t *fs, uint32_t allocation_group,
                                 uint32_t blocks, uint64_t *start);
static int xfs_free_real_bno(xfs_fs_t *fs, uint64_t start, uint32_t blocks);

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

static void store_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

static int xfs_validate_auth_cnt(const xfs_fs_t *fs, uint64_t ag_base,
                                 const xfs_agf_view_t *view) {
    uint8_t root[4096], leaf[4096];
    if (!fs || !view || !view->authentic || view->cnt_root == 0 ||
        view->cnt_level == 0 || view->cnt_level > 2 ||
        view->cnt_root >= fs->ag_blocks ||
        !xfs_read_block(fs, ag_base + view->cnt_root, root) ||
        be32(root) != XFS_BNO_MAGIC_REAL ||
        be16(&root[4]) != view->cnt_level - 1U) return 0;
    uint32_t capacity = (fs->block_size - 16U) /
                        (view->cnt_level == 1U ? 8U : 12U);
    uint32_t records = be16(&root[6]);
    if (records > capacity) return 0;
    if (records == 0)
        return view->free_blocks == 0 && view->longest == 0;
    uint32_t total = 0, longest = 0, previous_count = 0, previous_start = 0;
    uint32_t pointer_offset = 16U + capacity * 8U;
    uint32_t children = view->cnt_level == 1U ? 1U : records;
    for (uint32_t child_index = 0; child_index < children; ++child_index) {
        const uint8_t *source = root;
        if (view->cnt_level == 2U) {
            uint32_t child = be32(&root[pointer_offset + child_index * 4U]);
            if (child == 0 || child >= fs->ag_blocks ||
                !xfs_read_block(fs, ag_base + child, leaf) ||
                be32(leaf) != XFS_BNO_MAGIC_REAL || be16(&leaf[4]) != 0)
                return 0;
            source = leaf;
        }
        uint32_t leaf_records = view->cnt_level == 1U ? records : be16(&source[6]);
        if (leaf_records == 0 || leaf_records > (fs->block_size - 16U) / 8U)
            return 0;
        for (uint32_t i = 0; i < leaf_records; ++i) {
            uint32_t start = be32(&source[16U + i * 8U]);
            uint32_t count = be32(&source[20U + i * 8U]);
            if (count == 0 || start > fs->ag_blocks - count ||
                total > UINT32_MAX - count ||
                (total != 0 && (count < previous_count ||
                 (count == previous_count && start <= previous_start))))
                return 0;
            total += count; previous_count = count; previous_start = start;
            if (count > longest) longest = count;
        }
    }
    return total == view->free_blocks && longest == view->longest;
}

typedef struct {
    uint32_t start;
    uint32_t count;
} xfs_free_record_t;

static int xfs_auth_bno_mutate(xfs_fs_t *fs, uint32_t agno, uint32_t blocks,
                               uint32_t target, int allocate, uint64_t *start) {
    uint8_t agf[4096], original_agf[4096], bno[4096], original_bno[4096];
    uint8_t cnt[4096], original_cnt[4096];
    xfs_agf_view_t view;
    xfs_free_record_t records[512];
    if (!fs || !fs->mounted || !blocks || agno >= fs->ag_count ||
        fs->block_size < 512U) return 0;
    uint64_t ag_base = (uint64_t)agno * fs->ag_blocks;
    if (ag_base > fs->block_count - 2U || blocks > fs->ag_blocks ||
        !xfs_read_block(fs, ag_base + 1U, agf) ||
        !xfs_agf_view(agf, &view) || !view.authentic ||
        view.bno_level != 1U || view.cnt_level != 1U ||
        !xfs_read_block(fs, ag_base + view.bno_root, bno) ||
        !xfs_read_block(fs, ag_base + view.cnt_root, cnt) ||
        be32(bno) != XFS_BNO_MAGIC_REAL || be32(cnt) != XFS_BNO_MAGIC_REAL ||
        be16(&bno[4]) != 0 || be16(&cnt[4]) != 0) return 0;
    uint32_t capacity = (fs->block_size - 16U) / 8U;
    uint32_t bno_count = be16(&bno[6]), cnt_count = be16(&cnt[6]);
    if (bno_count > capacity || cnt_count != bno_count)
        return 0;
    uint32_t total = 0, previous_end = 0;
    for (uint32_t i = 0; i < bno_count; ++i) {
        uint32_t record_start = be32(&bno[16U + i * 8U]);
        uint32_t record_count = be32(&bno[20U + i * 8U]);
        if (!record_count || record_start > fs->ag_blocks - record_count ||
            (i && record_start < previous_end) ||
            total > UINT32_MAX - record_count) return 0;
        records[i] = (xfs_free_record_t){record_start, record_count};
        total += record_count;
        previous_end = record_start + record_count;
    }
    if (total != view.free_blocks) return 0;
    for (uint32_t i = 0; i < cnt_count; ++i) {
        uint32_t cnt_start = be32(&cnt[16U + i * 8U]);
        uint32_t cnt_count_value = be32(&cnt[20U + i * 8U]);
        uint32_t match = UINT32_MAX;
        for (uint32_t j = 0; j < bno_count; ++j)
            if (records[j].start == cnt_start &&
                records[j].count == cnt_count_value) { match = j; break; }
        if (match == UINT32_MAX) return 0;
        if (i != 0U) {
            uint32_t previous_count = be32(&cnt[20U + (i - 1U) * 8U]);
            uint32_t previous_start = be32(&cnt[16U + (i - 1U) * 8U]);
            if (cnt_count_value < previous_count ||
                (cnt_count_value == previous_count && cnt_start <= previous_start))
                return 0;
        }
    }
    for (uint32_t i = 0; i < fs->block_size; ++i) {
        original_agf[i] = agf[i]; original_bno[i] = bno[i]; original_cnt[i] = cnt[i];
    }
    uint32_t record = UINT32_MAX;
    if (allocate) {
        for (uint32_t i = 0; i < bno_count; ++i)
            if (records[i].count >= blocks &&
                (record == UINT32_MAX || records[i].count < records[record].count))
                record = i;
        if (record == UINT32_MAX) return 0;
        *start = ag_base + records[record].start;
        if (records[record].count == blocks) {
            for (uint32_t i = record; i + 1U < bno_count; ++i)
                records[i] = records[i + 1U];
            --bno_count;
        } else {
            records[record].start += blocks;
            records[record].count -= blocks;
        }
        if (view.free_blocks < blocks) return 0;
        store_be32(&agf[52], view.free_blocks - blocks);
    } else {
        if (blocks > fs->ag_blocks || target > fs->ag_blocks - blocks) return 0;
        for (uint32_t i = 0; i < bno_count; ++i)
            if (target < records[i].start) {
                if (target > UINT32_MAX - blocks ||
                    target + blocks > records[i].start) return 0;
                record = i;
                break;
            } else if (target < records[i].start + records[i].count) return 0;
        if (record == UINT32_MAX) record = bno_count;
        uint32_t previous = record == 0 ? UINT32_MAX : record - 1U;
        uint32_t next = record < bno_count ? record : UINT32_MAX;
        int join_previous = previous != UINT32_MAX &&
            records[previous].start + records[previous].count == target;
        int join_next = next != UINT32_MAX && target + blocks == records[next].start;
        if (join_previous && join_next) {
            records[previous].count += blocks + records[next].count;
            for (uint32_t i = next; i + 1U < bno_count; ++i)
                records[i] = records[i + 1U];
            --bno_count;
        } else if (join_previous) records[previous].count += blocks;
        else if (join_next) { records[next].start = target; records[next].count += blocks; }
        else {
            if (bno_count >= capacity) return 0;
            for (uint32_t i = bno_count; i > record; --i) records[i] = records[i - 1U];
            records[record] = (xfs_free_record_t){target, blocks};
            ++bno_count;
        }
        if (view.free_blocks > UINT32_MAX - blocks) return 0;
        store_be32(&agf[52], view.free_blocks + blocks);
    }
    for (uint32_t i = 0; i < bno_count; ++i) {
        store_be32(&bno[16U + i * 8U], records[i].start);
        store_be32(&bno[20U + i * 8U], records[i].count);
    }
    for (uint32_t i = bno_count; i < be16(&bno[6]); ++i) {
        store_be32(&bno[16U + i * 8U], 0); store_be32(&bno[20U + i * 8U], 0);
    }
    store_be16(&bno[6], (uint16_t)bno_count);
    for (uint32_t i = 0; i < bno_count; ++i)
        for (uint32_t j = i + 1U; j < bno_count; ++j)
            if (records[j].count < records[i].count ||
                (records[j].count == records[i].count && records[j].start < records[i].start)) {
                xfs_free_record_t swap = records[i]; records[i] = records[j]; records[j] = swap;
            }
    for (uint32_t i = 0; i < bno_count; ++i) {
        store_be32(&cnt[16U + i * 8U], records[i].start);
        store_be32(&cnt[20U + i * 8U], records[i].count);
    }
    store_be16(&cnt[6], (uint16_t)bno_count);
    uint32_t longest = 0;
    for (uint32_t i = 0; i < bno_count; ++i)
        if (records[i].count > longest) longest = records[i].count;
    store_be32(&agf[56], longest);
    if (!xfs_write_block(fs, ag_base + view.bno_root, bno) ||
        !xfs_write_block(fs, ag_base + view.cnt_root, cnt) ||
        !xfs_write_block(fs, ag_base + 1U, agf)) goto rollback;
    return 1;
rollback:
    (void)xfs_write_block(fs, ag_base + view.bno_root, original_bno);
    (void)xfs_write_block(fs, ag_base + view.cnt_root, original_cnt);
    (void)xfs_write_block(fs, ag_base + 1U, original_agf);
    return 0;
}

static int xfs_allocate_extent_locked(xfs_fs_t *fs, uint32_t allocation_group,
                        uint32_t blocks, uint64_t *start) {
    uint8_t agf[4096], tree[4096], original_agf[4096], original_tree[4096];
    xfs_agf_view_t agf_view;
    if (!fs || !fs->mounted || !start || blocks == 0 || allocation_group >= fs->ag_count ||
        fs->ag_blocks < 2U || fs->block_count < 2U) return 0;
    uint64_t ag_base = (uint64_t)allocation_group * fs->ag_blocks;
    if (ag_base > fs->block_count - 2U || !xfs_read_block(fs, ag_base + 1U, agf) ||
        !xfs_agf_view(agf, &agf_view)) return 0;
    if (agf_view.authentic)
        return xfs_auth_bno_mutate(fs, allocation_group, blocks, 0, 1, start);
    if (agf_view.bno_level > 1U)
        return xfs_allocate_real_bno(fs, allocation_group, blocks, start);
    uint32_t root = be32(&agf[16]);
    uint32_t level = be32(&agf[24]);
    if (be32(&agf[4]) == 1U) level = be32(&agf[28]);
    if (level != 1U || root == 0 || root >= fs->ag_blocks ||
        !xfs_read_block(fs, ag_base + root, tree)) return 0;
    xfs_bno_layout_t layout;
    if (!xfs_bno_layout(tree, fs->block_size, &layout)) return 0;
    for (uint32_t byte = 0; byte < fs->block_size; ++byte) {
        original_agf[byte] = agf[byte];
        original_tree[byte] = tree[byte];
    }
    uint32_t records = layout.records;
    if (records == 0 || records > (fs->block_size - 16U) / 8U) return 0;
    uint32_t selected = UINT32_MAX, selected_start = 0, selected_count = 0;
    uint32_t previous_end = 0;
    uint64_t total_free = 0;
    for (uint32_t i = 0; i < records; ++i) {
        uint32_t record_start = be32(&tree[16U + i * 8U]);
        uint32_t record_count = be32(&tree[20U + i * 8U]);
        if (record_count == 0 || record_count > fs->ag_blocks ||
            record_start > fs->ag_blocks - record_count ||
            (i != 0 && record_start < previous_end) ||
            total_free > UINT32_MAX - record_count)
            return 0;
        previous_end = record_start + record_count;
        total_free += record_count;
        if (record_count >= blocks && (selected == UINT32_MAX || record_count < selected_count)) {
            selected = i; selected_start = record_start; selected_count = record_count;
        }
    }
    if (total_free != be32(&agf[40])) return 0;
    if (selected == UINT32_MAX || selected_start > fs->ag_blocks - blocks ||
        ag_base + selected_start > UINT64_MAX - blocks ||
        ag_base + selected_start + blocks > fs->block_count) return 0;
    *start = ag_base + selected_start;
    if (selected_count == blocks) {
        for (uint32_t i = selected; i + 1U < records; ++i)
            for (uint32_t byte = 0; byte < 8U; ++byte)
                tree[16U + i * 8U + byte] = tree[16U + (i + 1U) * 8U + byte];
        --records;
    } else {
        store_be32(&tree[16U + selected * 8U], selected_start + blocks);
        store_be32(&tree[20U + selected * 8U], selected_count - blocks);
    }
    xfs_bno_store_records(tree, &layout, records);
    uint32_t longest = 0;
    for (uint32_t i = 0; i < records; ++i)
        if (be32(&tree[20U + i * 8U]) > longest) longest = be32(&tree[20U + i * 8U]);
    uint32_t free_blocks = be32(&agf[40]);
    if (free_blocks < blocks) return 0;
    store_be32(&agf[40], free_blocks - blocks);
    store_be32(&agf[44], longest);
    if (!xfs_write_block(fs, ag_base + root, tree)) return 0;
    if (!xfs_write_block(fs, ag_base + 1U, agf)) {
        (void)xfs_write_block(fs, ag_base + root, original_tree);
        (void)xfs_write_block(fs, ag_base + 1U, original_agf);
        return 0;
    }
    return 1;
}

int xfs_allocate_extent(xfs_fs_t *fs, uint32_t allocation_group,
                        uint32_t blocks, uint64_t *start) {
    if (!fs) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->lock);
    int result = xfs_allocate_extent_locked(fs, allocation_group, blocks, start);
    spinlock_unlock_irqrestore(&fs->lock, flags);
    return result;
}

static int xfs_free_extent_locked(xfs_fs_t *fs, uint64_t start, uint32_t blocks) {
    uint8_t agf[4096], tree[4096], original_agf[4096], original_tree[4096];
    xfs_agf_view_t agf_view;
    if (!fs || !fs->mounted || blocks == 0 || fs->ag_blocks < 2U ||
        fs->block_count == 0 || start >= fs->block_count ||
        blocks > fs->block_count - start) return 0;
    uint32_t allocation_group = (uint32_t)(start / fs->ag_blocks);
    uint32_t relative = (uint32_t)(start % fs->ag_blocks);
    if (allocation_group >= fs->ag_count || relative >= fs->ag_blocks ||
        blocks > fs->ag_blocks - relative) return 0;
    uint64_t ag_base = (uint64_t)allocation_group * fs->ag_blocks;
    if (ag_base > fs->block_count - 2U ||
        !xfs_read_block(fs, ag_base + 1U, agf) ||
        !xfs_agf_view(agf, &agf_view))
        return 0;
    if (agf_view.authentic)
        return xfs_auth_bno_mutate(fs, allocation_group, blocks, relative, 0, 0);
    if (agf_view.bno_level > 1U)
        return xfs_free_real_bno(fs, start, blocks);
    uint32_t root = be32(&agf[16]);
    uint32_t level = be32(&agf[24]);
    if (be32(&agf[4]) == 1U) level = be32(&agf[28]);
    if (root == 0 || root >= fs->ag_blocks || level != 1U ||
        !xfs_read_block(fs, ag_base + root, tree)) return 0;
    xfs_bno_layout_t layout;
    if (!xfs_bno_layout(tree, fs->block_size, &layout)) return 0;
    for (uint32_t byte = 0; byte < fs->block_size; ++byte) {
        original_agf[byte] = agf[byte];
        original_tree[byte] = tree[byte];
    }
    uint32_t records = layout.records;
    uint32_t capacity = (fs->block_size - 16U) / 8U;
    if (records > capacity) return 0;
    uint32_t insert = records;
    uint32_t previous_end = 0;
    uint64_t total_free = 0;
    for (uint32_t i = 0; i < records; ++i) {
        uint32_t record_start = be32(&tree[16U + i * 8U]);
        uint32_t record_count = be32(&tree[20U + i * 8U]);
        if (record_count == 0 || record_count > fs->ag_blocks ||
            record_start > fs->ag_blocks - record_count ||
            (i != 0 && record_start < previous_end) ||
            total_free > UINT32_MAX - record_count) return 0;
        previous_end = record_start + record_count;
        total_free += record_count;
        if (relative < record_start && insert == records) insert = i;
        if (relative < record_start + record_count &&
            relative + blocks > record_start) return 0;
    }
    if (total_free != be32(&agf[40])) return 0;
    uint32_t previous = insert == 0 ? UINT32_MAX : insert - 1U;
    uint32_t next = insert < records ? insert : UINT32_MAX;
    int joins_previous = previous != UINT32_MAX &&
        be32(&tree[16U + previous * 8U]) + be32(&tree[20U + previous * 8U]) == relative;
    int joins_next = next != UINT32_MAX &&
        relative + blocks == be32(&tree[16U + next * 8U]);
    if (joins_previous && joins_next) {
        uint32_t merged = be32(&tree[20U + previous * 8U]) + blocks +
                          be32(&tree[20U + next * 8U]);
        store_be32(&tree[20U + previous * 8U], merged);
        for (uint32_t i = next; i + 1U < records; ++i)
            for (uint32_t byte = 0; byte < 8U; ++byte)
                tree[16U + i * 8U + byte] = tree[16U + (i + 1U) * 8U + byte];
        --records;
    } else if (joins_previous) {
        uint32_t old = be32(&tree[20U + previous * 8U]);
        if (old > UINT32_MAX - blocks) return 0;
        store_be32(&tree[20U + previous * 8U], old + blocks);
    } else if (joins_next) {
        uint32_t old = be32(&tree[20U + next * 8U]);
        if (old > UINT32_MAX - blocks) return 0;
        store_be32(&tree[16U + next * 8U], relative);
        store_be32(&tree[20U + next * 8U], old + blocks);
    } else {
        if (records == capacity) return 0;
        for (uint32_t i = records; i > insert; --i)
            for (uint32_t byte = 0; byte < 8U; ++byte)
                tree[16U + i * 8U + byte] = tree[16U + (i - 1U) * 8U + byte];
        store_be32(&tree[16U + insert * 8U], relative);
        store_be32(&tree[20U + insert * 8U], blocks);
        ++records;
    }
    uint32_t free_blocks = be32(&agf[40]);
    if (free_blocks > fs->ag_blocks - blocks) return 0;
    uint32_t longest = 0;
    for (uint32_t i = 0; i < records; ++i)
        if (be32(&tree[20U + i * 8U]) > longest) longest = be32(&tree[20U + i * 8U]);
    xfs_bno_store_records(tree, &layout, records);
    store_be32(&agf[40], free_blocks + blocks);
    store_be32(&agf[44], longest);
    if (!xfs_write_block(fs, ag_base + root, tree)) return 0;
    if (!xfs_write_block(fs, ag_base + 1U, agf)) {
        (void)xfs_write_block(fs, ag_base + root, original_tree);
        (void)xfs_write_block(fs, ag_base + 1U, original_agf);
        return 0;
    }
    return 1;
}

int xfs_free_extent(xfs_fs_t *fs, uint64_t start, uint32_t blocks) {
    if (!fs) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->lock);
    int result = xfs_free_extent_locked(fs, start, blocks);
    spinlock_unlock_irqrestore(&fs->lock, flags);
    return result;
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
    spinlock_init(&fs->lock);
    fs->device = device; fs->block_size = block_size; fs->inode_size = inode_size;
    fs->ag_count = ag_count; fs->ag_blocks = ag_blocks; fs->block_count = blocks;
    fs->ag_block_log = sb[112]; fs->inode_per_block_log = inopblock_log;
    fs->root_inode = be64(&sb[56]); fs->mounted = 1;
    for (uint32_t agno = 0; agno < fs->ag_count; ++agno) {
        uint8_t agf[4096];
        xfs_agf_view_t view;
        uint64_t ag_base = (uint64_t)agno * fs->ag_blocks;
        if (ag_base > fs->block_count - 2U ||
            !xfs_read_block(fs, ag_base + 1U, agf)) {
            fs->mounted = 0;
            return 0;
        }
        if (be32(&agf[20]) != 0U &&
            (!xfs_agf_view(agf, &view) ||
             !xfs_validate_auth_cnt(fs, ag_base, &view))) {
            fs->mounted = 0;
            return 0;
        }
    }
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

/* The existing two-level path handles a root and leaf set.  Keep the deeper
 * path deliberately bounded: it supports one additional authenticated index
 * level without silently treating an arbitrary on-disk tree as flat. */
typedef struct {
    xfs_fs_t *fs;
    uint64_t ag_base;
    uint32_t requested;
    uint32_t target;
    uint32_t root_level;
    uint32_t path_blocks[4];
    uint8_t path[4][4096];
    uint32_t selected_blocks[4];
    uint8_t selected[4][4096];
    uint32_t selected_depth;
    uint32_t selected_record;
    uint32_t selected_start;
    uint32_t selected_count;
    uint32_t last_blocks[4];
    uint8_t last[4][4096];
    uint32_t last_depth;
    uint32_t total_free;
    uint32_t longest;
    uint32_t longest_records;
    uint8_t allocate;
    uint8_t invalid;
    uint8_t found;
} xfs_deep_bno_context_t;

static void xfs_deep_copy_path(xfs_deep_bno_context_t *context,
                               uint8_t destination[4][4096],
                               uint32_t blocks[4], uint32_t *depth) {
    *depth = context->root_level + 1U;
    for (uint32_t i = 0; i < *depth; ++i) {
        blocks[i] = context->path_blocks[i];
        for (uint32_t byte = 0; byte < context->fs->block_size; ++byte)
            destination[i][byte] = context->path[i][byte];
    }
}

static int xfs_deep_scan(xfs_deep_bno_context_t *context, uint32_t block,
                         uint32_t level, uint32_t depth) {
    uint8_t node[4096];
    if (!context || !context->fs || depth >= 4U || block == 0 ||
        block >= context->fs->ag_blocks ||
        !xfs_read_block(context->fs, context->ag_base + block, node) ||
        be32(node) != XFS_BNO_MAGIC_REAL || be16(&node[4]) != level)
        return 0;
    context->path_blocks[depth] = block;
    for (uint32_t byte = 0; byte < context->fs->block_size; ++byte)
        context->path[depth][byte] = node[byte];
    uint32_t capacity = (context->fs->block_size - 16U) /
                        (level == 0 ? 8U : 12U);
    uint32_t records = be16(&node[6]);
    if (records == 0 || records > capacity) return 0;
    if (level != 0) {
        uint32_t pointer_offset = 16U + capacity * 8U;
        for (uint32_t i = 0; i < records; ++i) {
            uint32_t key = be32(&node[16U + i * 8U]);
            uint32_t count = be32(&node[20U + i * 8U]);
            uint32_t child = be32(&node[pointer_offset + i * 4U]);
            if (count == 0 || key > context->fs->ag_blocks - count ||
                child == 0 || child >= context->fs->ag_blocks ||
                (i != 0 && key < be32(&node[16U + (i - 1U) * 8U]))) return 0;
            if (!xfs_deep_scan(context, child, level - 1U, depth + 1U) ||
                key != be32(&context->path[depth + 1U][16]) ||
                count != be32(&context->path[depth + 1U][20])) return 0;
        }
        return 1;
    }
    uint32_t previous_end = 0;
    uint32_t last_end = 0;
    for (uint32_t i = 0; i < records; ++i) {
        uint32_t start = be32(&node[16U + i * 8U]);
        uint32_t count = be32(&node[20U + i * 8U]);
        if (count == 0 || start > context->fs->ag_blocks - count ||
            (i != 0 && start < previous_end) ||
            context->total_free > UINT32_MAX - count) return 0;
        previous_end = start + count;
        last_end = previous_end;
        context->total_free += count;
        if (count > context->longest) {
            context->longest = count;
            context->longest_records = 1;
        } else if (count == context->longest) {
            ++context->longest_records;
        }
        if (context->allocate && count >= context->requested &&
            (!context->found || count < context->selected_count)) {
            context->found = 1;
            context->selected_record = i;
            context->selected_start = start;
            context->selected_count = count;
            xfs_deep_copy_path(context, context->selected,
                               context->selected_blocks,
                               &context->selected_depth);
        }
        if (!context->allocate) {
            if (context->target >= start && context->target < start + count)
                context->invalid = 1;
            if (!context->found && context->target <= start) {
                context->found = 1;
                xfs_deep_copy_path(context, context->selected,
                                   context->selected_blocks,
                                   &context->selected_depth);
            }
        }
    }
    if (!context->allocate) {
        if (context->target <= last_end && !context->found) {
            context->found = 1;
            xfs_deep_copy_path(context, context->selected,
                               context->selected_blocks,
                               &context->selected_depth);
        }
        xfs_deep_copy_path(context, context->last, context->last_blocks,
                           &context->last_depth);
    }
    return 1;
}

static int xfs_deep_bno_mutate(xfs_fs_t *fs, uint32_t allocation_group,
                               uint32_t blocks, uint32_t target, int allocate,
                               uint64_t *start) {
    uint8_t agf[4096], original_agf[4096];
    uint8_t original_path[4][4096];
    xfs_deep_bno_context_t context = {0};
    if (!fs || !fs->mounted || !blocks || allocation_group >= fs->ag_count ||
        fs->block_size < 512U) return 0;
    uint64_t ag_base = (uint64_t)allocation_group * fs->ag_blocks;
    if (ag_base > fs->block_count - 2U ||
        !xfs_read_block(fs, ag_base + 1U, agf) || be32(agf) != XFS_AGF_MAGIC ||
        be32(&agf[4]) != 1U || be32(&agf[28]) < 3U || be32(&agf[28]) > 4U)
        return 0;
    uint32_t root = be32(&agf[16]);
    context.fs = fs; context.ag_base = ag_base; context.requested = blocks;
    context.target = target; context.root_level = be32(&agf[28]);
    context.allocate = (uint8_t)allocate;
    if (!xfs_deep_scan(&context, root, context.root_level, 0) ||
        context.total_free != be32(&agf[40]) || context.invalid) return 0;
    if (!allocate && !context.found) {
        if (context.last_depth == 0) return 0;
        context.found = 1;
        for (uint32_t i = 0; i < context.last_depth; ++i) {
            context.selected_blocks[i] = context.last_blocks[i];
            for (uint32_t byte = 0; byte < fs->block_size; ++byte)
                context.selected[i][byte] = context.last[i][byte];
        }
        context.selected_depth = context.last_depth;
    }
    if (!context.found || context.selected_depth != context.root_level + 1U)
        return 0;
    uint8_t *leaf = context.selected[context.root_level];
    for (uint32_t depth = 0; depth <= context.root_level; ++depth)
        for (uint32_t byte = 0; byte < fs->block_size; ++byte)
            original_path[depth][byte] = context.selected[depth][byte];
    uint32_t leaf_records = be16(&leaf[6]);
    if (allocate) {
        uint32_t record = context.selected_record;
        if (context.selected_count == blocks) return 0;
        store_be32(&leaf[16U + record * 8U], context.selected_start + blocks);
        store_be32(&leaf[20U + record * 8U], context.selected_count - blocks);
        *start = ag_base + context.selected_start;
        if (context.selected_count == context.longest &&
            context.longest_records == 1U) {
            context.longest = 0;
            for (uint32_t i = 0; i < be16(&leaf[6]); ++i) {
                uint32_t count = be32(&leaf[20U + i * 8U]);
                if (count > context.longest) context.longest = count;
            }
        }
    } else {
        uint32_t relative = target;
        uint32_t insert = leaf_records;
        for (uint32_t i = 0; i < leaf_records; ++i)
            if (relative < be32(&leaf[16U + i * 8U])) { insert = i; break; }
        uint32_t previous = insert == 0 ? UINT32_MAX : insert - 1U;
        uint32_t next = insert < leaf_records ? insert : UINT32_MAX;
        int joins_previous = previous != UINT32_MAX &&
            be32(&leaf[16U + previous * 8U]) +
                be32(&leaf[20U + previous * 8U]) == relative;
        int joins_next = next != UINT32_MAX && relative + blocks ==
                         be32(&leaf[16U + next * 8U]);
        if (joins_previous && joins_next) {
            store_be32(&leaf[20U + previous * 8U],
                       be32(&leaf[20U + previous * 8U]) + blocks +
                       be32(&leaf[20U + next * 8U]));
            for (uint32_t i = next; i + 1U < leaf_records; ++i)
                for (uint32_t byte = 0; byte < 8U; ++byte)
                    leaf[16U + i * 8U + byte] = leaf[16U + (i + 1U) * 8U + byte];
            --leaf_records;
        } else if (joins_previous) {
            store_be32(&leaf[20U + previous * 8U],
                       be32(&leaf[20U + previous * 8U]) + blocks);
        } else if (joins_next) {
            store_be32(&leaf[16U + next * 8U], relative);
            store_be32(&leaf[20U + next * 8U],
                       be32(&leaf[20U + next * 8U]) + blocks);
        } else {
            if (leaf_records >= (fs->block_size - 16U) / 8U) return 0;
            for (uint32_t i = leaf_records; i > insert; --i)
                for (uint32_t byte = 0; byte < 8U; ++byte)
                    leaf[16U + i * 8U + byte] = leaf[16U + (i - 1U) * 8U + byte];
            store_be32(&leaf[16U + insert * 8U], relative);
            store_be32(&leaf[20U + insert * 8U], blocks);
            ++leaf_records;
        }
        store_be16(&leaf[6], (uint16_t)leaf_records);
    }
    for (uint32_t depth = context.root_level; depth != 0; --depth) {
        uint8_t *parent = context.selected[depth - 1U];
        uint32_t capacity = (fs->block_size - 16U) / 12U;
        uint32_t pointer_offset = 16U + capacity * 8U;
        uint32_t child = context.selected_blocks[depth];
        uint32_t records = be16(&parent[6]);
        uint32_t first = be32(&leaf[16]);
        uint32_t first_count = be32(&leaf[20]);
        int found = 0;
        for (uint32_t i = 0; i < records; ++i)
            if (be32(&parent[pointer_offset + i * 4U]) == child) {
                store_be32(&parent[16U + i * 8U], first);
                store_be32(&parent[20U + i * 8U], first_count);
                found = 1; break;
            }
        if (!found) return 0;
        leaf = parent;
    }
    for (uint32_t i = 0; i < fs->block_size; ++i) original_agf[i] = agf[i];
    uint32_t free_blocks = be32(&agf[40]);
    if (allocate) {
        if (free_blocks < blocks) return 0;
        store_be32(&agf[40], free_blocks - blocks);
    } else {
        if (free_blocks > UINT32_MAX - blocks) return 0;
        store_be32(&agf[40], free_blocks + blocks);
        for (uint32_t i = 0; i < be16(&context.selected[context.root_level][6]); ++i) {
            uint32_t count = be32(&context.selected[context.root_level][20U + i * 8U]);
            if (count > context.longest) context.longest = count;
        }
    }
    for (uint32_t depth = context.root_level; ; --depth) {
        if (!xfs_write_block(fs, ag_base + context.selected_blocks[depth],
                             context.selected[depth])) goto rollback;
        if (depth == 0) break;
    }
    store_be32(&agf[44], context.longest);
    if (!xfs_write_block(fs, ag_base + 1U, agf)) goto rollback;
    return 1;
rollback:
    for (uint32_t depth = 0; depth <= context.root_level; ++depth)
        (void)xfs_write_block(fs, ag_base + context.selected_blocks[depth],
                              original_path[depth]);
    (void)xfs_write_block(fs, ag_base + 1U, original_agf);
    return 0;
}

static int xfs_allocate_real_bno(xfs_fs_t *fs, uint32_t allocation_group,
                                 uint32_t blocks, uint64_t *start) {
    uint8_t agf[4096], original_agf[4096], root[4096], original_root[4096];
    uint8_t scan[4096];
    uint8_t leaf[4096], original_leaf[4096];
    if (!fs || !fs->mounted || !start || blocks == 0 ||
        allocation_group >= fs->ag_count || fs->block_size < 512U) return 0;
    uint64_t ag_base = (uint64_t)allocation_group * fs->ag_blocks;
    if (ag_base > fs->block_count - 2U ||
        !xfs_read_block(fs, ag_base + 1U, agf) || be32(agf) != XFS_AGF_MAGIC ||
        be32(&agf[4]) != 1U) return 0;
    if (be32(&agf[28]) >= 3U)
        return xfs_deep_bno_mutate(fs, allocation_group, blocks, 0, 1, start);
    if (be32(&agf[28]) != 2U) return 0;
    uint32_t root_block = be32(&agf[16]);
    uint32_t capacity = (fs->block_size - 16U) / 12U;
    if (root_block == 0 || root_block >= fs->ag_blocks || capacity == 0 ||
        !xfs_read_block(fs, ag_base + root_block, root) ||
        be32(root) != XFS_BNO_MAGIC_REAL || be16(&root[4]) != 1U) return 0;
    uint32_t root_records = be16(&root[6]);
    if (root_records == 0 || root_records > capacity) return 0;
    for (uint32_t i = 0; i < fs->block_size; ++i) {
        original_agf[i] = agf[i]; original_root[i] = root[i];
    }
    uint32_t pointer_offset = 16U + capacity * 8U;
    uint32_t selected = UINT32_MAX, selected_leaf = 0;
    uint32_t selected_start = 0, selected_count = 0;
    uint64_t total_free = 0;
    uint32_t previous_key = 0;
    for (uint32_t i = 0; i < root_records; ++i) {
        uint32_t key_start = be32(&root[16U + i * 8U]);
        uint32_t key_count = be32(&root[20U + i * 8U]);
        if (key_count == 0 || key_count > fs->ag_blocks ||
            key_start > fs->ag_blocks - key_count ||
            (i != 0 && key_start < previous_key)) return 0;
        previous_key = key_start;
        uint32_t child = be32(&root[pointer_offset + i * 4U]);
        if (child == 0 || child >= fs->ag_blocks ||
            !xfs_read_block(fs, ag_base + child, leaf) ||
            be32(leaf) != XFS_BNO_MAGIC_REAL || be16(&leaf[4]) != 0) return 0;
        uint32_t records = be16(&leaf[6]);
        if (records == 0 || records > (fs->block_size - 16U) / 8U) return 0;
        uint32_t previous_end = 0;
        for (uint32_t r = 0; r < records; ++r) {
            uint32_t record_start = be32(&leaf[16U + r * 8U]);
            uint32_t record_count = be32(&leaf[20U + r * 8U]);
            if (record_count == 0 || record_start > fs->ag_blocks - record_count ||
                (r != 0 && record_start < previous_end) ||
                total_free > UINT32_MAX - record_count) return 0;
            previous_end = record_start + record_count;
            total_free += record_count;
            if (record_count >= blocks &&
                (selected == UINT32_MAX || record_count < selected_count)) {
                selected = i; selected_leaf = child; selected_start = record_start;
                selected_count = record_count;
                for (uint32_t b = 0; b < fs->block_size; ++b)
                    original_leaf[b] = leaf[b];
            }
        }
    }
    if (total_free != be32(&agf[40]) || selected == UINT32_MAX ||
        selected_start > fs->ag_blocks - blocks ||
        ag_base + selected_start > UINT64_MAX - blocks ||
        ag_base + selected_start + blocks > fs->block_count) return 0;
    if (!xfs_read_block(fs, ag_base + selected_leaf, leaf)) return 0;
    uint32_t leaf_records = be16(&leaf[6]);
    uint32_t chosen_record = 0;
    while (chosen_record < leaf_records &&
           be32(&leaf[20U + chosen_record * 8U]) != selected_count) ++chosen_record;
    while (chosen_record < leaf_records &&
           be32(&leaf[16U + chosen_record * 8U]) != selected_start) ++chosen_record;
    if (chosen_record == leaf_records) return 0;
    if (selected_count == blocks) {
        for (uint32_t i = chosen_record; i + 1U < leaf_records; ++i)
            for (uint32_t b = 0; b < 8U; ++b)
                leaf[16U + i * 8U + b] = leaf[16U + (i + 1U) * 8U + b];
        --leaf_records;
    } else {
        store_be32(&leaf[16U + chosen_record * 8U], selected_start + blocks);
        store_be32(&leaf[20U + chosen_record * 8U], selected_count - blocks);
    }
    store_be16(&leaf[6], (uint16_t)leaf_records);
    uint32_t free_blocks = be32(&agf[40]);
    if (free_blocks < blocks) return 0;
    store_be32(&agf[40], free_blocks - blocks);
    uint32_t longest = 0;
    for (uint32_t i = 0; i < root_records; ++i) {
        uint32_t child = be32(&root[pointer_offset + i * 4U]);
        if (child == selected_leaf) {
            if (leaf_records == 0) {
                for (uint32_t r = i; r + 1U < root_records; ++r) {
                    for (uint32_t b = 0; b < 8U; ++b)
                        root[16U + r * 8U + b] = root[16U + (r + 1U) * 8U + b];
                    for (uint32_t b = 0; b < 4U; ++b)
                        root[pointer_offset + r * 4U + b] =
                            root[pointer_offset + (r + 1U) * 4U + b];
                }
                --root_records;
                store_be16(&root[6], (uint16_t)root_records);
            } else {
                for (uint32_t b = 0; b < 8U; ++b)
                    root[16U + i * 8U + b] = leaf[16U + b];
            }
        }
        const uint8_t *source = child == selected_leaf ? leaf : scan;
        if (child != selected_leaf && !xfs_read_block(fs, ag_base + child, scan)) return 0;
        uint32_t records = be16(&source[6]);
        for (uint32_t r = 0; r < records; ++r)
            if (be32(&source[20U + r * 8U]) > longest)
                longest = be32(&source[20U + r * 8U]);
    }
    store_be32(&agf[44], longest);
    *start = ag_base + selected_start;
    if (!xfs_write_block(fs, ag_base + selected_leaf, leaf) ||
        !xfs_write_block(fs, ag_base + root_block, root) ||
        !xfs_write_block(fs, ag_base + 1U, agf)) {
        (void)xfs_write_block(fs, ag_base + selected_leaf, original_leaf);
        (void)xfs_write_block(fs, ag_base + root_block, original_root);
        (void)xfs_write_block(fs, ag_base + 1U, original_agf);
        return 0;
    }
    return 1;
}

static int xfs_free_real_bno(xfs_fs_t *fs, uint64_t start, uint32_t blocks) {
    uint8_t agf[4096], original_agf[4096], root[4096], original_root[4096];
    uint8_t leaf[4096], original_leaf[4096], scan[4096];
    uint8_t next_leaf[4096], original_next[4096];
    if (!fs || !fs->mounted || blocks == 0 || start >= fs->block_count ||
        blocks > fs->block_count - start || fs->block_size < 512U) return 0;
    uint32_t agno = (uint32_t)(start / fs->ag_blocks);
    uint32_t relative = (uint32_t)(start % fs->ag_blocks);
    if (agno >= fs->ag_count || blocks > fs->ag_blocks - relative) return 0;
    uint64_t ag_base = (uint64_t)agno * fs->ag_blocks;
    if (ag_base > fs->block_count - 2U ||
        !xfs_read_block(fs, ag_base + 1U, agf) || be32(agf) != XFS_AGF_MAGIC ||
        be32(&agf[4]) != 1U) return 0;
    if (be32(&agf[28]) >= 3U)
        return xfs_deep_bno_mutate(fs, agno, blocks, relative, 0, 0);
    if (be32(&agf[28]) != 2U) return 0;
    uint32_t root_block = be32(&agf[16]);
    uint32_t capacity = (fs->block_size - 16U) / 12U;
    if (root_block == 0 || root_block >= fs->ag_blocks || capacity == 0 ||
        !xfs_read_block(fs, ag_base + root_block, root) ||
        be32(root) != XFS_BNO_MAGIC_REAL || be16(&root[4]) != 1U) return 0;
    uint32_t root_records = be16(&root[6]);
    if (root_records == 0 || root_records > capacity) return 0;
    for (uint32_t i = 0; i < fs->block_size; ++i) {
        original_agf[i] = agf[i]; original_root[i] = root[i];
    }
    uint32_t pointer_offset = 16U + capacity * 8U;
    uint32_t selected = UINT32_MAX, selected_leaf = 0;
    uint64_t total_free = 0;
    uint32_t previous_key = 0;
    uint32_t previous_child = 0, previous_leaf_end = 0;
    for (uint32_t i = 0; i < root_records; ++i) {
        uint32_t key_start = be32(&root[16U + i * 8U]);
        uint32_t key_count = be32(&root[20U + i * 8U]);
        if (key_count == 0 || key_count > fs->ag_blocks ||
            key_start > fs->ag_blocks - key_count ||
            (i != 0 && key_start < previous_key)) return 0;
        previous_key = key_start;
        uint32_t child = be32(&root[pointer_offset + i * 4U]);
        if (child == 0 || child >= fs->ag_blocks ||
            !xfs_read_block(fs, ag_base + child, scan) ||
            be32(scan) != XFS_BNO_MAGIC_REAL || be16(&scan[4]) != 0) return 0;
        uint32_t records = be16(&scan[6]);
        if (records == 0 || records > (fs->block_size - 16U) / 8U) return 0;
        uint32_t previous_end = 0;
        for (uint32_t r = 0; r < records; ++r) {
            uint32_t record_start = be32(&scan[16U + r * 8U]);
            uint32_t record_count = be32(&scan[20U + r * 8U]);
            if (record_count == 0 || record_start > fs->ag_blocks - record_count ||
                (r != 0 && record_start < previous_end) ||
                total_free > UINT32_MAX - record_count) return 0;
            if (relative < record_start) {
                if (relative + blocks > record_start) return 0;
                if (selected == UINT32_MAX) {
                    if (r == 0 && i != 0 && previous_child != 0 &&
                        previous_leaf_end == relative) {
                        selected = i - 1U;
                        selected_leaf = previous_child;
                    } else {
                        selected = i;
                        selected_leaf = child;
                    }
                }
            } else if (relative < record_start + record_count) return 0;
            previous_end = record_start + record_count;
            total_free += record_count;
        }
        if (selected == UINT32_MAX && i != 0 && previous_child != 0 &&
            previous_leaf_end == relative) {
            selected = i - 1U;
            selected_leaf = previous_child;
        }
        previous_child = child;
        previous_leaf_end = previous_end;
        if (selected == UINT32_MAX && i + 1U == root_records) {
            selected = i; selected_leaf = child;
        }
    }
    if (selected == UINT32_MAX || total_free != be32(&agf[40]) ||
        be32(&agf[40]) > UINT32_MAX - blocks ||
        !xfs_read_block(fs, ag_base + selected_leaf, leaf)) return 0;
    for (uint32_t i = 0; i < fs->block_size; ++i) original_leaf[i] = leaf[i];
    uint32_t leaf_records = be16(&leaf[6]);
    uint32_t insert = leaf_records;
    for (uint32_t r = 0; r < leaf_records; ++r)
        if (relative < be32(&leaf[16U + r * 8U])) { insert = r; break; }
    uint32_t previous = insert == 0 ? UINT32_MAX : insert - 1U;
    uint32_t next = insert < leaf_records ? insert : UINT32_MAX;
    int joins_previous = previous != UINT32_MAX &&
        be32(&leaf[16U + previous * 8U]) + be32(&leaf[20U + previous * 8U]) == relative;
    int joins_next = next != UINT32_MAX &&
        relative + blocks == be32(&leaf[16U + next * 8U]);
    if (joins_previous && joins_next) {
        uint32_t left = be32(&leaf[20U + previous * 8U]);
        uint32_t right = be32(&leaf[20U + next * 8U]);
        if (left > UINT32_MAX - blocks || left + blocks > UINT32_MAX - right) return 0;
        store_be32(&leaf[20U + previous * 8U], left + blocks + right);
        for (uint32_t r = next; r + 1U < leaf_records; ++r)
            for (uint32_t b = 0; b < 8U; ++b)
                leaf[16U + r * 8U + b] = leaf[16U + (r + 1U) * 8U + b];
        --leaf_records;
    } else if (joins_previous) {
        uint32_t old = be32(&leaf[20U + previous * 8U]);
        if (old > UINT32_MAX - blocks) return 0;
        store_be32(&leaf[20U + previous * 8U], old + blocks);
    } else if (joins_next) {
        uint32_t old = be32(&leaf[20U + next * 8U]);
        if (old > UINT32_MAX - blocks) return 0;
        store_be32(&leaf[16U + next * 8U], relative);
        store_be32(&leaf[20U + next * 8U], old + blocks);
    } else {
        if (leaf_records >= (fs->block_size - 16U) / 8U) return 0;
        for (uint32_t r = leaf_records; r > insert; --r)
            for (uint32_t b = 0; b < 8U; ++b)
                leaf[16U + r * 8U + b] = leaf[16U + (r - 1U) * 8U + b];
        store_be32(&leaf[16U + insert * 8U], relative);
        store_be32(&leaf[20U + insert * 8U], blocks);
        ++leaf_records;
    }
    store_be16(&leaf[6], (uint16_t)leaf_records);
    uint32_t merged_next_child = 0, merged_next_records = 0;
    if (joins_previous && selected + 1U < root_records) {
        uint32_t next_index = selected + 1U;
        uint32_t next_child = be32(&root[pointer_offset + next_index * 4U]);
        if (next_child == 0 || next_child >= fs->ag_blocks ||
            !xfs_read_block(fs, ag_base + next_child, next_leaf) ||
            be32(next_leaf) != XFS_BNO_MAGIC_REAL || be16(&next_leaf[4]) != 0)
            return 0;
        for (uint32_t b = 0; b < fs->block_size; ++b)
            original_next[b] = next_leaf[b];
        uint32_t next_records = be16(&next_leaf[6]);
        if (next_records == 0 ||
            next_records > (fs->block_size - 16U) / 8U ||
            relative > UINT32_MAX - blocks ||
            be32(&next_leaf[16]) != relative + blocks) return 0;
        uint32_t left_count = be32(&leaf[20U + (leaf_records - 1U) * 8U]);
        uint32_t right_count = be32(&next_leaf[20]);
        if (left_count > UINT32_MAX - right_count) return 0;
        store_be32(&leaf[20U + (leaf_records - 1U) * 8U],
                   left_count + right_count);
        for (uint32_t r = 0; r + 1U < next_records; ++r)
            for (uint32_t b = 0; b < 8U; ++b)
                next_leaf[16U + r * 8U + b] =
                    next_leaf[16U + (r + 1U) * 8U + b];
        merged_next_records = next_records - 1U;
        merged_next_child = next_child;
        if (merged_next_records != 0) {
            store_be16(&next_leaf[6], (uint16_t)merged_next_records);
            for (uint32_t b = 0; b < 8U; ++b)
                root[16U + next_index * 8U + b] = next_leaf[16U + b];
        } else {
            for (uint32_t r = next_index; r + 1U < root_records; ++r) {
                for (uint32_t b = 0; b < 8U; ++b)
                    root[16U + r * 8U + b] = root[16U + (r + 1U) * 8U + b];
                for (uint32_t b = 0; b < 4U; ++b)
                    root[pointer_offset + r * 4U + b] =
                        root[pointer_offset + (r + 1U) * 4U + b];
            }
            --root_records;
            store_be16(&root[6], (uint16_t)root_records);
        }
    }
    for (uint32_t i = 0; i < root_records; ++i) {
        uint32_t child = be32(&root[pointer_offset + i * 4U]);
        if (child == selected_leaf) {
            if (leaf_records == 0) return 0;
            for (uint32_t b = 0; b < 8U; ++b)
                root[16U + i * 8U + b] = leaf[16U + b];
        }
    }
    uint32_t longest = 0;
    for (uint32_t i = 0; i < root_records; ++i) {
        uint32_t child = be32(&root[pointer_offset + i * 4U]);
        if (child == selected_leaf) {
            for (uint32_t r = 0; r < leaf_records; ++r)
                if (be32(&leaf[20U + r * 8U]) > longest)
                    longest = be32(&leaf[20U + r * 8U]);
        } else if (child == merged_next_child) {
            for (uint32_t r = 0; r < merged_next_records; ++r)
                if (be32(&next_leaf[20U + r * 8U]) > longest)
                    longest = be32(&next_leaf[20U + r * 8U]);
        } else {
            if (!xfs_read_block(fs, ag_base + child, scan)) return 0;
            uint32_t records = be16(&scan[6]);
            for (uint32_t r = 0; r < records; ++r)
                if (be32(&scan[20U + r * 8U]) > longest)
                    longest = be32(&scan[20U + r * 8U]);
        }
    }
    store_be32(&agf[40], be32(&agf[40]) + blocks);
    store_be32(&agf[44], longest);
    if (!xfs_write_block(fs, ag_base + selected_leaf, leaf) ||
        (merged_next_child != 0 && merged_next_records != 0 &&
         !xfs_write_block(fs, ag_base + merged_next_child, next_leaf)) ||
        !xfs_write_block(fs, ag_base + root_block, root) ||
        !xfs_write_block(fs, ag_base + 1U, agf)) {
        (void)xfs_write_block(fs, ag_base + selected_leaf, original_leaf);
        if (merged_next_child != 0 && merged_next_records != 0)
            (void)xfs_write_block(fs, ag_base + merged_next_child, original_next);
        (void)xfs_write_block(fs, ag_base + root_block, original_root);
        (void)xfs_write_block(fs, ag_base + 1U, original_agf);
        return 0;
    }
    return 1;
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

int xfs_set_mode(xfs_fs_t *fs, uint64_t inode, uint16_t mode) {
    uint8_t data[4096];
    if (!fs || !xfs_read_inode(fs, inode, data)) return 0;
    store_be16(&data[2], (uint16_t)((be16(&data[2]) & 0xf000U) | (mode & 0x0fffU)));
    return xfs_write_inode(fs, inode, data);
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

int xfs_add_local_entry(xfs_fs_t *fs, uint64_t directory_inode,
                        const char *name, uint64_t child_inode) {
    uint8_t data[4096];
    if (!fs || !name || !name[0] || child_inode == 0 ||
        !xfs_read_inode(fs, directory_inode, data) ||
        (be16(&data[2]) & 0xf000U) != 0x4000U || data[5] != XFS_FORMAT_LOCAL)
        return 0;
    uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
    uint64_t directory_size = be64(&data[56]);
    uint8_t wide = data[core + 1] != 0;
    uint32_t inode_bytes = wide ? 8U : 4U, length = 0;
    for (const char *p = name; *p; ++p) {
        if ((uint8_t)*p < 0x20U || (uint8_t)*p > 0x7eU || length == 255U) return 0;
        ++length;
    }
    uint64_t record_size = 3U + inode_bytes + length;
    if (directory_size < 2U + inode_bytes || directory_size > fs->inode_size - core ||
        record_size > fs->inode_size - core - directory_size ||
        data[core] == 0xffU || data[core] == 255U ||
        xfs_lookup(fs, directory_inode, name, &child_inode)) return 0;
    uint8_t *local = &data[core];
    uint32_t position = (uint32_t)directory_size;
    local[position] = (uint8_t)length; local[position + 1] = 0; local[position + 2] = 0;
    if (wide) store_be64(&local[position + 3], child_inode);
    else if (child_inode > UINT32_MAX) return 0;
    else store_be32(&local[position + 3], (uint32_t)child_inode);
    for (uint32_t i = 0; i < length; ++i) local[position + 3U + inode_bytes + i] = (uint8_t)name[i];
    local[0] = (uint8_t)(local[0] + 1U);
    store_be64(&data[56], directory_size + record_size);
    return xfs_write_inode(fs, directory_inode, data);
}

int xfs_remove_local_entry(xfs_fs_t *fs, uint64_t directory_inode,
                           const char *name) {
    uint8_t data[4096];
    if (!fs || !name || !name[0] || !xfs_read_inode(fs, directory_inode, data) ||
        (be16(&data[2]) & 0xf000U) != 0x4000U || data[5] != XFS_FORMAT_LOCAL)
        return 0;
    uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
    uint64_t directory_size = be64(&data[56]); uint8_t wide = data[core + 1] != 0;
    uint32_t inode_bytes = wide ? 8U : 4U, position = 2U + inode_bytes;
    uint32_t wanted_length = 0;
    for (const char *p = name; *p; ++p) {
        if (wanted_length == 255U) return 0;
        ++wanted_length;
    }
    for (uint32_t index = 0; index < data[core]; ++index) {
        if (position + 3U > directory_size) return 0;
        uint32_t length = data[core + position];
        uint32_t record = 3U + inode_bytes + length;
        if (position + record > directory_size) return 0;
        const uint8_t *entry_name = &data[core + position + 3U + inode_bytes];
        uint32_t matched = wanted_length == length;
        for (uint32_t i = 0; matched && i < length; ++i)
            if (entry_name[i] != (uint8_t)name[i]) matched = 0;
        if (matched) {
            uint32_t start = core + position, end = core + (uint32_t)directory_size;
            for (uint32_t i = start; i + record < end; ++i) data[i] = data[i + record];
            for (uint32_t i = end - record; i < end; ++i) data[i] = 0;
            data[core] = (uint8_t)(data[core] - 1U);
            store_be64(&data[56], directory_size - record);
            return xfs_write_inode(fs, directory_inode, data);
        }
        position += record;
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
    int metadata_changed = 0;
    if (grew) {
        uint64_t physical = 0, extent_length = 0;
        uint8_t unwritten = 0, found = 0;
        uint64_t last_logical = (end - 1U) / fs->block_size;
        for (uint32_t i = 0; i < extent_count; ++i)
            if (xfs_extent(&data[core + i * 16U], last_logical, &physical,
                           &extent_length, &unwritten)) { found = 1; break; }
        if (!found) return 0;
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
            metadata_changed = 1;
        }
        source += chunk; remaining -= chunk; ++logical; in_block = 0;
    }
    if (grew || metadata_changed) {
        if (grew) store_be64(&data[56], end);
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
    } else if (data[5] == XFS_FORMAT_EXTENTS) {
        uint32_t core = data[4] == 2 ? XFS_CORE_V2_SIZE : 100U;
        uint32_t extent_count = be32(&data[76]);
        if (extent_count == 0 || extent_count > (fs->inode_size - core) / 16U ||
            extent_count > 256U)
            return 0;
        typedef struct {
            uint64_t logical, physical, length;
            uint8_t unwritten;
        } truncate_extent_t;
        truncate_extent_t retained[256], detached[256];
        uint32_t retained_count = 0, detached_count = 0;
        uint64_t keep_blocks = size / fs->block_size +
                               (size % fs->block_size != 0);
        for (uint32_t i = 0; i < extent_count; ++i) {
            const uint8_t *record = &data[core + i * 16U];
            uint64_t high = be64(record), low = be64(record + 8U);
            uint64_t logical = (high & 0x7fffffffffffffffULL) >> 9;
            uint64_t physical = ((high & 0x1ffU) << 43) | (low >> 21);
            uint64_t length = low & 0x1fffffU;
            uint8_t unwritten = (uint8_t)((high >> 63) != 0);
            if (length == 0 || logical > UINT64_MAX - length ||
                physical >= fs->block_count || length > fs->block_count - physical)
                return 0;
            if (logical >= keep_blocks) {
                detached[detached_count++] =
                    (truncate_extent_t){logical, physical, length, unwritten};
            } else if (logical + length > keep_blocks) {
                uint64_t retained_length = keep_blocks - logical;
                detached[detached_count++] = (truncate_extent_t){
                    keep_blocks, physical + retained_length,
                    length - retained_length, unwritten};
                retained[retained_count++] =
                    (truncate_extent_t){logical, physical, retained_length, unwritten};
            } else {
                retained[retained_count++] =
                    (truncate_extent_t){logical, physical, length, unwritten};
            }
        }
        if (size % fs->block_size != 0) {
            uint64_t logical = size / fs->block_size;
            for (uint32_t i = 0; i < retained_count; ++i)
                if (retained[i].logical <= logical &&
                    logical - retained[i].logical < retained[i].length &&
                    !retained[i].unwritten) {
                    uint8_t block[4096];
                    uint64_t physical = retained[i].physical +
                                        (logical - retained[i].logical);
                    if (physical >= fs->block_count ||
                        !xfs_read_block(fs, physical, block)) return 0;
                    for (uint32_t byte = (uint32_t)(size % fs->block_size);
                         byte < fs->block_size; ++byte) block[byte] = 0;
                    if (!xfs_write_block(fs, physical, block)) return 0;
                    break;
                }
        }
        for (uint32_t i = 0; i < retained_count; ++i)
            xfs_store_extent(&data[core + i * 16U], retained[i].logical,
                             retained[i].physical, retained[i].length,
                             retained[i].unwritten);
        store_be32(&data[76], retained_count);
        store_be64(&data[56], size);
        if (!xfs_write_inode(fs, inode, data)) return 0;
        for (uint32_t i = 0; i < detached_count; ++i)
            if (!xfs_free_extent(fs, detached[i].physical,
                                 (uint32_t)detached[i].length)) return 0;
        return 1;
    }
    store_be64(&data[56], size);
    return xfs_write_inode(fs, inode, data);
}
