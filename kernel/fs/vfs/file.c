#include "file.h"
#include "../../mm/heap/heap.h"

struct vfs_file {
    spinlock_t lock;
    vfs_node_t *node;
    uint64_t offset;
    uint32_t flags;
    uint32_t references;
};

vfs_file_t *vfs_file_open(vfs_node_t *node, uint32_t flags) {
    if (!node || flags == 0 || (flags & ~(VFS_FILE_READ | VFS_FILE_WRITE)) != 0 ||
        (node->type == VFS_NODE_DIRECTORY && flags != VFS_FILE_READ))
        return 0;
    vfs_file_t *file = (vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    spinlock_init(&file->lock);
    file->node = node;
    file->offset = 0;
    file->flags = flags;
    file->references = 1;
    vfs_node_retain(node);
    return file;
}

void vfs_file_retain(vfs_file_t *file) {
    if (!file) return;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    if (file->references != UINT32_MAX) ++file->references;
    spinlock_unlock_irqrestore(&file->lock, flags);
}

void vfs_file_release(vfs_file_t *file) {
    if (!file) return;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    if (file->references == 0) {
        spinlock_unlock_irqrestore(&file->lock, flags);
        return;
    }
    int destroy = --file->references == 0;
    vfs_node_t *node = destroy ? file->node : 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    if (destroy) {
        vfs_node_release(node);
        kfree(file);
    }
}

static void release_handle(void *object) {
    vfs_file_release((vfs_file_t *)object);
}

int vfs_file_open_handle(process_handle_table_t *table, vfs_node_t *node,
                         uint32_t flags) {
    vfs_file_t *file = vfs_file_open(node, flags);
    if (!file) return 0;
    uint32_t rights = 0;
    if (flags & VFS_FILE_READ) rights |= PROCESS_HANDLE_READ;
    if (flags & VFS_FILE_WRITE) rights |= PROCESS_HANDLE_WRITE;
    int handle = process_handle_open_owned(table, file, rights, release_handle);
    if (!handle) vfs_file_release(file);
    return handle;
}

int vfs_file_open_path_handle(process_handle_table_t *table, vfs_node_t *root,
                              const char *path, uint32_t flags) {
    if (!table || !root || !path) return 0;
    vfs_node_t *node = vfs_lookup_path(root, path);
    if (!node) return 0;
    int handle = vfs_file_open_handle(table, node, flags);
    vfs_node_release(node);
    return handle;
}

int vfs_file_read(vfs_file_t *file, void *buffer, uint32_t size) {
    if (!file || !buffer || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    if (!(file->flags & VFS_FILE_READ) || file->offset > UINT64_MAX - size) {
        spinlock_unlock_irqrestore(&file->lock, flags);
        return 0;
    }
    int result = vfs_node_read(file->node, file->offset, buffer, size);
    if (result > 0 && (uint32_t)result <= size) file->offset += (uint32_t)result;
    else if (result < 0 || (uint32_t)result > size) result = 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

int vfs_file_write(vfs_file_t *file, const void *buffer, uint32_t size) {
    if (!file || !buffer || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    if (!(file->flags & VFS_FILE_WRITE) || file->offset > UINT64_MAX - size) {
        spinlock_unlock_irqrestore(&file->lock, flags);
        return 0;
    }
    int result = vfs_node_write(file->node, file->offset, buffer, size);
    if (result > 0 && (uint32_t)result <= size) file->offset += (uint32_t)result;
    else if (result < 0 || (uint32_t)result > size) result = 0;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return result;
}

int vfs_file_readdir(vfs_file_t *file, vfs_dirent_t *entry) {
    if (!file || !entry) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    if (!(file->flags & VFS_FILE_READ) || !file->node ||
        file->node->type != VFS_NODE_DIRECTORY || file->offset > UINT32_MAX) {
        spinlock_unlock_irqrestore(&file->lock, flags);
        return 0;
    }
    vfs_node_t *child = vfs_node_child(file->node, (uint32_t)file->offset);
    if (!child) {
        spinlock_unlock_irqrestore(&file->lock, flags);
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(entry->name); ++i) entry->name[i] = child->name[i];
    entry->type = child->type;
    ++file->offset;
    vfs_node_release(child);
    spinlock_unlock_irqrestore(&file->lock, flags);
    return 1;
}

int vfs_file_stat(vfs_file_t *file, vfs_stat_t *stat) {
    if (!file || !stat || !file->node) return 0;
    vfs_node_t *node = file->node;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    stat->owner_uid = node->owner_uid;
    stat->owner_gid = node->owner_gid;
    stat->mode = node->mode;
    stat->type = node->type;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return 1;
}

int vfs_file_seek(vfs_file_t *file, uint64_t offset) {
    if (!file) return 0;
    uint64_t flags = spinlock_lock_irqsave(&file->lock);
    file->offset = offset;
    spinlock_unlock_irqrestore(&file->lock, flags);
    return 1;
}

uint64_t vfs_file_offset(const vfs_file_t *file) {
    if (!file) return 0;
    vfs_file_t *mutable_file = (vfs_file_t *)file;
    uint64_t flags = spinlock_lock_irqsave(&mutable_file->lock);
    uint64_t offset = file->offset;
    spinlock_unlock_irqrestore(&mutable_file->lock, flags);
    return offset;
}
