#include <stdint.h>
#include "irq.h"
#include "../cpu/tables.h"
#include "../smp/percpu.h"
#include "apic.h"
#include "../../../core/printk/serial.h"
#include "../../../drivers/input/ps2.h"

extern void arch_timer_irq_stub(void);
extern void arch_keyboard_irq_stub(void);
extern void arch_mouse_irq_stub(void);

static void out8(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port)); }

void interrupts_initialize(void) {
    arch_set_interrupt_gate(32, arch_timer_irq_stub);
    arch_set_interrupt_gate(33, arch_keyboard_irq_stub);
    arch_set_interrupt_gate(44, arch_mouse_irq_stub);
    out8(0x20, 0x11); out8(0xa0, 0x11);
    out8(0x21, 0x20); out8(0xa1, 0x28);
    out8(0x21, 0x04); out8(0xa1, 0x02);
    out8(0x21, 0x01); out8(0xa1, 0x01);
    out8(0xa1, 0xff);
    if (arch_ioapic_route_irq(1, 33)) out8(0x21, 0xff);
    else out8(0x21, 0xfd);
    if (ps2_mouse_enabled()) (void)arch_ioapic_route_irq(12, 44);
    if (!arch_apic_timer_initialize()) return;
    serial_write("interrupt controller configured\r\n");
    __asm__ volatile ("sti" ::: "memory");
    arch_percpu_release_interrupts();
    serial_write("interrupts enabled\r\n");
}
