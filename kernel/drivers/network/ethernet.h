#ifndef OS_KERNEL_DRIVERS_NETWORK_ETHERNET_H
#define OS_KERNEL_DRIVERS_NETWORK_ETHERNET_H

#include <stdint.h>

#define ETHERNET_ADDRESS_SIZE 6U
#define ETHERNET_HEADER_SIZE 14U
#define ETHERNET_MIN_FRAME_SIZE 60U
#define ETHERNET_MAX_FRAME_SIZE 1518U
#define ETHERNET_MAX_PAYLOAD_SIZE 1500U

typedef struct {
    const uint8_t *destination;
    const uint8_t *source;
    uint16_t ether_type;
    const uint8_t *payload;
    uint16_t payload_length;
} ethernet_frame_view_t;

int ethernet_frame_build(void *frame, uint16_t capacity,
                         const uint8_t destination[ETHERNET_ADDRESS_SIZE],
                         const uint8_t source[ETHERNET_ADDRESS_SIZE],
                         uint16_t ether_type, const void *payload,
                         uint16_t payload_length, uint16_t *frame_length);
int ethernet_frame_parse(const void *frame, uint16_t frame_length,
                         ethernet_frame_view_t *view);
int ethernet_address_is_broadcast(const uint8_t address[ETHERNET_ADDRESS_SIZE]);

#endif
