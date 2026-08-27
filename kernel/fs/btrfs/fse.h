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
int btrfs_fse_decode(const btrfs_fse_table_t *table, const uint8_t *stream,
                     uint32_t stream_size, uint8_t *symbols,
                     uint32_t symbol_count);
int btrfs_fse_decode_interleaved2(const btrfs_fse_table_t *table,
                                  const uint8_t *stream, uint32_t stream_size,
                                  uint8_t *symbols, uint32_t capacity,
                                  uint32_t *symbol_count);
int btrfs_fse_read_header(btrfs_fse_table_t *table, const uint8_t *stream,
                          uint32_t stream_size, uint32_t max_accuracy_log,
                          uint32_t *consumed);

#endif
