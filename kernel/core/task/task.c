#include "task.h"
#include "../../mm/heap/heap.h"

void task_initialize(task_t *task, uint32_t id) {
    task->id = id;
    task->state = TASK_READY;
    task->context = (task_context_t){0};
    task->wait_node = (task_wait_node_t){0};
    task->wait_node.owner = task;
    task->stack = 0;
    task->stack_size = 0;
}

task_t *task_create_kernel(uint32_t id, void (*entry)(void *), void *argument,
                           uint64_t stack_size) {
    if (!entry || stack_size < 4096) return 0;
    task_t *task = (task_t *)kmalloc(sizeof(*task));
    if (!task) return 0;
    void *stack = kmalloc(stack_size);
    if (!stack) {
        kfree(task);
        return 0;
    }
    task_initialize(task, id);
    task->stack = stack;
    task->stack_size = stack_size;
    task_context_initialize(&task->context, (uint8_t *)stack + stack_size,
                            entry, argument);
    return task;
}

int task_destroy_kernel(task_t *task) {
    if (!task || task->wait_node.queued || task->state == TASK_RUNNING) return 0;
    kfree(task->stack);
    kfree(task);
    return 1;
}
