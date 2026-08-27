#include "apic.h"

#define CPUID_FEAT_EDX_APIC (1u << 9)
#define IA32_APIC_BASE 0x1b
#define APIC_BASE_ENABLE (1ull << 11)
#define APIC_BASE_X2APIC (1ull << 10)
#define APIC_REG_ID 0x020
#define APIC_REG_TPR 0x080
#define APIC_REG_EOI 0x0b0
#define APIC_REG_SVR 0x0f0
#define APIC_REG_LVT_TIMER 0x320
#define APIC_REG_LVT_THERMAL 0x330
#define APIC_REG_LVT_PERF 0x340
#define APIC_REG_LVT_ERROR 0x370
#define APIC_REG_TIMER_DIVIDE 0x3e0
#define APIC_TIMER_VECTOR 0x20
#define APIC_REG_ICR_LOW 0x300
#define APIC_REG_ICR_HIGH 0x310
#define APIC_ICR_DELIVERY_STATUS (1u << 12)

static volatile uint32_t *lapic;

static uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

static void lapic_write(uint32_t reg, uint32_t value) {
    lapic[reg / sizeof(uint32_t)] = value;
}

static uint32_t lapic_read(uint32_t reg) {
    return lapic[reg / sizeof(uint32_t)];
}

static void apic_delay(void) {
    for (volatile uint32_t attempts = 0; attempts < 1000; ++attempts)
        __asm__ volatile ("outb %%al, $0x80" :: "a"(0));
}

static int apic_wait_for_delivery(void) {
    for (uint32_t attempts = 0; attempts < 100000; ++attempts) {
        if ((lapic_read(APIC_REG_ICR_LOW) & APIC_ICR_DELIVERY_STATUS) == 0)
            return 1;
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

int arch_apic_initialize(const arch_cpu_info_t *cpu) {
    if (!cpu || (cpu->features_edx & CPUID_FEAT_EDX_APIC) == 0) return 0;
    uint64_t base = read_msr(IA32_APIC_BASE);
    if ((base & APIC_BASE_X2APIC) != 0) return 0;
    base = (base & 0xfffff000ULL) | APIC_BASE_ENABLE;
    write_msr(IA32_APIC_BASE, base);
    lapic = (volatile uint32_t *)(uintptr_t)(base & 0xfffff000ULL);
    lapic_write(APIC_REG_TPR, 0);
    lapic_write(APIC_REG_LVT_TIMER, 0x00010000);
    lapic_write(APIC_REG_LVT_THERMAL, 0x00010000);
    lapic_write(APIC_REG_LVT_PERF, 0x00010000);
    lapic_write(APIC_REG_LVT_ERROR, 0x00010000);
    lapic_write(APIC_REG_SVR, 0x000001ff);
    return (lapic_read(APIC_REG_SVR) & 0x100) != 0;
}

uint32_t arch_apic_id(void) {
    return lapic ? lapic_read(APIC_REG_ID) >> 24 : 0;
}

void arch_apic_eoi(void) {
    if (lapic) lapic_write(APIC_REG_EOI, 0);
}

int arch_apic_startup(uint32_t apic_id, uint8_t vector) {
    if (!lapic || vector == 0 || (vector & 1) != 0) return 0;
    lapic_write(APIC_REG_ICR_HIGH, apic_id << 24);
    lapic_write(APIC_REG_ICR_LOW, 0x00004500);
    if (!apic_wait_for_delivery()) return 0;
    apic_delay();
    lapic_write(APIC_REG_ICR_LOW, 0x00008500);
    if (!apic_wait_for_delivery()) return 0;
    apic_delay();
    lapic_write(APIC_REG_ICR_LOW, 0x00000600 | vector);
    if (!apic_wait_for_delivery()) return 0;
    apic_delay();
    lapic_write(APIC_REG_ICR_LOW, 0x00000600 | vector);
    return apic_wait_for_delivery();
}

int arch_apic_timer_initialize(void) {
    if (!lapic) return 0;
    lapic_write(APIC_REG_TIMER_DIVIDE, 0x3);
    lapic_write(APIC_REG_LVT_TIMER, APIC_TIMER_VECTOR | (1u << 17));
    lapic_write(0x380, 62500);
    return 1;
}
