#include "reassembly.h"

static int address_equal(const uint8_t left[4], const uint8_t right[4]) {
    for (uint32_t i = 0; i < 4; ++i)
        if (left[i] != right[i]) return 0;
    return 1;
}

static void clear_entry(ipv4_reassembly_entry_t *entry) {
    entry->valid = 0;
    entry->first_fragment = 0;
    entry->total_length = 0;
    entry->received = 0;
}

static int same_datagram(const ipv4_reassembly_entry_t *entry,
                         uint16_t identification, const uint8_t source[4],
                         const uint8_t destination[4], uint8_t protocol) {
    return entry->valid && entry->identification == identification &&
           entry->protocol == protocol && address_equal(entry->source, source) &&
           address_equal(entry->destination, destination);
}

void ipv4_reassembly_initialize(ipv4_reassembly_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < IPV4_REASSEMBLY_SLOTS; ++i)
        clear_entry(&table->entries[i]);
}

int ipv4_reassembly_add(ipv4_reassembly_table_t *table, uint16_t identification,
                        const uint8_t source[4], const uint8_t destination[4],
                        uint8_t protocol, uint16_t offset, uint8_t more,
                        const void *payload, uint16_t payload_length,
                        uint64_t now, uint64_t timeout, void *output,
                        uint16_t capacity, uint16_t *output_length) {
    if (!table || !source || !destination || !payload || !output ||
        !output_length || payload_length == 0 || offset >= IPV4_REASSEMBLY_MAX_PAYLOAD ||
        payload_length > IPV4_REASSEMBLY_MAX_PAYLOAD - offset ||
        (more && (payload_length & 7U) != 0)) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < IPV4_REASSEMBLY_SLOTS; ++i) {
        ipv4_reassembly_entry_t *entry = &table->entries[i];
        if (entry->valid && timeout != 0 && now >= entry->last_seen &&
            now - entry->last_seen >= timeout) clear_entry(entry);
    }
    uint32_t selected = IPV4_REASSEMBLY_SLOTS;
    uint32_t oldest = 0;
    for (uint32_t i = 0; i < IPV4_REASSEMBLY_SLOTS; ++i) {
        if (same_datagram(&table->entries[i], identification, source,
                          destination, protocol)) {
            selected = i;
            break;
        }
        if (!table->entries[i].valid) selected = i;
        else if (selected == IPV4_REASSEMBLY_SLOTS &&
                 table->entries[i].last_seen < table->entries[oldest].last_seen)
            oldest = i;
    }
    if (selected == IPV4_REASSEMBLY_SLOTS) selected = oldest;
    ipv4_reassembly_entry_t *entry = &table->entries[selected];
    if (!entry->valid) {
        entry->valid = 1;
        entry->protocol = protocol;
        entry->identification = identification;
        for (uint32_t i = 0; i < 4; ++i) {
            entry->source[i] = source[i];
            entry->destination[i] = destination[i];
        }
        entry->total_length = 0;
        entry->received = 0;
        entry->first_fragment = 0;
        for (uint32_t i = 0; i < IPV4_REASSEMBLY_BITMAP_SIZE; ++i)
            entry->bitmap[i] = 0;
    }
    uint32_t end = (uint32_t)offset + payload_length;
    if ((entry->total_length != 0 && end > entry->total_length) ||
        (!more && entry->total_length != 0 && entry->total_length != end)) {
        clear_entry(entry);
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    if (!more) entry->total_length = (uint16_t)end;
    for (uint32_t i = offset; i < end; ++i) {
        if ((entry->bitmap[i / 8U] & (uint8_t)(1U << (i & 7U))) != 0) {
            clear_entry(entry);
            spinlock_unlock_irqrestore(&table->lock, flags);
            return 0;
        }
    }
    const uint8_t *source_bytes = (const uint8_t *)payload;
    for (uint32_t i = 0; i < payload_length; ++i) {
        uint32_t position = offset + i;
        entry->payload[position] = source_bytes[i];
        entry->bitmap[position / 8U] |= (uint8_t)(1U << (position & 7U));
    }
    entry->received += payload_length;
    if (offset == 0) entry->first_fragment = 1;
    entry->last_seen = now;
    if (!entry->first_fragment || entry->total_length == 0 ||
        entry->received != entry->total_length ||
        capacity < entry->total_length) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    uint8_t *destination_bytes = (uint8_t *)output;
    for (uint32_t i = 0; i < entry->total_length; ++i)
        destination_bytes[i] = entry->payload[i];
    *output_length = entry->total_length;
    clear_entry(entry);
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}
