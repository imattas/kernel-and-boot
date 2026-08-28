bits 64
default rel
global _start
extern cmp_main

section .text
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    mov rdx, [rsp + rdi * 8 + 16]
    call cmp_main
    mov edi, eax
    mov eax, 60
    syscall
