#ifndef OS_KERNEL_DRIVERS_NETWORK_ROUTE_H
#define OS_KERNEL_DRIVERS_NETWORK_ROUTE_H

#include <stdint.h>
#include "../../core/sync/spinlock.h"

#define IPV4_ROUTE_CAPACITY 16U
#define IPV4_ROUTE_DEFAULT_PREFIX 0U

typedef struct {
    uint32_t network;
    uint32_t gateway;
    uint32_t interface_id;
    uint8_t prefix_length;
    uint16_t metric;
    uint8_t valid;
} ipv4_route_entry_t;

typedef struct {
    spinlock_t lock;
    ipv4_route_entry_t entries[IPV4_ROUTE_CAPACITY];
} ipv4_route_table_t;

void ipv4_route_table_initialize(ipv4_route_table_t *table);
int ipv4_route_add(ipv4_route_table_t *table, uint32_t network,
                   uint8_t prefix_length, uint32_t gateway,
                   uint32_t interface_id, uint16_t metric);
int ipv4_route_remove(ipv4_route_table_t *table, uint32_t network,
                      uint8_t prefix_length, uint32_t interface_id);
int ipv4_route_lookup(ipv4_route_table_t *table, uint32_t destination,
                      ipv4_route_entry_t *result);

#endif
