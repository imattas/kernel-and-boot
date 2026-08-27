#include "device.h"

static device_t registry[DEVICE_REGISTRY_CAPACITY];
static uint32_t registry_count;
static const device_driver_t *drivers[DEVICE_DRIVER_CAPACITY];
static uint32_t driver_count;

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
    registry_count = 0;
    driver_count = 0;
}

int device_register(const device_t *device) {
    if (!device || registry_count >= DEVICE_REGISTRY_CAPACITY) return 0;
    for (uint32_t i = 0; i < registry_count; ++i)
        if (same_pci_device(&registry[i], device)) return 0;
    registry[registry_count++] = *device;
    return 1;
}

uint32_t device_count(void) { return registry_count; }

const device_t *device_at(uint32_t index) {
    return index < registry_count ? &registry[index] : 0;
}

int device_driver_register(const device_driver_t *driver) {
    if (!driver || !driver->name || !driver->match || !driver->probe ||
        driver_count >= DEVICE_DRIVER_CAPACITY) return 0;
    for (uint32_t i = 0; i < driver_count; ++i)
        if (same_name(drivers[i]->name, driver->name)) return 0;
    drivers[driver_count++] = driver;
    return 1;
}

int device_bind_drivers(void) {
    for (uint32_t i = 0; i < registry_count; ++i) {
        if (registry[i].driver) continue;
        for (uint32_t j = 0; j < driver_count; ++j) {
            const device_driver_t *driver = drivers[j];
            if (driver->bus != registry[i].bus || !driver->match(&registry[i]))
                continue;
            if (driver->probe(&registry[i])) {
                registry[i].driver = driver;
                break;
            }
        }
    }
    return 1;
}

uint32_t device_driver_count(void) { return driver_count; }

int device_claim_resource(device_t *device, uint32_t index,
                          const device_driver_t *driver) {
    if (!device || !driver || index >= 6 || device->resources[index].size == 0 ||
        device->resource_owner[index]) return 0;
    device->resource_owner[index] = driver;
    return 1;
}

void device_release_resource(device_t *device, uint32_t index,
                             const device_driver_t *driver) {
    if (device && index < 6 && device->resource_owner[index] == driver)
        device->resource_owner[index] = 0;
}

const device_driver_t *device_resource_owner(const device_t *device,
                                             uint32_t index) {
    return device && index < 6 ? device->resource_owner[index] : 0;
}
