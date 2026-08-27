#include "packet_queue.h"

void network_packet_queue_initialize(network_packet_queue_t *queue) {
    if (!queue) return;
    spinlock_init(&queue->lock);
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->dropped = 0;
}

int network_packet_queue_push(network_packet_queue_t *queue,
                              const void *packet, uint16_t length) {
    if (!queue || !packet || length < ETHERNET_MIN_FRAME_SIZE ||
        length > ETHERNET_MAX_FRAME_SIZE) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    if (queue->count == NETWORK_PACKET_QUEUE_CAPACITY) {
        ++queue->dropped;
        spinlock_unlock_irqrestore(&queue->lock, flags);
        return 0;
    }
    network_packet_t *entry = &queue->packets[queue->tail];
    const uint8_t *source = (const uint8_t *)packet;
    for (uint16_t i = 0; i < length; ++i) entry->bytes[i] = source[i];
    entry->length = length;
    queue->tail = (queue->tail + 1U) % NETWORK_PACKET_QUEUE_CAPACITY;
    ++queue->count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return 1;
}

int network_packet_queue_pop(network_packet_queue_t *queue,
                             void *packet, uint16_t capacity,
                             uint16_t *length) {
    if (!queue || !packet || !length || capacity < ETHERNET_MIN_FRAME_SIZE)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    if (queue->count == 0 || queue->packets[queue->head].length > capacity) {
        spinlock_unlock_irqrestore(&queue->lock, flags);
        return 0;
    }
    network_packet_t *entry = &queue->packets[queue->head];
    uint8_t *destination = (uint8_t *)packet;
    for (uint16_t i = 0; i < entry->length; ++i)
        destination[i] = entry->bytes[i];
    *length = entry->length;
    queue->head = (queue->head + 1U) % NETWORK_PACKET_QUEUE_CAPACITY;
    --queue->count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return 1;
}

uint32_t network_packet_queue_count(network_packet_queue_t *queue) {
    if (!queue) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    uint32_t count = queue->count;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return count;
}

uint32_t network_packet_queue_dropped(network_packet_queue_t *queue) {
    if (!queue) return 0;
    uint64_t flags = spinlock_lock_irqsave(&queue->lock);
    uint32_t dropped = queue->dropped;
    spinlock_unlock_irqrestore(&queue->lock, flags);
    return dropped;
}
