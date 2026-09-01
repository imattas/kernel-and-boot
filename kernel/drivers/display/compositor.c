#include "compositor.h"

static int valid_layer(const compositor_t *compositor, uint32_t layer) {
    return compositor && layer < compositor->layer_count &&
           compositor->layers[layer].surface != 0;
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
    return 1;
}

uint32_t compositor_add_layer(compositor_t *compositor,
                              display_surface_t *surface, int32_t x,
                              int32_t y, uint32_t z) {
    if (!compositor || !surface || !surface->pixels ||
        compositor->layer_count >= COMPOSITOR_LAYER_CAPACITY)
        return COMPOSITOR_INVALID_LAYER;
    uint32_t layer = compositor->layer_count++;
    compositor->layers[layer] = (compositor_layer_t){surface, x, y, z, 1};
    return layer;
}

int compositor_remove_layer(compositor_t *compositor, uint32_t layer) {
    if (!valid_layer(compositor, layer)) return 0;
    compositor->layers[layer].surface = 0;
    compositor->layers[layer].visible = 0;
    return 1;
}

int compositor_set_layer_position(compositor_t *compositor, uint32_t layer,
                                  int32_t x, int32_t y) {
    if (!valid_layer(compositor, layer)) return 0;
    compositor->layers[layer].x = x;
    compositor->layers[layer].y = y;
    return 1;
}

int compositor_set_layer_visible(compositor_t *compositor, uint32_t layer,
                                 int visible) {
    if (!valid_layer(compositor, layer)) return 0;
    compositor->layers[layer].visible = visible != 0;
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
    return 1;
}
