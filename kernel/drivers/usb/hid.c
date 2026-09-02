#include "hid.h"
#include "../../arch/x86_64/time/timer.h"

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
    /* Boot-protocol keyboard reports use the first eight bytes. Some
       descriptors advertise a larger interrupt packet, so accept the
       trailing padding instead of rejecting an otherwise valid report. */
    if (!report || !events || !event_count || length < 8 || report[1] != 0)
        return 0;
    for (uint32_t i = 2; i < 8; ++i)
        for (uint32_t j = i + 1; j < 8; ++j)
            if (report[i] != 0 && (report[i] < 4 || report[i] == report[j]))
                return 0;
    *event_count = 0;
    uint64_t timestamp = timer_ticks();
    for (uint32_t i = 2; i < 8; ++i) {
        if (report[i] == 0) continue;
        events[*event_count] = (input_event_t){INPUT_EVENT_KEY, report[i], 1,
                                              timestamp, INPUT_SOURCE_HID};
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
    uint64_t timestamp = timer_ticks();
    uint8_t next[6];
    for (uint32_t i = 0; i < 6; ++i) next[i] = report[i + 2];
    uint32_t count = 0;
    for (uint32_t bit = 0; bit < 8; ++bit)
        if ((state->modifiers & (uint8_t)(1U << bit)) != 0 &&
            (report[0] & (uint8_t)(1U << bit)) == 0) {
            if (count == 20) return 0;
            events[count++] = (input_event_t){INPUT_EVENT_KEY,
                                              (uint16_t)(0xe0U + bit), 0,
                                              timestamp, INPUT_SOURCE_HID};
        }
    for (uint32_t i = 0; i < 6; ++i)
        if (state->keys[i] != 0 && !hid_key_present(next, state->keys[i])) {
            if (count == 20) return 0;
            events[count++] = (input_event_t){INPUT_EVENT_KEY, state->keys[i], 0,
                                              timestamp, INPUT_SOURCE_HID};
        }
    for (uint32_t bit = 0; bit < 8; ++bit)
        if ((report[0] & (uint8_t)(1U << bit)) != 0 &&
            (state->modifiers & (uint8_t)(1U << bit)) == 0) {
            if (count == 20) return 0;
            events[count++] = (input_event_t){INPUT_EVENT_KEY,
                                              (uint16_t)(0xe0U + bit), 1,
                                              timestamp, INPUT_SOURCE_HID};
        }
    for (uint32_t i = 0; i < 6; ++i)
        if (next[i] != 0 && !hid_key_present(state->keys, next[i])) {
            if (count == 20) return 0;
            events[count++] = (input_event_t){INPUT_EVENT_KEY, next[i], 1,
                                              timestamp, INPUT_SOURCE_HID};
        }
    for (uint32_t i = 0; i < 6; ++i) state->keys[i] = next[i];
    state->modifiers = report[0];
    *event_count = count;
    return 1;
}

int usb_hid_mouse_decode(const uint8_t *report, uint32_t length,
                         input_event_t events[4], uint32_t *event_count) {
    if (!report || !events || !event_count || (length != 3 && length != 4) ||
        (report[0] & 0xf0U) != 0) return 0;
    uint64_t timestamp = timer_ticks();
    events[0] = (input_event_t){INPUT_EVENT_BUTTON, 0,
                                (int32_t)(report[0] & 0x0fU), timestamp,
                                INPUT_SOURCE_HID};
    events[1] = (input_event_t){INPUT_EVENT_AXIS, 0,
                                (int32_t)(int8_t)report[1], timestamp,
                                INPUT_SOURCE_HID};
    events[2] = (input_event_t){INPUT_EVENT_AXIS, 1,
                                (int32_t)(int8_t)report[2], timestamp,
                                INPUT_SOURCE_HID};
    if (length == 4) {
        events[3] = (input_event_t){INPUT_EVENT_AXIS, 2,
                                    (int32_t)(int8_t)report[3], timestamp,
                                    INPUT_SOURCE_HID};
        *event_count = 4;
    } else {
        *event_count = 3;
    }
    return 1;
}
