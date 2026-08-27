#ifndef OS_KERNEL_DRIVERS_ATA_H
#define OS_KERNEL_DRIVERS_ATA_H

#include <stdint.h>

int ata_initialize(void);
int ata_read_boot_sector(void *buffer);

#endif
