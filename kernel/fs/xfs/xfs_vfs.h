#ifndef OS_KERNEL_FS_XFS_VFS_H
#define OS_KERNEL_FS_XFS_VFS_H
#include "xfs.h"
int xfs_vfs_attach_file(xfs_fs_t *fs, vfs_node_t *root,
                        const char *filesystem_name, const char *name);
int xfs_vfs_attach_file_in_directory(xfs_fs_t *fs, vfs_node_t *root,
                                     uint64_t directory,
                                     const char *filesystem_name,
                                     const char *name);
int xfs_vfs_attach_directory(xfs_fs_t *fs, vfs_node_t *root,
                             const char *name);
int xfs_vfs_attach_directory_in_directory(xfs_fs_t *fs, vfs_node_t *root,
                                           uint64_t directory,
                                           const char *filesystem_name,
                                           const char *name);
#endif
