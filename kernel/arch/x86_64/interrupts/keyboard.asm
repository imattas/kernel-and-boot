BITS 64
default rel

global arch_keyboard_irq_stub
extern ps2_keyboard_irq

arch_keyboard_irq_stub:
    push rax
    push rcx
    push rdx
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    call ps2_keyboard_irq
    mov al, 0x20
    out 0x20, al
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rdx
    pop rcx
    pop rax
    iretq
