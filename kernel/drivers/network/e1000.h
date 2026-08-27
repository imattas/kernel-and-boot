#ifndef OS_KERNEL_DRIVERS_NETWORK_E1000_H
#define OS_KERNEL_DRIVERS_NETWORK_E1000_H
#include <stdint.h>
int e1000_initialize(void);
uint32_t e1000_controller_count(void);
int e1000_link_up(void);
int e1000_interrupt_enabled(void);
uint32_t e1000_interrupt_count(void);
uint32_t e1000_tx_error_count(void);
void e1000_interrupt_handler(void);
int e1000_transmit(const void *data, uint16_t length);
int e1000_receive(void *data, uint16_t capacity, uint16_t *length);
uint32_t e1000_service(void);
#endif
