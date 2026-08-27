#include "ps2.h"
#include "../../arch/x86_64/time/timer.h"

static input_queue_t *keyboard_queue;

static void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int wait_write(void) {
    for (uint32_t i = 0; i < 100000; ++i)
        if ((in8(0x64) & 2) == 0) return 1;
    return 0;
}

static int wait_read(void) {
    for (uint32_t i = 0; i < 100000; ++i)
        if ((in8(0x64) & 1) != 0) return 1;
    return 0;
}

static int keyboard_command(uint8_t command, uint8_t argument) {
    if (!wait_write()) return 0;
    out8(0x60, command);
    if (!wait_read() || in8(0x60) != 0xfa) return 0;
    if (!wait_write()) return 0;
    out8(0x60, argument);
    return wait_read() && in8(0x60) == 0xfa;
}

int ps2_keyboard_initialize(input_queue_t *queue) {
    if (!queue || !wait_write()) return 0;
    out8(0x64, 0xae);
    if (!keyboard_command(0xf0, 1)) return 0;
    while ((in8(0x64) & 1) != 0) (void)in8(0x60);
    keyboard_queue = queue;
    return 1;
}

void ps2_keyboard_irq(void) {
    if (keyboard_queue) (void)ps2_keyboard_poll(keyboard_queue);
}

int ps2_keyboard_poll(input_queue_t *queue) {
    if (!queue || (in8(0x64) & 1) == 0) return 0;
    uint8_t scancode = in8(0x60);
    input_event_t event = {
        .type = INPUT_EVENT_KEY,
        .code = (uint16_t)(scancode & 0x7f),
        .value = (scancode & 0x80) == 0,
        .timestamp = timer_ticks()
    };
    return input_queue_push(queue, &event);
}
