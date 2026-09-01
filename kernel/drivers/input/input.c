#include "input.h"
#include "ps2.h"

static input_queue_t *standard_queue;
static uint8_t keyboard_ctrl;
static uint8_t keyboard_shift;
static uint8_t keyboard_caps;

static char key_to_ascii(uint16_t code) {
    if ((code & INPUT_KEY_PS2) != 0) {
        code &= (uint16_t)~INPUT_KEY_PS2;
        if (code == 0x1c) return '\n';
        if (code == 0x0e) return '\b';
        if (code == 0x39) return ' ';
        if (code >= 0x1e && code <= 0x32) {
            static const char ps2[21] = {
                'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
                0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm'
            };
            char value = ps2[code - 0x1e];
            if (value >= 'a' && value <= 'z' && (keyboard_shift ^ keyboard_caps))
                value = (char)(value - 'a' + 'A');
            if (keyboard_ctrl && value >= 'a' && value <= 'z')
                return (char)(value - 'a' + 1);
            if (keyboard_ctrl && value >= 'A' && value <= 'Z')
                return (char)(value - 'A' + 1);
            return value;
        }
        return 0;
    }
    if (code >= 0x1e && code <= 0x32) {
        static const char ps2[21] = {
            'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
            0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm'
        };
        char value = ps2[code - 0x1e];
        if (value >= 'a' && value <= 'z' && (keyboard_shift ^ keyboard_caps))
            value = (char)(value - 'a' + 'A');
        if (keyboard_ctrl && value >= 'a' && value <= 'z')
            return (char)(value - 'a' + 1);
        if (keyboard_ctrl && value >= 'A' && value <= 'Z')
            return (char)(value - 'A' + 1);
        return value;
    }
    if (code >= 4 && code <= 29) {
        char value = (char)('a' + code - 4);
        if (keyboard_shift ^ keyboard_caps) value = (char)(value - 'a' + 'A');
        if (keyboard_ctrl) return (char)((value & 0x1f));
        return value;
    }
    if (code == 0x1c || code == 40) return '\n';
    if (code == 0x39 || code == 44) return ' ';
    if (code == 0x0e || code == 42) return '\b';
    return 0;
}

static void keyboard_update_modifiers(uint16_t code, int pressed) {
    uint8_t ps2 = (code & INPUT_KEY_PS2) != 0;
    if (ps2) code &= (uint16_t)~INPUT_KEY_PS2;
    if (ps2) {
        if (code == 0x1d) keyboard_ctrl = (uint8_t)pressed;
        else if (code == 0x2a || code == 0x36) keyboard_shift = (uint8_t)pressed;
        else if (code == 0x3a && pressed) keyboard_caps ^= 1U;
    } else {
        if (code == 0xe0 || code == 0xe4) keyboard_ctrl = (uint8_t)pressed;
        else if (code == 0xe1 || code == 0xe5) keyboard_shift = (uint8_t)pressed;
        else if (code == 0x39 && pressed) keyboard_caps ^= 1U;
    }
}

void input_queue_initialize(input_queue_t *queue) {
    if (!queue) return;
    spinlock_init(&queue->lock);
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

int input_queue_push(input_queue_t *queue, const input_event_t *event) {
    if (!queue || !event || event->type < INPUT_EVENT_KEY ||
        event->type > INPUT_EVENT_AXIS) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    if (queue->count == INPUT_EVENT_CAPACITY) {
        spinlock_unlock_irqrestore(&queue->lock, flags);
        return 0;
    }
    queue->events[queue->tail] = *event;
    queue->tail = (queue->tail + 1U) % INPUT_EVENT_CAPACITY;
    ++queue->count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return 1;
}

int input_queue_push_batch(input_queue_t *queue, const input_event_t *events,
                           uint32_t count) {
    if (!queue || !events || count == 0 || count > INPUT_EVENT_CAPACITY)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (events[i].type < INPUT_EVENT_KEY ||
            events[i].type > INPUT_EVENT_AXIS)
            return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    if (count > INPUT_EVENT_CAPACITY - queue->count) {
        spinlock_unlock_irqrestore(&queue->lock, flags);
        return 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        queue->events[queue->tail] = events[i];
        queue->tail = (queue->tail + 1U) % INPUT_EVENT_CAPACITY;
    }
    queue->count += count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return 1;
}

int input_queue_pop(input_queue_t *queue, input_event_t *event) {
    if (!queue || !event) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    if (queue->count == 0) {
        spinlock_unlock_irqrestore(&queue->lock, flags);
        return 0;
    }
    *event = queue->events[queue->head];
    queue->head = (queue->head + 1U) % INPUT_EVENT_CAPACITY;
    --queue->count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return 1;
}

uint32_t input_queue_count(const input_queue_t *queue) {
    if (!queue) return 0;
    input_queue_t *mutable_queue = (input_queue_t *)queue;
    uint64_t flags = spinlock_lock_irqsave(&mutable_queue->lock);
    uint32_t count = mutable_queue->count;
    spinlock_unlock_irqrestore(&mutable_queue->lock, flags);
    return count;
}

void input_set_standard_queue(input_queue_t *queue) { standard_queue = queue; }

uint32_t input_read_standard(void *buffer, uint32_t capacity) {
    if (!standard_queue || !buffer || capacity == 0) return 0;
    /* Keep console input usable on firmware/QEMU configurations that do not
       route the legacy keyboard IRQ while the scheduler is running. */
    (void)ps2_keyboard_poll(standard_queue);
    uint8_t *output = (uint8_t *)buffer;
    uint32_t count = 0;
    input_event_t event;
    while (count < capacity && input_queue_pop(standard_queue, &event)) {
        if (event.type == INPUT_EVENT_KEY)
            keyboard_update_modifiers(event.code, event.value != 0);
        if (event.value == 0) continue;
        char value = event.type == INPUT_EVENT_TEXT ? (char)event.code :
                     event.type == INPUT_EVENT_KEY ? key_to_ascii(event.code) : 0;
        if (value) output[count++] = (uint8_t)value;
    }
    return count;
}

int input_queue_push_text(input_queue_t *queue, const char *text,
                          uint32_t length, uint64_t timestamp) {
    if (!queue || !text || length == 0 || length > INPUT_EVENT_CAPACITY)
        return 0;
    input_event_t events[INPUT_EVENT_CAPACITY];
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] == 0) return 0;
        events[index] = (input_event_t){INPUT_EVENT_TEXT,
                                        (uint16_t)(uint8_t)text[index], 1,
                                        timestamp};
    }
    return input_queue_push_batch(queue, events, length);
}
