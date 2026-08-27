#ifndef OS_KERNEL_DRIVERS_NETWORK_TCP_ENDPOINT_H
#define OS_KERNEL_DRIVERS_NETWORK_TCP_ENDPOINT_H

#include <stdint.h>
#include "tcp.h"
#include "../../core/sync/spinlock.h"

#define TCP_ENDPOINT_CAPACITY 8U
#define TCP_ENDPOINT_QUEUE_CAPACITY 8U
#define TCP_ENDPOINT_PAYLOAD_MAX 1500U

typedef uint32_t tcp_endpoint_handle_t;
typedef struct {
    uint16_t length;
    uint8_t source_address[4];
    uint8_t payload[TCP_ENDPOINT_PAYLOAD_MAX];
} tcp_stream_data_t;

typedef struct {
    uint8_t valid;
    uint8_t local_address[4];
    uint8_t remote_address[4];
    tcp_connection_t connection;
    tcp_stream_data_t queue[TCP_ENDPOINT_QUEUE_CAPACITY];
    uint32_t head, tail, count, dropped;
} tcp_endpoint_t;

typedef struct {
    spinlock_t lock;
    tcp_endpoint_t endpoints[TCP_ENDPOINT_CAPACITY];
} tcp_endpoint_table_t;

void tcp_endpoint_table_initialize(tcp_endpoint_table_t *table);
int tcp_endpoint_listen(tcp_endpoint_table_t *table,
                        const uint8_t local_address[4], uint16_t local_port,
                        uint16_t window, tcp_endpoint_handle_t *handle);
int tcp_endpoint_connect(tcp_endpoint_table_t *table,
                         const uint8_t local_address[4], uint16_t local_port,
                         const uint8_t remote_address[4], uint16_t remote_port,
                         uint32_t sequence, uint16_t window,
                         tcp_endpoint_handle_t *handle, void *segment,
                         uint16_t capacity, uint16_t *segment_length);
int tcp_endpoint_unbind(tcp_endpoint_table_t *table,
                        tcp_endpoint_handle_t handle);
int tcp_endpoint_deliver(tcp_endpoint_table_t *table,
                         const uint8_t destination_address[4],
                         const uint8_t source_address[4],
                         const tcp_segment_view_t *segment,
                         tcp_connection_result_t *result);
int tcp_endpoint_send_segment(tcp_endpoint_table_t *table,
                              tcp_endpoint_handle_t handle,
                              const void *payload, uint16_t payload_length,
                              uint8_t flags, void *segment, uint16_t capacity,
                              uint16_t *segment_length);
int tcp_endpoint_retransmit_due(tcp_endpoint_table_t *table,
                                tcp_endpoint_handle_t handle, uint64_t now,
                                uint64_t timeout, void *segment,
                                uint16_t capacity, uint16_t *length);
int tcp_endpoint_receive(tcp_endpoint_table_t *table,
                         tcp_endpoint_handle_t handle,
                         uint8_t source_address[4], void *payload,
                         uint16_t capacity, uint16_t *length);
uint32_t tcp_endpoint_dropped(tcp_endpoint_table_t *table,
                              tcp_endpoint_handle_t handle);

#endif
