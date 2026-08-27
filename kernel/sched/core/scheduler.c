#include "scheduler.h"
#include "../policy/round_robin.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../../arch/x86_64/smp/percpu.h"

static task_wait_queue_t ready_queue;
static task_t *current_task;
static task_t *idle_task;
static task_context_t host_context;
static int host_active;
static int host_context_valid;
static int preemption_enabled;
static uint64_t preemptions;
static spinlock_t scheduler_lock;

void scheduler_initialize(void) {
    spinlock_init(&scheduler_lock);
    task_wait_queue_initialize(&ready_queue);
    current_task = 0; idle_task = 0; host_active = 0; host_context_valid = 0;
    preemption_enabled = 0; preemptions = 0;
}

int scheduler_enqueue(task_t *task) {
    if (!task) return 0;
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    if (task->state == TASK_TERMINATED ||
        !task_wait_queue_enqueue(&ready_queue, &task->wait_node)) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    task->state = TASK_READY;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return 1;
}

int scheduler_start_task(task_t *task) {
    if (!task) return 0;
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    if (task->state != TASK_READY ||
        task_wait_node_queued(&task->wait_node) ||
        !task_wait_queue_enqueue(&ready_queue, &task->wait_node)) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return 1;
}

int scheduler_remove(task_t *task) {
    if (!task) return 0;
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    int result = task != current_task && task_wait_node_queued(&task->wait_node) &&
                 task_wait_queue_remove(&ready_queue, &task->wait_node);
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return result;
}

task_t *scheduler_next(void) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    task_t *next = scheduler_policy_next(&ready_queue, idle_task);
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return next;
}

void scheduler_set_current(task_t *task) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    current_task = task;
    if (task) task->state = TASK_RUNNING;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    const arch_percpu_t *cpu = arch_percpu_current();
    if (task && cpu && task->stack && task->stack_size)
        arch_set_kernel_stack(cpu->logical_id,
                              ((uint64_t)(uintptr_t)task->stack + task->stack_size) & ~0xfULL);
}

void scheduler_set_idle(task_t *task) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    idle_task = task;
    if (task) task->state = TASK_READY;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
}

task_t *scheduler_current(void) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    task_t *task = current_task;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return task;
}

void scheduler_yield(void) {
    task_t *previous = scheduler_current();
    if (previous && !scheduler_enqueue(previous)) return;
    task_t *next = scheduler_next();
    if (!next) {
        if (previous) scheduler_set_current(previous);
        return;
    }
    scheduler_set_current(next);
    if (previous && next != previous)
        task_context_switch(&previous->context, &next->context);
}

void scheduler_enable_preemption(int enabled) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    preemption_enabled = enabled != 0;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
}

void scheduler_timer_interrupt(void) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    int enabled = preemption_enabled;
    task_t *previous = current_task;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    if (!enabled || !previous) return;
    if (!scheduler_enqueue(previous)) return;
    task_t *next = scheduler_next();
    if (!next) {
        scheduler_set_current(previous); return;
    }
    scheduler_set_current(next);
    if (next != previous) {
        flags = spinlock_lock_irqsave(&scheduler_lock);
        ++preemptions;
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        task_context_switch(&previous->context, &next->context);
        __asm__ volatile ("" ::: "memory");
    }
}

int scheduler_start(void) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    int active = host_active || current_task != 0;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    if (active) return 0;
    task_t *next = scheduler_next();
    if (!next || next == idle_task) return 0;
    flags = spinlock_lock_irqsave(&scheduler_lock);
    host_active = 1;
    host_context_valid = 1;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    scheduler_set_current(next);
    task_context_switch(&host_context, &next->context);
    host_active = 0;
    host_context_valid = 0;
    return 1;
}

__attribute__((noreturn)) void scheduler_task_exit(void) {
    task_t *finished = scheduler_current();
    if (!finished) for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    finished->state = TASK_TERMINATED;
    current_task = 0;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    task_t *next = scheduler_next();
    if (next) {
        scheduler_set_current(next);
        task_context_switch(&finished->context, &next->context);
    }
    if (host_context_valid) task_context_switch(&finished->context, &host_context);
    for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
}

int scheduler_block(task_t *task, task_wait_queue_t *queue) {
    if (!task || !queue) return 0;
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    if (task->state == TASK_TERMINATED || task_wait_node_queued(&task->wait_node)) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    if (!task_wait_queue_enqueue(queue, &task->wait_node)) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    task->state = TASK_BLOCKED;
    if (current_task == task) current_task = 0;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return 1;
}

int scheduler_block_current(task_wait_queue_t *queue) {
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    task_t *blocked = current_task;
    if (!blocked || blocked->state == TASK_TERMINATED ||
        task_wait_node_queued(&blocked->wait_node) || !queue ||
        !task_wait_queue_enqueue(queue, &blocked->wait_node)) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    blocked->state = TASK_BLOCKED;
    current_task = 0;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    task_t *next = scheduler_next();
    if (!next) {
        task_wait_queue_remove(queue, &blocked->wait_node);
        flags = spinlock_lock_irqsave(&scheduler_lock);
        blocked->state = TASK_RUNNING;
        current_task = blocked;
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }

    scheduler_set_current(next);
    task_context_switch(&blocked->context, &next->context);
    return 1;
}

int scheduler_block_current_with_lock(task_wait_queue_t *queue,
                                       spinlock_t *held_lock,
                                       uint64_t held_flags) {
    task_t *blocked = scheduler_current();
    if (!held_lock || !queue || !blocked) {
        if (held_lock) spinlock_unlock_irqrestore(held_lock, held_flags);
        return 0;
    }

    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    if (blocked->state == TASK_TERMINATED ||
        task_wait_node_queued(&blocked->wait_node) ||
        !task_wait_queue_enqueue(queue, &blocked->wait_node)) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        spinlock_unlock_irqrestore(held_lock, held_flags);
        return 0;
    }
    blocked->state = TASK_BLOCKED;
    if (current_task == blocked) current_task = 0;
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    spinlock_unlock_irqrestore(held_lock, held_flags);

    task_t *next = scheduler_next();
    if (!next) {
        (void)task_wait_queue_remove(queue, &blocked->wait_node);
        flags = spinlock_lock_irqsave(&scheduler_lock);
        blocked->state = TASK_RUNNING;
        current_task = blocked;
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    scheduler_set_current(next);
    task_context_switch(&blocked->context, &next->context);
    return 1;
}

task_t *scheduler_wake_one(task_wait_queue_t *queue) {
    if (!queue) return 0;
    uint64_t flags = spinlock_lock_irqsave(&scheduler_lock);
    task_wait_node_t *node = task_wait_queue_dequeue(queue);
    if (!node) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    task_t *task = (task_t *)node->owner;
    if (!task || task->state != TASK_BLOCKED) {
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    task->state = TASK_READY;
    if (!task_wait_queue_enqueue(&ready_queue, &task->wait_node)) {
        task->state = TASK_BLOCKED;
        (void)task_wait_queue_enqueue(queue, &task->wait_node);
        spinlock_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    spinlock_unlock_irqrestore(&scheduler_lock, flags);
    return task;
}

uint32_t scheduler_ready_count(void) { return task_wait_queue_count(&ready_queue); }
uint64_t scheduler_preemption_count(void) { return preemptions; }
