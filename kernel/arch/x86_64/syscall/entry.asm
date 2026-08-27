bits 64

global arch_syscall_interrupt
global arch_enter_user
extern syscall_dispatch

section .text
arch_syscall_interrupt:
    cld
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_dispatch
    iretq

arch_enter_user:
    cli
    mov rax, rsi
    push qword 0x33
    push rax
    push qword 0x202
    push qword 0x2b
    push rdi
    iretq
