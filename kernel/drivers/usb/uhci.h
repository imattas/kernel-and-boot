#ifndef OS_KERNEL_DRIVERS_UHCI_H
#define OS_KERNEL_DRIVERS_UHCI_H

#include <stdint.h>

int uhci_initialize(void);
uint32_t uhci_controller_count(void);
uint32_t uhci_root_port_count(void);
int uhci_interrupt_enabled(void);
uint32_t uhci_interrupt_count(void);
uint32_t uhci_last_transfer_td_status(void);
uint16_t uhci_last_controller_status(void);
void uhci_interrupt_handler(void);
int uhci_control_transfer(uint8_t address, uint8_t endpoint,
                          const uint8_t setup[8], void *data, uint16_t length);
int uhci_interrupt_transfer(uint8_t address, uint8_t endpoint, void *data,
                            uint16_t length, uint16_t max_packet,
                            uint8_t *toggle);
int uhci_interrupt_submit(uint8_t address, uint8_t endpoint, void *data,
                          uint16_t length, uint16_t max_packet,
                          uint8_t interval, uint8_t *toggle);
int uhci_interrupt_poll(void);

#endif
