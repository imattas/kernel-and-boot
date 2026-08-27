#include "input.h"

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
