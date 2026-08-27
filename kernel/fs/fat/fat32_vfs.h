#ifndef OS_KERNEL_FS_FAT32_VFS_H
#define OS_KERNEL_FS_FAT32_VFS_H

#include "fat32.h"
#include "../vfs/vfs.h"

int fat32_vfs_attach_file(fat32_fs_t *fs, vfs_node_t *root,
                          const char short_name[11], const char *name);
int fat32_vfs_attach_file_in_directory(fat32_fs_t *fs, vfs_node_t *root,
                                        uint32_t directory_cluster,
                                        const char short_name[11],
                                        const char *name);

#endif
