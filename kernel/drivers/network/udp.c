#include "udp.h"

static uint16_t load_be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static void store_be16(uint8_t *value, uint16_t number) {
    value[0] = (uint8_t)(number >> 8);
    value[1] = (uint8_t)number;
}

static void add_bytes(uint32_t *sum, const uint8_t *bytes, uint16_t length) {
    for (uint16_t i = 0; i + 1U < length; i += 2)
        *sum += ((uint16_t)bytes[i] << 8) | bytes[i + 1U];
    if ((length & 1U) != 0) *sum += (uint16_t)bytes[length - 1U] << 8;
}

static uint16_t packet_checksum(const uint8_t *packet, uint16_t length,
                                const uint8_t source[4],
                                const uint8_t destination[4]) {
    uint32_t sum = 0;
    add_bytes(&sum, source, 4);
    add_bytes(&sum, destination, 4);
    sum += 17U;
    sum += length;
    add_bytes(&sum, packet, UDP_HEADER_SIZE);
    add_bytes(&sum, packet + 8, (uint16_t)(length - UDP_HEADER_SIZE));
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    uint16_t result = (uint16_t)~sum;
    return result ? result : 0xffffU;
}

int udp_packet_build(void *packet, uint16_t capacity,
                     const uint8_t source_address[4],
                     const uint8_t destination_address[4],
                     uint16_t source_port, uint16_t destination_port,
                     const void *payload, uint16_t payload_length,
                     uint16_t *packet_length) {
    if (!packet || !source_address || !destination_address ||
        !packet_length || source_port == 0 || destination_port == 0 ||
        payload_length > UDP_MAX_PACKET_SIZE - UDP_HEADER_SIZE ||
        (payload_length != 0 && !payload)) return 0;
    uint16_t length = (uint16_t)(UDP_HEADER_SIZE + payload_length);
    if (capacity < length) return 0;
    uint8_t *bytes = (uint8_t *)packet;
    store_be16(bytes + 0, source_port);
    store_be16(bytes + 2, destination_port);
    store_be16(bytes + 4, length);
    store_be16(bytes + 6, 0);
    for (uint32_t i = 0; i < payload_length; ++i)
        bytes[UDP_HEADER_SIZE + i] = ((const uint8_t *)payload)[i];
    store_be16(bytes + 6, packet_checksum(bytes, length, source_address,
                                          destination_address));
    *packet_length = length;
    return 1;
}

int udp_packet_parse(const void *packet, uint16_t packet_length,
                     const uint8_t source_address[4],
                     const uint8_t destination_address[4],
                     udp_packet_view_t *view) {
    if (!packet || !source_address || !destination_address || !view ||
        packet_length < UDP_HEADER_SIZE) return 0;
    const uint8_t *bytes = (const uint8_t *)packet;
    uint16_t length = load_be16(bytes + 4);
    uint16_t checksum = load_be16(bytes + 6);
    if (load_be16(bytes + 0) == 0 || load_be16(bytes + 2) == 0 ||
        length < UDP_HEADER_SIZE || length > packet_length ||
        (checksum != 0 && packet_checksum(bytes, length, source_address,
                                           destination_address) != 0xffffU))
        return 0;
    view->source_port = load_be16(bytes + 0);
    view->destination_port = load_be16(bytes + 2);
    view->payload = bytes + UDP_HEADER_SIZE;
    view->payload_length = (uint16_t)(length - UDP_HEADER_SIZE);
    return 1;
}
