#ifndef OS_BOOT_UEFI_CONSOLE_H
#define OS_BOOT_UEFI_CONSOLE_H
#include "efi_types.h"
void uefi_console_write(efi_system_table_t *st, const efi_char16_t *message);
void uefi_console_hex(efi_system_table_t *st, const efi_char16_t *prefix,
                      uint64_t value);
efi_status_t uefi_fail(efi_system_table_t *st, efi_char16_t code,
                       efi_status_t status);
#endif
