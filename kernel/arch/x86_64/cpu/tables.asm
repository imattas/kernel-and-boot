bits 64

global arch_default_interrupt
global arch_load_idt
global arch_load_gdt
section .text
arch_load_gdt:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ret

arch_load_idt:
    cli
    lidt [rdi]
    ret

arch_default_interrupt:
    cli
.halt:
    hlt
    jmp .halt

extern arch_exception_panic
%assign vector 0
%rep 32
global arch_exception_stub_%+vector
arch_exception_stub_%+vector:
    cli
    push vector
    pop rdi
    call arch_exception_panic
.exception_halt_%+vector:
    hlt
    jmp .exception_halt_%+vector
%assign vector vector + 1
%endrep
