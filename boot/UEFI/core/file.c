#include "file.h"

static const efi_guid_t file_info_guid = {0x09576e92,0x6d3f,0x11d2,
    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
#define EFI_BUFFER_TOO_SMALL 5U
typedef efi_status_t (*efi_free_pool_t)(void *);

efi_status_t uefi_read_kernel_file(efi_boot_services_t *bs,
                                   efi_file_protocol_t *kernel,
                                   uint8_t **buffer, efi_uintn_t *size) {
    if (!bs || !bs->allocate_pool || !kernel || !kernel->get_info ||
        !kernel->read || !buffer || !size) return 1;
    efi_uintn_t info_size = 4096;
    efi_file_info_t *info = 0;
    efi_free_pool_t free_pool = (efi_free_pool_t)bs->free_pool;
    if (bs->allocate_pool(2, info_size, (void **)&info) != 0 || !info) return 1;
    efi_status_t status = kernel->get_info(kernel, (efi_guid_t *)&file_info_guid,
                                            &info_size, info);
    if (status == EFI_BUFFER_TOO_SMALL) {
        if (info_size < sizeof(efi_file_info_t) || !free_pool ||
            free_pool(info) != 0) return status;
        info = 0;
        if (bs->allocate_pool(2, info_size, (void **)&info) != 0 || !info) return 1;
        status = kernel->get_info(kernel, (efi_guid_t *)&file_info_guid,
                                  &info_size, info);
    }
    if (status != 0 || info_size < sizeof(efi_file_info_t) ||
        info->file_size == 0 || info->file_size > 64ULL * 1024ULL * 1024ULL) {
        if (free_pool) free_pool(info);
        return status ? status : 1;
    }
    if (bs->allocate_pool(2, info->file_size, (void **)buffer) != 0 || !*buffer) {
        if (free_pool) free_pool(info);
        return 1;
    }
    efi_uintn_t read_size = info->file_size;
    status = kernel->read(kernel, &read_size, *buffer);
    efi_uintn_t expected_size = info->file_size;
    if (free_pool) free_pool(info);
    if (status != 0 || read_size != expected_size) {
        if (free_pool) free_pool(*buffer);
        *buffer = 0;
        return status ? status : 1;
    }
    *size = read_size;
    return 0;
}
