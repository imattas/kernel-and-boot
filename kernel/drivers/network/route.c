#include "route.h"

static uint32_t route_mask(uint8_t prefix_length) {
    if (prefix_length == 0) return 0;
    return UINT32_MAX << (32U - prefix_length);
}

static int route_matches(const ipv4_route_entry_t *entry, uint32_t address) {
    return entry->valid && (address & route_mask(entry->prefix_length)) ==
           entry->network;
}

void ipv4_route_table_initialize(ipv4_route_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < IPV4_ROUTE_CAPACITY; ++i)
        table->entries[i].valid = 0;
}

int ipv4_route_add(ipv4_route_table_t *table, uint32_t network,
                   uint8_t prefix_length, uint32_t gateway,
                   uint32_t interface_id, uint16_t metric) {
    if (!table || prefix_length > 32 || interface_id == 0)
        return 0;
    network &= route_mask(prefix_length);
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    uint32_t selected = IPV4_ROUTE_CAPACITY;
    for (uint32_t i = 0; i < IPV4_ROUTE_CAPACITY; ++i) {
        ipv4_route_entry_t *entry = &table->entries[i];
        if (entry->valid && entry->network == network &&
            entry->prefix_length == prefix_length &&
            entry->interface_id == interface_id) {
            selected = i;
            break;
        }
        if (!entry->valid && selected == IPV4_ROUTE_CAPACITY) selected = i;
    }
    if (selected == IPV4_ROUTE_CAPACITY) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    ipv4_route_entry_t *entry = &table->entries[selected];
    entry->network = network;
    entry->gateway = gateway;
    entry->interface_id = interface_id;
    entry->prefix_length = prefix_length;
    entry->metric = metric;
    entry->valid = 1;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}

int ipv4_route_remove(ipv4_route_table_t *table, uint32_t network,
                      uint8_t prefix_length, uint32_t interface_id) {
    if (!table || prefix_length > 32 || interface_id == 0) return 0;
    network &= route_mask(prefix_length);
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < IPV4_ROUTE_CAPACITY; ++i) {
        ipv4_route_entry_t *entry = &table->entries[i];
        if (!entry->valid || entry->network != network ||
            entry->prefix_length != prefix_length ||
            entry->interface_id != interface_id) continue;
        entry->valid = 0;
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 1;
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 0;
}

int ipv4_route_lookup(ipv4_route_table_t *table, uint32_t destination,
                      ipv4_route_entry_t *result) {
    if (!table || !result) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    uint32_t selected = IPV4_ROUTE_CAPACITY;
    for (uint32_t i = 0; i < IPV4_ROUTE_CAPACITY; ++i) {
        ipv4_route_entry_t *entry = &table->entries[i];
        if (!route_matches(entry, destination)) continue;
        if (selected == IPV4_ROUTE_CAPACITY ||
            entry->prefix_length > table->entries[selected].prefix_length ||
            (entry->prefix_length == table->entries[selected].prefix_length &&
             entry->metric < table->entries[selected].metric)) selected = i;
    }
    if (selected == IPV4_ROUTE_CAPACITY) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    *result = table->entries[selected];
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}
