#ifndef OS_KERNEL_DRIVERS_PS2_H
#define OS_KERNEL_DRIVERS_PS2_H

#include "input.h"

int ps2_keyboard_initialize(input_queue_t *queue);
int ps2_keyboard_poll(input_queue_t *queue);
void ps2_keyboard_irq(void);
int ps2_mouse_decode(const uint8_t packet[3], input_event_t events[3],
                     uint32_t *event_count);
int ps2_mouse_decode_wheel(const uint8_t packet[4], input_event_t events[4],
                           uint32_t *event_count);
int ps2_mouse_initialize(input_queue_t *queue);
int ps2_mouse_poll(input_queue_t *queue);
void ps2_mouse_irq(void);
int ps2_mouse_enabled(void);

#endif
