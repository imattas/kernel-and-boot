#include "fat32_vfs.h"
#include "../../mm/heap/heap.h"

typedef struct {
    fat32_fs_t *fs;
    uint32_t directory_cluster;
    char short_name[11];
    uint32_t size;
    spinlock_t lock;
} fat32_vfs_file_t;

static void fat32_vfs_destroy(void *private_data) { kfree(private_data); }

static int fat32_vfs_read(vfs_node_t *node, uint64_t offset,
                          void *buffer, uint32_t size) {
    fat32_vfs_file_t *file = node ? (fat32_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = fat32_read_file_in_directory(file->fs, file->directory_cluster,
                                               file->short_name, (uint32_t)offset,
                                               buffer, size) ? (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

static int fat32_vfs_write(vfs_node_t *node, uint64_t offset,
                           const void *buffer, uint32_t size) {
    fat32_vfs_file_t *file = node ? (fat32_vfs_file_t *)node->private_data : 0;
    if (!file || offset > UINT32_MAX || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = 0;
    if (offset <= file->size && size <= file->size - offset)
        result = fat32_write_file_in_directory(file->fs,
                                               file->directory_cluster,
                                               file->short_name, (uint32_t)offset,
                                               buffer, size) ? (int)size : 0;
    else if (file->directory_cluster == file->fs->root_cluster &&
             offset == file->size &&
             fat32_append_file(file->fs, file->short_name, buffer, size)) {
        file->size += size;
        result = (int)size;
    }
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

static int fat32_vfs_truncate(vfs_node_t *node, uint32_t size) {
    fat32_vfs_file_t *file = node ? (fat32_vfs_file_t *)node->private_data : 0;
    if (!file || file->directory_cluster != file->fs->root_cluster) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = size <= file->size && fat32_truncate_file(file->fs,
                                                           file->short_name,
                                                           size);
    if (result) file->size = size;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

int fat32_vfs_attach_file_in_directory(fat32_fs_t *fs, vfs_node_t *root,
                                        uint32_t directory_cluster,
                                        const char short_name[11],
                                        const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !short_name || !name) return 0;
    uint32_t cluster, size;
    uint8_t is_directory = 0;
    if (!fat32_lookup_in_directory(fs, directory_cluster, short_name, &cluster,
                                   &size, &is_directory) || is_directory) return 0;
    fat32_vfs_file_t *file = (fat32_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->directory_cluster = directory_cluster;
    file->size = size; spinlock_init(&file->lock);
    for (uint32_t i = 0; i < 11; ++i) file->short_name[i] = short_name[i];
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0, 0644);
    if (!node || !vfs_node_set_read(node, fat32_vfs_read, file) ||
        !vfs_node_set_write(node, fat32_vfs_write, file) ||
        !vfs_node_set_truncate(node, fat32_vfs_truncate) ||
        !vfs_node_set_private_destructor(node, fat32_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int destructor_owns_file = node->private_destroy == fat32_vfs_destroy &&
                                       node->private_data == file;
            vfs_node_release(node);
            if (!destructor_owns_file) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node);
    return 1;
}

int fat32_vfs_attach_file(fat32_fs_t *fs, vfs_node_t *root,
                          const char short_name[11], const char *name) {
    return fat32_vfs_attach_file_in_directory(fs, root,
                                               fs ? fs->root_cluster : 0,
                                               short_name, name);
}
