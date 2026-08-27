#ifndef OS_KERNEL_FS_BTRFS_VFS_H
#define OS_KERNEL_FS_BTRFS_VFS_H
#include "btrfs.h"
#include "../vfs/vfs.h"
int btrfs_vfs_attach_file(btrfs_fs_t *fs, vfs_node_t *root,
                          const char *filesystem_name, const char *name);
#endif
