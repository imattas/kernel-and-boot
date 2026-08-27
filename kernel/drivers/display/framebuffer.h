#ifndef OS_KERNEL_DRIVERS_FRAMEBUFFER_H
#define OS_KERNEL_DRIVERS_FRAMEBUFFER_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

typedef struct {
    spinlock_t lock;
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
} framebuffer_t;

int framebuffer_initialize(framebuffer_t *framebuffer, void *pixels,
                           uint32_t width, uint32_t height,
                           uint32_t pitch_pixels);
void framebuffer_clear(framebuffer_t *framebuffer, uint32_t color);
int framebuffer_put_pixel(framebuffer_t *framebuffer, uint32_t x, uint32_t y,
                          uint32_t color);
int framebuffer_fill_rect(framebuffer_t *framebuffer, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height, uint32_t color);

#endif
