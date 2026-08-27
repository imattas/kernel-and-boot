#include "vfs.h"
#include "../../mm/heap/heap.h"

static uint32_t string_length(const char *value) {
    uint32_t length = 0;
    while (value && value[length] != '\0') ++length;
    return length;
}

static int string_equal(const char *left, const char *right) {
    uint32_t i = 0;
    if (!left || !right) return 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return 0;
        ++i;
    }
    return left[i] == right[i];
}

vfs_node_t *vfs_node_create(const char *name, vfs_node_type_t type,
                            uint64_t owner_uid, uint64_t owner_gid,
                            uint32_t mode) {
    uint32_t length = string_length(name);
    if (!name || length == 0 || length >= sizeof(((vfs_node_t *)0)->name) ||
        type > VFS_NODE_DEVICE || (mode & ~0777U) != 0) return 0;
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(*node));
    if (!node) return 0;
    spinlock_init(&node->lock);
    node->parent = 0;
    node->first_child = 0;
    node->next_sibling = 0;
    node->child_count = 0;
    node->references = 1;
    node->destroying = 0;
    node->owner_uid = owner_uid;
    node->owner_gid = owner_gid;
    node->mode = mode;
    node->type = type;
    node->read = 0;
    node->write = 0;
    node->private_data = 0;
    node->private_destroy = 0;
    for (uint32_t i = 0; i <= length; ++i) node->name[i] = name[i];
    return node;
}

int vfs_node_set_private_destructor(vfs_node_t *node,
                                    vfs_private_destroy_fn destroy) {
    if (!node || !destroy) return 0;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (!node->private_data || node->private_destroy) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return 0;
    }
    node->private_destroy = destroy;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return 1;
}

int vfs_node_set_read(vfs_node_t *node, vfs_read_fn read, void *private_data) {
    if (!node || !read) return 0;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (node->read || (node->private_data && node->private_data != private_data)) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return 0;
    }
    node->read = read;
    node->private_data = private_data;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return 1;
}

int vfs_node_set_write(vfs_node_t *node, vfs_write_fn write, void *private_data) {
    if (!node || !write) return 0;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (node->write || (node->private_data && node->private_data != private_data)) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return 0;
    }
    node->write = write;
    if (!node->private_data) node->private_data = private_data;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return 1;
}

static void try_destroy(vfs_node_t *node) {
    if (!node) return;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (node->destroying || node->references != 0 || node->child_count != 0) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return;
    }
    node->destroying = 1;
    vfs_private_destroy_fn destroy = node->private_destroy;
    void *private_data = node->private_data;
    node->private_destroy = 0;
    node->private_data = 0;
    spinlock_unlock_irqrestore(&node->lock, flags);
    if (destroy) destroy(private_data);
    kfree(node);
}

int vfs_node_read(vfs_node_t *node, uint64_t offset, void *buffer, uint32_t size) {
    if (!node || !buffer || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    vfs_read_fn read = node->read;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return read ? read(node, offset, buffer, size) : 0;
}

int vfs_node_write(vfs_node_t *node, uint64_t offset, const void *buffer,
                   uint32_t size) {
    if (!node || !buffer || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    vfs_write_fn write = node->write;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return write ? write(node, offset, buffer, size) : 0;
}

int vfs_node_add_child(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child || parent == child ||
        parent->type != VFS_NODE_DIRECTORY || child->name[0] == '\0') return 0;
    int parent_first = (uintptr_t)parent < (uintptr_t)child;
    vfs_node_t *first = parent_first ? parent : child;
    vfs_node_t *second = parent_first ? child : parent;
    uint64_t first_flags = spinlock_lock_irqsave(&first->lock);
    uint64_t second_flags = spinlock_lock_irqsave(&second->lock);
    if (child->parent || child->destroying || child->references == 0) {
        spinlock_unlock_irqrestore(&second->lock, second_flags);
        spinlock_unlock_irqrestore(&first->lock, first_flags);
        return 0;
    }
    for (vfs_node_t *ancestor = parent; ancestor; ancestor = ancestor->parent) {
        if (ancestor == child) {
            spinlock_unlock_irqrestore(&second->lock, second_flags);
            spinlock_unlock_irqrestore(&first->lock, first_flags);
            return 0;
        }
    }
    for (vfs_node_t *existing = parent->first_child; existing;
         existing = existing->next_sibling) {
        if (string_equal(existing->name, child->name)) {
            spinlock_unlock_irqrestore(&second->lock, second_flags);
            spinlock_unlock_irqrestore(&first->lock, first_flags);
            return 0;
        }
    }
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
    ++parent->child_count;
    __atomic_add_fetch(&child->references, 1, __ATOMIC_RELAXED);
    spinlock_unlock_irqrestore(&second->lock, second_flags);
    spinlock_unlock_irqrestore(&first->lock, first_flags);
    return 1;
}

vfs_node_t *vfs_node_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_NODE_DIRECTORY || !name) return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    vfs_node_t *result = 0;
    for (vfs_node_t *child = parent->first_child; child;
         child = child->next_sibling) {
        if (string_equal(child->name, name)) {
            if (!child->destroying && child->references != 0) {
                ++child->references;
                result = child;
            }
            break;
        }
    }
    spinlock_unlock_irqrestore(&parent->lock, flags);
    return result;
}

vfs_node_t *vfs_node_child(vfs_node_t *parent, uint32_t index) {
    if (!parent || parent->type != VFS_NODE_DIRECTORY) return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    vfs_node_t *result = 0;
    uint32_t current = 0;
    for (vfs_node_t *child = parent->first_child; child;
         child = child->next_sibling) {
        if (!child->destroying && child->references != 0) {
            if (current == index) {
                ++child->references;
                result = child;
                break;
            }
            ++current;
        }
    }
    spinlock_unlock_irqrestore(&parent->lock, flags);
    return result;
}

vfs_node_t *vfs_lookup_path(vfs_node_t *root, const char *path) {
    if (!root || !path || path[0] != '/') return 0;
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
            next = current != root && current->parent ? current->parent : current;
            vfs_node_retain(next);
        } else {
            next = vfs_node_lookup(current, component);
        }
        vfs_node_release(current);
        if (!next) return 0;
        current = next;
    }
    return current;
}

void vfs_node_retain(vfs_node_t *node) {
    if (!node) return;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (!node->destroying && node->references != 0) ++node->references;
    spinlock_unlock_irqrestore(&node->lock, flags);
}

void vfs_node_release(vfs_node_t *node) {
    if (!node) return;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (node->destroying || node->references == 0) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return;
    }
    --node->references;
    spinlock_unlock_irqrestore(&node->lock, flags);
    try_destroy(node);
}

int vfs_node_remove(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child) return 0;
    if (parent == child) return 0;
    int parent_first = (uintptr_t)parent < (uintptr_t)child;
    vfs_node_t *first = parent_first ? parent : child;
    vfs_node_t *second = parent_first ? child : parent;
    uint64_t first_flags = spinlock_lock_irqsave(&first->lock);
    uint64_t second_flags = spinlock_lock_irqsave(&second->lock);
    if (parent->type != VFS_NODE_DIRECTORY || child->parent != parent ||
        child->child_count != 0) {
        spinlock_unlock_irqrestore(&second->lock, second_flags);
        spinlock_unlock_irqrestore(&first->lock, first_flags);
        return 0;
    }
    vfs_node_t **link = &parent->first_child;
    while (*link && *link != child) link = &(*link)->next_sibling;
    if (!*link) {
        spinlock_unlock_irqrestore(&second->lock, second_flags);
        spinlock_unlock_irqrestore(&first->lock, first_flags);
        return 0;
    }
    *link = child->next_sibling;
    child->next_sibling = 0;
    child->parent = 0;
    --parent->child_count;
    spinlock_unlock_irqrestore(&second->lock, second_flags);
    spinlock_unlock_irqrestore(&first->lock, first_flags);
    vfs_node_release(child);
    try_destroy(parent);
    return 1;
}
