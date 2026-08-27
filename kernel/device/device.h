#ifndef OS_KERNEL_DEVICE_DEVICE_H
#define OS_KERNEL_DEVICE_DEVICE_H

#include <stdint.h>

#define DEVICE_REGISTRY_CAPACITY 128U
#define DEVICE_DRIVER_CAPACITY 32U

typedef enum {
    DEVICE_BUS_PCI = 1
} device_bus_t;

typedef struct device device_t;
typedef struct device_driver device_driver_t;

typedef int (*device_match_fn)(const device_t *device);
typedef int (*device_probe_fn)(device_t *device);

struct device_driver {
    const char *name;
    device_bus_t bus;
    device_match_fn match;
    device_probe_fn probe;
};

typedef struct {
    uint64_t address;
    uint64_t size;
    uint32_t flags;
} device_resource_t;

struct device {
    device_bus_t bus;
    uint8_t bus_number;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    device_resource_t resources[6];
    const device_driver_t *resource_owner[6];
    const device_driver_t *driver;
};

void device_registry_initialize(void);
int device_register(const device_t *device);
uint32_t device_count(void);
const device_t *device_at(uint32_t index);
int device_driver_register(const device_driver_t *driver);
int device_bind_drivers(void);
uint32_t device_driver_count(void);
int device_claim_resource(device_t *device, uint32_t index,
                          const device_driver_t *driver);
void device_release_resource(device_t *device, uint32_t index,
                             const device_driver_t *driver);
const device_driver_t *device_resource_owner(const device_t *device,
                                             uint32_t index);

#endif
