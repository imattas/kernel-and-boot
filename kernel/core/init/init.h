#ifndef OS_KERNEL_CORE_INIT_H
#define OS_KERNEL_CORE_INIT_H
#include <stdint.h>
typedef enum { KERNEL_INIT_RESET = 0, KERNEL_INIT_EARLY, KERNEL_INIT_MEMORY,
    KERNEL_INIT_PLATFORM, KERNEL_INIT_DRIVERS, KERNEL_INIT_SERVICES } kernel_init_stage_t;
typedef struct { kernel_init_stage_t stage; } kernel_init_state_t;
static inline void kernel_init_state_initialize(kernel_init_state_t *state) {
    if (state) state->stage = KERNEL_INIT_RESET;
}
static inline int kernel_init_state_advance(kernel_init_state_t *state,
                                            kernel_init_stage_t next) {
    if (!state || next <= state->stage || next > KERNEL_INIT_SERVICES) return 0;
    state->stage = next; return 1;
}
#endif
