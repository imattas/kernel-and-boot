#ifndef OS_X86_64_TABLES_H
#define OS_X86_64_TABLES_H

#include <stdint.h>

void arch_init_tables(void);
void arch_init_tables_for_cpu(uint32_t logical_id);
void arch_set_interrupt_gate(uint8_t vector, void (*handler)(void));
void arch_set_user_interrupt_gate(uint8_t vector, void (*handler)(void));
uint16_t arch_user_code_selector(void);
uint16_t arch_user_data_selector(void);

#endif
