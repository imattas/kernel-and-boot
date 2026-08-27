#ifndef OS_KERNEL_ARCH_X86_64_MEMORY_PAGING_H
#define OS_KERNEL_ARCH_X86_64_MEMORY_PAGING_H

#include <stdint.h>

void x86_64_load_page_root(uint64_t address);

#endif
