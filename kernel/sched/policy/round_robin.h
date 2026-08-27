#ifndef OS_KERNEL_SCHED_POLICY_ROUND_ROBIN_H
#define OS_KERNEL_SCHED_POLICY_ROUND_ROBIN_H

#include "../../core/task/task.h"

task_t *scheduler_policy_next(task_wait_queue_t *queue, task_t *idle_task);

#endif
