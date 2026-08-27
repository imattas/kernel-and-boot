#include "console.h"

void uefi_console_write(efi_system_table_t *st, const efi_char16_t *message) {
    if (st && st->con_out && st->con_out->output_string && message)
        st->con_out->output_string(st->con_out, message);
}

void uefi_console_hex(efi_system_table_t *st, const efi_char16_t *prefix,
                      uint64_t value) {
    if (!st || !st->con_out || !st->con_out->output_string || !prefix) return;
    efi_char16_t message[32] = {0};
    uint64_t length = 0;
    while (prefix[length] != 0 && length < 12) {
        message[length] = prefix[length];
        ++length;
    }
    message[length++] = '0'; message[length++] = 'x';
    for (int32_t shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> shift) & 0xfU);
        message[length++] = digit < 10 ? (efi_char16_t)('0' + digit) :
            (efi_char16_t)('A' + digit - 10);
    }
    message[length++] = '\r'; message[length++] = '\n'; message[length] = 0;
    uefi_console_write(st, message);
}

efi_status_t uefi_fail(efi_system_table_t *st, efi_char16_t code,
                       efi_status_t status) {
    efi_char16_t message[] = {'F', code, '\r', '\n', 0};
    uefi_console_write(st, message);
    return status;
}
