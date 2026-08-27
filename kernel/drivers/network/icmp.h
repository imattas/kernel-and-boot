#ifndef OS_KERNEL_DRIVERS_NETWORK_ICMP_H
#define OS_KERNEL_DRIVERS_NETWORK_ICMP_H

#include <stdint.h>

#define ICMP_ECHO_HEADER_SIZE 8U
#define ICMP_MAX_PACKET_SIZE 65535U
#define ICMP_TYPE_ECHO_REPLY 0U
#define ICMP_TYPE_ECHO_REQUEST 8U

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t identifier;
    uint16_t sequence;
    const uint8_t *payload;
    uint16_t payload_length;
} icmp_echo_view_t;

uint16_t icmp_checksum(const void *packet, uint16_t length);
int icmp_echo_build(void *packet, uint16_t capacity, uint8_t type,
                    uint16_t identifier, uint16_t sequence,
                    const void *payload, uint16_t payload_length,
                    uint16_t *packet_length);
int icmp_echo_parse(const void *packet, uint16_t packet_length,
                    icmp_echo_view_t *view);

#endif
