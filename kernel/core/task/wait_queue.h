#ifndef OS_CORE_TASK_WAIT_QUEUE_H
#define OS_CORE_TASK_WAIT_QUEUE_H

#include <stdint.h>
#include "../sync/spinlock.h"

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct task_wait_node {
    struct task_wait_node *previous;
    struct task_wait_node *next;
    void *owner;
    uint8_t queued;
} task_wait_node_t;

typedef struct {
    spinlock_t lock;
    task_wait_node_t *head;
    task_wait_node_t *tail;
    uint32_t count;
} task_wait_queue_t;

void task_wait_queue_initialize(task_wait_queue_t *queue);
int task_wait_queue_enqueue(task_wait_queue_t *queue, task_wait_node_t *node);
task_wait_node_t *task_wait_queue_dequeue(task_wait_queue_t *queue);
int task_wait_queue_remove(task_wait_queue_t *queue, task_wait_node_t *node);
uint32_t task_wait_queue_count(const task_wait_queue_t *queue);

#endif
