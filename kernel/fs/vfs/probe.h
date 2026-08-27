#ifndef OS_KERNEL_FS_VFS_PROBE_H
#define OS_KERNEL_FS_VFS_PROBE_H

#include <stdint.h>

typedef enum {
    VFS_FILESYSTEM_NONE = 0,
    VFS_FILESYSTEM_FAT32,
    VFS_FILESYSTEM_EXFAT,
    VFS_FILESYSTEM_EXT4,
    VFS_FILESYSTEM_XFS,
    VFS_FILESYSTEM_BTRFS
} vfs_filesystem_type_t;

int vfs_probe_filesystem(uint32_t device, vfs_filesystem_type_t *type);
const char *vfs_filesystem_name(vfs_filesystem_type_t type);

#endif
