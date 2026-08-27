#ifndef OS_KERNEL_DRIVERS_UHCI_H
#define OS_KERNEL_DRIVERS_UHCI_H

#include <stdint.h>

int uhci_initialize(void);
uint32_t uhci_controller_count(void);
uint32_t uhci_root_port_count(void);
int uhci_interrupt_enabled(void);
uint32_t uhci_interrupt_count(void);
void uhci_interrupt_handler(void);
int uhci_control_transfer(uint8_t address, uint8_t endpoint,
                          const uint8_t setup[8], void *data, uint16_t length);
int uhci_interrupt_transfer(uint8_t address, uint8_t endpoint, void *data,
                            uint16_t length, uint16_t max_packet,
                            uint8_t *toggle);

#endif
