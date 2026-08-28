#include "xfs_vfs.h"
#include "../vfs/vfs.h"
#include "../../mm/heap/heap.h"

typedef struct { xfs_fs_t *fs; uint64_t inode; uint64_t size; spinlock_t lock; } xfs_vfs_file_t;
static void xfs_vfs_destroy(void *data) { kfree(data); }
static int xfs_vfs_dot_name(const char *name, const char *dot) {
    uint32_t i = 0;
    while (name && dot && name[i] && dot[i] && name[i] == dot[i]) ++i;
    return name && dot && name[i] == 0 && dot[i] == 0;
}
static int xfs_vfs_read(vfs_node_t *node, uint64_t offset, void *buffer, uint32_t size) {
    xfs_vfs_file_t *file = node ? (xfs_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = xfs_read_file(file->fs, file->inode, offset, buffer, size) ?
                 (int)size : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
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
static int xfs_vfs_truncate(vfs_node_t *node, uint32_t size) {
    xfs_vfs_file_t *file = node ? (xfs_vfs_file_t *)node->private_data : 0;
    if (!file || (uint64_t)size >= file->size) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    int result = xfs_truncate_file(file->fs, file->inode, size);
    if (result) file->size = size;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}
int xfs_vfs_attach_file_in_directory(xfs_fs_t *fs, vfs_node_t *root,
                                     uint64_t directory,
                                     const char *filesystem_name,
                                     const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !filesystem_name || !name || !name[0]) return 0;
    uint64_t inode = 0, size = 0; uint16_t mode = 0;
    if (!xfs_lookup(fs, directory, filesystem_name, &inode) ||
        !xfs_inode_size(fs, inode, &size) || !xfs_inode_mode(fs, inode, &mode)) return 0;
    xfs_vfs_file_t *file = (xfs_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->inode = inode; file->size = size;
    spinlock_init(&file->lock);
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0, mode & 0777U);
    if (!node || !vfs_node_set_read(node, xfs_vfs_read, file) ||
        !vfs_node_set_write(node, xfs_vfs_write, file) ||
        !vfs_node_set_truncate(node, xfs_vfs_truncate) ||
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

int xfs_vfs_attach_file(xfs_fs_t *fs, vfs_node_t *root,
                        const char *filesystem_name, const char *name) {
    return xfs_vfs_attach_file_in_directory(fs, root, fs ? fs->root_inode : 0,
                                             filesystem_name, name);
}

static int xfs_vfs_attach_directory_tree(xfs_fs_t *fs, vfs_node_t *parent,
                                          uint64_t directory, const char *name,
                                          uint32_t depth) {
    uint16_t mode = 0;
    if (!fs || !parent || parent->type != VFS_NODE_DIRECTORY || !name ||
        !name[0] || depth > 32U || !xfs_inode_mode(fs, directory, &mode) ||
        (mode & 0xf000U) != 0x4000U) return 0;
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_DIRECTORY, 0, 0,
                                       mode & 0777U);
    if (!node) return 0;
    uint32_t entry_count = 0;
    if (!xfs_directory_count(fs, directory, &entry_count) || entry_count > 256U) {
        vfs_node_release(node); return 0;
    }
    char entry_name[32]; uint64_t child = 0;
    for (uint32_t index = 0; index < entry_count; ++index) {
        if (!xfs_directory_entry(fs, directory, index, entry_name,
                                  sizeof(entry_name), &child)) {
            vfs_node_release(node); return 0;
        }
        if (xfs_vfs_dot_name(entry_name, ".") ||
            xfs_vfs_dot_name(entry_name, "..")) continue;
        uint16_t child_mode = 0;
        if (!xfs_inode_mode(fs, child, &child_mode)) {
            vfs_node_release(node); return 0;
        }
        if ((child_mode & 0xf000U) == 0x4000U) {
            if (!xfs_vfs_attach_directory_tree(fs, node, child, entry_name,
                                               depth + 1U)) {
                vfs_node_release(node); return 0;
            }
        } else if ((child_mode & 0xf000U) == 0x8000U) {
            if (!xfs_vfs_attach_file_in_directory(fs, node, directory,
                                                   entry_name, entry_name)) {
                vfs_node_release(node); return 0;
            }
        } else {
            vfs_node_release(node); return 0;
        }
    }
    if (!vfs_node_add_child(parent, node)) {
        vfs_node_release(node); return 0;
    }
    vfs_node_release(node);
    return 1;
}

int xfs_vfs_attach_directory_in_directory(xfs_fs_t *fs, vfs_node_t *root,
                                           uint64_t directory,
                                           const char *filesystem_name,
                                           const char *name) {
    uint64_t inode = 0;
    if (!fs || !root || !filesystem_name || !filesystem_name[0] ||
        !name || !name[0] || !xfs_lookup(fs, directory, filesystem_name, &inode))
        return 0;
    return xfs_vfs_attach_directory_tree(fs, root, inode, name, 0);
}

int xfs_vfs_attach_directory(xfs_fs_t *fs, vfs_node_t *root,
                             const char *name) {
    return xfs_vfs_attach_directory_in_directory(fs, root,
                                                 fs ? fs->root_inode : 0,
                                                 name, name);
}
