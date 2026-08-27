#ifndef OS_BOOT_UEFI_EFI_CONTEXT_H
#define OS_BOOT_UEFI_EFI_CONTEXT_H
#include "efi_types.h"
#include "boot_info.h"
typedef struct { efi_handle_t image_handle; efi_system_table_t *system_table; efi_boot_services_t *boot_services; efi_loaded_image_t *loaded_image; efi_file_protocol_t *root; os_boot_info_t *boot_info; } efi_context_t;
#endif
