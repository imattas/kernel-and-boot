#ifndef OS_KERNEL_TIME_CLOCK_H
#define OS_KERNEL_TIME_CLOCK_H

#include <stdint.h>

uint64_t clock_monotonic_ns(void);
uint64_t clock_ticks(void);
uint64_t clock_cpu_ticks(uint32_t logical_id);
uint64_t clock_frequency_hz(void);

#endif
