#ifndef OS_BOOT_INFO_H
#define OS_BOOT_INFO_H

#include <stdint.h>

typedef struct {
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint32_t reserved;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t acpi_rsdp;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_format;
    uint64_t init_image;
    uint64_t init_image_size;
} os_boot_info_t;

#endif
