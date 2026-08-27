#ifndef OS_BOOT_UEFI_EFI_TYPES_H
#define OS_BOOT_UEFI_EFI_TYPES_H
#include <stdint.h>
typedef uint64_t efi_status_t; typedef uint16_t efi_char16_t; typedef void *efi_handle_t;
typedef uint64_t efi_physical_address_t; typedef uint64_t efi_uintn_t;
typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } efi_guid_t;
typedef struct efi_system_table efi_system_table_t; typedef struct efi_file_protocol efi_file_protocol_t;
typedef struct { efi_guid_t vendor_guid; void *vendor_table; } efi_configuration_table_t;
typedef struct { void *reset; efi_status_t (*output_string)(void *, const efi_char16_t *); } efi_simple_text_output_protocol_t;
typedef efi_status_t (*efi_handle_protocol_t)(efi_handle_t, efi_guid_t *, void **);
typedef efi_status_t (*efi_allocate_pool_t)(uint32_t, efi_uintn_t, void **);
typedef efi_status_t (*efi_allocate_pages_t)(uint32_t, uint32_t, efi_uintn_t, efi_physical_address_t *);
typedef efi_status_t (*efi_get_memory_map_t)(efi_uintn_t *, void *, efi_uintn_t *, efi_uintn_t *, uint32_t *);
typedef efi_status_t (*efi_exit_boot_services_t)(efi_handle_t, efi_uintn_t);
typedef efi_status_t (*efi_locate_protocol_t)(efi_guid_t *, void *, void **);
typedef struct { uint64_t header[3]; void *raise_tpl,*restore_tpl,*allocate_pages,*free_pages,*get_memory_map;
 efi_allocate_pool_t allocate_pool; void *free_pool,*create_event,*set_timer,*wait_for_event,*signal_event;
 void *close_event,*check_event,*install_protocol_interface,*reinstall_protocol_interface,*uninstall_protocol_interface;
 efi_handle_protocol_t handle_protocol; void *reserved,*register_protocol_notify,*locate_handle,*locate_device_path,*install_configuration_table;
 void *load_image,*start_image,*exit,*unload_image; efi_exit_boot_services_t exit_boot_services;
 void *get_next_monotonic_count,*stall,*set_watchdog_timer,*connect_controller,*disconnect_controller,*open_protocol,*close_protocol;
 void *open_protocol_information,*protocols_per_handle,*locate_handle_buffer; efi_locate_protocol_t locate_protocol; } efi_boot_services_t;
struct efi_system_table { uint64_t header[3]; efi_char16_t *firmware_vendor; uint32_t firmware_revision;
 efi_handle_t console_in_handle; void *con_in; efi_handle_t console_out_handle; efi_simple_text_output_protocol_t *con_out;
 efi_handle_t standard_error_handle; void *std_err,*runtime_services; efi_boot_services_t *boot_services;
 efi_uintn_t number_of_table_entries; efi_configuration_table_t *configuration_table; };
typedef struct { uint32_t revision; efi_handle_t parent_handle; efi_system_table_t *system_table; efi_handle_t device_handle; } efi_loaded_image_t;
typedef efi_status_t (*efi_file_open_t)(efi_file_protocol_t *,efi_file_protocol_t **,const efi_char16_t *,uint64_t,uint64_t);
typedef efi_status_t (*efi_file_close_t)(efi_file_protocol_t *); typedef efi_status_t (*efi_file_read_t)(efi_file_protocol_t *,efi_uintn_t *,void *);
typedef efi_status_t (*efi_file_get_info_t)(efi_file_protocol_t *,efi_guid_t *,efi_uintn_t *,void *);
typedef efi_status_t (*efi_simple_file_system_open_volume_t)(void *,efi_file_protocol_t **);
struct efi_file_protocol { uint64_t revision; efi_file_open_t open; efi_file_close_t close; void *delete; efi_file_read_t read; void *write,*get_position,*set_position; efi_file_get_info_t get_info; };
typedef struct { uint64_t revision; efi_simple_file_system_open_volume_t open_volume; } efi_simple_file_system_protocol_t;
typedef struct { uint64_t size,file_size,physical_size; } efi_file_info_t;
typedef struct { uint8_t ident[16]; uint16_t type,machine; uint32_t version; uint64_t entry,phoff,shoff; uint32_t flags; uint16_t ehsize,phentsize,phnum; } elf64_header_t;
typedef struct { uint32_t type,flags; uint64_t offset,vaddr,paddr,filesz,memsz,align; } elf64_program_t;
typedef void (__attribute__((sysv_abi)) *kernel_entry_t)(void *);
typedef struct { uint32_t version,horizontal_resolution,vertical_resolution,pixel_format; uint32_t pixel_information[4]; uint32_t pixels_per_scan_line; } efi_gop_mode_info_t;
typedef struct { uint32_t max_mode,mode; efi_gop_mode_info_t *info; efi_uintn_t info_size; efi_physical_address_t framebuffer_base; efi_uintn_t framebuffer_size; } efi_gop_mode_t;
typedef struct { efi_status_t (*query_mode)(void *,uint32_t,efi_uintn_t *,efi_gop_mode_info_t **); efi_status_t (*set_mode)(void *,uint32_t); efi_status_t (*blt)(void *,void *,uint32_t,efi_uintn_t,efi_uintn_t,efi_uintn_t,efi_uintn_t,efi_uintn_t,efi_uintn_t); efi_gop_mode_t *mode; } efi_gop_t;
#endif
