#ifndef OS_KERNEL_DRIVERS_DISPLAY_SERVICE_H
#define OS_KERNEL_DRIVERS_DISPLAY_SERVICE_H

#include <stdint.h>
#include "window_manager.h"

typedef struct {
    display_surface_t surface;
    compositor_t compositor;
    window_manager_t window_manager;
    uint8_t initialized;
} display_service_t;

int display_service_initialize(display_service_t *service, void *pixels,
                               uint32_t width, uint32_t height,
                               uint32_t pitch_pixels);
int display_service_shutdown(display_service_t *service);
int display_service_route_event(display_service_t *service,
                                const input_event_t *event);
int display_service_compose(display_service_t *service, uint32_t clear_color);
int display_service_compose_damage(display_service_t *service,
                                   uint32_t clear_color);

#endif
