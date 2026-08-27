#include "icmp.h"

static uint16_t load_be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static void store_be16(uint8_t *value, uint16_t number) {
    value[0] = (uint8_t)(number >> 8);
    value[1] = (uint8_t)number;
}

uint16_t icmp_checksum(const void *packet, uint16_t length) {
    if (!packet || length == 0) return 0;
    const uint8_t *bytes = (const uint8_t *)packet;
    uint32_t sum = 0;
    for (uint16_t i = 0; i + 1U < length; i += 2)
        sum += ((uint16_t)bytes[i] << 8) | bytes[i + 1U];
    if ((length & 1U) != 0) sum += (uint16_t)bytes[length - 1U] << 8;
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    return (uint16_t)~sum;
}

int icmp_echo_build(void *packet, uint16_t capacity, uint8_t type,
                    uint16_t identifier, uint16_t sequence,
                    const void *payload, uint16_t payload_length,
                    uint16_t *packet_length) {
    if (!packet || !packet_length ||
        (type != ICMP_TYPE_ECHO_REQUEST && type != ICMP_TYPE_ECHO_REPLY) ||
        (payload_length != 0 && !payload) ||
        payload_length > ICMP_MAX_PACKET_SIZE - ICMP_ECHO_HEADER_SIZE)
        return 0;
    uint16_t length = (uint16_t)(ICMP_ECHO_HEADER_SIZE + payload_length);
    if (capacity < length) return 0;
    uint8_t *bytes = (uint8_t *)packet;
    bytes[0] = type;
    bytes[1] = 0;
    store_be16(bytes + 2, 0);
    store_be16(bytes + 4, identifier);
    store_be16(bytes + 6, sequence);
    for (uint32_t i = 0; i < payload_length; ++i)
        bytes[ICMP_ECHO_HEADER_SIZE + i] = ((const uint8_t *)payload)[i];
    store_be16(bytes + 2, icmp_checksum(bytes, length));
    *packet_length = length;
    return 1;
}

int icmp_echo_parse(const void *packet, uint16_t packet_length,
                    icmp_echo_view_t *view) {
    if (!packet || !view || packet_length < ICMP_ECHO_HEADER_SIZE)
        return 0;
    const uint8_t *bytes = (const uint8_t *)packet;
    if ((bytes[0] != ICMP_TYPE_ECHO_REQUEST &&
         bytes[0] != ICMP_TYPE_ECHO_REPLY) || bytes[1] != 0 ||
        icmp_checksum(bytes, packet_length) != 0) return 0;
    view->type = bytes[0];
    view->code = bytes[1];
    view->identifier = load_be16(bytes + 4);
    view->sequence = load_be16(bytes + 6);
    view->payload = bytes + ICMP_ECHO_HEADER_SIZE;
    view->payload_length = (uint16_t)(packet_length - ICMP_ECHO_HEADER_SIZE);
    return 1;
}
