#ifndef OS_KERNEL_DRIVERS_NETWORK_ARP_CACHE_H
#define OS_KERNEL_DRIVERS_NETWORK_ARP_CACHE_H

#include <stdint.h>
#include "ethernet.h"
#include "../../core/sync/spinlock.h"

#define ARP_CACHE_CAPACITY 16U

typedef struct {
    uint8_t valid;
    uint8_t protocol[4];
    uint8_t hardware[ETHERNET_ADDRESS_SIZE];
    uint64_t last_seen;
} arp_cache_entry_t;

typedef struct {
    spinlock_t lock;
    arp_cache_entry_t entries[ARP_CACHE_CAPACITY];
} arp_cache_t;

void arp_cache_initialize(arp_cache_t *cache);
int arp_cache_update(arp_cache_t *cache,
                     const uint8_t protocol[4],
                     const uint8_t hardware[ETHERNET_ADDRESS_SIZE],
                     uint64_t now);
int arp_cache_lookup(arp_cache_t *cache, const uint8_t protocol[4],
                     uint8_t hardware[ETHERNET_ADDRESS_SIZE]);
uint32_t arp_cache_expire(arp_cache_t *cache, uint64_t now,
                          uint64_t max_age);

#endif
