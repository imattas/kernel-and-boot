#ifndef OS_KERNEL_DRIVERS_ACPI_H
#define OS_KERNEL_DRIVERS_ACPI_H

#include <stdint.h>

int acpi_initialize(uint64_t rsdp_address);
uint32_t acpi_cpu_count(void);
uint32_t acpi_cpu_apic_id(uint32_t index);
uint64_t acpi_lapic_base(void);

#endif
