#ifndef OS_KERNEL_FS_XFS_H
#define OS_KERNEL_FS_XFS_H
#include <stdint.h>
#include "../vfs/vfs.h"

typedef struct {
    uint32_t device;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t ag_count;
    uint32_t ag_blocks;
    uint8_t ag_block_log;
    uint8_t inode_per_block_log;
    uint64_t block_count;
    uint64_t root_inode;
    uint8_t mounted;
} xfs_fs_t;

int xfs_mount(xfs_fs_t *fs, uint32_t device);
int xfs_inode_size(xfs_fs_t *fs, uint64_t inode, uint64_t *size);
int xfs_lookup(xfs_fs_t *fs, uint64_t directory_inode, const char *name,
               uint64_t *inode);
int xfs_read_file(xfs_fs_t *fs, uint64_t inode, uint64_t offset,
                  void *buffer, uint32_t size);
int xfs_write_file(xfs_fs_t *fs, uint64_t inode, uint64_t offset,
                   const void *buffer, uint32_t size);

#endif
