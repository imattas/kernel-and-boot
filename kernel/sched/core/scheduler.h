#ifndef OS_KERNEL_SCHED_CORE_H
#define OS_KERNEL_SCHED_CORE_H

#include "../../core/task/task.h"

void scheduler_initialize(void);
int scheduler_enqueue(task_t *task);
int scheduler_remove(task_t *task);
task_t *scheduler_next(void);
void scheduler_set_current(task_t *task);
void scheduler_set_idle(task_t *task);
task_t *scheduler_current(void);
void scheduler_yield(void);
void scheduler_enable_preemption(int enabled);
void scheduler_timer_interrupt(void);
int scheduler_start(void);
__attribute__((noreturn)) void scheduler_task_exit(void);
uint64_t scheduler_preemption_count(void);
int scheduler_block(task_t *task, task_wait_queue_t *queue);
int scheduler_block_current(task_wait_queue_t *queue);
int scheduler_block_current_with_lock(task_wait_queue_t *queue,
                                       spinlock_t *held_lock,
                                       uint64_t held_flags);
task_t *scheduler_wake_one(task_wait_queue_t *queue);
uint32_t scheduler_ready_count(void);

#endif
