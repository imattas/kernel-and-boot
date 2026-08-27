#include "arp_cache.h"

static int protocol_equal(const uint8_t left[4], const uint8_t right[4]) {
    for (uint32_t i = 0; i < 4; ++i)
        if (left[i] != right[i]) return 0;
    return 1;
}

static int protocol_valid(const uint8_t address[4]) {
    if (!address || (address[0] == 0 && address[1] == 0 &&
                     address[2] == 0 && address[3] == 0) ||
        (address[0] >= 224U && address[0] <= 239U) ||
        (address[0] == 255U && address[1] == 255U &&
         address[2] == 255U && address[3] == 255U)) return 0;
    return 1;
}

static int hardware_valid(const uint8_t address[ETHERNET_ADDRESS_SIZE]) {
    if (!address || (address[0] & 1U) != 0) return 0;
    for (uint32_t i = 0; i < ETHERNET_ADDRESS_SIZE; ++i)
        if (address[i] != 0) return 1;
    return 0;
}

void arp_cache_initialize(arp_cache_t *cache) {
    if (!cache) return;
    spinlock_init(&cache->lock);
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i)
        cache->entries[i].valid = 0;
}

int arp_cache_update(arp_cache_t *cache, const uint8_t protocol[4],
                     const uint8_t hardware[ETHERNET_ADDRESS_SIZE],
                     uint64_t now) {
    if (!cache || !protocol_valid(protocol) || !hardware_valid(hardware))
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    uint32_t selected = ARP_CACHE_CAPACITY;
    uint32_t oldest = 0;
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
        if (cache->entries[i].valid &&
            protocol_equal(cache->entries[i].protocol, protocol)) {
            selected = i;
            break;
        }
        if (!cache->entries[i].valid) selected = i;
        else if (selected == ARP_CACHE_CAPACITY &&
                 cache->entries[i].last_seen < cache->entries[oldest].last_seen)
            oldest = i;
    }
    if (selected == ARP_CACHE_CAPACITY) selected = oldest;
    arp_cache_entry_t *entry = &cache->entries[selected];
    entry->valid = 1;
    for (uint32_t i = 0; i < 4; ++i) entry->protocol[i] = protocol[i];
    for (uint32_t i = 0; i < ETHERNET_ADDRESS_SIZE; ++i)
        entry->hardware[i] = hardware[i];
    entry->last_seen = now;
    spinlock_unlock_irqrestore(&cache->lock, flags);
    return 1;
}

int arp_cache_lookup(arp_cache_t *cache, const uint8_t protocol[4],
                     uint8_t hardware[ETHERNET_ADDRESS_SIZE]) {
    if (!cache || !protocol_valid(protocol) || !hardware) return 0;
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
        arp_cache_entry_t *entry = &cache->entries[i];
        if (!entry->valid || !protocol_equal(entry->protocol, protocol)) continue;
        for (uint32_t j = 0; j < ETHERNET_ADDRESS_SIZE; ++j)
            hardware[j] = entry->hardware[j];
        spinlock_unlock_irqrestore(&cache->lock, flags);
        return 1;
    }
    spinlock_unlock_irqrestore(&cache->lock, flags);
    return 0;
}

uint32_t arp_cache_expire(arp_cache_t *cache, uint64_t now, uint64_t max_age) {
    if (!cache || max_age == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&cache->lock);
    uint32_t expired = 0;
    for (uint32_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
        arp_cache_entry_t *entry = &cache->entries[i];
        if (!entry->valid || now < entry->last_seen ||
            now - entry->last_seen < max_age) continue;
        entry->valid = 0;
        ++expired;
    }
    spinlock_unlock_irqrestore(&cache->lock, flags);
    return expired;
}
