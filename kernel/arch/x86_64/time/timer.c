#include <stdint.h>
#include "timer.h"
#include "../interrupts/apic.h"
#include "../smp/percpu.h"
#include "../../../core/task/scheduler.h"

static volatile uint64_t ticks;
static volatile uint64_t cpu_ticks[64];
#define TIMER_FREQUENCY_HZ 100U
#define TIMER_NS_PER_TICK (1000000000ULL / TIMER_FREQUENCY_HZ)

void timer_tick(void) {
    const arch_percpu_t *cpu = arch_percpu_current();
    if (!cpu || cpu->logical_id >= 64) return;
    __atomic_fetch_add(&cpu_ticks[cpu->logical_id], 1, __ATOMIC_RELAXED);
    if (cpu->logical_id == 0) __atomic_fetch_add(&ticks, 1, __ATOMIC_RELAXED);
}
void arch_scheduler_timer_interrupt(void) {
    const arch_percpu_t *cpu = arch_percpu_current();
    if (cpu && cpu->logical_id == 0) scheduler_timer_interrupt();
}
uint64_t timer_ticks(void) { return __atomic_load_n(&ticks, __ATOMIC_RELAXED); }
uint64_t timer_cpu_ticks(uint32_t logical_id) {
    return logical_id < 64 ?
        __atomic_load_n(&cpu_ticks[logical_id], __ATOMIC_RELAXED) : 0;
}
uint32_t timer_frequency_hz(void) { return TIMER_FREQUENCY_HZ; }
uint64_t timer_now_ns(void) {
    uint64_t count = timer_ticks();
    if (count > UINT64_MAX / TIMER_NS_PER_TICK) return UINT64_MAX;
    return count * TIMER_NS_PER_TICK;
}
void timer_wait(uint64_t target) {
    if (target > (uint64_t)INT64_MAX) target = (uint64_t)INT64_MAX;
    uint64_t start = timer_ticks();
    uint64_t deadline = start + target;
    while ((int64_t)(timer_ticks() - deadline) < 0) {
        __asm__ volatile ("hlt" ::: "memory");
    }
}
