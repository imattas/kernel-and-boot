#ifndef OS_KERNEL_DRIVERS_DISPLAY_COMPOSITOR_H
#define OS_KERNEL_DRIVERS_DISPLAY_COMPOSITOR_H

#include <stdint.h>
#include "surface.h"

#define COMPOSITOR_LAYER_CAPACITY 16U
#define COMPOSITOR_INVALID_LAYER UINT32_MAX
#define COMPOSITOR_DAMAGE_CAPACITY 32U

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} compositor_damage_t;

typedef struct {
    display_surface_t *surface;
    int32_t x;
    int32_t y;
    uint32_t z;
    uint8_t visible;
} compositor_layer_t;

typedef struct {
    display_surface_t *target;
    compositor_layer_t layers[COMPOSITOR_LAYER_CAPACITY];
    uint32_t layer_count;
    compositor_damage_t damage[COMPOSITOR_DAMAGE_CAPACITY];
    uint32_t damage_count;
} compositor_t;

int compositor_initialize(compositor_t *compositor,
                          display_surface_t *target);
uint32_t compositor_add_layer(compositor_t *compositor,
                              display_surface_t *surface, int32_t x,
                              int32_t y, uint32_t z);
int compositor_remove_layer(compositor_t *compositor, uint32_t layer);
int compositor_set_layer_position(compositor_t *compositor, uint32_t layer,
                                  int32_t x, int32_t y);
int compositor_set_layer_surface(compositor_t *compositor, uint32_t layer,
                                 display_surface_t *surface);
int compositor_set_layer_z(compositor_t *compositor, uint32_t layer,
                           uint32_t z);
int compositor_set_layer_visible(compositor_t *compositor, uint32_t layer,
                                 int visible);
int compositor_compose_damage(compositor_t *compositor, uint32_t clear_color);
int compositor_compose(compositor_t *compositor, uint32_t clear_color);

#endif
