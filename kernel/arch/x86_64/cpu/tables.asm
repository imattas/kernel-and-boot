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

extern arch_exception_panic_frame
%assign vector 0
%rep 32
global arch_exception_stub_%+vector
arch_exception_stub_%+vector:
    cli
%if vector != 8 && vector != 10 && vector != 11 && vector != 12 && vector != 13 && vector != 14 && vector != 17 && vector != 21 && vector != 29 && vector != 30
    push qword 0
%endif
    push vector
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call arch_exception_panic_frame
.exception_halt_%+vector:
    hlt
    jmp .exception_halt_%+vector
%assign vector vector + 1
%endrep
