#include <stdint.h>
#include "timer.h"
#include "../interrupts/apic.h"
#include "../../../core/task/scheduler.h"

static volatile uint64_t ticks;
#define TIMER_FREQUENCY_HZ 100U
#define TIMER_NS_PER_TICK (1000000000ULL / TIMER_FREQUENCY_HZ)

void timer_tick(void) {
    if (arch_apic_id() == 0) ++ticks;
}
void arch_scheduler_timer_interrupt(void) {
    if (arch_apic_id() == 0) scheduler_timer_interrupt();
}
uint64_t timer_ticks(void) { return __atomic_load_n(&ticks, __ATOMIC_RELAXED); }
uint32_t timer_frequency_hz(void) { return TIMER_FREQUENCY_HZ; }
uint64_t timer_now_ns(void) {
    uint64_t count = timer_ticks();
    if (count > UINT64_MAX / TIMER_NS_PER_TICK) return UINT64_MAX;
    return count * TIMER_NS_PER_TICK;
}
void timer_wait(uint64_t target) {
    uint64_t start = timer_ticks();
    uint64_t deadline = start + target;
    if (deadline < start) deadline = UINT64_MAX;
    while ((int64_t)(timer_ticks() - deadline) < 0) {
        __asm__ volatile ("hlt" ::: "memory");
    }
}
