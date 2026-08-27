#include "mount.h"

static int string_equal(const char *left, const char *right) {
    uint32_t index = 0;
    if (!left || !right) return 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

void vfs_mount_table_initialize(vfs_mount_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        table->mounts[i].mountpoint = 0;
        table->mounts[i].root = 0;
        table->mounts[i].active = 0;
    }
}

int vfs_mount(vfs_mount_table_t *table, vfs_node_t *mountpoint,
              vfs_node_t *root) {
    if (!table || !mountpoint || !root ||
        mountpoint->type != VFS_NODE_DIRECTORY || root->type != VFS_NODE_DIRECTORY)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    uint32_t free_slot = VFS_MAX_MOUNTS;
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (table->mounts[i].active &&
            table->mounts[i].mountpoint == mountpoint) {
            spinlock_unlock_irqrestore(&table->lock, flags);
            return 0;
        }
        if (!table->mounts[i].active && free_slot == VFS_MAX_MOUNTS)
            free_slot = i;
    }
    if (free_slot == VFS_MAX_MOUNTS) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    vfs_node_retain(mountpoint);
    vfs_node_retain(root);
    table->mounts[free_slot].mountpoint = mountpoint;
    table->mounts[free_slot].root = root;
    table->mounts[free_slot].active = 1;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}

int vfs_unmount(vfs_mount_table_t *table, vfs_node_t *mountpoint) {
    if (!table || !mountpoint) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!table->mounts[i].active ||
            table->mounts[i].mountpoint != mountpoint) continue;
        vfs_node_t *mounted_root = table->mounts[i].root;
        vfs_node_t *mounted_at = table->mounts[i].mountpoint;
        table->mounts[i].active = 0;
        table->mounts[i].mountpoint = 0;
        table->mounts[i].root = 0;
        spinlock_unlock_irqrestore(&table->lock, flags);
        vfs_node_release(mounted_root);
        vfs_node_release(mounted_at);
        return 1;
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 0;
}

static vfs_node_t *mounted_root(vfs_mount_table_t *table, vfs_node_t *node) {
    if (!table || !node) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    vfs_node_t *result = 0;
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (table->mounts[i].active && table->mounts[i].mountpoint == node) {
            result = table->mounts[i].root;
            vfs_node_retain(result);
            break;
        }
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return result;
}

vfs_node_t *vfs_lookup_path_mounted(vfs_mount_table_t *table,
                                    vfs_node_t *root, const char *path) {
    if (!table || !root || !path || path[0] != '/') return 0;
    vfs_node_t *current = root;
    vfs_node_retain(current);
    uint32_t index = 1;
    while (path[index] != '\0') {
        while (path[index] == '/') ++index;
        if (path[index] == '\0') break;
        char component[32];
        uint32_t length = 0;
        while (path[index] != '\0' && path[index] != '/') {
            if (length + 1 >= sizeof(component)) {
                vfs_node_release(current);
                return 0;
            }
            component[length++] = path[index++];
        }
        component[length] = '\0';
        vfs_node_t *next;
        if (string_equal(component, ".")) {
            next = current;
            vfs_node_retain(next);
        } else if (string_equal(component, "..")) {
            next = current->parent ? current->parent : current;
            vfs_node_retain(next);
        } else {
            next = vfs_node_lookup(current, component);
        }
        vfs_node_release(current);
        if (!next) return 0;
        vfs_node_t *mounted = mounted_root(table, next);
        if (mounted) {
            vfs_node_release(next);
            next = mounted;
        }
        current = next;
    }
    vfs_node_t *mounted = mounted_root(table, current);
    if (mounted) {
        vfs_node_release(current);
        return mounted;
    }
    return current;
}
