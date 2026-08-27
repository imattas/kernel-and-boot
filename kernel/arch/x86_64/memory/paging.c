#include "paging.h"

void x86_64_load_page_root(uint64_t address) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(address) : "memory");
}
