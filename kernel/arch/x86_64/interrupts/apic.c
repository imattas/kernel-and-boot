#include "apic.h"
#include "../../../drivers/acpi/acpi.h"

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
#define APIC_REG_TIMER_INITIAL 0x380
#define APIC_REG_TIMER_CURRENT 0x390
#define APIC_TIMER_PERIODIC (1u << 17)
#define APIC_TIMER_MASKED (1u << 16)
#define PIT_COMMAND 0x43
#define PIT_CHANNEL2 0x42
#define PIT_CONTROL 0x61
#define PIT_INPUT_HZ 1193182U
#define PIT_CALIBRATION_HZ 100U
#define APIC_CALIBRATION_ATTEMPTS 10000U

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

static void io_write8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t io_read8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t apic_timer_calibrate(void) {
    uint32_t pit_count = PIT_INPUT_HZ / PIT_CALIBRATION_HZ;
    uint8_t control = io_read8(PIT_CONTROL);
    control = (uint8_t)((control | 0x01U) & (uint8_t)~0x02U);
    io_write8(PIT_CONTROL, control);
    io_write8(PIT_COMMAND, 0xb0);
    io_write8(PIT_CHANNEL2, (uint8_t)pit_count);
    io_write8(PIT_CHANNEL2, (uint8_t)(pit_count >> 8));

    lapic_write(APIC_REG_TIMER_INITIAL, 0xffffffffU);
    uint32_t attempts = APIC_CALIBRATION_ATTEMPTS;
    while ((io_read8(PIT_CONTROL) & 0x20U) == 0 && attempts-- != 0)
        __asm__ volatile ("pause" ::: "memory");
    uint32_t elapsed = 0xffffffffU - lapic_read(APIC_REG_TIMER_CURRENT);
    lapic_write(APIC_REG_TIMER_INITIAL, 0);
    io_write8(PIT_CONTROL, control);
    if (attempts == 0 || elapsed < 1000U || elapsed > 0x0fffffffU) return 62500U;
    return elapsed;
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
    lapic_write(APIC_REG_LVT_TIMER, APIC_TIMER_VECTOR | APIC_TIMER_PERIODIC | APIC_TIMER_MASKED);
    uint32_t initial_count = apic_timer_calibrate();
    lapic_write(APIC_REG_LVT_TIMER, APIC_TIMER_VECTOR | APIC_TIMER_PERIODIC);
    lapic_write(APIC_REG_TIMER_INITIAL, initial_count);
    return 1;
}

int arch_ioapic_route_irq(uint8_t irq, uint8_t vector) {
    uint32_t gsi = acpi_irq_to_gsi(irq);
    uint64_t base = acpi_ioapic_base_for_gsi(gsi);
    if (!base || gsi == 0xffffffffU || vector < 0x20 || vector > 0xfe)
        return 0;
    volatile uint32_t *ioapic = (volatile uint32_t *)(uintptr_t)base;
    ioapic[0] = 1;
    uint32_t max_redirection = (ioapic[4] >> 16) & 0xffU;
    uint32_t gsi_base = acpi_ioapic_gsi_base_for_gsi(gsi);
    if (gsi < gsi_base || gsi - gsi_base > max_redirection) return 0;
    uint32_t index = 0x10U + (gsi - gsi_base) * 2U;
    uint32_t low = vector | (1U << 16);
    uint16_t flags = acpi_irq_flags(irq);
    if ((flags & 0x3U) == 3U) low |= 1U << 13;
    if ((flags & 0xcU) == 0xcU) low |= 1U << 15;
    ioapic[0] = index + 1U;
    ioapic[4] = 0;
    ioapic[0] = index;
    ioapic[4] = low;
    ioapic[0] = index;
    ioapic[4] = low & ~(1U << 16);
    return 1;
}
