#include "ethernet.h"

static uint16_t load_be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static void store_be16(uint8_t *value, uint16_t number) {
    value[0] = (uint8_t)(number >> 8);
    value[1] = (uint8_t)number;
}

int ethernet_frame_build(void *frame, uint16_t capacity,
                         const uint8_t destination[ETHERNET_ADDRESS_SIZE],
                         const uint8_t source[ETHERNET_ADDRESS_SIZE],
                         uint16_t ether_type, const void *payload,
                         uint16_t payload_length, uint16_t *frame_length) {
    if (!frame || !destination || !source || !frame_length ||
        ether_type < 0x0600U || payload_length > ETHERNET_MAX_PAYLOAD_SIZE ||
        (payload_length != 0 && !payload)) return 0;
    uint16_t length = (uint16_t)(ETHERNET_HEADER_SIZE + payload_length);
    if (length < ETHERNET_MIN_FRAME_SIZE) length = ETHERNET_MIN_FRAME_SIZE;
    if (capacity < length || length > ETHERNET_MAX_FRAME_SIZE) return 0;
    uint8_t *bytes = (uint8_t *)frame;
    for (uint32_t i = 0; i < ETHERNET_ADDRESS_SIZE; ++i) {
        bytes[i] = destination[i];
        bytes[6U + i] = source[i];
    }
    store_be16(bytes + 12, ether_type);
    for (uint32_t i = 0; i < payload_length; ++i)
        bytes[ETHERNET_HEADER_SIZE + i] = ((const uint8_t *)payload)[i];
    for (uint32_t i = ETHERNET_HEADER_SIZE + payload_length; i < length; ++i)
        bytes[i] = 0;
    *frame_length = length;
    return 1;
}

int ethernet_frame_parse(const void *frame, uint16_t frame_length,
                         ethernet_frame_view_t *view) {
    if (!frame || !view || frame_length < ETHERNET_MIN_FRAME_SIZE ||
        frame_length > ETHERNET_MAX_FRAME_SIZE) return 0;
    const uint8_t *bytes = (const uint8_t *)frame;
    uint16_t ether_type = load_be16(bytes + 12);
    if (ether_type < 0x0600U) return 0;
    view->destination = bytes;
    view->source = bytes + 6;
    view->ether_type = ether_type;
    view->payload = bytes + ETHERNET_HEADER_SIZE;
    view->payload_length = (uint16_t)(frame_length - ETHERNET_HEADER_SIZE);
    return 1;
}

int ethernet_address_is_broadcast(const uint8_t address[ETHERNET_ADDRESS_SIZE]) {
    if (!address) return 0;
    for (uint32_t i = 0; i < ETHERNET_ADDRESS_SIZE; ++i)
        if (address[i] != 0xffU) return 0;
    return 1;
}
