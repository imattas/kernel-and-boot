#ifndef OS_KERNEL_DRIVERS_NETWORK_TCP_H
#define OS_KERNEL_DRIVERS_NETWORK_TCP_H

#include <stdint.h>

#define TCP_HEADER_SIZE 20U
#define TCP_MAX_PACKET_SIZE 65535U
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_FLAG_URG 0x20U
#define TCP_FLAG_ECE 0x40U
#define TCP_FLAG_CWR 0x80U
#define TCP_FLAG_KNOWN 0xffU

typedef struct {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgment;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t urgent;
    const uint8_t *payload;
    uint16_t payload_length;
} tcp_segment_view_t;

int tcp_segment_build(void *packet, uint16_t capacity,
                      const uint8_t source_address[4],
                      const uint8_t destination_address[4],
                      uint16_t source_port, uint16_t destination_port,
                      uint32_t sequence, uint32_t acknowledgment,
                      uint8_t flags, uint16_t window, const void *payload,
                      uint16_t payload_length, uint16_t *packet_length);
int tcp_segment_parse(const void *packet, uint16_t packet_length,
                      const uint8_t source_address[4],
                      const uint8_t destination_address[4],
                      tcp_segment_view_t *view);

typedef enum {
    TCP_CONNECTION_CLOSED,
    TCP_CONNECTION_LISTEN,
    TCP_CONNECTION_SYN_SENT,
    TCP_CONNECTION_SYN_RECEIVED,
    TCP_CONNECTION_ESTABLISHED,
    TCP_CONNECTION_FIN_WAIT_1,
    TCP_CONNECTION_FIN_WAIT_2,
    TCP_CONNECTION_CLOSE_WAIT,
    TCP_CONNECTION_LAST_ACK,
    TCP_CONNECTION_TIME_WAIT
} tcp_connection_state_t;

typedef struct {
    tcp_connection_state_t state;
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint16_t window;
    uint16_t peer_window;
    uint16_t local_port;
    uint16_t remote_port;
} tcp_connection_t;

typedef struct {
    uint8_t response_flags;
    uint32_t response_sequence;
    uint32_t response_acknowledgment;
    uint16_t accepted_payload;
} tcp_connection_result_t;

void tcp_connection_initialize(tcp_connection_t *connection,
                               uint16_t local_port, uint16_t window);
int tcp_connection_listen(tcp_connection_t *connection);
int tcp_connection_open(tcp_connection_t *connection, uint32_t sequence,
                        uint16_t remote_port);
int tcp_connection_receive(tcp_connection_t *connection,
                            const tcp_segment_view_t *segment,
                            tcp_connection_result_t *result);
int tcp_connection_build(tcp_connection_t *connection,
                          const uint8_t source_address[4],
                          const uint8_t destination_address[4],
                          uint8_t flags, const void *payload,
                          uint16_t payload_length, void *packet,
                          uint16_t capacity, uint16_t *packet_length);
int tcp_connection_close(tcp_connection_t *connection);

#endif
