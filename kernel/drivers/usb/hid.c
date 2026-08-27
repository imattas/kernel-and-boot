#include "hid.h"

int usb_hid_keyboard_decode(const uint8_t *report, uint32_t length,
                            input_event_t *event) {
    if (!report || !event || length != 8 || report[2] == 0) return 0;
    for (uint32_t i = 2; i < 8; ++i)
        for (uint32_t j = i + 1; j < 8; ++j)
            if (report[i] != 0 && report[i] == report[j]) return 0;
    event->type = INPUT_EVENT_KEY;
    event->code = report[2];
    event->value = report[0];
    event->timestamp = 0;
    return 1;
}

int usb_hid_mouse_decode(const uint8_t *report, uint32_t length,
                         input_event_t events[3], uint32_t *event_count) {
    if (!report || !events || !event_count || length != 3 ||
        (report[0] & 0xf8U) != 0) return 0;
    events[0] = (input_event_t){INPUT_EVENT_BUTTON, 0,
                                (int32_t)(report[0] & 0x07U), 0};
    events[1] = (input_event_t){INPUT_EVENT_AXIS, 0,
                                (int32_t)(int8_t)report[1], 0};
    events[2] = (input_event_t){INPUT_EVENT_AXIS, 1,
                                (int32_t)(int8_t)report[2], 0};
    *event_count = 3;
    return 1;
}
