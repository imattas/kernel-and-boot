#ifndef OS_X86_64_APIC_H
#define OS_X86_64_APIC_H

#include <stdint.h>
#include "../cpu/cpu.h"

int arch_apic_initialize(const arch_cpu_info_t *cpu);
uint32_t arch_apic_id(void);
void arch_apic_eoi(void);
int arch_apic_startup(uint32_t apic_id, uint8_t vector);
int arch_apic_timer_initialize(void);

#endif
