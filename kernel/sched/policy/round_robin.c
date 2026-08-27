#include "round_robin.h"

task_t *scheduler_policy_next(task_wait_queue_t *queue, task_t *idle_task) {
    task_wait_node_t *node = task_wait_queue_dequeue(queue);
    if (!node) {
        if (idle_task) idle_task->state = TASK_RUNNING;
        return idle_task;
    }
    task_t *task = (task_t *)node->owner;
    task->state = TASK_RUNNING;
    return task;
}
