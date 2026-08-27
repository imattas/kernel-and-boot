#include "tcp_endpoint.h"

static int address_equal(const uint8_t left[4], const uint8_t right[4]) {
    for (uint32_t i = 0; i < 4; ++i) if (left[i] != right[i]) return 0;
    return 1;
}
static int wildcard(const uint8_t address[4]) {
    return address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0;
}
static tcp_endpoint_t *endpoint_for(tcp_endpoint_table_t *table,
                                    tcp_endpoint_handle_t handle) {
    if (!table || handle == 0 || handle > TCP_ENDPOINT_CAPACITY) return 0;
    tcp_endpoint_t *endpoint = &table->endpoints[handle - 1U];
    return endpoint->valid ? endpoint : 0;
}

void tcp_endpoint_table_initialize(tcp_endpoint_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < TCP_ENDPOINT_CAPACITY; ++i)
        table->endpoints[i].valid = 0;
}

int tcp_endpoint_listen(tcp_endpoint_table_t *table,
                        const uint8_t local_address[4], uint16_t local_port,
                        uint16_t window, tcp_endpoint_handle_t *handle) {
    if (!table || !local_address || !handle || local_port == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < TCP_ENDPOINT_CAPACITY; ++i) {
        tcp_endpoint_t *endpoint = &table->endpoints[i];
        if (endpoint->valid && endpoint->connection.local_port == local_port &&
            (wildcard(local_address) || wildcard(endpoint->local_address) ||
             address_equal(local_address, endpoint->local_address))) {
            spinlock_unlock_irqrestore(&table->lock, flags); return 0;
        }
    }
    for (uint32_t i = 0; i < TCP_ENDPOINT_CAPACITY; ++i) {
        tcp_endpoint_t *endpoint = &table->endpoints[i];
        if (endpoint->valid) continue;
        endpoint->valid = 1;
        for (uint32_t j = 0; j < 4; ++j) endpoint->local_address[j] = local_address[j];
        tcp_connection_initialize(&endpoint->connection, local_port, window);
        if (!tcp_connection_listen(&endpoint->connection)) { endpoint->valid = 0; break; }
        endpoint->head = 0; endpoint->tail = 0; endpoint->count = 0; endpoint->dropped = 0;
        *handle = i + 1U;
        spinlock_unlock_irqrestore(&table->lock, flags); return 1;
    }
    spinlock_unlock_irqrestore(&table->lock, flags); return 0;
}

int tcp_endpoint_connect(tcp_endpoint_table_t *table,
                         const uint8_t local_address[4], uint16_t local_port,
                         const uint8_t remote_address[4], uint16_t remote_port,
                         uint32_t sequence, uint16_t window,
                         tcp_endpoint_handle_t *handle, void *segment,
                         uint16_t capacity, uint16_t *segment_length) {
    if (!table || !local_address || !remote_address || !handle || !segment ||
        !segment_length || local_port == 0 || remote_port == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < TCP_ENDPOINT_CAPACITY; ++i) {
        tcp_endpoint_t *endpoint = &table->endpoints[i];
        if (!endpoint->valid || endpoint->connection.local_port != local_port ||
            endpoint->connection.remote_port != remote_port) continue;
        if (address_equal(endpoint->local_address, local_address) &&
            address_equal(endpoint->remote_address, remote_address)) {
            spinlock_unlock_irqrestore(&table->lock, flags); return 0;
        }
    }
    tcp_endpoint_t *endpoint = 0;
    for (uint32_t i = 0; i < TCP_ENDPOINT_CAPACITY; ++i)
        if (!table->endpoints[i].valid) { endpoint = &table->endpoints[i]; *handle = i + 1U; break; }
    if (!endpoint) {
        spinlock_unlock_irqrestore(&table->lock, flags); return 0;
    }
    tcp_connection_initialize(&endpoint->connection, local_port, window);
    if (!tcp_connection_open(&endpoint->connection, sequence, remote_port)) {
        spinlock_unlock_irqrestore(&table->lock, flags); return 0;
    }
    endpoint->valid = 1;
    for (uint32_t i = 0; i < 4; ++i) {
        endpoint->local_address[i] = local_address[i];
        endpoint->remote_address[i] = remote_address[i];
    }
    endpoint->connection.window = window;
    endpoint->head = 0; endpoint->tail = 0; endpoint->count = 0; endpoint->dropped = 0;
    int result = tcp_segment_build(segment, capacity, local_address, remote_address,
                                   local_port, remote_port, sequence, 0,
                                   TCP_FLAG_SYN, window, 0, 0, segment_length);
    if (result) result = tcp_connection_record_segment(&endpoint->connection,
                                                       segment, *segment_length);
    if (!result) endpoint->valid = 0;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return result;
}

int tcp_endpoint_unbind(tcp_endpoint_table_t *table, tcp_endpoint_handle_t handle) {
    if (!table) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    tcp_endpoint_t *endpoint = endpoint_for(table, handle);
    if (!endpoint) { spinlock_unlock_irqrestore(&table->lock, flags); return 0; }
    endpoint->valid = 0;
    spinlock_unlock_irqrestore(&table->lock, flags); return 1;
}

int tcp_endpoint_deliver(tcp_endpoint_table_t *table,
                         const uint8_t destination_address[4],
                         const uint8_t source_address[4],
                         const tcp_segment_view_t *segment,
                         tcp_connection_result_t *result) {
    if (!table || !destination_address || !source_address || !segment || !result ||
        segment->payload_length > TCP_ENDPOINT_PAYLOAD_MAX) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    tcp_endpoint_t *endpoint = 0;
    for (uint32_t i = 0; i < TCP_ENDPOINT_CAPACITY; ++i) {
        tcp_endpoint_t *candidate = &table->endpoints[i];
        if (!candidate->valid || candidate->connection.local_port != segment->destination_port ||
            (!wildcard(candidate->local_address) &&
             !address_equal(candidate->local_address, destination_address))) continue;
        if (candidate->connection.remote_port == 0 ||
            (candidate->connection.remote_port == segment->source_port &&
             address_equal(candidate->remote_address, source_address))) {
            endpoint = candidate; break;
        }
    }
    if (!endpoint || !tcp_connection_receive(&endpoint->connection, segment, result)) {
        spinlock_unlock_irqrestore(&table->lock, flags); return 0;
    }
    if (endpoint->connection.remote_port == segment->source_port)
        for (uint32_t i = 0; i < 4; ++i) endpoint->remote_address[i] = source_address[i];
    if (result->accepted_payload != 0) {
        if (endpoint->count == TCP_ENDPOINT_QUEUE_CAPACITY) {
            ++endpoint->dropped; spinlock_unlock_irqrestore(&table->lock, flags); return 0;
        }
        tcp_stream_data_t *data = &endpoint->queue[endpoint->tail];
        data->length = result->accepted_payload;
        for (uint32_t i = 0; i < 4; ++i) data->source_address[i] = source_address[i];
        for (uint16_t i = 0; i < data->length; ++i) data->payload[i] = segment->payload[i];
        endpoint->tail = (endpoint->tail + 1U) % TCP_ENDPOINT_QUEUE_CAPACITY;
        ++endpoint->count;
    }
    spinlock_unlock_irqrestore(&table->lock, flags); return 1;
}

int tcp_endpoint_send_segment(tcp_endpoint_table_t *table,
                              tcp_endpoint_handle_t handle,
                              const void *payload, uint16_t payload_length,
                              uint8_t flags, void *segment, uint16_t capacity,
                              uint16_t *segment_length) {
    if (!table || !segment || !segment_length) return 0;
    uint64_t lock_flags = spinlock_lock_irqsave(&table->lock);
    tcp_endpoint_t *endpoint = endpoint_for(table, handle);
    int result = endpoint && endpoint->connection.remote_port != 0 &&
        tcp_connection_build(&endpoint->connection, endpoint->local_address,
                             endpoint->remote_address, flags, payload,
                             payload_length, segment, capacity, segment_length);
    spinlock_unlock_irqrestore(&table->lock, lock_flags);
    return result;
}

int tcp_endpoint_retransmit_due(tcp_endpoint_table_t *table,
                                tcp_endpoint_handle_t handle, uint64_t now,
                                uint64_t timeout, void *segment,
                                uint16_t capacity, uint16_t *length) {
    if (!table) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    tcp_endpoint_t *endpoint = endpoint_for(table, handle);
    int result = endpoint && tcp_connection_retransmit_due(
        &endpoint->connection, now, timeout, segment, capacity, length);
    spinlock_unlock_irqrestore(&table->lock, flags);
    return result;
}

int tcp_endpoint_receive(tcp_endpoint_table_t *table, tcp_endpoint_handle_t handle,
                         uint8_t source_address[4], void *payload,
                         uint16_t capacity, uint16_t *length) {
    if (!table || !source_address || !payload || !length) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    tcp_endpoint_t *endpoint = endpoint_for(table, handle);
    if (!endpoint || endpoint->count == 0 || endpoint->queue[endpoint->head].length > capacity) {
        spinlock_unlock_irqrestore(&table->lock, flags); return 0;
    }
    tcp_stream_data_t *data = &endpoint->queue[endpoint->head];
    for (uint32_t i = 0; i < 4; ++i) source_address[i] = data->source_address[i];
    for (uint16_t i = 0; i < data->length; ++i) ((uint8_t *)payload)[i] = data->payload[i];
    *length = data->length; endpoint->head = (endpoint->head + 1U) % TCP_ENDPOINT_QUEUE_CAPACITY;
    --endpoint->count;
    spinlock_unlock_irqrestore(&table->lock, flags); return 1;
}

uint32_t tcp_endpoint_dropped(tcp_endpoint_table_t *table, tcp_endpoint_handle_t handle) {
    if (!table) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    tcp_endpoint_t *endpoint = endpoint_for(table, handle);
    uint32_t dropped = endpoint ? endpoint->dropped : 0;
    spinlock_unlock_irqrestore(&table->lock, flags); return dropped;
}
