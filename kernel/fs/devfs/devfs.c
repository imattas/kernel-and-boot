#include "devfs.h"
#include "../../device/device.h"

static char hex_digit(uint32_t value) {
    return value < 10 ? (char)('0' + value) : (char)('a' + value - 10);
}

static void device_name(char *name, uint32_t index) {
    name[0] = 'p'; name[1] = 'c'; name[2] = 'i';
    if (index < 16) {
        name[3] = hex_digit(index);
        name[4] = '\0';
    } else {
        name[3] = hex_digit((index >> 4) & 0xf);
        name[4] = hex_digit(index & 0xf);
        name[5] = '\0';
    }
}

uint32_t devfs_populate(vfs_node_t *root) {
    if (!root || root->type != VFS_NODE_DIRECTORY) return 0;
    uint32_t added = 0;
    for (uint32_t index = 0; index < device_count(); ++index) {
        if (!device_at(index)) continue;
        char name[8];
        device_name(name, index);
        vfs_node_t *node = vfs_node_create(name, VFS_NODE_DEVICE, 0, 0, 0600);
        if (!node || !vfs_node_add_child(root, node)) {
            if (node) vfs_node_release(node);
            continue;
        }
        vfs_node_release(node);
        ++added;
    }
    return added;
}
