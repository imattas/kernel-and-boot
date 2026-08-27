#include "usb.h"

static uint16_t load16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

int usb_device_parse_descriptor(usb_device_t *device,
                                const uint8_t *descriptor, uint32_t length) {
    if (!device || !descriptor || length < 18 || descriptor[0] < 18 ||
        descriptor[1] != 1 || descriptor[0] > length || descriptor[2] != 0 ||
        descriptor[3] != 2 || load16(&descriptor[8]) == 0 ||
        (descriptor[7] != 8 && descriptor[7] != 16 &&
         descriptor[7] != 32 && descriptor[7] != 64)) return 0;
    device->address = 0;
    device->vendor_id = load16(&descriptor[8]);
    device->product_id = load16(&descriptor[10]);
    device->device_class = descriptor[4];
    device->device_subclass = descriptor[5];
    device->device_protocol = descriptor[6];
    device->endpoint_count = 0;
    return 1;
}

int usb_device_add_endpoint(usb_device_t *device, const uint8_t *descriptor,
                            uint32_t length) {
    uint16_t max_packet;
    uint8_t transfer_type;
    if (!device || !descriptor || length < 7 || descriptor[0] < 7 ||
        descriptor[1] != 5 || descriptor[0] > length ||
        (descriptor[2] & 0x7f) == 0 || (descriptor[2] & 0x7f) > 0x0f ||
        (descriptor[3] & 3) == 0 ||
        ((descriptor[3] & 3) == 3 && descriptor[6] == 0) ||
        (max_packet = load16(&descriptor[4])) == 0 ||
        (max_packet & 0xf800U) != 0 || max_packet > 64 ||
        device->endpoint_count >= USB_MAX_ENDPOINTS) return 0;
    transfer_type = descriptor[3] & 3U;
    if (transfer_type == 2U &&
        max_packet != 8 && max_packet != 16 && max_packet != 32 && max_packet != 64)
        return 0;
    for (uint32_t i = 0; i < device->endpoint_count; ++i)
        if (device->endpoints[i].address == descriptor[2]) return 0;
    usb_endpoint_t *endpoint = &device->endpoints[device->endpoint_count++];
    endpoint->address = descriptor[2];
    endpoint->attributes = descriptor[3];
    endpoint->max_packet_size = max_packet;
    endpoint->interval = descriptor[6];
    return 1;
}
