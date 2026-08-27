#ifndef OS_KERNEL_DRIVERS_NETWORK_IPV4_H
#define OS_KERNEL_DRIVERS_NETWORK_IPV4_H

#include <stdint.h>

#define IPV4_MIN_HEADER_SIZE 20U
#define IPV4_MAX_HEADER_SIZE 60U
#define IPV4_MAX_PACKET_SIZE 65535U

typedef struct {
    uint8_t ihl;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t identification;
    const uint8_t *source;
    const uint8_t *destination;
    const uint8_t *payload;
    uint16_t payload_length;
} ipv4_packet_view_t;

uint16_t ipv4_checksum(const void *data, uint16_t length);
int ipv4_packet_build(void *packet, uint16_t capacity,
                      const uint8_t source[4], const uint8_t destination[4],
                      uint8_t protocol, uint8_t ttl, uint16_t identification,
                      const void *payload, uint16_t payload_length,
                      uint16_t *packet_length);
int ipv4_packet_parse(const void *packet, uint16_t packet_length,
                      ipv4_packet_view_t *view);

#endif
