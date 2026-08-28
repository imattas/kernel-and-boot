#ifndef OS_BOOT_UEFI_FILE_H
#define OS_BOOT_UEFI_FILE_H
#include "efi_types.h"
efi_status_t uefi_read_file(efi_boot_services_t *bs,
                                   efi_file_protocol_t *file,
                                   uint8_t **buffer, efi_uintn_t *size);
#endif
