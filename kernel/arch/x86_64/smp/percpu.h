#ifndef OS_X86_64_SMP_PERCPU_H
#define OS_X86_64_SMP_PERCPU_H

#include <stdint.h>

typedef struct {
    uint32_t apic_id;
    uint32_t logical_id;
    uint8_t present;
    uint8_t online;
} arch_percpu_t;

int arch_percpu_initialize(void);
const arch_percpu_t *arch_percpu_current(void);
uint32_t arch_percpu_present_count(void);
uint32_t arch_percpu_online_count(void);
int arch_percpu_bringup(void);
void arch_percpu_ap_entry(uint32_t apic_id);
void arch_percpu_release_interrupts(void);
int arch_percpu_interrupts_released(void);

#endif
