#include "arp.h"

static uint16_t load_be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static void store_be16(uint8_t *value, uint16_t number) {
    value[0] = (uint8_t)(number >> 8);
    value[1] = (uint8_t)number;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) destination[i] = source[i];
}

int arp_packet_build(void *packet, uint16_t capacity, uint16_t operation,
                     const uint8_t sender_hardware[ETHERNET_ADDRESS_SIZE],
                     const uint8_t sender_protocol[4],
                     const uint8_t target_hardware[ETHERNET_ADDRESS_SIZE],
                     const uint8_t target_protocol[4], uint16_t *packet_length) {
    if (!packet || !packet_length || capacity < ARP_PACKET_SIZE ||
        (operation != ARP_OPERATION_REQUEST &&
         operation != ARP_OPERATION_REPLY) || !sender_hardware ||
        !sender_protocol || !target_hardware || !target_protocol) return 0;
    uint8_t *bytes = (uint8_t *)packet;
    store_be16(bytes + 0, ARP_HARDWARE_ETHERNET);
    store_be16(bytes + 2, ARP_PROTOCOL_IPV4);
    bytes[4] = ETHERNET_ADDRESS_SIZE;
    bytes[5] = 4;
    store_be16(bytes + 6, operation);
    copy_bytes(bytes + 8, sender_hardware, ETHERNET_ADDRESS_SIZE);
    copy_bytes(bytes + 14, sender_protocol, 4);
    copy_bytes(bytes + 18, target_hardware, ETHERNET_ADDRESS_SIZE);
    copy_bytes(bytes + 24, target_protocol, 4);
    *packet_length = ARP_PACKET_SIZE;
    return 1;
}

int arp_packet_parse(const void *packet, uint16_t packet_length,
                     arp_packet_view_t *view) {
    if (!packet || packet_length != ARP_PACKET_SIZE || !view) return 0;
    const uint8_t *bytes = (const uint8_t *)packet;
    uint16_t operation = load_be16(bytes + 6);
    if (load_be16(bytes + 0) != ARP_HARDWARE_ETHERNET ||
        load_be16(bytes + 2) != ARP_PROTOCOL_IPV4 || bytes[4] != 6 ||
        bytes[5] != 4 || (operation != ARP_OPERATION_REQUEST &&
                          operation != ARP_OPERATION_REPLY)) return 0;
    view->operation = operation;
    view->sender_hardware = bytes + 8;
    view->sender_protocol = bytes + 14;
    view->target_hardware = bytes + 18;
    view->target_protocol = bytes + 24;
    return 1;
}
