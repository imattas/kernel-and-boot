bits 64

global _start
extern head_main

section .text
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    mov rdx, rdi
    add rdx, 2
    shl rdx, 3
    add rdx, rsi
    call head_main
    ud2
