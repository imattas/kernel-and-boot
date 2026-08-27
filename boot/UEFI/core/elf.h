#ifndef OS_BOOT_UEFI_ELF_H
#define OS_BOOT_UEFI_ELF_H
#include "efi_types.h"
efi_status_t uefi_elf_load(efi_boot_services_t *bs, const void *file,
                           uint64_t size, efi_physical_address_t *base,
                           uint64_t *image_size, kernel_entry_t *entry);
#endif
