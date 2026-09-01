#ifndef OS_KERNEL_DRIVERS_DISPLAY_WINDOW_MANAGER_H
#define OS_KERNEL_DRIVERS_DISPLAY_WINDOW_MANAGER_H

#include <stdint.h>
#include "compositor.h"
#include "../input/input.h"

#define WINDOW_MANAGER_WINDOW_CAPACITY 16U
#define WINDOW_MANAGER_INVALID_WINDOW UINT32_MAX

typedef struct {
    display_surface_t *surface;
    uint32_t compositor_layer;
    int32_t x;
    int32_t y;
    uint8_t visible;
    uint8_t focused;
    void *owned_pixels;
    input_queue_t *input;
} window_t;

typedef struct {
    compositor_t *compositor;
    window_t windows[WINDOW_MANAGER_WINDOW_CAPACITY];
    uint32_t next_z;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t cursor_layer;
} window_manager_t;

int window_manager_initialize(window_manager_t *manager,
                              compositor_t *compositor);
uint32_t window_manager_create(window_manager_t *manager,
                               display_surface_t *surface, int32_t x,
                               int32_t y);
uint32_t window_manager_create_buffered(window_manager_t *manager,
                                        uint32_t width, uint32_t height,
                                        int32_t x, int32_t y);
int window_manager_destroy(window_manager_t *manager, uint32_t window);
int window_manager_move(window_manager_t *manager, uint32_t window,
                        int32_t x, int32_t y);
int window_manager_set_visible(window_manager_t *manager, uint32_t window,
                               int visible);
int window_manager_focus(window_manager_t *manager, uint32_t window);
int window_manager_set_cursor(window_manager_t *manager,
                              display_surface_t *surface);
int window_manager_set_cursor_visible(window_manager_t *manager, int visible);
int window_manager_route_event(window_manager_t *manager,
                               const input_event_t *event);
uint32_t window_manager_read_event(window_manager_t *manager, uint32_t window,
                                   input_event_t *event);
uint32_t window_manager_hit_test(const window_manager_t *manager, int32_t x,
                                 int32_t y);
int window_manager_compose(window_manager_t *manager, uint32_t clear_color);

#endif
