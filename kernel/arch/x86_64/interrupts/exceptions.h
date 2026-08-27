#ifndef OS_X86_64_EXCEPTIONS_H
#define OS_X86_64_EXCEPTIONS_H
#include <stdint.h>
void arch_exception_panic(uint64_t vector);
#endif
