#ifndef OS_KERNEL_DRIVERS_DISPLAY_DESKTOP_H
#define OS_KERNEL_DRIVERS_DISPLAY_DESKTOP_H

#include <stdint.h>
#include "display_service.h"

#define DESKTOP_INVALID_WINDOW UINT32_MAX

typedef struct {
    display_service_t *service;
    uint32_t background_window;
    uint32_t panel_window;
    uint32_t background_color;
    uint32_t panel_color;
    uint32_t panel_height;
    uint8_t initialized;
} desktop_t;

int desktop_initialize(desktop_t *desktop, display_service_t *service,
                       uint32_t background_color, uint32_t panel_color,
                       uint32_t panel_height);
int desktop_shutdown(desktop_t *desktop);
int desktop_set_colors(desktop_t *desktop, uint32_t background_color,
                       uint32_t panel_color);
int desktop_reflow(desktop_t *desktop);

#endif
