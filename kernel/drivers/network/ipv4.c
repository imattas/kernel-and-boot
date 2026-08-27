#include "ipv4.h"

static uint16_t load_be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static void store_be16(uint8_t *value, uint16_t number) {
    value[0] = (uint8_t)(number >> 8);
    value[1] = (uint8_t)number;
}

uint16_t ipv4_checksum(const void *data, uint16_t length) {
    if (!data || length == 0) return 0;
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    for (uint16_t i = 0; i + 1U < length; i += 2)
        sum += ((uint16_t)bytes[i] << 8) | bytes[i + 1U];
    if ((length & 1U) != 0) sum += (uint16_t)bytes[length - 1U] << 8;
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    return (uint16_t)~sum;
}

int ipv4_packet_build(void *packet, uint16_t capacity,
                      const uint8_t source[4], const uint8_t destination[4],
                      uint8_t protocol, uint8_t ttl, uint16_t identification,
                      const void *payload, uint16_t payload_length,
                      uint16_t *packet_length) {
    if (!packet || !source || !destination || !packet_length || ttl == 0 ||
        payload_length > IPV4_MAX_PACKET_SIZE - IPV4_MIN_HEADER_SIZE ||
        (payload_length != 0 && !payload)) return 0;
    uint16_t total = (uint16_t)(IPV4_MIN_HEADER_SIZE + payload_length);
    if (capacity < total) return 0;
    uint8_t *bytes = (uint8_t *)packet;
    for (uint32_t i = 0; i < IPV4_MIN_HEADER_SIZE; ++i) bytes[i] = 0;
    bytes[0] = 0x45;
    bytes[1] = 0;
    store_be16(bytes + 2, total);
    store_be16(bytes + 4, identification);
    store_be16(bytes + 6, 0);
    bytes[8] = ttl;
    bytes[9] = protocol;
    for (uint32_t i = 0; i < 4; ++i) {
        bytes[12U + i] = source[i];
        bytes[16U + i] = destination[i];
    }
    store_be16(bytes + 10, ipv4_checksum(bytes, IPV4_MIN_HEADER_SIZE));
    for (uint32_t i = 0; i < payload_length; ++i)
        bytes[IPV4_MIN_HEADER_SIZE + i] = ((const uint8_t *)payload)[i];
    *packet_length = total;
    return 1;
}

int ipv4_packet_parse(const void *packet, uint16_t packet_length,
                      ipv4_packet_view_t *view) {
    if (!packet || !view || packet_length < IPV4_MIN_HEADER_SIZE) return 0;
    const uint8_t *bytes = (const uint8_t *)packet;
    if ((bytes[0] >> 4) != 4) return 0;
    uint8_t header_length = (uint8_t)((bytes[0] & 0x0fU) * 4U);
    if (header_length < IPV4_MIN_HEADER_SIZE ||
        header_length > IPV4_MAX_HEADER_SIZE || header_length > packet_length)
        return 0;
    uint16_t total = load_be16(bytes + 2);
    uint16_t fragments = load_be16(bytes + 6);
    uint32_t fragment_offset = (uint32_t)(fragments & 0x1fffU) * 8U;
    if (total < header_length || total > packet_length ||
        fragment_offset > IPV4_MAX_PACKET_SIZE - header_length ||
        fragment_offset > IPV4_MAX_PACKET_SIZE - total + header_length ||
        ((fragments & 0x2000U) != 0 && (total - header_length) == 0) ||
        ((fragments & 0x2000U) != 0 && ((total - header_length) & 7U) != 0) ||
        ipv4_checksum(bytes, header_length) != 0)
        return 0;
    view->ihl = (uint8_t)(header_length / 4U);
    view->ttl = bytes[8];
    view->protocol = bytes[9];
    view->identification = load_be16(bytes + 4);
    view->fragment_offset = (uint16_t)fragment_offset;
    view->more_fragments = (uint8_t)((fragments & 0x2000U) != 0);
    view->source = bytes + 12;
    view->destination = bytes + 16;
    view->payload = bytes + header_length;
    view->payload_length = (uint16_t)(total - header_length);
    return 1;
}
