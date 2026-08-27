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
