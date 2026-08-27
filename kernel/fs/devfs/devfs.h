#ifndef OS_KERNEL_FS_DEVFS_H
#define OS_KERNEL_FS_DEVFS_H

#include <stdint.h>
#include "../vfs/vfs.h"

uint32_t devfs_populate(vfs_node_t *root);

#endif
