#ifndef OS_KERNEL_DRIVERS_CONSOLE_H
#define OS_KERNEL_DRIVERS_CONSOLE_H

#include <stdint.h>
#include "framebuffer.h"

int console_initialize(framebuffer_t *framebuffer);
typedef void (*console_present_fn)(void *context);
void console_set_present_callback(console_present_fn callback, void *context);
uint32_t console_write(const void *buffer, uint32_t length);

#endif
