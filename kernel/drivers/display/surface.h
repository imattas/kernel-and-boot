#ifndef OS_KERNEL_DRIVERS_DISPLAY_SURFACE_H
#define OS_KERNEL_DRIVERS_DISPLAY_SURFACE_H

#include <stdint.h>

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
} display_surface_t;

int display_surface_initialize(display_surface_t *surface, void *pixels,
                               uint32_t width, uint32_t height,
                               uint32_t pitch_pixels);
void display_surface_clear(display_surface_t *surface, uint32_t color);
int display_surface_fill_rect(display_surface_t *surface, int32_t x,
                              int32_t y, uint32_t width, uint32_t height,
                              uint32_t color);
int display_surface_blit(display_surface_t *destination,
                         const display_surface_t *source, int32_t x,
                         int32_t y);

#endif
