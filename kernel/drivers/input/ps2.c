#include "ps2.h"
#include "../../arch/x86_64/time/timer.h"

static input_queue_t *keyboard_queue;
static input_queue_t *mouse_queue;
static uint8_t extended_scancode;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_length;
static int mouse_enabled;

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int wait_write(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        if ((in8(0x64) & 2) == 0) return 1;
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

static int wait_read(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        if ((in8(0x64) & 1) != 0) return 1;
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

static void flush_output(void) {
    for (uint32_t i = 0; i < 32 && (in8(0x64) & 1) != 0; ++i)
        (void)in8(0x60);
}

static int controller_command(uint8_t command, uint8_t *response) {
    if (!wait_write()) return 0;
    out8(0x64, command);
    if (!wait_read()) return 0;
    if (response) *response = in8(0x60);
    else (void)in8(0x60);
    return 1;
}

static int controller_write_config(uint8_t config) {
    if (!wait_write()) return 0;
    out8(0x64, 0x60);
    if (!wait_write()) return 0;
    out8(0x60, config);
    return 1;
}

static int keyboard_command(uint8_t command, uint8_t argument) {
    if (!wait_write()) return 0;
    out8(0x60, command);
    if (!wait_read() || in8(0x60) != 0xfa) return 0;
    if (!wait_write()) return 0;
    out8(0x60, argument);
    return wait_read() && in8(0x60) == 0xfa;
}

static int keyboard_command_noarg(uint8_t command) {
    if (!wait_write()) return 0;
    out8(0x60, command);
    if (!wait_read()) return 0;
    return in8(0x60) == 0xfa;
}

static int mouse_command_noarg(uint8_t command) {
    if (!wait_write()) return 0;
    out8(0x64, 0xd4);
    if (!wait_write()) return 0;
    out8(0x60, command);
    return wait_read() && in8(0x60) == 0xfa;
}

int ps2_keyboard_initialize(input_queue_t *queue) {
    if (!queue || !wait_write()) return 0;
    out8(0x64, 0xad);
    if (!wait_write()) return 0;
    out8(0x64, 0xa7);
    flush_output();
    uint8_t response = 0;
    if (!controller_command(0xaa, &response) || response != 0x55) return 0;
    if (!controller_command(0xab, &response) || response != 0x00) return 0;
    if (!controller_command(0x20, &response)) return 0;
    response |= 0x01U;
    response &= (uint8_t)~0x10U;
    if (!controller_write_config(response)) return 0;
    out8(0x64, 0xae);
    if (!keyboard_command(0xf0, 1)) return 0;
    if (!keyboard_command_noarg(0xf4)) return 0;
    while ((in8(0x64) & 1) != 0) (void)in8(0x60);
    keyboard_queue = queue;
    extended_scancode = 0;
    return 1;
}

int ps2_mouse_initialize(input_queue_t *queue) {
    mouse_enabled = 0;
    mouse_queue = 0;
    mouse_packet_length = 0;
    if (!queue || !wait_write()) return 0;
    out8(0x64, 0xa8);
    uint8_t config = 0;
    if (!controller_command(0x20, &config)) return 0;
    config |= 0x02U;
    if (!controller_write_config(config) || !mouse_command_noarg(0xf4))
        return 0;
    mouse_queue = queue;
    mouse_enabled = 1;
    return 1;
}

int ps2_mouse_enabled(void) { return mouse_enabled; }

int ps2_mouse_poll(input_queue_t *queue) {
    if (!queue || !mouse_enabled || (in8(0x64) & 1U) == 0 ||
        (in8(0x64) & 0x20U) == 0) return 0;
    uint8_t byte = in8(0x60);
    if (mouse_packet_length == 0 && (byte & 0x08U) == 0) return 0;
    mouse_packet[mouse_packet_length++] = byte;
    if (mouse_packet_length < 3) return 1;
    input_event_t events[3];
    uint32_t event_count = 0;
    mouse_packet_length = 0;
    if (!ps2_mouse_decode(mouse_packet, events, &event_count)) return 0;
    for (uint32_t i = 0; i < event_count; ++i)
        if (!input_queue_push(queue, &events[i])) return 0;
    return 1;
}

void ps2_mouse_irq(void) {
    if (mouse_queue) (void)ps2_mouse_poll(mouse_queue);
}

void ps2_keyboard_irq(void) {
    if (keyboard_queue) (void)ps2_keyboard_poll(keyboard_queue);
}

int ps2_mouse_decode(const uint8_t packet[3], input_event_t events[3],
                     uint32_t *event_count) {
    if (!packet || !events || !event_count || (packet[0] & 0x08U) == 0 ||
        (packet[0] & 0xc0U) != 0) return 0;
    int32_t x = (int32_t)(int8_t)packet[1];
    int32_t y = (int32_t)(int8_t)packet[2];
    uint64_t timestamp = timer_ticks();
    events[0] = (input_event_t){INPUT_EVENT_BUTTON, 0,
                                (int32_t)(packet[0] & 0x07U), timestamp};
    events[1] = (input_event_t){INPUT_EVENT_AXIS, 0, x, timestamp};
    events[2] = (input_event_t){INPUT_EVENT_AXIS, 1, y, timestamp};
    *event_count = 3;
    return 1;
}

int ps2_keyboard_poll(input_queue_t *queue) {
    if (!queue || (in8(0x64) & 1) == 0) return 0;
    uint8_t scancode = in8(0x60);
    if (scancode == 0xe0) {
        extended_scancode = 1;
        return 1;
    }
    uint16_t code = (uint16_t)(scancode & 0x7fU);
    if (extended_scancode) {
        code |= 0x0100U;
        extended_scancode = 0;
    }
    input_event_t event = {
        .type = INPUT_EVENT_KEY,
        .code = code,
        .value = (scancode & 0x80) == 0,
        .timestamp = timer_ticks()
    };
    return input_queue_push(queue, &event);
}
