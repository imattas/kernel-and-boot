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

__attribute__((noreturn)) void kernel_panic_exception_frame(
    uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs,
    uint64_t rflags, const uint64_t registers[15]) {
    __asm__ volatile ("cli" ::: "memory");
    if (__atomic_exchange_n(&panic_state, 1, __ATOMIC_ACQ_REL) == 0) {
        serial_write("\r\nKERNEL EXCEPTION vector="); serial_write_hex(vector);
        serial_write(" error="); serial_write_hex(error_code);
        serial_write(" rip="); serial_write_hex(rip);
        serial_write(" cs="); serial_write_hex(cs);
        serial_write(" rflags="); serial_write_hex(rflags);
        static const char *const names[15] = {
            " r15=", " r14=", " r13=", " r12=", " r11=", " r10=",
            " r9=", " r8=", " rdi=", " rsi=", " rbp=", " rbx=",
            " rdx=", " rcx=", " rax="};
        if (registers)
            for (uint32_t i = 0; i < 15; ++i) {
                serial_write(names[i]); serial_write_hex(registers[i]);
            }
        serial_write("\r\n");
    }
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}

uint32_t kernel_panic_state(void) {
    return __atomic_load_n(&panic_state, __ATOMIC_ACQUIRE);
}
