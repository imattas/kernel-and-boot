bits 64

global arch_timer_irq_stub
extern timer_tick
extern arch_apic_eoi
extern arch_scheduler_timer_interrupt

section .text
arch_timer_irq_stub:
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
    call timer_tick
    call arch_apic_eoi
    call arch_scheduler_timer_interrupt
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
