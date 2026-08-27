#ifndef OS_KERNEL_FS_EXFAT_VFS_H
#define OS_KERNEL_FS_EXFAT_VFS_H
#include "exfat.h"
#include "../vfs/vfs.h"
int exfat_vfs_attach_file(exfat_fs_t *fs, vfs_node_t *root,
                          const char *filesystem_name, const char *name);
#endif
