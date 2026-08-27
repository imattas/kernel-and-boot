#ifndef OS_KERNEL_DRIVERS_INPUT_H
#define OS_KERNEL_DRIVERS_INPUT_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

#define INPUT_EVENT_CAPACITY 64U

typedef enum {
    INPUT_EVENT_KEY,
    INPUT_EVENT_BUTTON,
    INPUT_EVENT_AXIS
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    uint16_t code;
    int32_t value;
    uint64_t timestamp;
} input_event_t;

typedef struct {
    spinlock_t lock;
    input_event_t events[INPUT_EVENT_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} input_queue_t;

void input_queue_initialize(input_queue_t *queue);
int input_queue_push(input_queue_t *queue, const input_event_t *event);
int input_queue_pop(input_queue_t *queue, input_event_t *event);
uint32_t input_queue_count(const input_queue_t *queue);

#endif
