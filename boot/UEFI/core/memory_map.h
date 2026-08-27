#ifndef OS_BOOT_UEFI_MEMORY_MAP_H
#define OS_BOOT_UEFI_MEMORY_MAP_H
#include "efi_types.h"
#include "boot_info.h"
efi_status_t uefi_capture_memory_map(efi_boot_services_t *bs, uint8_t **buffer,
                                     efi_uintn_t *capacity, os_boot_info_t *info,
                                     efi_uintn_t *map_key);
#endif
