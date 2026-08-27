#ifndef OS_KERNEL_DRIVERS_ATA_H
#define OS_KERNEL_DRIVERS_ATA_H

#include <stdint.h>

int ata_initialize(void);
int ata_read_boot_sector(void *buffer);
int ata_read_sectors(uint64_t lba, uint32_t count, void *buffer);
int ata_write_sectors(uint64_t lba, uint32_t count, const void *buffer);

#endif
