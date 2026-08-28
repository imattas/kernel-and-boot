#ifndef OS_KERNEL_DRIVERS_USB_HID_H
#define OS_KERNEL_DRIVERS_USB_HID_H

#include <stdint.h>
#include "../input/input.h"

typedef struct {
    uint8_t keys[6];
    uint8_t modifiers;
} usb_hid_keyboard_state_t;

int usb_hid_keyboard_decode(const uint8_t *report, uint32_t length,
                            input_event_t *event);
int usb_hid_keyboard_decode_report(const uint8_t *report, uint32_t length,
                                   input_event_t events[6],
                                   uint32_t *event_count);
void usb_hid_keyboard_state_initialize(usb_hid_keyboard_state_t *state);
int usb_hid_keyboard_decode_state(const uint8_t *report, uint32_t length,
                                  usb_hid_keyboard_state_t *state,
                                  input_event_t events[20],
                                  uint32_t *event_count);
int usb_hid_mouse_decode(const uint8_t *report, uint32_t length,
                         input_event_t events[4], uint32_t *event_count);

#endif
