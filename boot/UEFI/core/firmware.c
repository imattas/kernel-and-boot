#include "firmware.h"

static const efi_guid_t acpi20_guid = {0x8868e871,0xe4f1,0x11d3,
    {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static const efi_guid_t acpi10_guid = {0xeb9d2d31,0x2d88,0x11d3,
    {0x9a,0x16,0x00,0x00,0x0f,0x8e,0x0d,0x7a}};
static const efi_guid_t graphics_output_guid = {0x9042a9de,0x23dc,0x4a38,
    {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

static int same_guid(const efi_guid_t *a, const efi_guid_t *b) {
    if (!a || !b || a->a != b->a || a->b != b->b || a->c != b->c) return 0;
    for (uint32_t i = 0; i < 8; ++i) if (a->d[i] != b->d[i]) return 0;
    return 1;
}

uint64_t uefi_find_acpi_rsdp(const efi_system_table_t *st) {
    if (!st || !st->configuration_table) return 0;
    uint64_t fallback = 0;
    for (efi_uintn_t i = 0; i < st->number_of_table_entries; ++i) {
        const efi_configuration_table_t *entry = &st->configuration_table[i];
        if (same_guid(&entry->vendor_guid, &acpi20_guid))
            return (uint64_t)(uintptr_t)entry->vendor_table;
        if (same_guid(&entry->vendor_guid, &acpi10_guid))
            fallback = (uint64_t)(uintptr_t)entry->vendor_table;
    }
    return fallback;
}

void uefi_find_framebuffer(efi_boot_services_t *bs, os_boot_info_t *info) {
    if (!bs || !bs->locate_protocol || !info) return;
    efi_gop_t *gop = 0;
    if (bs->locate_protocol((efi_guid_t *)&graphics_output_guid, 0,
                            (void **)&gop) != 0 || !gop || !gop->mode ||
        !gop->mode->info) return;
    efi_gop_mode_info_t *mode = gop->mode->info;
    if (mode->pixel_format > 1 || !mode->horizontal_resolution ||
        !mode->vertical_resolution ||
        mode->pixels_per_scan_line < mode->horizontal_resolution ||
        !gop->mode->framebuffer_base) return;
    uint64_t pixels = (uint64_t)mode->pixels_per_scan_line *
                      mode->vertical_resolution;
    if (pixels > UINT64_MAX / 4ULL ||
        gop->mode->framebuffer_size < pixels * 4ULL ||
        gop->mode->framebuffer_base >= (1ULL << 32) ||
        pixels * 4ULL > (1ULL << 32) - gop->mode->framebuffer_base) return;
    info->framebuffer_base = gop->mode->framebuffer_base;
    info->framebuffer_size = gop->mode->framebuffer_size;
    info->framebuffer_width = mode->horizontal_resolution;
    info->framebuffer_height = mode->vertical_resolution;
    info->framebuffer_pitch = mode->pixels_per_scan_line;
    info->framebuffer_format = mode->pixel_format;
}
