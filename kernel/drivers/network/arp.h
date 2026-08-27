#ifndef OS_KERNEL_DRIVERS_NETWORK_ARP_H
#define OS_KERNEL_DRIVERS_NETWORK_ARP_H

#include <stdint.h>
#include "ethernet.h"

#define ARP_PACKET_SIZE 28U
#define ARP_HARDWARE_ETHERNET 1U
#define ARP_PROTOCOL_IPV4 0x0800U
#define ARP_OPERATION_REQUEST 1U
#define ARP_OPERATION_REPLY 2U

typedef struct {
    uint16_t operation;
    const uint8_t *sender_hardware;
    const uint8_t *sender_protocol;
    const uint8_t *target_hardware;
    const uint8_t *target_protocol;
} arp_packet_view_t;

int arp_packet_build(void *packet, uint16_t capacity, uint16_t operation,
                     const uint8_t sender_hardware[ETHERNET_ADDRESS_SIZE],
                     const uint8_t sender_protocol[4],
                     const uint8_t target_hardware[ETHERNET_ADDRESS_SIZE],
                     const uint8_t target_protocol[4], uint16_t *packet_length);
int arp_packet_parse(const void *packet, uint16_t packet_length,
                     arp_packet_view_t *view);

#endif
