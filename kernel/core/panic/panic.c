#include "panic.h"
#include "../printk/serial.h"

static volatile uint32_t panic_state;

__attribute__((noreturn)) void kernel_panic(const char *reason) {
    __asm__ volatile ("cli" ::: "memory");
    if (__atomic_exchange_n(&panic_state, 1, __ATOMIC_ACQ_REL) == 0) {
        serial_write("\r\nKERNEL PANIC: ");
        serial_write(reason ? reason : "unknown failure");
        serial_write("\r\n");
    }
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}

__attribute__((noreturn)) void kernel_panic_exception(uint64_t vector) {
    __asm__ volatile ("cli" ::: "memory");
    if (__atomic_exchange_n(&panic_state, 1, __ATOMIC_ACQ_REL) == 0) {
        serial_write("\r\nKERNEL EXCEPTION vector=");
        serial_write_hex(vector);
        serial_write("\r\n");
    }
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}

uint32_t kernel_panic_state(void) {
    return __atomic_load_n(&panic_state, __ATOMIC_ACQUIRE);
}
