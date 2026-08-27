#ifndef OS_KERNEL_DRIVERS_NVME_H
#define OS_KERNEL_DRIVERS_NVME_H

#include <stdint.h>

int nvme_initialize(void);
uint32_t nvme_controller_count(void);
int nvme_interrupt_enabled(void);
uint32_t nvme_interrupt_count(void);
uint32_t nvme_error_count(void);
uint16_t nvme_last_completion_status(void);
void nvme_interrupt_handler(void);
int nvme_admin_command(uint8_t opcode, uint32_t namespace_id,
                       uint64_t prp1, uint32_t *result);
int nvme_identify_controller(void *buffer);
int nvme_identify_namespace(void *buffer);
int nvme_read_sector(uint64_t lba, void *buffer);
int nvme_write_sector(uint64_t lba, const void *buffer);
int nvme_read_sectors(uint64_t lba, uint32_t count, void *buffer);
int nvme_write_sectors(uint64_t lba, uint32_t count, const void *buffer);
uint32_t nvme_last_io_page_count(void);

#endif
