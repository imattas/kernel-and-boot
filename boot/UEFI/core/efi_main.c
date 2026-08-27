#include "efi_context.h"
#include "console.h"
#include "firmware.h"
#include "file.h"
#include "elf.h"
#include "memory_map.h"

static const efi_guid_t loaded_image_guid = {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const efi_guid_t simple_file_system_guid = {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
typedef efi_status_t (*efi_set_watchdog_timer_t)(efi_uintn_t, uint64_t,
                                                  efi_uintn_t, efi_char16_t *);

static void free_pool(efi_boot_services_t *bs, void *buffer) {
    if (bs && bs->free_pool && buffer)
        ((efi_status_t (*)(void *))bs->free_pool)(buffer);
}

static void close_file(efi_file_protocol_t *file) {
    if (file && file->close) file->close(file);
}

efi_status_t efi_main(efi_handle_t image_handle, efi_system_table_t *st) {
    static const efi_char16_t message[] = {'o','s',' ','U','E','F','I',' ','l','o','a','d','e','r','\r','\n',0};
    static const efi_char16_t kernel_name[] = {'K','E','R','N','E','L','.','E','L','F',0};
    efi_boot_services_t *bs = st ? st->boot_services : 0;
    if (!bs || !bs->handle_protocol || !bs->allocate_pool || !bs->free_pool ||
        !st->con_out) return 1;
    uefi_console_write(st, message);
    if (bs->set_watchdog_timer)
        ((efi_set_watchdog_timer_t)bs->set_watchdog_timer)(0, 0, 0, 0);
    efi_loaded_image_t *loaded = 0;
    if (bs->handle_protocol(image_handle, (efi_guid_t *)&loaded_image_guid, (void **)&loaded) != 0 || !loaded) return uefi_fail(st, '2', 2);
    efi_simple_file_system_protocol_t *fs = 0;
    if (bs->handle_protocol(loaded->device_handle, (efi_guid_t *)&simple_file_system_guid, (void **)&fs) != 0 || !fs) return uefi_fail(st, '3', 3);
    efi_file_protocol_t *root = 0, *kernel = 0;
    if (fs->open_volume(fs, &root) != 0 || !root || !root->open ||
        root->open(root, &kernel, kernel_name, 1, 0) != 0 || !kernel)
        { close_file(root); return uefi_fail(st, '4', 4); }
    uint8_t *file_buffer = 0;
    efi_uintn_t read_size = 0;
    if (uefi_read_kernel_file(bs, kernel, &file_buffer, &read_size) != 0 ||
        read_size < sizeof(elf64_header_t))
        { close_file(kernel); close_file(root); free_pool(bs, file_buffer);
          return uefi_fail(st, '8', 8); }
    close_file(kernel); close_file(root);
    uint64_t image_size = 0;
    efi_physical_address_t load_address = 0;
    kernel_entry_t entry = 0;
    if (uefi_elf_load(bs, file_buffer, read_size, &load_address,
                      &image_size, &entry) != 0) {
        free_pool(bs, file_buffer);
        return uefi_fail(st, '9', 9);
    }
    free_pool(bs, file_buffer);
    file_buffer = 0;
    uint8_t *memory_map = 0; os_boot_info_t *boot_info = 0;
    efi_uintn_t memory_map_capacity = 256 * 1024, map_key = 0;
    if (!bs->exit_boot_services ||
        bs->allocate_pool(2, memory_map_capacity, (void **)&memory_map) != 0 || !memory_map ||
        bs->allocate_pool(2, sizeof(*boot_info), (void **)&boot_info) != 0 || !boot_info ||
        uefi_capture_memory_map(bs, &memory_map, &memory_map_capacity, boot_info, &map_key) != 0) {
        free_pool(bs, memory_map); free_pool(bs, boot_info);
        return uefi_fail(st, 'E', 14);
    }
    static const efi_char16_t base_prefix[] = {'B','A','S','E',' ',0};
    static const efi_char16_t entry_prefix[] = {'E','N','T','R','Y',' ',0};
    uefi_console_hex(st, base_prefix, load_address);
    uefi_console_hex(st, entry_prefix, (uint64_t)(uintptr_t)entry);
    boot_info->kernel_base = load_address;
    boot_info->kernel_size = image_size;
    boot_info->acpi_rsdp = uefi_find_acpi_rsdp(st);
    uefi_find_framebuffer(bs, boot_info);
    efi_status_t exit_status = 1;
    for (uint32_t attempt = 0; attempt < 4; ++attempt) {
        if (attempt != 0 &&
            uefi_capture_memory_map(bs, &memory_map, &memory_map_capacity,
                                     boot_info, &map_key) != 0)
            break;
        exit_status = bs->exit_boot_services(image_handle, map_key);
        if (exit_status == 0) break;
    }
    if (exit_status != 0) {
        free_pool(bs, memory_map); free_pool(bs, boot_info);
        return uefi_fail(st, 'F', 15);
    }
    entry(boot_info);
    return 13;
}
