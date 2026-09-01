#include "window_manager.h"
#include "../../mm/heap/heap.h"

static int valid_window(const window_manager_t *manager, uint32_t window) {
    return manager && window < WINDOW_MANAGER_WINDOW_CAPACITY &&
           manager->windows[window].surface != 0;
}

int window_manager_initialize(window_manager_t *manager,
                              compositor_t *compositor) {
    if (!manager || !compositor || !compositor->target) return 0;
    manager->compositor = compositor;
    manager->next_z = 0;
    manager->pointer_x = 0;
    manager->pointer_y = 0;
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
            input_queue_t *input = (input_queue_t *)kmalloc(sizeof(*input));
            if (!input) {
                compositor_remove_layer(manager->compositor, layer);
                return WINDOW_MANAGER_INVALID_WINDOW;
            }
            input_queue_initialize(input);
            manager->windows[index] = (window_t){
                .surface = surface,
                .compositor_layer = layer,
                .x = x,
                .y = y,
                .visible = 1,
                .input = input
            };
            return index;
        }
    return WINDOW_MANAGER_INVALID_WINDOW;
}

uint32_t window_manager_create_buffered(window_manager_t *manager,
                                        uint32_t width, uint32_t height,
                                        int32_t x, int32_t y) {
    if (!manager || width == 0 || height == 0 || width > 4096U ||
        height > 4096U || (uint64_t)width * height > UINT64_MAX / 4U)
        return WINDOW_MANAGER_INVALID_WINDOW;
    uint64_t bytes = (uint64_t)width * height * 4U;
    void *pixels = kmalloc(bytes);
    if (!pixels) return WINDOW_MANAGER_INVALID_WINDOW;
    display_surface_t *surface = (display_surface_t *)kmalloc(sizeof(*surface));
    if (!surface || !display_surface_initialize(surface, pixels, width, height,
                                                width)) {
        kfree(surface);
        kfree(pixels);
        return WINDOW_MANAGER_INVALID_WINDOW;
    }
    uint32_t window = window_manager_create(manager, surface, x, y);
    if (window == WINDOW_MANAGER_INVALID_WINDOW) {
        kfree(surface);
        kfree(pixels);
        return window;
    }
    manager->windows[window].owned_pixels = pixels;
    return window;
}

int window_manager_destroy(window_manager_t *manager, uint32_t window) {
    if (!valid_window(manager, window) ||
        !compositor_remove_layer(manager->compositor,
                                 manager->windows[window].compositor_layer))
        return 0;
    display_surface_t *surface = manager->windows[window].surface;
    void *owned_pixels = manager->windows[window].owned_pixels;
    input_queue_t *input = manager->windows[window].input;
    manager->windows[window] = (window_t){0};
    if (owned_pixels) kfree(owned_pixels);
    if (surface && owned_pixels) kfree(surface);
    if (input) kfree(input);
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

int window_manager_route_event(window_manager_t *manager,
                               const input_event_t *event) {
    if (!manager || !event) return 0;
    uint32_t target = WINDOW_MANAGER_INVALID_WINDOW;
    if (event->type == INPUT_EVENT_KEY) {
        for (uint32_t index = 0; index < WINDOW_MANAGER_WINDOW_CAPACITY; ++index)
            if (manager->windows[index].surface &&
                manager->windows[index].visible && manager->windows[index].focused) {
                target = index;
                break;
            }
    } else if (event->type == INPUT_EVENT_AXIS) {
        int64_t coordinate = event->code == 0 ? manager->pointer_x :
                             event->code == 1 ? manager->pointer_y : 0;
        coordinate += event->value;
        int64_t limit = event->code == 0 ? manager->compositor->target->width :
                         event->code == 1 ? manager->compositor->target->height : 0;
        if (event->code <= 1 && limit > 0) {
            if (coordinate < 0) coordinate = 0;
            if (coordinate >= limit) coordinate = limit - 1;
            if (event->code == 0) manager->pointer_x = (int32_t)coordinate;
            else manager->pointer_y = (int32_t)coordinate;
        }
        target = window_manager_hit_test(manager, manager->pointer_x,
                                         manager->pointer_y);
        if (target == WINDOW_MANAGER_INVALID_WINDOW)
            for (uint32_t index = 0; index < WINDOW_MANAGER_WINDOW_CAPACITY;
                 ++index)
                if (manager->windows[index].surface &&
                    manager->windows[index].visible &&
                    manager->windows[index].focused) {
                    target = index;
                    break;
                }
    } else if (event->type == INPUT_EVENT_BUTTON) {
        target = window_manager_hit_test(manager, manager->pointer_x,
                                         manager->pointer_y);
        if (target != WINDOW_MANAGER_INVALID_WINDOW && event->value != 0 &&
            !window_manager_focus(manager, target))
            return 0;
    }
    return target != WINDOW_MANAGER_INVALID_WINDOW &&
           input_queue_push(manager->windows[target].input, event);
}

uint32_t window_manager_read_event(window_manager_t *manager, uint32_t window,
                                   input_event_t *event) {
    if (!valid_window(manager, window) || !event) return 0;
    return input_queue_pop(manager->windows[window].input, event);
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
