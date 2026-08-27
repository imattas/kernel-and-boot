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

typedef struct {
    uint8_t address_space; uint8_t bit_width; uint8_t bit_offset;
    uint8_t access_size; uint64_t address;
} __attribute__((packed)) acpi_gas_t;

#define ACPI_MAX_TABLE_LENGTH (1024U * 1024U)

static uint32_t enabled_cpus;
static uint32_t apic_ids[64];
static uint64_t local_apic_base;
static uint64_t io_apic_base;
static uint32_t io_apic_gsi_base;
#define ACPI_IOAPIC_CAPACITY 8U
static uint64_t io_apic_bases[ACPI_IOAPIC_CAPACITY];
static uint32_t io_apic_gsi_bases[ACPI_IOAPIC_CAPACITY];
static uint32_t io_apic_gsi_limits[ACPI_IOAPIC_CAPACITY];
static uint32_t io_apic_count;
static uint32_t irq_gsi[16];
static uint16_t irq_flags[16];
static uint8_t irq_override[16];
static uint16_t reset_port;
static uint8_t reset_value;
static int reset_ready;

static void io_write8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static int table_range_valid(uint64_t address, uint32_t length) {
    return address != 0 && address <= UINT32_MAX && length != 0 &&
           length <= ACPI_MAX_TABLE_LENGTH &&
           address <= UINT64_MAX - (uint64_t)length;
}

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
    io_apic_count = 0;
    reset_port = 0; reset_value = 0; reset_ready = 0;
    for (uint32_t i = 0; i < ACPI_IOAPIC_CAPACITY; ++i) {
        io_apic_bases[i] = 0; io_apic_gsi_bases[i] = 0;
        io_apic_gsi_limits[i] = 0;
    }
    for (uint32_t i = 0; i < 64; ++i) apic_ids[i] = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        irq_gsi[i] = i; irq_flags[i] = 0; irq_override[i] = 0;
    }
    if (!table_range_valid(rsdp_address, sizeof(acpi_rsdp_t))) return 0;
    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)(uintptr_t)rsdp_address;
    if (!signature_is(rsdp->signature, "RSD PTR ", 8) || !checksum_ok(rsdp, 20) ||
        rsdp->revision < 2 || rsdp->length < sizeof(acpi_rsdp_t) ||
        !table_range_valid(rsdp_address, rsdp->length) ||
        !checksum_ok(rsdp, rsdp->length) ||
        !table_range_valid(rsdp->xsdt_address, sizeof(acpi_header_t))) return 0;
    const acpi_header_t *xsdt = (const acpi_header_t *)(uintptr_t)rsdp->xsdt_address;
    if (!signature_is(xsdt->signature, "XSDT", 4) ||
        xsdt->length < sizeof(acpi_header_t) ||
        !table_range_valid(rsdp->xsdt_address, xsdt->length) ||
        !checksum_ok(xsdt, xsdt->length)) return 0;
    uint32_t entry_bytes = xsdt->length - sizeof(acpi_header_t);
    if (entry_bytes & 7) return 0;
    const uint64_t *entries = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_header_t));
    const acpi_madt_t *madt = 0;
    for (uint32_t i = 0; i < entry_bytes / sizeof(uint64_t); ++i) {
        if (!table_range_valid(entries[i], sizeof(acpi_header_t))) continue;
        const acpi_header_t *table = (const acpi_header_t *)(uintptr_t)entries[i];
        if (signature_is(table->signature, "FACP", 4) && table->length >= 129 &&
            table_range_valid(entries[i], table->length) &&
            checksum_ok(table, table->length)) {
            const acpi_gas_t *gas = (const acpi_gas_t *)
                ((const uint8_t *)table + 116);
            if (gas->address_space == 1 && gas->bit_offset == 0 &&
                gas->bit_width >= 8 && gas->access_size <= 2 &&
                gas->address <= 0xffffU) {
                reset_port = (uint16_t)gas->address;
                reset_value = *((const uint8_t *)table + 128);
                reset_ready = 1;
            }
        }
        if (signature_is(table->signature, "APIC", 4)) {
            if (table->length >= sizeof(acpi_madt_t) &&
                table_range_valid(entries[i], table->length) &&
                checksum_ok(table, table->length))
                madt = (const acpi_madt_t *)table;
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
        else if (type == 1 && length >= 12 && io_apic_count < ACPI_IOAPIC_CAPACITY) {
            uint64_t base = *(const uint32_t *)(cursor + 4);
            uint32_t gsi_base = *(const uint32_t *)(cursor + 8);
            if (base == 0 || base >= (1ULL << 32)) return 0;
            volatile uint32_t *ioapic = (volatile uint32_t *)(uintptr_t)base;
            ioapic[0] = 1;
            uint32_t version = ioapic[4];
            uint32_t max_redirection = (version >> 16) & 0xffU;
            if (gsi_base > UINT32_MAX - max_redirection) return 0;
            io_apic_bases[io_apic_count] = base;
            io_apic_gsi_bases[io_apic_count] = gsi_base;
            io_apic_gsi_limits[io_apic_count] = gsi_base + max_redirection;
            if (!io_apic_base) {
                io_apic_base = base;
                io_apic_gsi_base = gsi_base;
            }
            ++io_apic_count;
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
uint64_t acpi_ioapic_base_for_gsi(uint32_t gsi) {
    uint32_t selected = 0xffffffffU;
    for (uint32_t i = 0; i < io_apic_count; ++i) {
        if (gsi >= io_apic_gsi_bases[i] && gsi <= io_apic_gsi_limits[i] &&
            (selected == 0xffffffffU ||
             io_apic_gsi_bases[i] > io_apic_gsi_bases[selected])) selected = i;
    }
    return selected == 0xffffffffU ? 0 : io_apic_bases[selected];
}
uint32_t acpi_ioapic_gsi_base_for_gsi(uint32_t gsi) {
    uint32_t selected = 0xffffffffU;
    for (uint32_t i = 0; i < io_apic_count; ++i) {
        if (gsi >= io_apic_gsi_bases[i] && gsi <= io_apic_gsi_limits[i] &&
            (selected == 0xffffffffU ||
             io_apic_gsi_bases[i] > io_apic_gsi_bases[selected])) selected = i;
    }
    return selected == 0xffffffffU ? 0xffffffffU : io_apic_gsi_bases[selected];
}
uint32_t acpi_irq_to_gsi(uint8_t irq) { return irq < 16 ? irq_gsi[irq] : 0xffffffffU; }
uint16_t acpi_irq_flags(uint8_t irq) { return irq < 16 && irq_override[irq] ? irq_flags[irq] : 0; }
int acpi_reset_available(void) { return reset_ready; }
int acpi_reset(void) {
    if (!reset_ready) return 0;
    io_write8(reset_port, reset_value);
    return 1;
}
