#include "display_service.h"

int display_service_initialize(display_service_t *service, void *pixels,
                               uint32_t width, uint32_t height,
                               uint32_t pitch_pixels) {
    if (!service || !display_surface_initialize(&service->surface, pixels,
                                                width, height, pitch_pixels) ||
        !compositor_initialize(&service->compositor, &service->surface) ||
        !window_manager_initialize(&service->window_manager,
                                   &service->compositor))
        return 0;
    service->initialized = 1;
    return 1;
}

int display_service_shutdown(display_service_t *service) {
    if (!service || !service->initialized ||
        !window_manager_shutdown(&service->window_manager))
        return 0;
    service->initialized = 0;
    return 1;
}

int display_service_route_event(display_service_t *service,
                                const input_event_t *event) {
    return service && service->initialized &&
           window_manager_route_event(&service->window_manager, event);
}

int display_service_compose(display_service_t *service, uint32_t clear_color) {
    return service && service->initialized &&
           window_manager_compose(&service->window_manager, clear_color);
}

int display_service_compose_damage(display_service_t *service,
                                   uint32_t clear_color) {
    return service && service->initialized &&
           window_manager_compose_damage(&service->window_manager, clear_color);
}
