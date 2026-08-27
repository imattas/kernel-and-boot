#include "scheduler.h"
#include "../policy/round_robin.h"

static task_wait_queue_t ready_queue;
static task_t *current_task;
static task_t *idle_task;
static task_context_t host_context;
static int host_active;
static int preemption_enabled;
static uint64_t preemptions;

void scheduler_initialize(void) {
    task_wait_queue_initialize(&ready_queue);
    current_task = 0; idle_task = 0; host_active = 0;
    preemption_enabled = 0; preemptions = 0;
}

int scheduler_enqueue(task_t *task) {
    if (!task || task->state == TASK_TERMINATED) return 0;
    if (!task_wait_queue_enqueue(&ready_queue, &task->wait_node)) return 0;
    task->state = TASK_READY;
    return 1;
}

int scheduler_remove(task_t *task) {
    if (!task || task == current_task || !task->wait_node.queued) return 0;
    return task_wait_queue_remove(&ready_queue, &task->wait_node);
}

task_t *scheduler_next(void) {
    return scheduler_policy_next(&ready_queue, idle_task);
}

void scheduler_set_current(task_t *task) {
    current_task = task;
    if (task) task->state = TASK_RUNNING;
}

void scheduler_set_idle(task_t *task) {
    idle_task = task;
    if (task) task->state = TASK_READY;
}

task_t *scheduler_current(void) { return current_task; }

void scheduler_yield(void) {
    task_t *previous = current_task;
    if (previous && !scheduler_enqueue(previous)) return;
    task_t *next = scheduler_next();
    if (!next) {
        if (previous) { previous->state = TASK_RUNNING; current_task = previous; }
        return;
    }
    current_task = next;
    if (previous && next != previous)
        task_context_switch(&previous->context, &next->context);
}

void scheduler_enable_preemption(int enabled) { preemption_enabled = enabled != 0; }

void scheduler_timer_interrupt(void) {
    if (!preemption_enabled || !current_task) return;
    task_t *previous = current_task;
    if (!scheduler_enqueue(previous)) return;
    task_t *next = scheduler_next();
    if (!next) {
        previous->state = TASK_RUNNING; current_task = previous; return;
    }
    current_task = next;
    if (next != previous) {
        ++preemptions;
        task_context_switch(&previous->context, &next->context);
        __asm__ volatile ("" ::: "memory");
    }
}

int scheduler_start(void) {
    if (host_active || current_task) return 0;
    task_t *next = scheduler_next();
    if (!next || next == idle_task) return 0;
    host_active = 1; current_task = next;
    task_context_switch(&host_context, &next->context);
    host_active = 0;
    return 1;
}

__attribute__((noreturn)) void scheduler_task_exit(void) {
    task_t *finished = current_task;
    if (!finished) for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    finished->state = TASK_TERMINATED; current_task = 0;
    task_t *next = scheduler_next();
    if (next) {
        current_task = next;
        task_context_switch(&finished->context, &next->context);
    }
    if (host_active) task_context_switch(&finished->context, &host_context);
    for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
}

int scheduler_block(task_t *task, task_wait_queue_t *queue) {
    if (!task || !queue || task->state == TASK_TERMINATED || task->wait_node.queued)
        return 0;
    if (!task_wait_queue_enqueue(queue, &task->wait_node)) return 0;
    task->state = TASK_BLOCKED;
    if (current_task == task) current_task = 0;
    return 1;
}

int scheduler_block_current(task_wait_queue_t *queue) {
    task_t *blocked = current_task;
    if (!blocked || blocked->state == TASK_TERMINATED ||
        blocked->wait_node.queued || !queue ||
        !task_wait_queue_enqueue(queue, &blocked->wait_node)) return 0;

    blocked->state = TASK_BLOCKED;
    task_t *next = scheduler_next();
    if (!next) {
        task_wait_queue_remove(queue, &blocked->wait_node);
        blocked->state = TASK_RUNNING;
        return 0;
    }

    current_task = next;
    task_context_switch(&blocked->context, &next->context);
    return 1;
}

task_t *scheduler_wake_one(task_wait_queue_t *queue) {
    task_wait_node_t *node = task_wait_queue_dequeue(queue);
    if (!node) return 0;
    task_t *task = (task_t *)node->owner;
    if (task->state != TASK_BLOCKED) return 0;
    task->state = TASK_READY;
    if (!task_wait_queue_enqueue(&ready_queue, &task->wait_node)) return 0;
    return task;
}

uint32_t scheduler_ready_count(void) { return task_wait_queue_count(&ready_queue); }
uint64_t scheduler_preemption_count(void) { return preemptions; }
