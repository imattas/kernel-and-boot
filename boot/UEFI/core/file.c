#include "file.h"

static const efi_guid_t file_info_guid = {0x09576e92,0x6d3f,0x11d2,
    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
#define EFI_BUFFER_TOO_SMALL 5U

efi_status_t uefi_read_kernel_file(efi_boot_services_t *bs,
                                   efi_file_protocol_t *kernel,
                                   uint8_t **buffer, efi_uintn_t *size) {
    if (!bs || !bs->allocate_pool || !kernel || !kernel->get_info ||
        !kernel->read || !buffer || !size) return 1;
    efi_uintn_t info_size = 4096;
    efi_file_info_t *info = 0;
    if (bs->allocate_pool(2, info_size, (void **)&info) != 0 || !info) return 1;
    efi_status_t status = kernel->get_info(kernel, (efi_guid_t *)&file_info_guid,
                                            &info_size, info);
    if (status != 0 || info_size < sizeof(efi_file_info_t) ||
        info->file_size == 0 || info->file_size > 64ULL * 1024ULL * 1024ULL)
        return status ? status : 1;
    if (bs->allocate_pool(2, info->file_size, (void **)buffer) != 0 || !*buffer) return 1;
    efi_uintn_t read_size = info->file_size;
    status = kernel->read(kernel, &read_size, *buffer);
    if (status != 0 || read_size != info->file_size) return status ? status : 1;
    *size = read_size;
    return 0;
}
