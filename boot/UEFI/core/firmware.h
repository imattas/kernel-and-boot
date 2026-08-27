#ifndef OS_BOOT_UEFI_FIRMWARE_H
#define OS_BOOT_UEFI_FIRMWARE_H
#include "efi_types.h"
#include "boot_info.h"
uint64_t uefi_find_acpi_rsdp(const efi_system_table_t *st);
void uefi_find_framebuffer(efi_boot_services_t *bs, os_boot_info_t *info);
#endif
