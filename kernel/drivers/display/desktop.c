#include "desktop.h"

static int desktop_valid(const desktop_t *desktop) {
    return desktop && desktop->initialized && desktop->service &&
           desktop->service->initialized;
}

static void fill_window(window_manager_t *manager, uint32_t window,
                        uint32_t color) {
    window_t *entry = &manager->windows[window];
    uint64_t count = (uint64_t)entry->surface->width * entry->surface->height;
    for (uint64_t index = 0; index < count; ++index)
        entry->surface->pixels[index] = color;
    (void)window_manager_invalidate(manager, window);
}

int desktop_initialize(desktop_t *desktop, display_service_t *service,
                       uint32_t background_color, uint32_t panel_color,
                       uint32_t panel_height) {
    if (!desktop || desktop->initialized || !service ||
        !service->initialized || panel_height == 0 ||
        panel_height > service->surface.height)
        return 0;
    window_manager_t *manager = &service->window_manager;
    uint32_t background = window_manager_create_buffered(
        manager, service->surface.width, service->surface.height, 0, 0);
    if (background == WINDOW_MANAGER_INVALID_WINDOW) return 0;
    uint32_t panel = window_manager_create_buffered(
        manager, service->surface.width, panel_height, 0,
        (int32_t)(service->surface.height - panel_height));
    if (panel == WINDOW_MANAGER_INVALID_WINDOW) {
        (void)window_manager_destroy(manager, background);
        return 0;
    }
    desktop->service = service;
    desktop->background_window = background;
    desktop->panel_window = panel;
    desktop->background_color = background_color;
    desktop->panel_color = panel_color;
    desktop->panel_height = panel_height;
    desktop->initialized = 1;
    fill_window(manager, background, background_color);
    fill_window(manager, panel, panel_color);
    return 1;
}

int desktop_shutdown(desktop_t *desktop) {
    if (!desktop_valid(desktop)) return 0;
    window_manager_t *manager = &desktop->service->window_manager;
    int result = window_manager_destroy(manager, desktop->panel_window) &&
                 window_manager_destroy(manager, desktop->background_window);
    if (result) *desktop = (desktop_t){0};
    return result;
}

int desktop_set_colors(desktop_t *desktop, uint32_t background_color,
                       uint32_t panel_color) {
    if (!desktop_valid(desktop)) return 0;
    window_manager_t *manager = &desktop->service->window_manager;
    desktop->background_color = background_color;
    desktop->panel_color = panel_color;
    fill_window(manager, desktop->background_window, background_color);
    fill_window(manager, desktop->panel_window, panel_color);
    return 1;
}

int desktop_reflow(desktop_t *desktop) {
    if (!desktop_valid(desktop) || desktop->panel_height == 0 ||
        desktop->panel_height > desktop->service->surface.height)
        return 0;
    window_manager_t *manager = &desktop->service->window_manager;
    if (!window_manager_resize_buffered(manager, desktop->background_window,
                                        desktop->service->surface.width,
                                        desktop->service->surface.height) ||
        !window_manager_resize_buffered(manager, desktop->panel_window,
                                        desktop->service->surface.width,
                                        desktop->panel_height) ||
        !window_manager_move(manager, desktop->background_window, 0, 0) ||
        !window_manager_move(manager, desktop->panel_window, 0,
                             (int32_t)(desktop->service->surface.height -
                                       desktop->panel_height)))
        return 0;
    return desktop_set_colors(desktop, desktop->background_color,
                              desktop->panel_color);
}
