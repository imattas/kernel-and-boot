#include "task.h"
#include "../../mm/heap/heap.h"
#include "../../mm/virtual/address_space.h"
#include "../../sched/core/scheduler.h"
#include "../process/process.h"

extern void arch_enter_user(uint64_t entry, uint64_t user_stack);

__attribute__((noreturn)) void arch_user_task_start(
    struct process *process, const struct address_space *space,
    uint64_t entry, uint64_t user_stack) {
    (void)space;
    if (!process_activate(process))
        scheduler_task_exit();
    arch_enter_user(entry, user_stack);
    scheduler_task_exit();
}

void task_initialize(task_t *task, uint32_t id) {
    task->id = id;
    task->state = TASK_READY;
    task->context = (task_context_t){0};
    task_wait_node_initialize(&task->wait_node, task);
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

task_t *task_create_user(uint32_t id, struct process *process,
                         const struct address_space *space,
                         uint64_t entry, uint64_t user_stack,
                         uint64_t kernel_stack_size) {
    if (!process || !space || kernel_stack_size < 4096 ||
        entry < (1ULL << 39) || entry >= (1ULL << 48) ||
        !address_space_page_executable(space, entry & ~0xfffULL) ||
        user_stack <= (1ULL << 39) || user_stack > (1ULL << 48) ||
        !address_space_user_range_valid(space, user_stack - 1, 1, 1))
        return 0;
    task_t *task = (task_t *)kmalloc(sizeof(*task));
    if (!task) return 0;
    void *stack = kmalloc(kernel_stack_size);
    if (!stack) {
        kfree(task);
        return 0;
    }
    task_initialize(task, id);
    task->stack = stack;
    task->stack_size = kernel_stack_size;
    task_context_initialize_user(&task->context,
                                 (uint8_t *)stack + kernel_stack_size,
                                 process, space, entry, user_stack);
    return task;
}

int task_destroy_kernel(task_t *task) {
    if (!task || task_wait_node_queued(&task->wait_node) ||
        task->state == TASK_RUNNING) return 0;
    kfree(task->stack);
    kfree(task);
    return 1;
}
