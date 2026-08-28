bits 64
default rel
global _start
extern printf_main

section .text
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    mov rdx, [rsp + rdi * 8 + 16]
    call printf_main
    mov edi, eax
    mov eax, 60
    syscall
