#include "probe.h"
#include "../fat/fat32.h"
#include "../exfat/exfat.h"
#include "../ext4/ext4.h"
#include "../xfs/xfs.h"
#include "../btrfs/btrfs.h"

int vfs_probe_filesystem(uint32_t device, vfs_filesystem_type_t *type) {
    if (!type) return 0;
    *type = VFS_FILESYSTEM_NONE;
    fat32_fs_t fat32 = {0};
    if (fat32_mount(&fat32, device)) {
        *type = VFS_FILESYSTEM_FAT32;
        return 1;
    }
    exfat_fs_t exfat = {0};
    if (exfat_mount(&exfat, device)) {
        *type = VFS_FILESYSTEM_EXFAT;
        return 1;
    }
    ext4_fs_t ext4 = {0};
    if (ext4_mount(&ext4, device)) {
        *type = VFS_FILESYSTEM_EXT4;
        return 1;
    }
    xfs_fs_t xfs = {0};
    if (xfs_mount(&xfs, device)) {
        *type = VFS_FILESYSTEM_XFS;
        return 1;
    }
    btrfs_fs_t btrfs = {0};
    if (btrfs_mount(&btrfs, device)) {
        *type = VFS_FILESYSTEM_BTRFS;
        return 1;
    }
    return 0;
}

const char *vfs_filesystem_name(vfs_filesystem_type_t type) {
    switch (type) {
        case VFS_FILESYSTEM_FAT32: return "FAT32";
        case VFS_FILESYSTEM_EXFAT: return "exFAT";
        case VFS_FILESYSTEM_EXT4: return "ext4";
        case VFS_FILESYSTEM_XFS: return "XFS";
        case VFS_FILESYSTEM_BTRFS: return "Btrfs";
        default: return "none";
    }
}
