default rel

section .text
global efi_entry
extern efi_main

efi_entry:
    sub rsp, 40
    call efi_main
    add rsp, 40
    ret
