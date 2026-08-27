#ifndef OS_KERNEL_FS_BTRFS_FSE_H
#define OS_KERNEL_FS_BTRFS_FSE_H

#include <stdint.h>

#define BTRFS_FSE_TABLE_MAX 1024U

typedef struct {
    uint8_t symbols[BTRFS_FSE_TABLE_MAX];
    uint8_t bits[BTRFS_FSE_TABLE_MAX];
    uint16_t new_state[BTRFS_FSE_TABLE_MAX];
    uint32_t size;
    uint32_t accuracy_log;
} btrfs_fse_table_t;

int btrfs_fse_build(btrfs_fse_table_t *table, const int16_t *normalized,
                    uint32_t symbol_count, uint32_t accuracy_log);

#endif
