#ifndef OS_X86_64_TIMER_H
#define OS_X86_64_TIMER_H

#include <stdint.h>

void timer_tick(void);
uint64_t timer_ticks(void);
uint64_t timer_now_ns(void);
uint32_t timer_frequency_hz(void);
void timer_wait(uint64_t ticks);

#endif
