#include <stdint.h>
#include "serial.h"

#define SERIAL_COM1 0x3f8

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void putc_serial(char value) { out8(SERIAL_COM1, (uint8_t)value); }
static volatile uint32_t serial_lock;

static uint64_t lock_serial(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq\n\t pop %0\n\t cli" : "=r"(flags) :: "memory");
    while (__atomic_exchange_n(&serial_lock, 1, __ATOMIC_ACQUIRE) != 0)
        __asm__ volatile ("pause");
    return flags;
}

static void unlock_serial(uint64_t flags) {
    __atomic_store_n(&serial_lock, 0, __ATOMIC_RELEASE);
    __asm__ volatile ("push %0\n\t popfq" :: "r"(flags) : "memory", "cc");
}

static void write_chars(const char *message) {
    for (uint64_t i = 0; message[i]; ++i) putc_serial(message[i]);
}

void serial_init(void) {
    out8(SERIAL_COM1 + 1, 0);
    out8(SERIAL_COM1 + 3, 0x80);
    out8(SERIAL_COM1, 3);
    out8(SERIAL_COM1 + 1, 0);
    out8(SERIAL_COM1 + 3, 3);
    out8(SERIAL_COM1 + 2, 0xc7);
    out8(SERIAL_COM1 + 4, 0x0b);
}

void serial_write(const char *message) {
    uint64_t flags = lock_serial();
    write_chars(message);
    unlock_serial(flags);
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    uint64_t flags = lock_serial();
    write_chars("0x");
    for (int32_t shift = 60; shift >= 0; shift -= 4)
        putc_serial(digits[(value >> shift) & 0xf]);
    unlock_serial(flags);
}

void serial_write_hex_line(const char *prefix, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    uint64_t flags = lock_serial();
    write_chars(prefix);
    write_chars("0x");
    for (int32_t shift = 60; shift >= 0; shift -= 4)
        putc_serial(digits[(value >> shift) & 0xf]);
    write_chars("\r\n");
    unlock_serial(flags);
}
