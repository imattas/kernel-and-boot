#ifndef OS_CORE_PROCESS_THREAD_H
#define OS_CORE_PROCESS_THREAD_H

#include <stdint.h>
#include "../task/task.h"

struct process;

typedef struct process_thread {
    struct process *process;
    task_t *task;
    struct process_thread *next;
    uint32_t references;
} process_thread_t;

process_thread_t *process_thread_create(struct process *process, uint32_t id,
                                        void (*entry)(void *), void *argument,
                                        uint64_t stack_size);
process_thread_t *process_thread_lookup(const struct process *process,
                                        uint32_t id);
void process_thread_release(process_thread_t *thread);
int process_thread_start(process_thread_t *thread);
int process_thread_destroy(process_thread_t *thread);
int process_thread_destroy_all(struct process *process);
int process_thread_destroy_all_locked(struct process *process);

#endif
