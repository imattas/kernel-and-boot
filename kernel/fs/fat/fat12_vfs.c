#include "fat12_vfs.h"
#include "../../mm/heap/heap.h"

typedef struct {
    fat12_fs_t *fs;
    char short_name[11];
    uint32_t size;
} fat12_vfs_file_t;

static void fat12_vfs_destroy(void *private_data) { kfree(private_data); }

static int fat12_vfs_read(vfs_node_t *node, uint64_t offset,
                          void *buffer, uint32_t size) {
    fat12_vfs_file_t *file = node ? (fat12_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    return fat12_read_file(file->fs, file->short_name, (uint32_t)offset,
                           buffer, size) ? (int)size : 0;
}

int fat12_vfs_attach_file(fat12_fs_t *fs, vfs_node_t *root,
                          const char short_name[11], const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !short_name || !name) return 0;
    uint16_t cluster;
    uint32_t size;
    if (!fat12_lookup(fs, short_name, &cluster, &size)) return 0;
    fat12_vfs_file_t *file = (fat12_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs;
    file->size = size;
    for (uint32_t i = 0; i < 11; ++i) file->short_name[i] = short_name[i];
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0, 0444);
    if (!node || !vfs_node_set_read(node, fat12_vfs_read, file) ||
        !vfs_node_set_private_destructor(node, fat12_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int destructor_owns_file = node->private_destroy == fat12_vfs_destroy &&
                                       node->private_data == file;
            vfs_node_release(node);
            if (!destructor_owns_file) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node);
    return 1;
}
