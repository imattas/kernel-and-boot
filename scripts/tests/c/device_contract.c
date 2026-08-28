#include <assert.h>
#include <stdint.h>
#include "kernel/device/device.h"

static device_t *bound_device;
static const device_driver_t *fallback_driver;

static int matches(const device_t *device) { return device && device->device_id == 1; }
static int claim_then_fail(device_t *device) {
    extern const device_driver_t failing;
    assert(device_claim_resource(device, 0, &failing));
    return 0;
}

static int fallback_probe(device_t *device) {
    assert(device_resource_owner(device, 0) == 0);
    assert(device_claim_resource(device, 0, fallback_driver));
    bound_device = device;
    return 1;
}

const device_driver_t failing = {"failing", DEVICE_BUS_PCI, matches, claim_then_fail};

int main(void) {
    device_registry_initialize();
    device_t device = {0};
    device.bus = DEVICE_BUS_PCI;
    device.device_id = 1;
    device.resources[0].address = 0x1000;
    device.resources[0].size = 0x100;
    assert(device_register(&device));
    device_driver_t fallback = {"fallback", DEVICE_BUS_PCI, matches, fallback_probe};
    fallback_driver = &fallback;
    assert(device_driver_register(&failing));
    assert(device_driver_register(&fallback));
    assert(device_bind_drivers());
    assert(bound_device && device_resource_owner(bound_device, 0) == &fallback);
    assert(device_at(0)->driver == &fallback);
    return 0;
}
