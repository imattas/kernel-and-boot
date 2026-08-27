#include "udp_endpoint.h"

static int address_equal(const uint8_t left[4], const uint8_t right[4]) {
    for (uint32_t i = 0; i < 4; ++i)
        if (left[i] != right[i]) return 0;
    return 1;
}

static int address_is_wildcard(const uint8_t address[4]) {
    return address[0] == 0 && address[1] == 0 && address[2] == 0 &&
           address[3] == 0;
}

static udp_endpoint_t *endpoint_for(udp_endpoint_table_t *table,
                                    udp_endpoint_handle_t handle) {
    if (!handle || handle > UDP_ENDPOINT_CAPACITY) return 0;
    udp_endpoint_t *endpoint = &table->endpoints[handle - 1U];
    return endpoint->valid ? endpoint : 0;
}

void udp_endpoint_table_initialize(udp_endpoint_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < UDP_ENDPOINT_CAPACITY; ++i)
        table->endpoints[i].valid = 0;
}

int udp_endpoint_bind(udp_endpoint_table_t *table,
                      const uint8_t local_address[4], uint16_t local_port,
                      udp_endpoint_handle_t *handle) {
    if (!table || !local_address || !handle || local_port == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    int wildcard = address_is_wildcard(local_address);
    for (uint32_t i = 0; i < UDP_ENDPOINT_CAPACITY; ++i) {
        udp_endpoint_t *endpoint = &table->endpoints[i];
        if (!endpoint->valid || endpoint->local_port != local_port) continue;
        if (wildcard || address_is_wildcard(endpoint->local_address) ||
            address_equal(endpoint->local_address, local_address)) {
            spinlock_unlock_irqrestore(&table->lock, flags);
            return 0;
        }
    }
    for (uint32_t i = 0; i < UDP_ENDPOINT_CAPACITY; ++i) {
        udp_endpoint_t *endpoint = &table->endpoints[i];
        if (endpoint->valid) continue;
        endpoint->valid = 1;
        for (uint32_t j = 0; j < 4; ++j) endpoint->local_address[j] = local_address[j];
        endpoint->local_port = local_port;
        endpoint->head = 0;
        endpoint->tail = 0;
        endpoint->count = 0;
        endpoint->dropped = 0;
        *handle = i + 1U;
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 1;
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 0;
}

int udp_endpoint_unbind(udp_endpoint_table_t *table,
                        udp_endpoint_handle_t handle) {
    if (!table) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    udp_endpoint_t *endpoint = endpoint_for(table, handle);
    if (!endpoint) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    endpoint->valid = 0;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}

int udp_endpoint_deliver(udp_endpoint_table_t *table,
                         const uint8_t destination_address[4],
                         uint16_t destination_port,
                         const uint8_t source_address[4], uint16_t source_port,
                         const void *payload, uint16_t length) {
    if (!table || !destination_address || !source_address || !payload ||
        destination_port == 0 || source_port == 0 || length == 0 ||
        length > UDP_ENDPOINT_PAYLOAD_MAX) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    udp_endpoint_t *wildcard = 0;
    udp_endpoint_t *endpoint = 0;
    for (uint32_t i = 0; i < UDP_ENDPOINT_CAPACITY; ++i) {
        udp_endpoint_t *candidate = &table->endpoints[i];
        if (!candidate->valid || candidate->local_port != destination_port) continue;
        if (address_equal(candidate->local_address, destination_address)) {
            endpoint = candidate;
            break;
        }
        if (address_is_wildcard(candidate->local_address)) wildcard = candidate;
    }
    if (!endpoint) endpoint = wildcard;
    if (!endpoint) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    if (endpoint->count == UDP_ENDPOINT_QUEUE_CAPACITY) {
        ++endpoint->dropped;
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    udp_datagram_t *datagram = &endpoint->queue[endpoint->tail];
    for (uint32_t i = 0; i < 4; ++i) datagram->source_address[i] = source_address[i];
    datagram->source_port = source_port;
    datagram->length = length;
    const uint8_t *source_bytes = (const uint8_t *)payload;
    for (uint16_t i = 0; i < length; ++i) datagram->payload[i] = source_bytes[i];
    endpoint->tail = (endpoint->tail + 1U) % UDP_ENDPOINT_QUEUE_CAPACITY;
    ++endpoint->count;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}

int udp_endpoint_receive(udp_endpoint_table_t *table,
                         udp_endpoint_handle_t handle,
                         uint8_t source_address[4], uint16_t *source_port,
                         void *payload, uint16_t capacity, uint16_t *length) {
    if (!table || !source_address || !source_port || !payload || !length) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    udp_endpoint_t *endpoint = endpoint_for(table, handle);
    if (!endpoint || endpoint->count == 0 ||
        endpoint->queue[endpoint->head].length > capacity) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    udp_datagram_t *datagram = &endpoint->queue[endpoint->head];
    for (uint32_t i = 0; i < 4; ++i) source_address[i] = datagram->source_address[i];
    *source_port = datagram->source_port;
    *length = datagram->length;
    uint8_t *destination = (uint8_t *)payload;
    for (uint16_t i = 0; i < datagram->length; ++i) destination[i] = datagram->payload[i];
    endpoint->head = (endpoint->head + 1U) % UDP_ENDPOINT_QUEUE_CAPACITY;
    --endpoint->count;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}

uint32_t udp_endpoint_dropped(udp_endpoint_table_t *table,
                              udp_endpoint_handle_t handle) {
    if (!table) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    udp_endpoint_t *endpoint = endpoint_for(table, handle);
    uint32_t dropped = endpoint ? endpoint->dropped : 0;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return dropped;
}
