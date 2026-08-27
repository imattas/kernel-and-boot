#include "procfs.h"

static int pid_read(vfs_node_t *node, uint64_t offset, void *buffer,
                    uint32_t size) {
    if (!node || !buffer || offset >= 21 || size == 0) return 0;
    char text[21];
    uint64_t value = (uint64_t)(uintptr_t)node->private_data;
    uint32_t length = 0;
    do {
        text[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && length < sizeof(text) - 1);
    uint32_t total = length + 1;
    if (offset >= total) return 0;
    for (uint32_t i = 0; i < size && offset + i < total; ++i) {
        uint32_t position = (uint32_t)offset + i;
        ((char *)buffer)[i] = position < length ?
            text[length - position - 1] : '\n';
    }
    return size < total - offset ? (int)size : (int)(total - offset);
}

vfs_node_t *procfs_create(uint64_t process_id) {
    vfs_node_t *root = vfs_node_create("proc", VFS_NODE_DIRECTORY, 0, 0, 0555);
    vfs_node_t *self = vfs_node_create("self", VFS_NODE_DIRECTORY, 0, 0, 0555);
    vfs_node_t *pid = vfs_node_create("pid", VFS_NODE_REGULAR, 0, 0, 0444);
    if (!root || !self || !pid || !vfs_node_set_read(pid, pid_read,
                                                       (void *)(uintptr_t)process_id) ||
        !vfs_node_add_child(root, self) || !vfs_node_add_child(self, pid)) {
        if (pid) vfs_node_release(pid);
        if (self) vfs_node_release(self);
        if (root) vfs_node_release(root);
        return 0;
    }
    vfs_node_release(pid);
    vfs_node_release(self);
    return root;
}
