#include "acpi.h"

typedef struct {
    char signature[8]; uint8_t checksum; char oem_id[6]; uint8_t revision;
    uint32_t rsdt_address; uint32_t length; uint64_t xsdt_address;
    uint8_t extended_checksum; uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char signature[4]; uint32_t length; uint8_t revision; uint8_t checksum;
    uint8_t body[26];
} __attribute__((packed)) acpi_header_t;

typedef struct {
    acpi_header_t header; uint32_t lapic_address; uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

static uint32_t enabled_cpus;
static uint32_t apic_ids[64];
static uint64_t local_apic_base;
static uint64_t io_apic_base;
static uint32_t io_apic_gsi_base;
static uint32_t irq_gsi[16];
static uint16_t irq_flags[16];
static uint8_t irq_override[16];

static int checksum_ok(const void *address, uint32_t length) {
    const uint8_t *bytes = address; uint8_t checksum = 0;
    for (uint32_t i = 0; i < length; ++i) checksum = (uint8_t)(checksum + bytes[i]);
    return checksum == 0;
}

static int signature_is(const char *actual, const char *expected, uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) if (actual[i] != expected[i]) return 0;
    return 1;
}

int acpi_initialize(uint64_t rsdp_address) {
    enabled_cpus = 0; local_apic_base = 0; io_apic_base = 0; io_apic_gsi_base = 0;
    for (uint32_t i = 0; i < 64; ++i) apic_ids[i] = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        irq_gsi[i] = i; irq_flags[i] = 0; irq_override[i] = 0;
    }
    if (!rsdp_address) return 0;
    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_address;
    if (!signature_is(rsdp->signature, "RSD PTR ", 8) || !checksum_ok(rsdp, 20) ||
        rsdp->revision < 2 || rsdp->length < sizeof(acpi_rsdp_t) ||
        !checksum_ok(rsdp, rsdp->length) || !rsdp->xsdt_address) return 0;
    const acpi_header_t *xsdt = (const acpi_header_t *)(uintptr_t)rsdp->xsdt_address;
    if (!signature_is(xsdt->signature, "XSDT", 4) ||
        xsdt->length < sizeof(acpi_header_t) || !checksum_ok(xsdt, xsdt->length)) return 0;
    uint32_t entry_bytes = xsdt->length - sizeof(acpi_header_t);
    if (entry_bytes & 7) return 0;
    const uint64_t *entries = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_header_t));
    const acpi_madt_t *madt = 0;
    for (uint32_t i = 0; i < entry_bytes / sizeof(uint64_t); ++i) {
        const acpi_header_t *table = (const acpi_header_t *)(uintptr_t)entries[i];
        if (table && signature_is(table->signature, "APIC", 4)) {
            if (table->length >= sizeof(acpi_madt_t) && checksum_ok(table, table->length))
                madt = (const acpi_madt_t *)table;
            break;
        }
    }
    if (!madt) return 0;
    local_apic_base = madt->lapic_address;
    const uint8_t *cursor = (const uint8_t *)madt + sizeof(acpi_madt_t);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;
    while (cursor + 2 <= end) {
        uint8_t type = cursor[0], length = cursor[1];
        if (length < 2 || cursor + length > end) return 0;
        if (type == 0 && length >= 8 && (*(const uint32_t *)(cursor + 4) & 1) && enabled_cpus < 64)
            apic_ids[enabled_cpus++] = cursor[3];
        else if (type == 1 && length >= 12 && !io_apic_base) {
            io_apic_base = *(const uint32_t *)(cursor + 4);
            io_apic_gsi_base = *(const uint32_t *)(cursor + 8);
        }
        else if (type == 2 && length >= 10 && cursor[3] < 16) {
            irq_gsi[cursor[3]] = *(const uint32_t *)(cursor + 4);
            irq_flags[cursor[3]] = *(const uint16_t *)(cursor + 8);
            irq_override[cursor[3]] = 1;
        }
        else if (type == 9 && length >= 16 && (*(const uint32_t *)(cursor + 8) & 1) && enabled_cpus < 64)
            apic_ids[enabled_cpus++] = *(const uint32_t *)(cursor + 4);
        cursor += length;
    }
    return enabled_cpus != 0 && local_apic_base != 0;
}

uint32_t acpi_cpu_count(void) { return enabled_cpus; }
uint32_t acpi_cpu_apic_id(uint32_t index) { return index < enabled_cpus ? apic_ids[index] : 0xffffffffU; }
uint64_t acpi_lapic_base(void) { return local_apic_base; }
uint64_t acpi_ioapic_base(void) { return io_apic_base; }
uint32_t acpi_ioapic_gsi_base(void) { return io_apic_gsi_base; }
uint32_t acpi_irq_to_gsi(uint8_t irq) { return irq < 16 ? irq_gsi[irq] : 0xffffffffU; }
uint16_t acpi_irq_flags(uint8_t irq) { return irq < 16 && irq_override[irq] ? irq_flags[irq] : 0; }
