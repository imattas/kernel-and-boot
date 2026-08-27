#ifndef OS_KERNEL_FS_VFS_FILE_H
#define OS_KERNEL_FS_VFS_FILE_H

#include <stdint.h>
#include "vfs.h"
#include "../../core/process/handle.h"

#define VFS_FILE_READ  0x01U
#define VFS_FILE_WRITE 0x02U

typedef struct vfs_file vfs_file_t;

vfs_file_t *vfs_file_open(vfs_node_t *node, uint32_t flags);
int vfs_file_open_handle(process_handle_table_t *table, vfs_node_t *node,
                         uint32_t flags);
void vfs_file_retain(vfs_file_t *file);
void vfs_file_release(vfs_file_t *file);
int vfs_file_read(vfs_file_t *file, void *buffer, uint32_t size);
int vfs_file_write(vfs_file_t *file, const void *buffer, uint32_t size);
int vfs_file_seek(vfs_file_t *file, uint64_t offset);
uint64_t vfs_file_offset(const vfs_file_t *file);

#endif
