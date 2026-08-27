#include "device.h"
#include "../core/sync/spinlock.h"

static device_t registry[DEVICE_REGISTRY_CAPACITY];
static uint32_t registry_count;
static const device_driver_t *drivers[DEVICE_DRIVER_CAPACITY];
static uint32_t driver_count;
static spinlock_t resource_lock;
static spinlock_t registry_lock;
static spinlock_t binding_lock;

static int same_name(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static int same_pci_device(const device_t *left, const device_t *right) {
    return left->bus == DEVICE_BUS_PCI && right->bus == DEVICE_BUS_PCI &&
           left->bus_number == right->bus_number && left->slot == right->slot &&
           left->function == right->function;
}

void device_registry_initialize(void) {
    spinlock_init(&resource_lock);
    spinlock_init(&registry_lock);
    spinlock_init(&binding_lock);
    registry_count = 0;
    driver_count = 0;
}

int device_register(const device_t *device) {
    if (!device) return 0;
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    if (registry_count >= DEVICE_REGISTRY_CAPACITY) {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return 0;
    }
    for (uint32_t i = 0; i < registry_count; ++i)
        if (same_pci_device(&registry[i], device)) {
            spinlock_unlock_irqrestore(&registry_lock, flags);
            return 0;
        }
    registry[registry_count++] = *device;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return 1;
}

uint32_t device_count(void) {
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    uint32_t count = registry_count;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return count;
}

const device_t *device_at(uint32_t index) {
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    const device_t *device = index < registry_count ? &registry[index] : 0;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return device;
}

int device_driver_register(const device_driver_t *driver) {
    if (!driver) return 0;
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    if (!driver || !driver->name || !driver->match || !driver->probe ||
        driver_count >= DEVICE_DRIVER_CAPACITY) {
        spinlock_unlock_irqrestore(&registry_lock, flags);
        return 0;
    }
    for (uint32_t i = 0; i < driver_count; ++i)
        if (same_name(drivers[i]->name, driver->name)) {
            spinlock_unlock_irqrestore(&registry_lock, flags);
            return 0;
        }
    drivers[driver_count++] = driver;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return 1;
}

int device_bind_drivers(void) {
    uint64_t binding_flags = spinlock_lock_irqsave(&binding_lock);
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    uint32_t devices = registry_count;
    uint32_t drivers_total = driver_count;
    const device_driver_t *driver_snapshot[DEVICE_DRIVER_CAPACITY];
    for (uint32_t i = 0; i < drivers_total; ++i)
        driver_snapshot[i] = drivers[i];
    spinlock_unlock_irqrestore(&registry_lock, flags);
    for (uint32_t i = 0; i < devices; ++i) {
        flags = spinlock_lock_irqsave(&registry_lock);
        int already_bound = registry[i].driver != 0;
        spinlock_unlock_irqrestore(&registry_lock, flags);
        if (already_bound) continue;
        for (uint32_t j = 0; j < drivers_total; ++j) {
            const device_driver_t *driver = driver_snapshot[j];
            if (driver->bus != registry[i].bus || !driver->match(&registry[i]))
                continue;
            if (driver->probe(&registry[i])) {
                flags = spinlock_lock_irqsave(&registry_lock);
                if (!registry[i].driver) registry[i].driver = driver;
                spinlock_unlock_irqrestore(&registry_lock, flags);
                break;
            }
        }
    }
    spinlock_unlock_irqrestore(&binding_lock, binding_flags);
    return 1;
}

uint32_t device_driver_count(void) {
    uint64_t flags = spinlock_lock_irqsave(&registry_lock);
    uint32_t count = driver_count;
    spinlock_unlock_irqrestore(&registry_lock, flags);
    return count;
}

int device_claim_resource(device_t *device, uint32_t index,
                          const device_driver_t *driver) {
    if (!device || !driver || index >= 6 || device->resources[index].size == 0)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&resource_lock);
    if (device->resource_owner[index]) {
        spinlock_unlock_irqrestore(&resource_lock, flags);
        return 0;
    }
    device->resource_owner[index] = driver;
    spinlock_unlock_irqrestore(&resource_lock, flags);
    return 1;
}

void device_release_resource(device_t *device, uint32_t index,
                             const device_driver_t *driver) {
    if (!device || index >= 6) return;
    uint64_t flags = spinlock_lock_irqsave(&resource_lock);
    if (device->resource_owner[index] == driver)
        device->resource_owner[index] = 0;
    spinlock_unlock_irqrestore(&resource_lock, flags);
}

const device_driver_t *device_resource_owner(const device_t *device,
                                             uint32_t index) {
    if (!device || index >= 6) return 0;
    uint64_t flags = spinlock_lock_irqsave(&resource_lock);
    const device_driver_t *owner = device->resource_owner[index];
    spinlock_unlock_irqrestore(&resource_lock, flags);
    return owner;
}
