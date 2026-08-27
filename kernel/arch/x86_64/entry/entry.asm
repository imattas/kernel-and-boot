bits 64

global kernel_entry
extern kernel_main

section .text
kernel_entry:
    cli
    mov r12, rdi

    sub rsp, 16
    lea rax, [rel gdt_start]
    mov [rsp + 2], rax
    mov word [rsp], gdt_end - gdt_start - 1
    lgdt [rsp]

    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    add rsp, 16
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rdi, r12
    and rsp, -16
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00af9a000000ffff
    dq 0x00cf92000000ffff
gdt_end:
