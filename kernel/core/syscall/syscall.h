#ifndef OS_CORE_SYSCALL_H
#define OS_CORE_SYSCALL_H

#include <stdint.h>

void syscall_initialize(void);
uint64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3);
int syscall_copy_from_user(void *destination, uint64_t source, uint64_t size);
int syscall_copy_to_user(uint64_t destination, const void *source, uint64_t size);
void arch_enter_user(uint64_t entry, uint64_t stack);

#endif
