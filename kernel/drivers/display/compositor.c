#include "compositor.h"

static int valid_layer(const compositor_t *compositor, uint32_t layer) {
    return compositor && layer < compositor->layer_count &&
           compositor->layers[layer].surface != 0;
}

static void mark_damage(compositor_t *compositor, int32_t x, int32_t y,
                        uint32_t width, uint32_t height) {
    if (!compositor || !compositor->target || width == 0 || height == 0) return;
    int64_t left = x < 0 ? 0 : x;
    int64_t top = y < 0 ? 0 : y;
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;
    if (right > compositor->target->width) right = compositor->target->width;
    if (bottom > compositor->target->height) bottom = compositor->target->height;
    if (left >= right || top >= bottom) return;
    if (compositor->damage_count != 0) {
        compositor_damage_t *damage = &compositor->damage[0];
        int64_t damage_right = (int64_t)damage->x + damage->width;
        int64_t damage_bottom = (int64_t)damage->y + damage->height;
        if (left < damage->x) damage->x = (int32_t)left;
        if (top < damage->y) damage->y = (int32_t)top;
        if (right > damage_right) damage_right = right;
        if (bottom > damage_bottom) damage_bottom = bottom;
        damage->width = (uint32_t)(damage_right - damage->x);
        damage->height = (uint32_t)(damage_bottom - damage->y);
        return;
    }
    compositor->damage[0] = (compositor_damage_t){(int32_t)left, (int32_t)top,
                                                   (uint32_t)(right - left),
                                                   (uint32_t)(bottom - top)};
    compositor->damage_count = 1;
}

static void mark_layer_damage(compositor_t *compositor,
                              const compositor_layer_t *layer) {
    if (layer && layer->surface)
        mark_damage(compositor, layer->x, layer->y, layer->surface->width,
                    layer->surface->height);
}

int compositor_initialize(compositor_t *compositor,
                          display_surface_t *target) {
    if (!compositor || !target || !target->pixels || target->width == 0 ||
        target->height == 0)
        return 0;
    compositor->target = target;
    compositor->layer_count = 0;
    for (uint32_t index = 0; index < COMPOSITOR_LAYER_CAPACITY; ++index)
        compositor->layers[index] = (compositor_layer_t){0};
    compositor->damage_count = 0;
    return 1;
}

uint32_t compositor_add_layer(compositor_t *compositor,
                              display_surface_t *surface, int32_t x,
                              int32_t y, uint32_t z) {
    if (!compositor || !surface || !surface->pixels)
        return COMPOSITOR_INVALID_LAYER;
    uint32_t layer = COMPOSITOR_INVALID_LAYER;
    for (uint32_t index = 0; index < compositor->layer_count; ++index)
        if (!compositor->layers[index].surface) {
            layer = index;
            break;
        }
    if (layer == COMPOSITOR_INVALID_LAYER) {
        if (compositor->layer_count >= COMPOSITOR_LAYER_CAPACITY)
            return COMPOSITOR_INVALID_LAYER;
        layer = compositor->layer_count++;
    }
    compositor->layers[layer] = (compositor_layer_t){surface, x, y, z, 1};
    mark_layer_damage(compositor, &compositor->layers[layer]);
    return layer;
}

int compositor_remove_layer(compositor_t *compositor, uint32_t layer) {
    if (!valid_layer(compositor, layer)) return 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    compositor->layers[layer].surface = 0;
    compositor->layers[layer].visible = 0;
    return 1;
}

int compositor_set_layer_position(compositor_t *compositor, uint32_t layer,
                                   int32_t x, int32_t y) {
    if (!valid_layer(compositor, layer)) return 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    compositor->layers[layer].x = x;
    compositor->layers[layer].y = y;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    return 1;
}

int compositor_set_layer_surface(compositor_t *compositor, uint32_t layer,
                                 display_surface_t *surface) {
    if (!valid_layer(compositor, layer) || !surface || !surface->pixels ||
        surface->width == 0 || surface->height == 0)
        return 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    compositor->layers[layer].surface = surface;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    return 1;
}

int compositor_invalidate_layer(compositor_t *compositor, uint32_t layer) {
    if (!valid_layer(compositor, layer)) return 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    return 1;
}

int compositor_set_layer_z(compositor_t *compositor, uint32_t layer,
                           uint32_t z) {
    if (!valid_layer(compositor, layer)) return 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    compositor->layers[layer].z = z;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    return 1;
}

int compositor_set_layer_visible(compositor_t *compositor, uint32_t layer,
                                 int visible) {
    if (!valid_layer(compositor, layer)) return 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    compositor->layers[layer].visible = visible != 0;
    mark_layer_damage(compositor, &compositor->layers[layer]);
    return 1;
}

int compositor_compose_damage(compositor_t *compositor, uint32_t clear_color) {
    if (!compositor || !compositor->target || !compositor->target->pixels)
        return 0;
    if (compositor->damage_count == 0) return 1;
    compositor_damage_t damage = compositor->damage[0];
    if (!display_surface_fill_rect(compositor->target, damage.x, damage.y,
                                   damage.width, damage.height, clear_color))
        return 0;
    uint32_t order[COMPOSITOR_LAYER_CAPACITY];
    uint32_t count = 0;
    for (uint32_t index = 0; index < compositor->layer_count; ++index)
        if (compositor->layers[index].surface && compositor->layers[index].visible) {
            uint32_t position = count++;
            while (position != 0 && compositor->layers[order[position - 1U]].z >
                       compositor->layers[index].z) {
                order[position] = order[position - 1U];
                --position;
            }
            order[position] = index;
        }
    for (uint32_t position = 0; position < count; ++position) {
        compositor_layer_t *layer = &compositor->layers[order[position]];
        if (!display_surface_blit_region(compositor->target, layer->surface,
                                         layer->x, layer->y, damage.x, damage.y,
                                         damage.width, damage.height))
            return 0;
    }
    compositor->damage_count = 0;
    return 1;
}

int compositor_compose(compositor_t *compositor, uint32_t clear_color) {
    if (!compositor || !compositor->target || !compositor->target->pixels)
        return 0;
    uint32_t order[COMPOSITOR_LAYER_CAPACITY];
    uint32_t count = 0;
    for (uint32_t index = 0; index < compositor->layer_count; ++index)
        if (compositor->layers[index].surface &&
            compositor->layers[index].visible) {
            uint32_t position = count++;
            while (position != 0 &&
                   compositor->layers[order[position - 1U]].z >
                       compositor->layers[index].z) {
                order[position] = order[position - 1U];
                --position;
            }
            order[position] = index;
        }
    display_surface_clear(compositor->target, clear_color);
    for (uint32_t position = 0; position < count; ++position) {
        compositor_layer_t *layer = &compositor->layers[order[position]];
        if (!display_surface_blit(compositor->target, layer->surface,
                                  layer->x, layer->y))
            return 0;
    }
    compositor->damage_count = 0;
    return 1;
}
