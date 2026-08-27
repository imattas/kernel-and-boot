#ifndef OS_KERNEL_DRIVERS_PCI_H
#define OS_KERNEL_DRIVERS_PCI_H

#include <stdint.h>
#include "../../device/device.h"

int pci_initialize(void);
uint32_t pci_device_count(void);
uint32_t pci_resource_count(void);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);
int pci_enable_msi(const device_t *device, uint8_t vector);
int pci_enable_legacy_irq(const device_t *device, uint8_t vector);

#endif
