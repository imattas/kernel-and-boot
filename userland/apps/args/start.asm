bits 64

global _start
extern args_main

section .text
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call args_main
    ud2
