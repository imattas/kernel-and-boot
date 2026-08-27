#include "hid.h"

int usb_hid_keyboard_decode(const uint8_t *report, uint32_t length,
                            input_event_t *event) {
    if (!event) return 0;
    input_event_t events[6];
    uint32_t event_count = 0;
    if (!usb_hid_keyboard_decode_report(report, length, events, &event_count) ||
        event_count == 0) return 0;
    *event = events[0];
    return 1;
}

int usb_hid_keyboard_decode_report(const uint8_t *report, uint32_t length,
                                   input_event_t events[6],
                                   uint32_t *event_count) {
    if (!report || !events || !event_count || length != 8 || report[1] != 0)
        return 0;
    for (uint32_t i = 2; i < 8; ++i)
        for (uint32_t j = i + 1; j < 8; ++j)
            if (report[i] != 0 && (report[i] < 4 || report[i] == report[j]))
                return 0;
    *event_count = 0;
    for (uint32_t i = 2; i < 8; ++i) {
        if (report[i] == 0) continue;
        events[*event_count] = (input_event_t){INPUT_EVENT_KEY, report[i], 1, 0};
        ++*event_count;
    }
    return 1;
}

void usb_hid_keyboard_state_initialize(usb_hid_keyboard_state_t *state) {
    if (!state) return;
    for (uint32_t i = 0; i < 6; ++i) state->keys[i] = 0;
    state->modifiers = 0;
}

static int hid_key_present(const uint8_t keys[6], uint8_t key) {
    for (uint32_t i = 0; i < 6; ++i)
        if (keys[i] == key) return 1;
    return 0;
}

int usb_hid_keyboard_decode_state(const uint8_t *report, uint32_t length,
                                  usb_hid_keyboard_state_t *state,
                                  input_event_t events[20],
                                  uint32_t *event_count) {
    if (!state || !events || !event_count ||
        !usb_hid_keyboard_decode_report(report, length, events, event_count))
        return 0;
    uint8_t next[6];
    for (uint32_t i = 0; i < 6; ++i) next[i] = report[i + 2];
    uint32_t count = 0;
    for (uint32_t bit = 0; bit < 8; ++bit)
        if ((state->modifiers & (uint8_t)(1U << bit)) != 0 &&
            (report[0] & (uint8_t)(1U << bit)) == 0)
            events[count++] = (input_event_t){INPUT_EVENT_KEY,
                                              (uint16_t)(0xe0U + bit), 0, 0};
    for (uint32_t i = 0; i < 6; ++i)
        if (state->keys[i] != 0 && !hid_key_present(next, state->keys[i]))
            events[count++] = (input_event_t){INPUT_EVENT_KEY, state->keys[i], 0, 0};
    for (uint32_t bit = 0; bit < 8; ++bit)
        if ((report[0] & (uint8_t)(1U << bit)) != 0 &&
            (state->modifiers & (uint8_t)(1U << bit)) == 0)
            events[count++] = (input_event_t){INPUT_EVENT_KEY,
                                              (uint16_t)(0xe0U + bit), 1, 0};
    for (uint32_t i = 0; i < 6; ++i)
        if (next[i] != 0 && !hid_key_present(state->keys, next[i]))
            events[count++] = (input_event_t){INPUT_EVENT_KEY, next[i], 1, 0};
    for (uint32_t i = 0; i < 6; ++i) state->keys[i] = next[i];
    state->modifiers = report[0];
    *event_count = count;
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
