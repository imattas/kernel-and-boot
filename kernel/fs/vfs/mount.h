#ifndef OS_KERNEL_FS_VFS_MOUNT_H
#define OS_KERNEL_FS_VFS_MOUNT_H

#include <stdint.h>
#include "vfs.h"

#define VFS_MAX_MOUNTS 16U

typedef struct {
    vfs_node_t *mountpoint;
    vfs_node_t *root;
    uint8_t active;
} vfs_mount_t;

typedef struct {
    spinlock_t lock;
    vfs_mount_t mounts[VFS_MAX_MOUNTS];
} vfs_mount_table_t;

void vfs_mount_table_initialize(vfs_mount_table_t *table);
int vfs_mount(vfs_mount_table_t *table, vfs_node_t *mountpoint,
              vfs_node_t *root);
int vfs_unmount(vfs_mount_table_t *table, vfs_node_t *mountpoint);
vfs_node_t *vfs_lookup_path_mounted(vfs_mount_table_t *table,
                                    vfs_node_t *root, const char *path);

#endif
