bits 64

global os_syscall3

section .text
os_syscall3:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    int 0x80
    ret
