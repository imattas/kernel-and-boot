#ifndef OS_KERNEL_DRIVERS_AHCI_H
#define OS_KERNEL_DRIVERS_AHCI_H

#include <stdint.h>

int ahci_initialize(void);
uint32_t ahci_controller_count(void);
uint32_t ahci_port_mask(void);
uint32_t ahci_ready_port_count(void);
uint32_t ahci_ready_port_mask(void);
int ahci_register_storage_devices(void);
int ahci_interrupt_enabled(void);
uint32_t ahci_interrupt_count(void);
void ahci_interrupt_handler(void);
int ahci_identify(uint16_t *words);
int ahci_read_sector(uint64_t lba, void *buffer);
int ahci_write_sector(uint64_t lba, const void *buffer);
int ahci_read_sectors(uint64_t lba, uint32_t count, void *buffer);
int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buffer);
uint32_t ahci_last_io_prdt_length(void);
uint32_t ahci_error_count(void);
uint32_t ahci_last_task_file_status(void);
uint32_t ahci_last_interrupt_status(void);
uint32_t ahci_last_serial_error(void);

#endif
