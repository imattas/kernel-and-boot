#include "bochs_vga.h"
#include "../../device/device.h"
#include "../pci/pci.h"

#define BOCHS_VENDOR 0x1234U
#define BOCHS_DEVICE 0x1111U
#define VBE_INDEX 0x01ceU
#define VBE_DATA  0x01cfU
#define VBE_ID 0
#define VBE_XRES 1
#define VBE_YRES 2
#define VBE_BPP 3
#define VBE_ENABLE 4
#define VBE_VIRT_WIDTH 6
#define VBE_VIRT_HEIGHT 7
#define VBE_LFB_ENABLED 0x40U
#define VBE_ENABLED 0x01U

static uint64_t lfb_address;
static uint64_t lfb_size;
static uint32_t mode_width;
static uint32_t mode_height;
static int initialized;
static const device_driver_t bochs_resource_owner = {
    "bochs-vga", DEVICE_BUS_PCI, 0, 0
};

static void out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port));
}

static uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void vbe_write(uint16_t index, uint16_t value) {
    out16(VBE_INDEX, index);
    out16(VBE_DATA, value);
}

static uint16_t vbe_read(uint16_t index) {
    out16(VBE_INDEX, index);
    return in16(VBE_DATA);
}

int bochs_vga_initialize(uint32_t width, uint32_t height) {
    initialized = 0;
    lfb_address = 0;
    lfb_size = 0;
    mode_width = 0;
    mode_height = 0;
    if (width == 0 || height == 0 || width > 4096U || height > 4096U ||
        (uint64_t)width * height > UINT64_MAX / 4U)
        return 0;
    uint64_t required = (uint64_t)width * height * 4U;
    for (uint32_t index = 0; index < device_count(); ++index) {
        const device_t *device = device_at(index);
        if (!device || device->vendor_id != BOCHS_VENDOR ||
            device->device_id != BOCHS_DEVICE)
            continue;
        device_t *mutable_device = (device_t *)(uintptr_t)device;
        uint32_t bar = 6;
        for (uint32_t candidate = 0; candidate < 6; ++candidate) {
            if ((device->resources[candidate].flags & 1U) == 0 &&
                device->resources[candidate].address != 0 &&
                device->resources[candidate].size >= required) {
                bar = candidate;
                break;
            }
        }
        if (bar == 6 || !device_claim_resource(mutable_device, bar,
                                                &bochs_resource_owner))
            continue;
        uint32_t command = pci_config_read32(device->bus_number,
                                             device->slot, device->function, 4);
        pci_config_write32(device->bus_number, device->slot, device->function,
                           4, command | 3U);
        if (vbe_read(VBE_ID) < 0xb0c0U || vbe_read(VBE_ID) > 0xb0c5U) {
            device_release_resource(mutable_device, bar, &bochs_resource_owner);
            continue;
        }
        vbe_write(VBE_ENABLE, 0);
        vbe_write(VBE_XRES, (uint16_t)width);
        vbe_write(VBE_YRES, (uint16_t)height);
        vbe_write(VBE_BPP, 32);
        vbe_write(VBE_VIRT_WIDTH, (uint16_t)width);
        vbe_write(VBE_VIRT_HEIGHT, (uint16_t)height);
        vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB_ENABLED);
        if (vbe_read(VBE_XRES) != width || vbe_read(VBE_YRES) != height ||
            vbe_read(VBE_BPP) != 32) {
            device_release_resource(mutable_device, bar, &bochs_resource_owner);
            continue;
        }
        lfb_address = device->resources[bar].address;
        lfb_size = device->resources[bar].size;
        mode_width = width;
        mode_height = height;
        initialized = 1;
        return 1;
    }
    return 0;
}

int bochs_vga_present(void) { return initialized; }
uint64_t bochs_vga_framebuffer(void) { return lfb_address; }
uint64_t bochs_vga_framebuffer_size(void) { return lfb_size; }
uint32_t bochs_vga_width(void) { return mode_width; }
uint32_t bochs_vga_height(void) { return mode_height; }
