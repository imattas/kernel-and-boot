#include "exfat_vfs.h"
#include "../../mm/heap/heap.h"

typedef struct { exfat_fs_t *fs; uint32_t directory_cluster; char name[32]; uint64_t size; spinlock_t lock; } exfat_vfs_file_t;
static void exfat_vfs_destroy(void *private_data) { kfree(private_data); }

static int exfat_vfs_read(vfs_node_t *node, uint64_t offset,
                          void *buffer, uint32_t size) {
    exfat_vfs_file_t *file = node ? (exfat_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = exfat_read_file_in_directory(file->fs, file->directory_cluster,
                                               file->name, offset, buffer, size) ?
                 (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

static int exfat_vfs_write(vfs_node_t *node, uint64_t offset,
                           const void *buffer, uint32_t size) {
    exfat_vfs_file_t *file = node ? (exfat_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || (uint64_t)size > file->size - offset)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = exfat_write_file_in_directory(file->fs,
                                               file->directory_cluster,
                                               file->name, offset, buffer, size) ?
                 (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

static int exfat_vfs_truncate(vfs_node_t *node, uint32_t size) {
    exfat_vfs_file_t *file = node ? (exfat_vfs_file_t *)node->private_data : 0;
    if (!file || size == 0 || (uint64_t)size >= file->size) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = exfat_truncate_file_in_directory(file->fs,
                                                  file->directory_cluster,
                                                  file->name, size);
    if (result) file->size = size;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

int exfat_vfs_attach_file_in_directory(exfat_fs_t *fs, vfs_node_t *root,
                                        uint32_t directory_cluster,
                                        const char *filesystem_name,
                                        const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !filesystem_name || !name) return 0;
    uint32_t cluster = 0; uint64_t size = 0; uint8_t no_fat_chain = 0;
    if (!exfat_lookup(fs, filesystem_name, &cluster, &size, &no_fat_chain) ||
        size > UINT64_MAX) return 0;
    exfat_vfs_file_t *file = (exfat_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->directory_cluster = directory_cluster;
    file->size = size; spinlock_init(&file->lock);
    uint32_t i = 0;
    for (; i + 1 < sizeof(file->name) && filesystem_name[i]; ++i)
        file->name[i] = filesystem_name[i];
    if (filesystem_name[i] || name[0] == 0) { kfree(file); return 0; }
    file->name[i] = 0;
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0, 0644);
    if (!node || !vfs_node_set_read(node, exfat_vfs_read, file) ||
        !vfs_node_set_write(node, exfat_vfs_write, file) ||
        !vfs_node_set_truncate(node, exfat_vfs_truncate) ||
        !vfs_node_set_private_destructor(node, exfat_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int owns = node->private_destroy == exfat_vfs_destroy && node->private_data == file;
            vfs_node_release(node);
            if (!owns) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node); return 1;
}

int exfat_vfs_attach_file(exfat_fs_t *fs, vfs_node_t *root,
                          const char *filesystem_name, const char *name) {
    return exfat_vfs_attach_file_in_directory(fs, root,
                                               fs ? fs->root_cluster : 0,
                                               filesystem_name, name);
}
