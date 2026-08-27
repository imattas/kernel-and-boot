#include "btrfs_vfs.h"
#include "../../mm/heap/heap.h"

typedef struct { btrfs_fs_t *fs; uint64_t tree; uint64_t inode; uint64_t size; spinlock_t lock; } btrfs_vfs_file_t;
static void btrfs_vfs_destroy(void *data) { kfree(data); }
static int btrfs_vfs_read(vfs_node_t *node, uint64_t offset, void *buffer, uint32_t size) {
    btrfs_vfs_file_t *file = node ? (btrfs_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = btrfs_read_file(file->fs, file->tree, file->inode, offset,
                                 buffer, size) ? (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}
static int btrfs_vfs_write(vfs_node_t *node, uint64_t offset,
                           const void *buffer, uint32_t size) {
    btrfs_vfs_file_t *file = node ? (btrfs_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || (uint64_t)size > file->size - offset)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = btrfs_write_file(file->fs, file->tree, file->inode, offset,
                                  buffer, size) ? (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

int btrfs_vfs_attach_file_in_directory(btrfs_fs_t *fs, vfs_node_t *root,
                                       uint64_t directory,
                                       const char *filesystem_name,
                                       const char *name) {
    if (!fs || !fs->mounted || !fs->fs_root_bytenr || !root ||
        root->type != VFS_NODE_DIRECTORY || !filesystem_name || !name || !name[0]) return 0;
    uint64_t inode = 0, size = 0; uint32_t mode = 0;
    if (!btrfs_lookup_dir(fs, fs->fs_root_bytenr, directory, filesystem_name, &inode) ||
        !btrfs_inode_stat(fs, fs->fs_root_bytenr, inode, &size, &mode) ||
        (mode & 0170000U) != 0100000U) return 0;
    btrfs_vfs_file_t *file = (btrfs_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->tree = fs->fs_root_bytenr; file->inode = inode; file->size = size;
    spinlock_init(&file->lock);
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0,
                                       mode & 0777U);
    if (!node || !vfs_node_set_read(node, btrfs_vfs_read, file) ||
        !vfs_node_set_write(node, btrfs_vfs_write, file) ||
        !vfs_node_set_private_destructor(node, btrfs_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int owns = node->private_destroy == btrfs_vfs_destroy && node->private_data == file;
            vfs_node_release(node); if (!owns) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node); return 1;
}

int btrfs_vfs_attach_file(btrfs_fs_t *fs, vfs_node_t *root,
                          const char *filesystem_name, const char *name) {
    return btrfs_vfs_attach_file_in_directory(fs, root, 256,
                                               filesystem_name, name);
}
