#include "window_manager.h"

static int valid_window(const window_manager_t *manager, uint32_t window) {
    return manager && window < WINDOW_MANAGER_WINDOW_CAPACITY &&
           manager->windows[window].surface != 0;
}

int window_manager_initialize(window_manager_t *manager,
                              compositor_t *compositor) {
    if (!manager || !compositor || !compositor->target) return 0;
    manager->compositor = compositor;
    manager->next_z = 0;
    for (uint32_t index = 0; index < WINDOW_MANAGER_WINDOW_CAPACITY; ++index)
        manager->windows[index] = (window_t){0};
    return 1;
}

uint32_t window_manager_create(window_manager_t *manager,
                               display_surface_t *surface, int32_t x,
                               int32_t y) {
    if (!manager || !surface || !surface->pixels) return WINDOW_MANAGER_INVALID_WINDOW;
    for (uint32_t index = 0; index < WINDOW_MANAGER_WINDOW_CAPACITY; ++index)
        if (!manager->windows[index].surface) {
            uint32_t layer = compositor_add_layer(manager->compositor, surface,
                                                   x, y, manager->next_z++);
            if (layer == COMPOSITOR_INVALID_LAYER)
                return WINDOW_MANAGER_INVALID_WINDOW;
            manager->windows[index] = (window_t){surface, layer, x, y, 1, 0};
            return index;
        }
    return WINDOW_MANAGER_INVALID_WINDOW;
}

int window_manager_destroy(window_manager_t *manager, uint32_t window) {
    if (!valid_window(manager, window) ||
        !compositor_remove_layer(manager->compositor,
                                 manager->windows[window].compositor_layer))
        return 0;
    manager->windows[window] = (window_t){0};
    return 1;
}

int window_manager_move(window_manager_t *manager, uint32_t window,
                        int32_t x, int32_t y) {
    if (!valid_window(manager, window)) return 0;
    window_t *entry = &manager->windows[window];
    if (!compositor_set_layer_position(manager->compositor,
                                       entry->compositor_layer, x, y))
        return 0;
    entry->x = x;
    entry->y = y;
    return 1;
}

int window_manager_set_visible(window_manager_t *manager, uint32_t window,
                               int visible) {
    if (!valid_window(manager, window) ||
        !compositor_set_layer_visible(manager->compositor,
                                      manager->windows[window].compositor_layer,
                                      visible))
        return 0;
    manager->windows[window].visible = visible != 0;
    if (!manager->windows[window].visible) manager->windows[window].focused = 0;
    return 1;
}

int window_manager_focus(window_manager_t *manager, uint32_t window) {
    if (!valid_window(manager, window) || !manager->windows[window].visible)
        return 0;
    for (uint32_t index = 0; index < WINDOW_MANAGER_WINDOW_CAPACITY; ++index)
        if (manager->windows[index].surface) manager->windows[index].focused = 0;
    window_t *entry = &manager->windows[window];
    if (!compositor_set_layer_z(manager->compositor, entry->compositor_layer,
                                manager->next_z++))
        return 0;
    entry->focused = 1;
    return 1;
}

uint32_t window_manager_hit_test(const window_manager_t *manager, int32_t x,
                                 int32_t y) {
    if (!manager) return WINDOW_MANAGER_INVALID_WINDOW;
    uint32_t result = WINDOW_MANAGER_INVALID_WINDOW;
    uint32_t highest_z = 0;
    for (uint32_t index = 0; index < WINDOW_MANAGER_WINDOW_CAPACITY; ++index) {
        const window_t *entry = &manager->windows[index];
        if (!entry->surface || !entry->visible) continue;
        if (x < entry->x || y < entry->y ||
            (int64_t)x >= (int64_t)entry->x + entry->surface->width ||
            (int64_t)y >= (int64_t)entry->y + entry->surface->height)
            continue;
        const compositor_layer_t *layer =
            &manager->compositor->layers[entry->compositor_layer];
        if (result == WINDOW_MANAGER_INVALID_WINDOW || layer->z >= highest_z) {
            result = index;
            highest_z = layer->z;
        }
    }
    return result;
}

int window_manager_compose(window_manager_t *manager, uint32_t clear_color) {
    return manager && compositor_compose(manager->compositor, clear_color);
}
