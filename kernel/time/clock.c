#include "clock.h"
#include "../arch/x86_64/time/timer.h"

uint64_t clock_monotonic_ns(void) {
    return timer_now_ns();
}

uint64_t clock_ticks(void) {
    return timer_ticks();
}

uint64_t clock_frequency_hz(void) {
    return timer_frequency_hz();
}
