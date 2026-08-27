#include "ext4_vfs.h"
#include "../../mm/heap/heap.h"

typedef struct { ext4_fs_t *fs; uint32_t inode; uint64_t size; spinlock_t lock; } ext4_vfs_file_t;
static void ext4_vfs_destroy(void *private_data) { kfree(private_data); }
static int ext4_vfs_read(vfs_node_t *node, uint64_t offset,
                         void *buffer, uint32_t size) {
    ext4_vfs_file_t *file = node ? (ext4_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = ext4_read_file(file->fs, file->inode, offset, buffer, size) ?
                 (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}
static int ext4_vfs_write(vfs_node_t *node, uint64_t offset,
                          const void *buffer, uint32_t size) {
    ext4_vfs_file_t *file = node ? (ext4_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || (uint64_t)size > file->size - offset)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = ext4_write_file(file->fs, file->inode, offset, buffer, size) ?
                 (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}
static int ext4_vfs_truncate(vfs_node_t *node, uint32_t size) {
    ext4_vfs_file_t *file = node ? (ext4_vfs_file_t *)node->private_data : 0;
    if (!file || size == 0 || (uint64_t)size >= file->size) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = ext4_truncate_file(file->fs, file->inode, size);
    if (result) file->size = size;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}
int ext4_vfs_attach_file_in_directory(ext4_fs_t *fs, vfs_node_t *root,
                                      uint32_t directory,
                                      const char *filesystem_name,
                                      const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !filesystem_name || !name) return 0;
    uint32_t inode_number = 0;
    if (!ext4_lookup(fs, directory, filesystem_name, &inode_number)) return 0;
    ext4_vfs_file_t *file = (ext4_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->inode = inode_number;
    spinlock_init(&file->lock);
    uint32_t mode = 0;
    if (!ext4_inode_size(fs, inode_number, &file->size) ||
        !ext4_inode_mode(fs, inode_number, &mode) ||
        (mode & 0170000U) != 0100000U) { kfree(file); return 0; }
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0,
                                       mode & 0777U);
    if (!node || !vfs_node_set_read(node, ext4_vfs_read, file) ||
        !vfs_node_set_write(node, ext4_vfs_write, file) ||
        !vfs_node_set_truncate(node, ext4_vfs_truncate) ||
        !vfs_node_set_private_destructor(node, ext4_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int owns = node->private_destroy == ext4_vfs_destroy && node->private_data == file;
            vfs_node_release(node); if (!owns) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node); return 1;
}

int ext4_vfs_attach_file(ext4_fs_t *fs, vfs_node_t *root,
                         const char *filesystem_name, const char *name) {
    return ext4_vfs_attach_file_in_directory(fs, root, 2,
                                              filesystem_name, name);
}
