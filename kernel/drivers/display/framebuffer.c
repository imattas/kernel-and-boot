#include "framebuffer.h"

int framebuffer_initialize(framebuffer_t *framebuffer, void *pixels,
                           uint32_t width, uint32_t height,
                           uint32_t pitch_pixels) {
    if (!framebuffer || !pixels || width == 0 || height == 0 ||
        pitch_pixels < width ||
        (uint64_t)(height - 1U) * pitch_pixels + width >
            UINT64_MAX / sizeof(uint32_t)) return 0;
    spinlock_init(&framebuffer->lock);
    framebuffer->pixels = (uint32_t *)pixels;
    framebuffer->width = width;
    framebuffer->height = height;
    framebuffer->pitch_pixels = pitch_pixels;
    return 1;
}

void framebuffer_clear(framebuffer_t *framebuffer, uint32_t color) {
    if (!framebuffer || !framebuffer->pixels) return;
    uint64_t flags = spinlock_lock_irqsave(&framebuffer->lock);
    for (uint32_t y = 0; y < framebuffer->height; ++y)
        for (uint32_t x = 0; x < framebuffer->width; ++x)
            framebuffer->pixels[(uint64_t)y * framebuffer->pitch_pixels + x] = color;
    spinlock_unlock_irqrestore(&framebuffer->lock, flags);
}

int framebuffer_put_pixel(framebuffer_t *framebuffer, uint32_t x, uint32_t y,
                          uint32_t color) {
    if (!framebuffer || !framebuffer->pixels || x >= framebuffer->width ||
        y >= framebuffer->height) return 0;
    uint64_t flags = spinlock_lock_irqsave(&framebuffer->lock);
    framebuffer->pixels[(uint64_t)y * framebuffer->pitch_pixels + x] = color;
    spinlock_unlock_irqrestore(&framebuffer->lock, flags);
    return 1;
}

int framebuffer_fill_rect(framebuffer_t *framebuffer, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height, uint32_t color) {
    if (!framebuffer || !framebuffer->pixels || width == 0 || height == 0 ||
        x >= framebuffer->width || y >= framebuffer->height ||
        width > framebuffer->width - x || height > framebuffer->height - y)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&framebuffer->lock);
    for (uint32_t row = 0; row < height; ++row)
        for (uint32_t column = 0; column < width; ++column)
            framebuffer->pixels[(uint64_t)(y + row) * framebuffer->pitch_pixels +
                                 x + column] = color;
    spinlock_unlock_irqrestore(&framebuffer->lock, flags);
    return 1;
}
