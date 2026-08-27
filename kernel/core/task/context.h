#ifndef OS_CORE_TASK_CONTEXT_H
#define OS_CORE_TASK_CONTEXT_H

#include <stdint.h>

typedef struct {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} task_context_t;

void task_context_initialize(task_context_t *context, void *stack_top,
                             void (*entry)(void *), void *argument);
void task_context_switch(task_context_t *from, task_context_t *to);

#endif
