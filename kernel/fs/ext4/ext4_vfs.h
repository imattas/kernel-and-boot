#ifndef OS_KERNEL_FS_EXT4_VFS_H
#define OS_KERNEL_FS_EXT4_VFS_H
#include "ext4.h"
#include "../vfs/vfs.h"
int ext4_vfs_attach_file(ext4_fs_t *fs, vfs_node_t *root,
                         const char *filesystem_name, const char *name);
#endif
