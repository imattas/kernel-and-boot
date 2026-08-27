#include "thread.h"
#include "process.h"
#include "../../mm/heap/heap.h"
#include "../../sched/core/scheduler.h"

static process_thread_t *process_thread_lookup_locked(const struct process *process,
                                                       uint32_t id) {
    for (process_thread_t *thread = process->threads; thread; thread = thread->next)
        if (thread->task && thread->task->id == id) return thread;
    return 0;
}

process_thread_t *process_thread_create(struct process *process, uint32_t id,
                                        void (*entry)(void *), void *argument,
                                        uint64_t stack_size) {
    if (!process || id == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED ||
        process_thread_lookup_locked(process, id)) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    process_thread_t *thread = (process_thread_t *)kmalloc(sizeof(*thread));
    if (!thread) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    thread->task = task_create_kernel(id, entry, argument, stack_size);
    if (!thread->task) {
        kfree(thread);
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    thread->process = process;
    thread->next = process->threads;
    thread->references = 1;
    thread->retained_references = 0;
    process->threads = thread;
    ++process->thread_count;
    spinlock_unlock_irqrestore(&process->lock, flags);
    return thread;
}

process_thread_t *process_thread_create_user(struct process *process, uint32_t id,
                                             uint64_t entry, uint64_t user_stack,
                                             uint64_t kernel_stack_size) {
    if (!process || id == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state != PROCESS_READY ||
        process_thread_lookup_locked(process, id)) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    process_thread_t *thread = (process_thread_t *)kmalloc(sizeof(*thread));
    if (!thread) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    thread->task = task_create_user(id, &process->address_space, entry,
                                    user_stack, kernel_stack_size);
    if (!thread->task) {
        kfree(thread);
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    thread->process = process;
    thread->next = process->threads;
    thread->references = 1;
    thread->retained_references = 0;
    process->threads = thread;
    ++process->thread_count;
    spinlock_unlock_irqrestore(&process->lock, flags);
    return thread;
}

process_thread_t *process_thread_lookup(const struct process *process,
                                        uint32_t id) {
    if (!process || id == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave((spinlock_t *)&process->lock);
    process_thread_t *result = process_thread_lookup_locked(process, id);
    if (result && result->references != UINT32_MAX &&
        process->retained_thread_references != UINT32_MAX &&
        result->retained_references != UINT32_MAX) {
        ++result->references;
        ++result->retained_references;
        ++((struct process *)process)->retained_thread_references;
    } else if (result) result = 0;
    spinlock_unlock_irqrestore((spinlock_t *)&process->lock, flags);
    return result;
}

void process_thread_release(process_thread_t *thread) {
    if (!thread || !thread->process) return;
    process_t *process = thread->process;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (thread->retained_references != 0 && thread->references != 0) {
        --thread->retained_references;
        --thread->references;
        --process->retained_thread_references;
        if (thread->references == 0) kfree(thread);
    }
    spinlock_unlock_irqrestore(&process->lock, flags);
}

int process_thread_start(process_thread_t *thread) {
    if (!thread || !thread->process || !thread->task) return 0;
    process_t *process = thread->process;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    int result = scheduler_start_task(thread->task);
    spinlock_unlock_irqrestore(&process->lock, flags);
    return result;
}

static int process_thread_destroy_locked(process_thread_t *thread) {
    if (!thread || !thread->process || !thread->task) return 0;
    process_t *process = thread->process;
    process_thread_t **link = &process->threads;
    while (*link && *link != thread) link = &(*link)->next;
    if (!*link) return 0;
    if (task_wait_node_queued(&thread->task->wait_node) &&
        !scheduler_remove(thread->task)) return 0;
    if (!task_destroy_kernel(thread->task)) return 0;
    *link = thread->next;
    --process->thread_count;
    thread->next = 0;
    if (thread->references != 0) {
        --thread->references;
        if (thread->references == 0) kfree(thread);
    }
    return 1;
}

int process_thread_destroy(process_thread_t *thread) {
    if (!thread || !thread->process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&thread->process->lock);
    int result = process_thread_destroy_locked(thread);
    spinlock_unlock_irqrestore(&thread->process->lock, flags);
    return result;
}

int process_thread_destroy_all_locked(struct process *process) {
    if (!process) return 0;
    while (process->threads) {
        if (!process_thread_destroy_locked(process->threads)) return 0;
    }
    return 1;
}

int process_thread_destroy_all(struct process *process) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    int result = process_thread_destroy_all_locked(process);
    spinlock_unlock_irqrestore(&process->lock, flags);
    return result;
}
