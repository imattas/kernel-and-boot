#include <assert.h>
#include <stdint.h>

#include "kernel/fs/btrfs/fse.h"

int main(void) {
    btrfs_fse_table_t table;
    uint8_t decoded[4];
    const int16_t four_symbols[] = {1, 1, 1, 1};
    const int16_t rare_symbol[] = {-1, 3};
    assert(btrfs_fse_build(&table, four_symbols, 4, 2));
    assert(table.size == 4 && table.accuracy_log == 2);
    assert(table.bits[0] <= 2 && table.bits[1] <= 2 &&
           table.bits[2] <= 2 && table.bits[3] <= 2);
    assert(btrfs_fse_build(&table, rare_symbol, 2, 2));
    assert(!btrfs_fse_build(&table, four_symbols, 4, 11));
    assert(!btrfs_fse_build(&table, (const int16_t[]){1, 1}, 2, 2));
    assert(btrfs_fse_build(&table, (const int16_t[]){4}, 1, 2));
    assert(btrfs_fse_decode(&table, (const uint8_t[]){0x04}, 1, decoded, 4));
    assert(decoded[0] == 0 && decoded[3] == 0);
    return 0;
}
