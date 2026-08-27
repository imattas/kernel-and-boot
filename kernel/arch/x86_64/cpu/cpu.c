#include <stdint.h>
#include "cpu.h"
static arch_cpu_info_t info;
static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(subleaf));
}
const arch_cpu_info_t *arch_cpu_initialize(void) {
    uint32_t a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d); info.maximum_basic_leaf = a;
    *(uint32_t *)&info.vendor[0] = b; *(uint32_t *)&info.vendor[4] = d; *(uint32_t *)&info.vendor[8] = c; info.vendor[12] = 0;
    if (a >= 1) { cpuid(1, 0, &a, &b, &c, &d); info.features_ecx = c; info.features_edx = d; }
    cpuid(0x80000000, 0, &a, &b, &c, &d); info.maximum_extended_leaf = a;
    return &info;
}
