bits 64

global _start

section .text
_start:
    mov eax, 1
    mov edi, 1
    lea rsi, [rel message]
    mov edx, message_end - message
    int 0x80
    mov eax, 15
    xor edi, edi
    xor esi, esi
    xor edx, edx
    int 0x80
    ud2

section .rodata
message db "os userland init\r\n"
message_end:
