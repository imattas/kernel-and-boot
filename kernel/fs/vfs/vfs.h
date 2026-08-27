#ifndef OS_KERNEL_FS_VFS_H
#define OS_KERNEL_FS_VFS_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"
#include "../../security/credentials.h"

typedef enum {
    VFS_NODE_DIRECTORY,
    VFS_NODE_REGULAR,
    VFS_NODE_DEVICE
} vfs_node_type_t;

typedef struct vfs_node vfs_node_t;
typedef int (*vfs_read_fn)(vfs_node_t *node, uint64_t offset,
                          void *buffer, uint32_t size);
typedef int (*vfs_write_fn)(vfs_node_t *node, uint64_t offset,
                           const void *buffer, uint32_t size);
typedef void (*vfs_private_destroy_fn)(void *private_data);

struct vfs_node {
    spinlock_t lock;
    struct vfs_node *parent;
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
    uint32_t child_count;
    uint32_t references;
    uint8_t destroying;
    uint64_t owner_uid;
    uint64_t owner_gid;
    uint32_t mode;
    vfs_node_type_t type;
    char name[32];
    vfs_read_fn read;
    vfs_write_fn write;
    void *private_data;
    vfs_private_destroy_fn private_destroy;
};

vfs_node_t *vfs_node_create(const char *name, vfs_node_type_t type,
                            uint64_t owner_uid, uint64_t owner_gid,
                            uint32_t mode);
int vfs_node_add_child(vfs_node_t *parent, vfs_node_t *child);
vfs_node_t *vfs_node_lookup(vfs_node_t *parent, const char *name);
vfs_node_t *vfs_node_child(vfs_node_t *parent, uint32_t index);
vfs_node_t *vfs_lookup_path(vfs_node_t *root, const char *path);
vfs_node_t *vfs_lookup_path_at(vfs_node_t *root, vfs_node_t *working,
                               const char *path);
int vfs_node_access(const vfs_node_t *node,
                    const security_context_t *context, uint32_t requested);
void vfs_node_retain(vfs_node_t *node);
void vfs_node_release(vfs_node_t *node);
int vfs_node_remove(vfs_node_t *parent, vfs_node_t *child);
int vfs_node_set_read(vfs_node_t *node, vfs_read_fn read, void *private_data);
int vfs_node_set_write(vfs_node_t *node, vfs_write_fn write, void *private_data);
int vfs_node_set_private_destructor(vfs_node_t *node,
                                     vfs_private_destroy_fn destroy);
int vfs_node_read(vfs_node_t *node, uint64_t offset, void *buffer, uint32_t size);
int vfs_node_write(vfs_node_t *node, uint64_t offset, const void *buffer,
                   uint32_t size);

#endif
