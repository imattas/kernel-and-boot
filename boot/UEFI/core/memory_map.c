#include "memory_map.h"
#define EFI_BUFFER_TOO_SMALL 5U
typedef efi_status_t (*efi_free_pool_t)(void *);

static int memory_map_shape_valid(efi_uintn_t size, efi_uintn_t descriptor_size) {
    return descriptor_size >= 48U && size != 0 && size % descriptor_size == 0;
}

efi_status_t uefi_capture_memory_map(efi_boot_services_t *bs, uint8_t **buffer,
                                     efi_uintn_t *capacity, os_boot_info_t *info,
                                     efi_uintn_t *map_key) {
    if (!bs || !buffer || !info || !map_key || !*buffer || !*capacity) return 1;
    efi_get_memory_map_t get_memory_map = (efi_get_memory_map_t)bs->get_memory_map;
    efi_free_pool_t free_pool = (efi_free_pool_t)bs->free_pool;
    if (!get_memory_map || !bs->allocate_pool || !free_pool ||
        !*buffer || !*capacity) return 1;
    efi_uintn_t available = *capacity, descriptor_size = 0;
    uint32_t version = 0;
    efi_uintn_t size = available;
    efi_status_t status = get_memory_map(&size, *buffer, map_key,
                                         &descriptor_size, &version);
    if (status == 0 && memory_map_shape_valid(size, descriptor_size)) {
        info->memory_map = (uint64_t)(uintptr_t)*buffer;
        info->memory_map_size = size;
        info->memory_descriptor_size = descriptor_size;
        info->memory_descriptor_version = version;
        return 0;
    }
    if (status == 0) return 1;
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size < 48 || size == 0 ||
        descriptor_size > UINT64_MAX / 4U ||
        size > UINT64_MAX - descriptor_size * 4U) return status;
    available = size + descriptor_size * 4U;
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        uint8_t *candidate = 0;
        if (bs->allocate_pool(2, available, (void **)&candidate) != 0 || !candidate) return 1;
        size = available;
        status = get_memory_map(&size, candidate, map_key, &descriptor_size, &version);
        if (status == 0 && memory_map_shape_valid(size, descriptor_size) &&
            size <= available) {
            free_pool(*buffer);
            *buffer = candidate; *capacity = available;
            info->memory_map = (uint64_t)(uintptr_t)candidate;
            info->memory_map_size = size;
            info->memory_descriptor_size = descriptor_size;
            info->memory_descriptor_version = version;
            return 0;
        }
        if (status == 0) {
            free_pool(candidate);
            return 1;
        }
        free_pool(candidate);
        if (status != EFI_BUFFER_TOO_SMALL || descriptor_size < 48U || size == 0 ||
            descriptor_size > UINT64_MAX / 4U ||
            size > UINT64_MAX - descriptor_size * 4U) return status;
        available = size + descriptor_size * 4U;
    }
    return 1;
}
