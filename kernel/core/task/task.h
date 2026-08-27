#ifndef OS_CORE_TASK_TASK_H
#define OS_CORE_TASK_TASK_H

#include <stdint.h>
#include "context.h"
#include "wait_queue.h"

struct address_space;

typedef struct task {
    uint32_t id;
    task_state_t state;
    task_context_t context;
    task_wait_node_t wait_node;
    void *stack;
    uint64_t stack_size;
} task_t;

void task_initialize(task_t *task, uint32_t id);
task_t *task_create_kernel(uint32_t id, void (*entry)(void *), void *argument,
                           uint64_t stack_size);
task_t *task_create_user(uint32_t id, const struct address_space *space,
                          uint64_t entry, uint64_t user_stack,
                          uint64_t kernel_stack_size);
int task_destroy_kernel(task_t *task);

#endif
