#include "wait_queue.h"

void task_wait_queue_initialize(task_wait_queue_t *queue) {
    spinlock_init(&queue->lock);
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

int task_wait_queue_enqueue(task_wait_queue_t *queue, task_wait_node_t *node) {
    if (!queue || !node) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    if (node->queued) {
        spinlock_unlock_irqrestore(&queue->lock, flags);
        return 0;
    }
    node->previous = queue->tail;
    node->next = 0;
    node->queue = queue;
    node->queued = 1;
    if (queue->tail) queue->tail->next = node;
    else queue->head = node;
    queue->tail = node;
    ++queue->count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return 1;
}

static void unlink_node(task_wait_queue_t *queue, task_wait_node_t *node) {
    if (node->previous) node->previous->next = node->next;
    else queue->head = node->next;
    if (node->next) node->next->previous = node->previous;
    else queue->tail = node->previous;
    node->previous = 0;
    node->next = 0;
    node->queue = 0;
    node->queued = 0;
    --queue->count;
}

task_wait_node_t *task_wait_queue_dequeue(task_wait_queue_t *queue) {
    if (!queue) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    task_wait_node_t *node = queue->head;
    if (node) unlink_node(queue, node);
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return node;
}

int task_wait_queue_remove(task_wait_queue_t *queue, task_wait_node_t *node) {
    if (!queue || !node) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    int removed = node->queued && node->queue == queue;
    if (removed) unlink_node(queue, node);
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return removed;
}

uint32_t task_wait_queue_count(const task_wait_queue_t *queue) {
    return queue ? __atomic_load_n(&queue->count, __ATOMIC_ACQUIRE) : 0;
}
