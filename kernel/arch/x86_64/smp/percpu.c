#include "percpu.h"
#include "../interrupts/apic.h"
#include "../platform/acpi.h"
#include "../cpu/tables.h"
#include "../../../core/printk/serial.h"

#define SMP_TRAMPOLINE_ADDRESS 0x8000ULL
#define SMP_TRAMPOLINE_VECTOR 0x08
#define SMP_STACK_SIZE 16384

extern uint8_t smp_trampoline_start[];
extern uint8_t smp_trampoline_end[];
extern uint8_t smp_trampoline_gdt_base[];
extern uint8_t smp_trampoline_gdt_start[];
extern uint8_t smp_trampoline_pml4[];
extern uint8_t smp_trampoline_stack[];
extern uint8_t smp_trampoline_entry[];
extern uint8_t smp_trampoline_argument[];
extern void smp_ap_entry(uint32_t apic_id);
extern uint64_t virtual_memory_root(void);
extern int arch_apic_startup(uint32_t apic_id, uint8_t vector);

static arch_percpu_t cpus[64];
static uint32_t present_count;
static uint32_t online_count;
static arch_percpu_t *current;
static uint8_t ap_stacks[64][SMP_STACK_SIZE] __attribute__((aligned(16)));
static uint32_t interrupts_released;

int arch_percpu_initialize(void) {
    present_count = acpi_cpu_count();
    online_count = 0;
    current = 0;
    interrupts_released = 0;
    if (present_count == 0 || present_count > 64) return 0;
    uint32_t bsp_id = arch_apic_id();
    for (uint32_t i = 0; i < present_count; ++i) {
        cpus[i].apic_id = acpi_cpu_apic_id(i);
        cpus[i].logical_id = i;
        cpus[i].present = 1;
        __atomic_store_n(&cpus[i].online, cpus[i].apic_id == bsp_id, __ATOMIC_RELAXED);
        if (__atomic_load_n(&cpus[i].online, __ATOMIC_RELAXED)) current = &cpus[i];
    }
    if (!current) return 0;
    online_count = 1;
    return 1;
}

const arch_percpu_t *arch_percpu_current(void) {
    uint32_t apic_id = arch_apic_id();
    for (uint32_t i = 0; i < present_count; ++i)
        if (cpus[i].apic_id == apic_id) return &cpus[i];
    return 0;
}
uint32_t arch_percpu_present_count(void) { return present_count; }
uint32_t arch_percpu_online_count(void) { return online_count; }

void arch_percpu_release_interrupts(void) {
    __atomic_store_n(&interrupts_released, 1, __ATOMIC_RELEASE);
}

int arch_percpu_interrupts_released(void) {
    return __atomic_load_n(&interrupts_released, __ATOMIC_ACQUIRE) != 0;
}

void arch_percpu_ap_entry(uint32_t apic_id) {
    for (uint32_t i = 0; i < present_count; ++i) {
        if (cpus[i].apic_id == apic_id) {
            __atomic_store_n(&cpus[i].online, 1, __ATOMIC_RELEASE);
            __atomic_fetch_add(&online_count, 1, __ATOMIC_RELEASE);
            break;
        }
    }
}

static void copy_trampoline(uint32_t apic_id, uint32_t cpu_index) {
    uint8_t *destination = (uint8_t *)(uintptr_t)SMP_TRAMPOLINE_ADDRESS;
    uint64_t size = (uint64_t)(smp_trampoline_end - smp_trampoline_start);
    for (uint64_t i = 0; i < size; ++i) destination[i] = smp_trampoline_start[i];
    *(uint32_t *)(destination + (smp_trampoline_gdt_base - smp_trampoline_start)) =
        (uint32_t)(SMP_TRAMPOLINE_ADDRESS + (smp_trampoline_gdt_start - smp_trampoline_start));
    *(uint64_t *)(destination + (smp_trampoline_pml4 - smp_trampoline_start)) = virtual_memory_root();
    *(uint64_t *)(destination + (smp_trampoline_stack - smp_trampoline_start)) =
        (uint64_t)(uintptr_t)&ap_stacks[cpu_index][SMP_STACK_SIZE];
    *(uint64_t *)(destination + (smp_trampoline_entry - smp_trampoline_start)) = (uint64_t)(uintptr_t)smp_ap_entry;
    *(uint64_t *)(destination + (smp_trampoline_argument - smp_trampoline_start)) = apic_id;
}

int arch_percpu_bringup(void) {
    for (uint32_t i = 0; i < present_count; ++i) {
        if (__atomic_load_n(&cpus[i].online, __ATOMIC_ACQUIRE)) continue;
        int online = 0;
        for (uint32_t retry = 0; retry < 3 && !online; ++retry) {
            copy_trampoline(cpus[i].apic_id, i);
            if (!arch_apic_startup(cpus[i].apic_id, SMP_TRAMPOLINE_VECTOR)) continue;
            uint32_t attempts = 10000000;
            while (!__atomic_load_n(&cpus[i].online, __ATOMIC_ACQUIRE) && attempts-- != 0)
                __asm__ volatile ("pause");
            online = __atomic_load_n(&cpus[i].online, __ATOMIC_ACQUIRE);
        }
        if (!online) return 0;
    }
    return 1;
}

void smp_ap_entry(uint32_t apic_id) {
    serial_write("AP entered\r\n");
    const arch_percpu_t *cpu = arch_percpu_current();
    arch_init_tables_for_cpu(cpu ? cpu->logical_id : 0);
    if (!arch_apic_timer_initialize()) {
        serial_write("AP timer initialization failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    arch_percpu_ap_entry(apic_id);
    serial_write_hex_line("AP online id=", apic_id);
    while (!arch_percpu_interrupts_released())
        __asm__ volatile ("pause");
    __asm__ volatile ("sti" ::: "memory");
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}
