#ifndef OS_KERNEL_DRIVERS_NETWORK_NETWORK_H
#define OS_KERNEL_DRIVERS_NETWORK_NETWORK_H

#include <stdint.h>
#include "packet_queue.h"
#include "arp.h"
#include "arp_cache.h"
#include "ipv4.h"
#include "udp.h"
#include "icmp.h"
#include "udp_endpoint.h"

typedef enum {
    NETWORK_FRAME_ETHERNET,
    NETWORK_FRAME_ARP,
    NETWORK_FRAME_IPV4,
    NETWORK_FRAME_UDP,
    NETWORK_FRAME_ICMP
} network_frame_kind_t;

typedef struct {
    network_frame_kind_t kind;
    ethernet_frame_view_t ethernet;
    arp_packet_view_t arp;
    ipv4_packet_view_t ipv4;
    udp_packet_view_t udp;
    icmp_echo_view_t icmp;
} network_frame_view_t;

int network_e1000_transmit(const void *frame, uint16_t length);
uint32_t network_e1000_poll(network_packet_queue_t *queue, uint32_t budget);
int network_decode_frame(const void *frame, uint16_t length,
                         network_frame_view_t *view);
int network_build_icmp_echo_reply(const void *frame, uint16_t length,
                                  void *reply, uint16_t capacity,
                                  uint16_t *reply_length);
int network_build_arp_reply(const void *frame, uint16_t length,
                            const uint8_t local_hardware[ETHERNET_ADDRESS_SIZE],
                            const uint8_t local_protocol[4], void *reply,
                            uint16_t capacity, uint16_t *reply_length);
uint32_t network_service(network_packet_queue_t *queue,
                         const uint8_t local_hardware[ETHERNET_ADDRESS_SIZE],
                         const uint8_t local_protocol[4], arp_cache_t *cache,
                         uint64_t now, uint32_t budget);
int network_deliver_frame(const void *frame, uint16_t length,
                          udp_endpoint_table_t *udp_table);

#endif
