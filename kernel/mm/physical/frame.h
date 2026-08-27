#ifndef OS_PHYSICAL_FRAME_H
#define OS_PHYSICAL_FRAME_H

#include <stdint.h>
#include "../../../boot/UEFI/core/boot_info.h"

typedef struct {
    uint64_t total_frames;
    uint64_t free_frames;
    uint64_t reserved_frames;
} physical_memory_stats_t;

int physical_init(const os_boot_info_t *boot_info);
const physical_memory_stats_t *physical_stats(void);
uint64_t physical_alloc_frame(void);
void physical_free_frame(uint64_t address);

#endif
