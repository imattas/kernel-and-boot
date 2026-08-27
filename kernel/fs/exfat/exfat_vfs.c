#include "exfat_vfs.h"
#include "../../mm/heap/heap.h"

typedef struct { exfat_fs_t *fs; char name[32]; uint64_t size; } exfat_vfs_file_t;
static void exfat_vfs_destroy(void *private_data) { kfree(private_data); }

static int exfat_vfs_read(vfs_node_t *node, uint64_t offset,
                          void *buffer, uint32_t size) {
    exfat_vfs_file_t *file = node ? (exfat_vfs_file_t *)node->private_data : 0;
    if (!file || offset > file->size || size > file->size - offset) return 0;
    return exfat_read_file(file->fs, file->name, offset, buffer, size) ? (int)size : 0;
}

int exfat_vfs_attach_file(exfat_fs_t *fs, vfs_node_t *root,
                          const char *filesystem_name, const char *name) {
    if (!fs || !fs->mounted || !root || root->type != VFS_NODE_DIRECTORY ||
        !filesystem_name || !name) return 0;
    uint32_t cluster = 0; uint64_t size = 0; uint8_t no_fat_chain = 0;
    if (!exfat_lookup(fs, filesystem_name, &cluster, &size, &no_fat_chain) ||
        size > UINT64_MAX) return 0;
    exfat_vfs_file_t *file = (exfat_vfs_file_t *)kmalloc(sizeof(*file));
    if (!file) return 0;
    file->fs = fs; file->size = size;
    uint32_t i = 0;
    for (; i + 1 < sizeof(file->name) && filesystem_name[i]; ++i)
        file->name[i] = filesystem_name[i];
    if (filesystem_name[i] || name[0] == 0) { kfree(file); return 0; }
    file->name[i] = 0;
    vfs_node_t *node = vfs_node_create(name, VFS_NODE_REGULAR, 0, 0, 0444);
    if (!node || !vfs_node_set_read(node, exfat_vfs_read, file) ||
        !vfs_node_set_private_destructor(node, exfat_vfs_destroy) ||
        !vfs_node_add_child(root, node)) {
        if (node) {
            int owns = node->private_destroy == exfat_vfs_destroy && node->private_data == file;
            vfs_node_release(node);
            if (!owns) kfree(file);
        } else kfree(file);
        return 0;
    }
    vfs_node_release(node); return 1;
}
