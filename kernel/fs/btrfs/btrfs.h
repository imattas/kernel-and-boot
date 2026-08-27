#ifndef OS_KERNEL_FS_BTRFS_H
#define OS_KERNEL_FS_BTRFS_H
#include <stdint.h>
#define BTRFS_MAX_SYSTEM_CHUNKS 64U

typedef struct {
    uint64_t logical;
    uint64_t length;
    uint64_t physical;
    uint64_t mirror_physical;
} btrfs_chunk_t;

typedef struct {
    uint32_t device;
    uint32_t sector_size;
    uint32_t node_size;
    uint64_t total_bytes;
    uint64_t root_bytenr;
    uint64_t chunk_root_bytenr;
    uint64_t fs_root_bytenr;
    uint64_t csum_root_bytenr;
    uint8_t fsid[16];
    btrfs_chunk_t chunks[BTRFS_MAX_SYSTEM_CHUNKS];
    uint32_t chunk_count;
    uint8_t mounted;
} btrfs_fs_t;

int btrfs_mount(btrfs_fs_t *fs, uint32_t device);
int btrfs_resolve_filesystem_tree(btrfs_fs_t *fs);
int btrfs_inode_stat(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                     uint64_t *size, uint32_t *mode);
int btrfs_lookup_dir(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t directory,
                     const char *name, uint64_t *inode);
int btrfs_read_item(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t objectid,
                    uint8_t type, uint64_t offset, void *buffer, uint32_t size);
int btrfs_read_extent_data(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                           uint64_t extent_offset, uint64_t file_offset,
                           void *buffer, uint32_t size);
int btrfs_read_file(btrfs_fs_t *fs, uint64_t tree_bytenr, uint64_t inode,
                    uint64_t offset, void *buffer, uint32_t size);

#endif
