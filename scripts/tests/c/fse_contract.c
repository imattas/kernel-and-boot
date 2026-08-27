#include <assert.h>
#include <stdint.h>

#include "kernel/fs/btrfs/fse.h"

int main(void) {
    btrfs_fse_table_t table;
    uint8_t decoded[4];
    uint32_t decoded_count = 0;
    uint32_t state = 0;
    int64_t offset = 0;
    uint8_t symbol = 0;
    uint32_t consumed = 0;
    const int16_t four_symbols[] = {1, 1, 1, 1};
    const int16_t rare_symbol[] = {-1, 3};
    assert(btrfs_fse_build(&table, four_symbols, 4, 2));
    assert(table.size == 4 && table.accuracy_log == 2);
    assert(table.bits[0] <= 2 && table.bits[1] <= 2 &&
           table.bits[2] <= 2 && table.bits[3] <= 2);
    assert(btrfs_fse_build(&table, rare_symbol, 2, 2));
    assert(!btrfs_fse_build(&table, four_symbols, 4, 11));
    assert(!btrfs_fse_build(&table, (const int16_t[]){1, 1}, 2, 2));
    assert(btrfs_fse_build_predefined(&table, 0) && table.size == 64);
    assert(btrfs_fse_build_predefined(&table, 1) && table.size == 32);
    assert(btrfs_fse_build_predefined(&table, 2) && table.size == 64);
    assert(!btrfs_fse_build_predefined(&table, 3));
    assert(btrfs_fse_build_rle(&table, 7) && table.size == 1 && table.symbols[0] == 7);
    assert(btrfs_fse_build(&table, (const int16_t[]){4}, 1, 2));
    assert(btrfs_fse_decode(&table, (const uint8_t[]){0x04}, 1, decoded, 4));
    assert(decoded[0] == 0 && decoded[3] == 0);
    assert(btrfs_fse_stream_init(&table, (const uint8_t[]){0x04}, 1, &state, &offset));
    assert(btrfs_fse_stream_peek(&table, state, &symbol) && symbol == 0);
    assert(btrfs_fse_stream_update(&table, (const uint8_t[]){0x04}, &state, &offset));
    assert(btrfs_fse_decode_interleaved2(&table, (const uint8_t[]){0x04}, 1,
                                         decoded, 4, &decoded_count));
    assert(decoded_count == 2 && decoded[0] == 0 && decoded[1] == 0);
    assert(btrfs_fse_read_header(&table, (const uint8_t[]){0xf0, 0x03}, 2, 5,
                                 &consumed));
    assert(consumed == 2 && table.size == 32);
    return 0;
}
