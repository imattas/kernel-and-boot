#include "framebuffer.h"

int framebuffer_initialize(framebuffer_t *framebuffer, void *pixels,
                           uint32_t width, uint32_t height,
                           uint32_t pitch_pixels) {
    if (!framebuffer || !pixels || width == 0 || height == 0 ||
        pitch_pixels < width) return 0;
    framebuffer->pixels = (uint32_t *)pixels;
    framebuffer->width = width;
    framebuffer->height = height;
    framebuffer->pitch_pixels = pitch_pixels;
    return 1;
}

void framebuffer_clear(framebuffer_t *framebuffer, uint32_t color) {
    if (!framebuffer || !framebuffer->pixels) return;
    for (uint32_t y = 0; y < framebuffer->height; ++y)
        for (uint32_t x = 0; x < framebuffer->width; ++x)
            framebuffer->pixels[y * framebuffer->pitch_pixels + x] = color;
}

int framebuffer_put_pixel(framebuffer_t *framebuffer, uint32_t x, uint32_t y,
                          uint32_t color) {
    if (!framebuffer || !framebuffer->pixels || x >= framebuffer->width ||
        y >= framebuffer->height) return 0;
    framebuffer->pixels[y * framebuffer->pitch_pixels + x] = color;
    return 1;
}

int framebuffer_fill_rect(framebuffer_t *framebuffer, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height, uint32_t color) {
    if (!framebuffer || !framebuffer->pixels || width == 0 || height == 0 ||
        x >= framebuffer->width || y >= framebuffer->height ||
        width > framebuffer->width - x || height > framebuffer->height - y)
        return 0;
    for (uint32_t row = 0; row < height; ++row)
        for (uint32_t column = 0; column < width; ++column)
            framebuffer->pixels[(y + row) * framebuffer->pitch_pixels + x + column] = color;
    return 1;
}
