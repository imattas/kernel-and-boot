#ifndef OS_KERNEL_DRIVERS_NETWORK_NETWORK_H
#define OS_KERNEL_DRIVERS_NETWORK_NETWORK_H

#include <stdint.h>
#include "packet_queue.h"

int network_e1000_transmit(const void *frame, uint16_t length);
uint32_t network_e1000_poll(network_packet_queue_t *queue, uint32_t budget);

#endif
