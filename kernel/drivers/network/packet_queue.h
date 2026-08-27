#ifndef OS_KERNEL_DRIVERS_NETWORK_PACKET_QUEUE_H
#define OS_KERNEL_DRIVERS_NETWORK_PACKET_QUEUE_H

#include <stdint.h>
#include "ethernet.h"
#include "../../core/sync/spinlock.h"

#define NETWORK_PACKET_QUEUE_CAPACITY 16U

typedef struct {
    uint16_t length;
    uint8_t bytes[ETHERNET_MAX_FRAME_SIZE];
} network_packet_t;

typedef struct {
    spinlock_t lock;
    network_packet_t packets[NETWORK_PACKET_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t dropped;
} network_packet_queue_t;

void network_packet_queue_initialize(network_packet_queue_t *queue);
int network_packet_queue_push(network_packet_queue_t *queue,
                              const void *packet, uint16_t length);
int network_packet_queue_pop(network_packet_queue_t *queue,
                             void *packet, uint16_t capacity,
                             uint16_t *length);
uint32_t network_packet_queue_count(network_packet_queue_t *queue);
uint32_t network_packet_queue_dropped(network_packet_queue_t *queue);

#endif
