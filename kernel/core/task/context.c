#include "context.h"

extern void arch_task_bootstrap(void);

void task_context_initialize(task_context_t *context, void *stack_top,
                             void (*entry)(void *), void *argument) {
    uint64_t top = (uint64_t)(uintptr_t)stack_top;
    context->rsp = (top & ~0xfULL) - 8;
    context->rip = (uint64_t)(uintptr_t)arch_task_bootstrap;
    context->rbx = 0;
    context->rbp = 0;
    context->r12 = (uint64_t)(uintptr_t)argument;
    context->r13 = (uint64_t)(uintptr_t)entry;
    context->r14 = 0;
    context->r15 = 0;
}
