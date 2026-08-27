#ifndef OS_KERNEL_DRIVERS_USB_H
#define OS_KERNEL_DRIVERS_USB_H

#include <stdint.h>

#define USB_MAX_ENDPOINTS 16U

typedef struct {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
} usb_endpoint_t;

typedef struct {
    uint8_t address;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t endpoint_count;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
} usb_device_t;

int usb_device_parse_descriptor(usb_device_t *device,
                                const uint8_t *descriptor, uint32_t length);
int usb_device_add_endpoint(usb_device_t *device, const uint8_t *descriptor,
                            uint32_t length);

#endif
