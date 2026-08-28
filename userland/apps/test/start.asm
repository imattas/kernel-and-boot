bits 64

global _start
extern test_main

section .text
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    mov rdx, [rsp + rdi * 8 + 16]
    call test_main
    ud2
