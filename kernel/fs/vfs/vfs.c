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

static void node_free_if_unused(vfs_node_t *node) {
    if (!node) return;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    if (__atomic_load_n(&node->references, __ATOMIC_ACQUIRE) != 0 ||
        node->child_count != 0) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return;
    }
    vfs_private_destroy_fn destroy = node->private_destroy;
    void *private_data = node->private_data;
    node->private_destroy = 0;
    node->private_data = 0;
    spinlock_unlock_irqrestore(&node->lock, flags);
    if (destroy) destroy(private_data);
    kfree(node);
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
    node->owner_uid = owner_uid;
    node->owner_gid = owner_gid;
    node->mode = mode;
    node->type = type;
    node->read = 0;
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
    if (node->read || node->private_data) {
        spinlock_unlock_irqrestore(&node->lock, flags);
        return 0;
    }
    node->read = read;
    node->private_data = private_data;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return 1;
}

int vfs_node_read(vfs_node_t *node, uint64_t offset, void *buffer, uint32_t size) {
    if (!node || !buffer || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&node->lock);
    vfs_read_fn read = node->read;
    spinlock_unlock_irqrestore(&node->lock, flags);
    return read ? read(node, offset, buffer, size) : 0;
}

int vfs_node_add_child(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child || parent == child ||
        parent->type != VFS_NODE_DIRECTORY || child->name[0] == '\0') return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    uint64_t child_flags = spinlock_lock_irqsave(&child->lock);
    if (child->parent) {
        spinlock_unlock_irqrestore(&child->lock, child_flags);
        spinlock_unlock_irqrestore(&parent->lock, flags);
        return 0;
    }
    for (vfs_node_t *existing = parent->first_child; existing;
         existing = existing->next_sibling) {
        if (string_equal(existing->name, child->name)) {
            spinlock_unlock_irqrestore(&child->lock, child_flags);
            spinlock_unlock_irqrestore(&parent->lock, flags);
            return 0;
        }
    }
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
    ++parent->child_count;
    __atomic_add_fetch(&child->references, 1, __ATOMIC_RELAXED);
    spinlock_unlock_irqrestore(&child->lock, child_flags);
    spinlock_unlock_irqrestore(&parent->lock, flags);
    return 1;
}

vfs_node_t *vfs_node_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_NODE_DIRECTORY || !name) return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    vfs_node_t *result = 0;
    for (vfs_node_t *child = parent->first_child; child;
         child = child->next_sibling) {
        if (string_equal(child->name, name)) {
            __atomic_add_fetch(&child->references, 1, __ATOMIC_RELAXED);
            result = child;
            break;
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
    if (node) __atomic_add_fetch(&node->references, 1, __ATOMIC_RELAXED);
}

void vfs_node_release(vfs_node_t *node) {
    if (!node) return;
    uint32_t references = __atomic_load_n(&node->references, __ATOMIC_ACQUIRE);
    while (references != 0 &&
           !__atomic_compare_exchange_n(&node->references, &references,
                                        references - 1, 0, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) { }
    if (references != 1) return;
    node_free_if_unused(node);
}

int vfs_node_remove(vfs_node_t *parent, vfs_node_t *child) {
    if (!parent || !child) return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    uint64_t child_flags = spinlock_lock_irqsave(&child->lock);
    if (parent->type != VFS_NODE_DIRECTORY || child->parent != parent ||
        child->child_count != 0) {
        spinlock_unlock_irqrestore(&child->lock, child_flags);
        spinlock_unlock_irqrestore(&parent->lock, flags);
        return 0;
    }
    vfs_node_t **link = &parent->first_child;
    while (*link && *link != child) link = &(*link)->next_sibling;
    if (!*link) {
        spinlock_unlock_irqrestore(&parent->lock, flags);
        return 0;
    }
    *link = child->next_sibling;
    child->next_sibling = 0;
    child->parent = 0;
    --parent->child_count;
    spinlock_unlock_irqrestore(&child->lock, child_flags);
    spinlock_unlock_irqrestore(&parent->lock, flags);
    vfs_node_release(child);
    return 1;
}
