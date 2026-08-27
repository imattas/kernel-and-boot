#ifndef OS_KERNEL_DRIVERS_FIRMWARE_H
#define OS_KERNEL_DRIVERS_FIRMWARE_H
#include <stdint.h>
#include "../../../boot/UEFI/core/boot_info.h"
static inline int firmware_boot_contract_valid(const os_boot_info_t *info) {
    return info && info->memory_map && info->memory_map_size &&
           info->memory_descriptor_size >= 48 &&
           info->memory_map_size >= info->memory_descriptor_size &&
           info->memory_map_size % info->memory_descriptor_size == 0 &&
           info->kernel_base < (1ULL << 32) && info->kernel_size != 0 &&
           info->kernel_size <= (1ULL << 32) - info->kernel_base;
}
#endif
