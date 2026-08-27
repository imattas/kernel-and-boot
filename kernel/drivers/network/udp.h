#ifndef OS_KERNEL_DRIVERS_NETWORK_UDP_H
#define OS_KERNEL_DRIVERS_NETWORK_UDP_H

#include <stdint.h>

#define UDP_HEADER_SIZE 8U
#define UDP_MAX_PACKET_SIZE 65535U

typedef struct {
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t *payload;
    uint16_t payload_length;
} udp_packet_view_t;

int udp_packet_build(void *packet, uint16_t capacity,
                     const uint8_t source_address[4],
                     const uint8_t destination_address[4],
                     uint16_t source_port, uint16_t destination_port,
                     const void *payload, uint16_t payload_length,
                     uint16_t *packet_length);
int udp_packet_parse(const void *packet, uint16_t packet_length,
                     const uint8_t source_address[4],
                     const uint8_t destination_address[4],
                     udp_packet_view_t *view);

#endif
