#ifndef OS_KERNEL_DRIVERS_NETWORK_REASSEMBLY_H
#define OS_KERNEL_DRIVERS_NETWORK_REASSEMBLY_H

#include <stdint.h>
#include "ipv4.h"
#include "../../core/sync/spinlock.h"

#define IPV4_REASSEMBLY_SLOTS 4U
#define IPV4_REASSEMBLY_MAX_PAYLOAD (IPV4_MAX_PACKET_SIZE - IPV4_MIN_HEADER_SIZE)
#define IPV4_REASSEMBLY_BITMAP_SIZE ((IPV4_REASSEMBLY_MAX_PAYLOAD + 7U) / 8U)

typedef struct {
    uint8_t valid;
    uint8_t first_fragment;
    uint8_t protocol;
    uint8_t source[4];
    uint8_t destination[4];
    uint16_t identification;
    uint16_t total_length;
    uint32_t received;
    uint64_t last_seen;
    uint8_t payload[IPV4_REASSEMBLY_MAX_PAYLOAD];
    uint8_t bitmap[IPV4_REASSEMBLY_BITMAP_SIZE];
} ipv4_reassembly_entry_t;

typedef struct {
    spinlock_t lock;
    ipv4_reassembly_entry_t entries[IPV4_REASSEMBLY_SLOTS];
} ipv4_reassembly_table_t;

void ipv4_reassembly_initialize(ipv4_reassembly_table_t *table);
int ipv4_reassembly_add(ipv4_reassembly_table_t *table, uint16_t identification,
                        const uint8_t source[4], const uint8_t destination[4],
                        uint8_t protocol, uint16_t offset, uint8_t more,
                        const void *payload, uint16_t payload_length,
                        uint64_t now, uint64_t timeout, void *output,
                        uint16_t capacity, uint16_t *output_length);

#endif
