#ifndef OS_KERNEL_FS_PROCFS_H
#define OS_KERNEL_FS_PROCFS_H

#include <stdint.h>
#include "../vfs/vfs.h"

vfs_node_t *procfs_create(uint64_t process_id);

#endif
