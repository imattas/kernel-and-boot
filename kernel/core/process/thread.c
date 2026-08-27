#include "thread.h"
#include "process.h"
#include "../../mm/heap/heap.h"
#include "../../sched/core/scheduler.h"

process_thread_t *process_thread_create(struct process *process, uint32_t id,
                                        void (*entry)(void *), void *argument,
                                        uint64_t stack_size) {
    if (!process || id == 0 || process->state == PROCESS_EXITED ||
        process_thread_lookup(process, id)) return 0;
    process_thread_t *thread = (process_thread_t *)kmalloc(sizeof(*thread));
    if (!thread) return 0;
    thread->task = task_create_kernel(id, entry, argument, stack_size);
    if (!thread->task) {
        kfree(thread);
        return 0;
    }
    thread->process = process;
    thread->next = process->threads;
    process->threads = thread;
    ++process->thread_count;
    return thread;
}

process_thread_t *process_thread_lookup(const struct process *process,
                                        uint32_t id) {
    if (!process || id == 0) return 0;
    for (process_thread_t *thread = process->threads; thread; thread = thread->next)
        if (thread->task && thread->task->id == id) return thread;
    return 0;
}

int process_thread_start(process_thread_t *thread) {
    if (!thread || !thread->process || !thread->task ||
        thread->process->state == PROCESS_EXITED ||
        thread->task->state != TASK_READY || thread->task->wait_node.queued)
        return 0;
    return scheduler_enqueue(thread->task);
}

int process_thread_destroy(process_thread_t *thread) {
    if (!thread || !thread->process || !thread->task) return 0;
    process_t *process = thread->process;
    process_thread_t **link = &process->threads;
    while (*link && *link != thread) link = &(*link)->next;
    if (!*link) return 0;
    if (thread->task->wait_node.queued &&
        !scheduler_remove(thread->task)) return 0;
    if (!task_destroy_kernel(thread->task)) return 0;
    *link = thread->next;
    --process->thread_count;
    kfree(thread);
    return 1;
}

int process_thread_destroy_all(struct process *process) {
    if (!process) return 0;
    while (process->threads) {
        if (!process_thread_destroy(process->threads)) return 0;
    }
    return 1;
}
