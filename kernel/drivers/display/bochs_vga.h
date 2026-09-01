#ifndef OS_KERNEL_DRIVERS_BOCHS_VGA_H
#define OS_KERNEL_DRIVERS_BOCHS_VGA_H

#include <stdint.h>

int bochs_vga_initialize(uint32_t width, uint32_t height);
int bochs_vga_present(void);
uint64_t bochs_vga_framebuffer(void);
uint64_t bochs_vga_framebuffer_size(void);
uint32_t bochs_vga_width(void);
uint32_t bochs_vga_height(void);

#endif
