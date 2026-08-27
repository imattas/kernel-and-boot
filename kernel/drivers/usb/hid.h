#ifndef OS_KERNEL_DRIVERS_USB_HID_H
#define OS_KERNEL_DRIVERS_USB_HID_H

#include <stdint.h>
#include "../input/input.h"

int usb_hid_keyboard_decode(const uint8_t *report, uint32_t length,
                            input_event_t *event);
int usb_hid_mouse_decode(const uint8_t *report, uint32_t length,
                         input_event_t events[3], uint32_t *event_count);

#endif
