#ifndef OS_KERNEL_FS_XFS_H
#define OS_KERNEL_FS_XFS_H
#include <stdint.h>
#include "../vfs/vfs.h"

typedef struct {
    spinlock_t lock;
    uint32_t device;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t ag_count;
    uint32_t ag_blocks;
    uint8_t ag_block_log;
    uint8_t inode_per_block_log;
    uint64_t block_count;
    uint64_t root_inode;
    uint64_t journal_start;
    uint32_t journal_blocks;
    uint64_t journal_sequence;
    uint8_t mounted;
} xfs_fs_t;

int xfs_mount(xfs_fs_t *fs, uint32_t device);
int xfs_inode_size(xfs_fs_t *fs, uint64_t inode, uint64_t *size);
int xfs_inode_mode(xfs_fs_t *fs, uint64_t inode, uint16_t *mode);
int xfs_set_mode(xfs_fs_t *fs, uint64_t inode, uint16_t mode);
int xfs_allocate_extent(xfs_fs_t *fs, uint32_t allocation_group,
                        uint32_t blocks, uint64_t *start);
int xfs_free_extent(xfs_fs_t *fs, uint64_t start, uint32_t blocks);
int xfs_lookup(xfs_fs_t *fs, uint64_t directory_inode, const char *name,
               uint64_t *inode);
int xfs_directory_entry(xfs_fs_t *fs, uint64_t directory_inode,
                        uint32_t index, char *name, uint32_t name_size,
                        uint64_t *inode);
int xfs_add_local_entry(xfs_fs_t *fs, uint64_t directory_inode,
                        const char *name, uint64_t child_inode);
int xfs_remove_local_entry(xfs_fs_t *fs, uint64_t directory_inode,
                           const char *name);
int xfs_rename_local_entry(xfs_fs_t *fs, uint64_t directory_inode,
                           const char *old_name, const char *new_name);
int xfs_rename_local_entry_between(xfs_fs_t *fs, uint64_t source_directory,
                                   const char *old_name, uint64_t target_directory,
                                   const char *new_name);
int xfs_read_file(xfs_fs_t *fs, uint64_t inode, uint64_t offset,
                  void *buffer, uint32_t size);
int xfs_write_file(xfs_fs_t *fs, uint64_t inode, uint64_t offset,
                   const void *buffer, uint32_t size);
int xfs_truncate_file(xfs_fs_t *fs, uint64_t inode, uint64_t size);

#endif
