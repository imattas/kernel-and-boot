#include "surface.h"

int display_surface_initialize(display_surface_t *surface, void *pixels,
                               uint32_t width, uint32_t height,
                               uint32_t pitch_pixels) {
    if (!surface || !pixels || width == 0 || height == 0 ||
        pitch_pixels < width ||
        (uint64_t)(height - 1U) * pitch_pixels + width >
            UINT64_MAX / sizeof(uint32_t))
        return 0;
    surface->pixels = (uint32_t *)pixels;
    surface->width = width;
    surface->height = height;
    surface->pitch_pixels = pitch_pixels;
    return 1;
}

void display_surface_clear(display_surface_t *surface, uint32_t color) {
    if (!surface || !surface->pixels) return;
    for (uint32_t row = 0; row < surface->height; ++row)
        for (uint32_t column = 0; column < surface->width; ++column)
            surface->pixels[(uint64_t)row * surface->pitch_pixels + column] =
                color;
}

int display_surface_fill_rect(display_surface_t *surface, int32_t x,
                              int32_t y, uint32_t width, uint32_t height,
                              uint32_t color) {
    if (!surface || !surface->pixels || width == 0 || height == 0) return 0;
    int64_t left = x < 0 ? 0 : x;
    int64_t top = y < 0 ? 0 : y;
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    if (left >= right || top >= bottom) return 1;
    for (int64_t row = top; row < bottom; ++row)
        for (int64_t column = left; column < right; ++column)
            surface->pixels[(uint64_t)row * surface->pitch_pixels + column] =
                color;
    return 1;
}

int display_surface_blit(display_surface_t *destination,
                         const display_surface_t *source, int32_t x,
                         int32_t y) {
    if (!destination || !source || !destination->pixels || !source->pixels ||
        source->width == 0 || source->height == 0)
        return 0;
    return display_surface_blit_region(destination, source, x, y, 0, 0,
                                       destination->width,
                                       destination->height);
}

int display_surface_blit_region(display_surface_t *destination,
                                const display_surface_t *source, int32_t x,
                                int32_t y, int32_t clip_x, int32_t clip_y,
                                uint32_t clip_width, uint32_t clip_height) {
    if (!destination || !source || !destination->pixels || !source->pixels ||
        source->width == 0 || source->height == 0 || clip_width == 0 ||
        clip_height == 0)
        return 0;
    int64_t left = x < 0 ? 0 : x;
    int64_t top = y < 0 ? 0 : y;
    int64_t right = (int64_t)x + source->width;
    int64_t bottom = (int64_t)y + source->height;
    int64_t clip_right = (int64_t)clip_x + clip_width;
    int64_t clip_bottom = (int64_t)clip_y + clip_height;
    if (left < clip_x) left = clip_x;
    if (top < clip_y) top = clip_y;
    if (right > clip_right) right = clip_right;
    if (bottom > clip_bottom) bottom = clip_bottom;
    if (right > destination->width) right = destination->width;
    if (bottom > destination->height) bottom = destination->height;
    if (left >= right || top >= bottom) return 1;
    uint32_t source_x = (uint32_t)(left - x);
    uint32_t source_y = (uint32_t)(top - y);
    for (int64_t row = top; row < bottom; ++row)
        for (int64_t column = left; column < right; ++column)
            destination->pixels[(uint64_t)row * destination->pitch_pixels +
                                column] = source->pixels[
                (uint64_t)(source_y + (uint32_t)(row - top)) *
                    source->pitch_pixels + source_x +
                (uint32_t)(column - left)];
    return 1;
}
