bits 64

global arch_e1000_irq_stub
extern e1000_service
extern e1000_interrupt_handler
extern arch_apic_eoi

section .text
arch_e1000_irq_stub:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    call e1000_interrupt_handler
    call e1000_service
    call arch_apic_eoi
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    iretq
