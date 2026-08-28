#ifndef OS_KERNEL_DRIVERS_SERIAL_H
#define OS_KERNEL_DRIVERS_SERIAL_H

#include <stdint.h>

void serial_init(void);
uint32_t serial_poll_input(char *buffer, uint32_t capacity);
void serial_write(const char *message);
void serial_write_hex(uint64_t value);
void serial_write_hex_line(const char *prefix, uint64_t value);

#endif
