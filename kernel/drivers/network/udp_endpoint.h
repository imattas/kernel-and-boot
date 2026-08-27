#ifndef OS_KERNEL_DRIVERS_NETWORK_UDP_ENDPOINT_H
#define OS_KERNEL_DRIVERS_NETWORK_UDP_ENDPOINT_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

#define UDP_ENDPOINT_CAPACITY 16U
#define UDP_ENDPOINT_QUEUE_CAPACITY 8U
#define UDP_ENDPOINT_PAYLOAD_MAX 1500U

typedef uint32_t udp_endpoint_handle_t;

typedef struct {
    uint8_t source_address[4];
    uint16_t source_port;
    uint16_t length;
    uint8_t payload[UDP_ENDPOINT_PAYLOAD_MAX];
} udp_datagram_t;

typedef struct {
    uint8_t valid;
    uint8_t local_address[4];
    uint16_t local_port;
    udp_datagram_t queue[UDP_ENDPOINT_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t dropped;
} udp_endpoint_t;

typedef struct {
    spinlock_t lock;
    udp_endpoint_t endpoints[UDP_ENDPOINT_CAPACITY];
} udp_endpoint_table_t;

void udp_endpoint_table_initialize(udp_endpoint_table_t *table);
int udp_endpoint_bind(udp_endpoint_table_t *table,
                      const uint8_t local_address[4], uint16_t local_port,
                      udp_endpoint_handle_t *handle);
int udp_endpoint_unbind(udp_endpoint_table_t *table,
                        udp_endpoint_handle_t handle);
int udp_endpoint_deliver(udp_endpoint_table_t *table,
                         const uint8_t destination_address[4],
                         uint16_t destination_port,
                         const uint8_t source_address[4], uint16_t source_port,
                         const void *payload, uint16_t length);
int udp_endpoint_receive(udp_endpoint_table_t *table,
                         udp_endpoint_handle_t handle,
                         uint8_t source_address[4], uint16_t *source_port,
                         void *payload, uint16_t capacity, uint16_t *length);
uint32_t udp_endpoint_dropped(udp_endpoint_table_t *table,
                              udp_endpoint_handle_t handle);

#endif
