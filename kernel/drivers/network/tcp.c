#include "tcp.h"

static uint16_t load_be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static uint32_t load_be32(const uint8_t *value) {
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | value[3];
}

static void store_be16(uint8_t *value, uint16_t number) {
    value[0] = (uint8_t)(number >> 8); value[1] = (uint8_t)number;
}

static void store_be32(uint8_t *value, uint32_t number) {
    value[0] = (uint8_t)(number >> 24); value[1] = (uint8_t)(number >> 16);
    value[2] = (uint8_t)(number >> 8); value[3] = (uint8_t)number;
}

static void add_bytes(uint32_t *sum, const uint8_t *bytes, uint32_t length) {
    for (uint32_t i = 0; i + 1U < length; i += 2)
        *sum += ((uint16_t)bytes[i] << 8) | bytes[i + 1U];
    if ((length & 1U) != 0) *sum += (uint16_t)bytes[length - 1U] << 8;
}

static uint16_t checksum(const uint8_t *packet, uint16_t length,
                         const uint8_t source[4],
                         const uint8_t destination[4]) {
    uint32_t sum = 0;
    add_bytes(&sum, source, 4); add_bytes(&sum, destination, 4);
    sum += 6U; sum += length;
    add_bytes(&sum, packet, length);
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    return (uint16_t)~sum;
}

int tcp_segment_build(void *packet, uint16_t capacity,
                      const uint8_t source_address[4],
                      const uint8_t destination_address[4],
                      uint16_t source_port, uint16_t destination_port,
                      uint32_t sequence, uint32_t acknowledgment,
                      uint8_t flags, uint16_t window, const void *payload,
                      uint16_t payload_length, uint16_t *packet_length) {
    if (!packet || !source_address || !destination_address || !packet_length ||
        source_port == 0 || destination_port == 0 ||
        (flags & ~TCP_FLAG_KNOWN) != 0 ||
        (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ==
            (TCP_FLAG_SYN | TCP_FLAG_FIN) ||
        payload_length > TCP_MAX_PACKET_SIZE - TCP_HEADER_SIZE ||
        (payload_length != 0 && !payload)) return 0;
    uint16_t length = (uint16_t)(TCP_HEADER_SIZE + payload_length);
    if (capacity < length) return 0;
    uint8_t *bytes = (uint8_t *)packet;
    store_be16(bytes + 0, source_port); store_be16(bytes + 2, destination_port);
    store_be32(bytes + 4, sequence); store_be32(bytes + 8, acknowledgment);
    bytes[12] = (uint8_t)(5U << 4); bytes[13] = flags;
    store_be16(bytes + 14, window); store_be16(bytes + 16, 0);
    store_be16(bytes + 18, 0);
    for (uint32_t i = 0; i < payload_length; ++i)
        bytes[TCP_HEADER_SIZE + i] = ((const uint8_t *)payload)[i];
    store_be16(bytes + 16, checksum(bytes, length, source_address,
                                    destination_address));
    *packet_length = length;
    return 1;
}

int tcp_segment_parse(const void *packet, uint16_t packet_length,
                      const uint8_t source_address[4],
                      const uint8_t destination_address[4],
                      tcp_segment_view_t *view) {
    if (!packet || !source_address || !destination_address || !view ||
        packet_length < TCP_HEADER_SIZE) return 0;
    const uint8_t *bytes = (const uint8_t *)packet;
    uint8_t data_offset = (uint8_t)(bytes[12] >> 4);
    uint8_t flags = bytes[13];
    uint16_t header_length = (uint16_t)data_offset * 4U;
    if (data_offset < 5 || header_length > packet_length ||
        load_be16(bytes) == 0 || load_be16(bytes + 2) == 0 ||
        (bytes[12] & 0x0fU) != 0 ||
        (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ==
            (TCP_FLAG_SYN | TCP_FLAG_FIN) ||
        checksum(bytes, packet_length, source_address, destination_address) != 0)
        return 0;
    view->source_port = load_be16(bytes); view->destination_port = load_be16(bytes + 2);
    view->sequence = load_be32(bytes + 4); view->acknowledgment = load_be32(bytes + 8);
    view->data_offset = data_offset; view->flags = flags;
    view->window = load_be16(bytes + 14); view->urgent = load_be16(bytes + 18);
    view->payload = bytes + header_length;
    view->payload_length = (uint16_t)(packet_length - header_length);
    return 1;
}

void tcp_connection_initialize(tcp_connection_t *connection,
                               uint16_t local_port, uint16_t window) {
    if (!connection) return;
    *connection = (tcp_connection_t){.state = TCP_CONNECTION_CLOSED,
        .window = window, .peer_window = window, .local_port = local_port};
}

int tcp_connection_listen(tcp_connection_t *connection) {
    if (!connection || connection->local_port == 0 ||
        connection->state != TCP_CONNECTION_CLOSED) return 0;
    connection->state = TCP_CONNECTION_LISTEN;
    return 1;
}

int tcp_connection_open(tcp_connection_t *connection, uint32_t sequence,
                        uint16_t remote_port) {
    if (!connection || connection->local_port == 0 || remote_port == 0 ||
        connection->state != TCP_CONNECTION_CLOSED) return 0;
    connection->remote_port = remote_port; connection->send_next = sequence + 1U;
    connection->send_unacknowledged = sequence;
    connection->state = TCP_CONNECTION_SYN_SENT;
    return 1;
}

int tcp_connection_receive(tcp_connection_t *connection,
                            const tcp_segment_view_t *segment,
                            tcp_connection_result_t *result) {
    if (!connection || !segment || !result || segment->destination_port !=
        connection->local_port || (connection->remote_port != 0 &&
        segment->source_port != connection->remote_port)) return 0;
    *result = (tcp_connection_result_t){0};
    if ((segment->flags & TCP_FLAG_RST) != 0) {
        connection->state = TCP_CONNECTION_CLOSED;
        result->response_flags = TCP_FLAG_RST;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_LISTEN &&
        (segment->flags & TCP_FLAG_SYN) != 0) {
        connection->remote_port = segment->source_port;
        connection->receive_next = segment->sequence + 1U;
        connection->send_next = 1;
        connection->send_unacknowledged = 0;
        connection->peer_window = segment->window;
        connection->state = TCP_CONNECTION_SYN_RECEIVED;
        result->response_flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
        result->response_sequence = 0; result->response_acknowledgment = connection->receive_next;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_SYN_SENT &&
        (segment->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
            (TCP_FLAG_SYN | TCP_FLAG_ACK) && segment->acknowledgment ==
            connection->send_next) {
        if (segment->acknowledgment < connection->send_unacknowledged)
            return 0;
        connection->send_unacknowledged = connection->send_next;
        connection->peer_window = segment->window;
        connection->retransmission_pending = 0;
        connection->receive_next = segment->sequence + 1U;
        connection->state = TCP_CONNECTION_ESTABLISHED;
        result->response_flags = TCP_FLAG_ACK;
        result->response_sequence = connection->send_next;
        result->response_acknowledgment = connection->receive_next;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_SYN_RECEIVED &&
        (segment->flags & TCP_FLAG_SYN) != 0 &&
        segment->sequence + 1U == connection->receive_next) {
        result->response_flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
        result->response_sequence = connection->send_unacknowledged;
        result->response_acknowledgment = connection->receive_next;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_SYN_RECEIVED &&
        (segment->flags & TCP_FLAG_ACK) != 0 &&
        segment->acknowledgment == connection->send_next) {
        connection->send_unacknowledged = connection->send_next;
        connection->peer_window = segment->window;
        connection->retransmission_pending = 0;
        connection->state = TCP_CONNECTION_ESTABLISHED;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_FIN_WAIT_1 &&
        (segment->flags & TCP_FLAG_ACK) != 0 &&
        segment->acknowledgment == connection->send_next) {
        connection->send_unacknowledged = connection->send_next;
        connection->peer_window = segment->window;
        connection->retransmission_pending = 0;
        connection->state = TCP_CONNECTION_FIN_WAIT_2;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_FIN_WAIT_2 &&
        (segment->flags & TCP_FLAG_FIN) != 0 &&
        segment->sequence == connection->receive_next) {
        ++connection->receive_next; connection->state = TCP_CONNECTION_TIME_WAIT;
        result->response_flags = TCP_FLAG_ACK;
        result->response_sequence = connection->send_next;
        result->response_acknowledgment = connection->receive_next;
        return 1;
    }
    if (connection->state == TCP_CONNECTION_LAST_ACK &&
        (segment->flags & TCP_FLAG_ACK) != 0 &&
        segment->acknowledgment == connection->send_next) {
        connection->send_unacknowledged = connection->send_next;
        connection->state = TCP_CONNECTION_CLOSED; return 1;
    }
    if (connection->state != TCP_CONNECTION_ESTABLISHED &&
        connection->state != TCP_CONNECTION_CLOSE_WAIT) return 0;
    if ((segment->flags & TCP_FLAG_ACK) == 0 ||
        segment->acknowledgment < connection->send_unacknowledged ||
        segment->acknowledgment > connection->send_next) return 0;
    if (segment->sequence != connection->receive_next) {
        result->response_flags = TCP_FLAG_ACK;
        result->response_sequence = connection->send_next;
        result->response_acknowledgment = connection->receive_next;
        return 1;
    }
        connection->send_unacknowledged = segment->acknowledgment;
    connection->peer_window = segment->window;
    if (connection->retransmission_pending &&
        segment->acknowledgment >= connection->retransmission_sequence_end)
        connection->retransmission_pending = 0;
    uint32_t advance = segment->payload_length +
                       ((segment->flags & TCP_FLAG_FIN) != 0);
    connection->receive_next += advance;
    if ((segment->flags & TCP_FLAG_FIN) != 0)
        connection->state = TCP_CONNECTION_CLOSE_WAIT;
    result->response_flags = TCP_FLAG_ACK;
    result->response_sequence = connection->send_next;
    result->response_acknowledgment = connection->receive_next;
    result->accepted_payload = segment->payload_length;
    return 1;
}

int tcp_connection_build(tcp_connection_t *connection,
                          const uint8_t source_address[4],
                          const uint8_t destination_address[4],
                          uint8_t flags, const void *payload,
                          uint16_t payload_length, void *packet,
                          uint16_t capacity, uint16_t *packet_length) {
    if (!connection || !source_address || !destination_address || !packet ||
        !packet_length || (connection->state != TCP_CONNECTION_ESTABLISHED &&
                           connection->state != TCP_CONNECTION_CLOSE_WAIT) ||
        (flags & (TCP_FLAG_SYN | TCP_FLAG_RST)) != 0 ||
        payload_length > TCP_RETRANSMIT_MAX_SIZE - TCP_HEADER_SIZE ||
        (payload_length != 0 && !payload)) return 0;
    if (payload_length != 0) flags |= TCP_FLAG_ACK | TCP_FLAG_PSH;
    if ((flags & TCP_FLAG_FIN) != 0) flags |= TCP_FLAG_ACK;
    uint32_t sequence_space = payload_length +
                              ((flags & TCP_FLAG_FIN) != 0);
    uint32_t outstanding = connection->send_next -
                           connection->send_unacknowledged;
    if (sequence_space > connection->peer_window ||
        outstanding > connection->peer_window - sequence_space) return 0;
    uint32_t sequence = connection->send_next;
    if (!tcp_segment_build(packet, capacity, source_address, destination_address,
                           connection->local_port, connection->remote_port,
                           sequence, connection->receive_next, flags,
                           connection->window, payload, payload_length,
                           packet_length)) return 0;
    connection->send_next += sequence_space;
    if (!tcp_connection_record_segment(connection, packet, *packet_length))
        return 0;
    connection->retransmission_sequence_end = connection->send_next;
    if ((flags & TCP_FLAG_FIN) != 0)
        connection->state = connection->state == TCP_CONNECTION_CLOSE_WAIT ?
            TCP_CONNECTION_LAST_ACK : TCP_CONNECTION_FIN_WAIT_1;
    return 1;
}

int tcp_connection_close(tcp_connection_t *connection) {
    return connection && (connection->state == TCP_CONNECTION_ESTABLISHED ||
                          connection->state == TCP_CONNECTION_CLOSE_WAIT);
}

int tcp_connection_record_segment(tcp_connection_t *connection,
                                  const void *segment, uint16_t length) {
    if (!connection || !segment || length == 0 ||
        length > TCP_RETRANSMIT_MAX_SIZE) return 0;
    const uint8_t *source = (const uint8_t *)segment;
    for (uint16_t i = 0; i < length; ++i)
        connection->retransmission_segment[i] = source[i];
    connection->retransmission_length = length;
    connection->retransmission_retries = 0;
    connection->retransmission_last_tick = 0;
    connection->retransmission_pending = 1;
    return 1;
}

int tcp_connection_retransmit_due(tcp_connection_t *connection,
                                  uint64_t now, uint64_t timeout,
                                  void *segment, uint16_t capacity,
                                  uint16_t *length) {
    if (!connection || !segment || !length || timeout == 0 ||
        !connection->retransmission_pending ||
        connection->retransmission_length > capacity ||
        connection->retransmission_retries >= TCP_RETRANSMIT_MAX_RETRIES ||
        now < connection->retransmission_last_tick ||
        now - connection->retransmission_last_tick < timeout) return 0;
    for (uint16_t i = 0; i < connection->retransmission_length; ++i)
        ((uint8_t *)segment)[i] = connection->retransmission_segment[i];
    *length = connection->retransmission_length;
    connection->retransmission_last_tick = now;
    ++connection->retransmission_retries;
    return 1;
}
