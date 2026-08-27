#ifndef OS_KERNEL_FS_FAT12_VFS_H
#define OS_KERNEL_FS_FAT12_VFS_H

#include "fat12.h"
#include "../vfs/vfs.h"

int fat12_vfs_attach_file(fat12_fs_t *fs, vfs_node_t *root,
                          const char short_name[11], const char *name);

#endif
