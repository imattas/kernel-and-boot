#include "xfs_vfs.h"
#include "../vfs/vfs.h"
#include "../../mm/heap/heap.h"

typedef struct { xfs_fs_t *fs; uint64_t inode; uint64_t size; spinlock_t lock; } xfs_vfs_file_t;
static void xfs_vfs_destroy(void *data) { kfree(data); }
static int xfs_vfs_read(vfs_node_t *node, uint64_t offset, void *buffer, uint32_t size) {
    xfs_vfs_file_t *file = node ? (xfs_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    return xfs_read_file(file->fs, file->inode, offset, buffer, size) ? (int)size : 0;
}
static int xfs_vfs_write(vfs_node_t *node, uint64_t offset,
                         const void *buffer, uint32_t size) {
    xfs_vfs_file_t *file = node ? (xfs_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || (uint64_t)size > file->size - offset)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = xfs_write_file(file->fs, file->inode, offset, buffer, size) ?
                 (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}
int xfs_vfs_attach_file(xfs_fs_t *fs, vfs_node_t *root,
                        const char *filesystem_name, const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !filesystem_name || !name || !name[0]) return 0;
    uint64_t inode = 0, size = 0;
    if (!xfs_lookup(fs, fs->root_inode, filesystem_name, &inode) ||
        !xfs_inode_size(fs, inode, &size)) return 0;
    xfs_vfs_file_t *file = (xfs_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->inode = inode; file->size = size;
    spinlock_init(&file->lock);
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0, 0644);
    if (!node || !vfs_node_set_read(node, xfs_vfs_read, file) ||
        !vfs_node_set_write(node, xfs_vfs_write, file) ||
        !vfs_node_set_private_destructor(node, xfs_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int owns = node->private_destroy == xfs_vfs_destroy && node->private_data == file;
            vfs_node_release(node); if (!owns) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node); return 1;
}
