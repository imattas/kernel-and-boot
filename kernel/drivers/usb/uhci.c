#include "uhci.h"
#include "../../device/device.h"
#include "../pci/pci.h"
#include "../../mm/physical/frame.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../../core/sync/spinlock.h"

#define UHCI_BAR_INDEX 4
#define UHCI_USBCMD 0x00
#define UHCI_USBSTS 0x02
#define UHCI_USBINTR 0x04
#define UHCI_FLBASEADD 0x08
#define UHCI_SOFMOD 0x0c
#define UHCI_CMD_RUN 0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_CONFIGURE 0x0040
#define UHCI_STATUS_HALTED 0x0020
#define UHCI_STATUS_ERROR 0x0002
#define UHCI_STATUS_HOST_SYSTEM_ERROR 0x0008
#define UHCI_STATUS_PROCESS_ERROR 0x0010
#define UHCI_TD_ACTIVE (1U << 23)
#define UHCI_TD_IOC (1U << 24)
#define UHCI_TD_LOW_SPEED (1U << 26)
#define UHCI_TD_ERROR_MASK (0x3fU << 17)
#define UHCI_PORT_BASE 0x10
#define UHCI_PORT_COUNT 2
#define UHCI_PORT_CONNECT 0x0001
#define UHCI_PORT_ENABLE 0x0004
#define UHCI_PORT_RESET 0x0200
#define UHCI_USBLEGSUP 0xc0
#define UHCI_LEGACY_BIOS_OWNED 0x0001U
#define UHCI_LEGACY_OS_OWNED 0x0002U
#define UHCI_IRQ_VECTOR 0x51
#define UHCI_INTR_MASK 0x0007U
#define UHCI_IO_SPACE_SIZE 0x20U

extern void arch_pci_shared_irq_stub(void);

static void uhci_dma_write_barrier(void) {
    __asm__ volatile ("sfence" ::: "memory");
}

static void uhci_dma_read_barrier(void) {
    __asm__ volatile ("lfence" ::: "memory");
}

static uint32_t controllers;
static uint32_t root_ports;
static uint16_t controller_base;
static uint64_t controller_frame_list;
static device_driver_t uhci_driver;
static int uhci_irq_enabled;
static volatile uint32_t uhci_interrupts;
static volatile uint32_t uhci_last_td_status_value;
static volatile uint16_t uhci_last_status_value;
static int uhci_low_speed;
static int uhci_io_disabled;
static spinlock_t uhci_lock;
static uint64_t uhci_async_qh_frame;
static uint64_t uhci_bulk_anchor_frame;
static uint64_t uhci_async_td_frame;
static uint64_t uhci_async_data_frame;
static uint8_t *uhci_async_data;
static uint16_t uhci_async_length;
static uint8_t *uhci_async_toggle;
static uint16_t uhci_async_packet_count;
static uint32_t uhci_async_td_pages;
static uint8_t uhci_async_input;
static uint8_t uhci_async_pending;
static int uhci_async_completion;

static void uhci_acknowledge_status(uint16_t status);

typedef struct {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
} __attribute__((packed, aligned(16))) uhci_td_t;

typedef struct {
    uint32_t head;
    uint32_t element;
} __attribute__((packed, aligned(16))) uhci_qh_t;

static void uhci_release_transfer_frames(uint64_t qh_frame, uint64_t td_frame,
                                         uint64_t setup_frame,
                                         uint32_t td_pages,
                                         uint64_t data_frame) {
    if (qh_frame) physical_free_frame(qh_frame);
    if (td_frame && td_pages) physical_free_frames(td_frame, td_pages);
    if (setup_frame) physical_free_frame(setup_frame);
    if (data_frame) physical_free_frame(data_frame);
}

static uint32_t uhci_token(uint8_t pid, uint8_t address, uint8_t endpoint,
                           uint8_t toggle, uint16_t length) {
    uint32_t max_length = length == 0 ? 0x7ffU : (uint32_t)length - 1U;
    return pid | ((uint32_t)address << 8) | ((uint32_t)endpoint << 15) |
           ((uint32_t)toggle << 19) | (max_length << 21);
}

static int uhci_td_complete(const uhci_td_t *td) {
    return (td->status & UHCI_TD_ACTIVE) == 0 &&
           (td->status & UHCI_TD_ERROR_MASK) == 0;
}

static uint32_t uhci_td_status(int interrupt_on_complete) {
    return (3U << 27) | (uhci_low_speed ? UHCI_TD_LOW_SPEED : 0) |
           (interrupt_on_complete ? UHCI_TD_IOC : 0) | UHCI_TD_ACTIVE;
}

static int uhci_set_running(int running) {
    uint16_t command = running ? (UHCI_CMD_RUN | UHCI_CMD_CONFIGURE) : 0;
    uhci_dma_write_barrier();
    __asm__ volatile ("outw %0, %1" :: "a"(command),
                      "Nd"((uint16_t)(controller_base + UHCI_USBCMD)));
    if (!running) {
        for (uint32_t wait = 0; wait < 100000; ++wait) {
            uint16_t status;
            __asm__ volatile ("inw %1, %0" : "=a"(status)
                              : "Nd"((uint16_t)(controller_base + UHCI_USBSTS)));
            if ((status & UHCI_STATUS_HALTED) != 0) return 1;
        }
        return 0;
    }
    for (uint32_t wait = 0; wait < 100000; ++wait) {
        uint16_t status;
        __asm__ volatile ("inw %1, %0" : "=a"(status)
                          : "Nd"((uint16_t)(controller_base + UHCI_USBSTS)));
        if ((status & (UHCI_STATUS_ERROR | UHCI_STATUS_HOST_SYSTEM_ERROR |
                       UHCI_STATUS_PROCESS_ERROR)) != 0) {
            uhci_acknowledge_status(status & (UHCI_STATUS_ERROR |
                                              UHCI_STATUS_HOST_SYSTEM_ERROR |
                                              UHCI_STATUS_PROCESS_ERROR));
            return 0;
        }
        if ((status & UHCI_STATUS_HALTED) == 0) return 1;
    }
    return 0;
}

static uint16_t uhci_status(void) {
    uint16_t status;
    __asm__ volatile ("inw %1, %0" : "=a"(status)
                      : "Nd"((uint16_t)(controller_base + UHCI_USBSTS)));
    return status;
}

static void uhci_acknowledge_status(uint16_t status) {
    __asm__ volatile ("outw %0, %1" :: "a"(status),
                      "Nd"((uint16_t)(controller_base + UHCI_USBSTS)));
}

static int uhci_match(const device_t *device) {
    return device && device->bus == DEVICE_BUS_PCI &&
           device->class_code == 0x0c && device->subclass == 0x03 &&
           device->programming_interface == 0x00;
}

static int uhci_probe(device_t *device) {
    if (!device || device->resources[UHCI_BAR_INDEX].size < UHCI_IO_SPACE_SIZE ||
        device->resources[UHCI_BAR_INDEX].address == 0 ||
        device->resources[UHCI_BAR_INDEX].address > 0xfffcULL ||
        (device->resources[UHCI_BAR_INDEX].flags & 1U) == 0 ||
        !device_claim_resource(device, UHCI_BAR_INDEX, &uhci_driver)) return 0;
    uint16_t base = (uint16_t)(device->resources[UHCI_BAR_INDEX].address & 0xfffc);
    uint32_t legacy = pci_config_read32(device->bus_number, device->slot,
                                        device->function, UHCI_USBLEGSUP);
    pci_config_write32(device->bus_number, device->slot, device->function,
                       UHCI_USBLEGSUP,
                       (legacy & ~UHCI_LEGACY_BIOS_OWNED) | UHCI_LEGACY_OS_OWNED);
    for (uint32_t wait = 0; wait < 100000; ++wait) {
        legacy = pci_config_read32(device->bus_number, device->slot,
                                   device->function, UHCI_USBLEGSUP);
        if ((legacy & UHCI_LEGACY_BIOS_OWNED) == 0) break;
    }
    if ((legacy & UHCI_LEGACY_BIOS_OWNED) != 0) {
        device_release_resource(device, UHCI_BAR_INDEX, &uhci_driver);
        return 0;
    }
    uint32_t command = pci_config_read32(device->bus_number, device->slot,
                                         device->function, 0x04);
    pci_config_write32(device->bus_number, device->slot, device->function,
                       0x04, command | 0x00000005U);
    __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)UHCI_CMD_HCRESET),
                      "Nd"((uint16_t)(base + UHCI_USBCMD)));
    int reset_complete = 0;
    for (uint32_t i = 0; i < 100000; ++i) {
        uint16_t status;
        __asm__ volatile ("inw %1, %0" : "=a"(status) : "Nd"((uint16_t)(base + UHCI_USBCMD)));
        if ((status & UHCI_CMD_HCRESET) == 0) { reset_complete = 1; break; }
    }
    if (!reset_complete) {
        device_release_resource(device, UHCI_BAR_INDEX, &uhci_driver);
        return 0;
    }
    uint64_t frame_list = physical_alloc_frame();
    uint64_t bulk_anchor = physical_alloc_frame();
    if (!frame_list || !bulk_anchor) {
        if (frame_list) physical_free_frame(frame_list);
        if (bulk_anchor) physical_free_frame(bulk_anchor);
        device_release_resource(device, UHCI_BAR_INDEX, &uhci_driver);
        return 0;
    }
    uint32_t *entries = (uint32_t *)(uintptr_t)frame_list;
    for (uint32_t i = 0; i < 1024; ++i) entries[i] = 1;
    uhci_qh_t *anchor = (uhci_qh_t *)(uintptr_t)bulk_anchor;
    anchor->head = 1U;
    anchor->element = 1U;
    __asm__ volatile ("outl %0, %1" :: "a"((uint32_t)frame_list),
                      "Nd"((uint16_t)(base + UHCI_FLBASEADD)));
    __asm__ volatile ("outb %0, %1" :: "a"((uint8_t)64), "Nd"((uint16_t)(base + UHCI_SOFMOD)));
    arch_set_interrupt_gate(UHCI_IRQ_VECTOR, arch_pci_shared_irq_stub);
    uhci_irq_enabled = pci_enable_legacy_irq(device, UHCI_IRQ_VECTOR);
    __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)(uhci_irq_enabled ? UHCI_INTR_MASK : 0)),
                      "Nd"((uint16_t)(base + UHCI_USBINTR)));
    __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)(UHCI_CMD_RUN | UHCI_CMD_CONFIGURE)),
                      "Nd"((uint16_t)(base + UHCI_USBCMD)));
    controller_base = base;
    controller_frame_list = frame_list;
    uhci_bulk_anchor_frame = bulk_anchor;
    for (uint32_t port = 0; port < UHCI_PORT_COUNT; ++port) {
        uint16_t port_address = (uint16_t)(base + UHCI_PORT_BASE + port * 2U);
        uint16_t status;
        __asm__ volatile ("inw %1, %0" : "=a"(status) : "Nd"(port_address));
        if ((status & UHCI_PORT_CONNECT) == 0) continue;
        if (root_ports == 0) uhci_low_speed = (status & (1U << 8)) != 0;
        __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)UHCI_PORT_RESET), "Nd"(port_address));
        for (volatile uint32_t delay = 0; delay < 10000; ++delay) __asm__ volatile ("pause");
        __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)(UHCI_PORT_RESET | UHCI_PORT_CONNECT)),
                          "Nd"(port_address));
        __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)(UHCI_PORT_ENABLE | UHCI_PORT_CONNECT)),
                          "Nd"(port_address));
        ++root_ports;
    }
    ++controllers;
    return 1;
}

static int uhci_control_transfer_locked(uint8_t address, uint8_t endpoint,
                          const uint8_t setup[8], void *data, uint16_t length) {
    if (uhci_io_disabled || !controller_base || !controller_frame_list || !setup || address > 127 ||
        endpoint > 15 || length > 4096 || (length != 0 && !data) ||
        length != (uint16_t)(setup[6] | ((uint16_t)setup[7] << 8))) return 0;
    uint64_t qh_frame = physical_alloc_frame();
    uint64_t td_frame = physical_alloc_frame();
    uint64_t setup_frame = physical_alloc_frame();
    uint64_t data_frame = length ? physical_alloc_frame() : 0;
    if (!qh_frame || !td_frame || !setup_frame || (length && !data_frame)) {
        uhci_release_transfer_frames(qh_frame, td_frame, setup_frame, 1, data_frame);
        return 0;
    }
    uhci_qh_t *qh = (uhci_qh_t *)(uintptr_t)qh_frame;
    uhci_td_t *td = (uhci_td_t *)(uintptr_t)td_frame;
    uint8_t *setup_copy = (uint8_t *)(uintptr_t)setup_frame;
    uint8_t *data_copy = (uint8_t *)(uintptr_t)data_frame;
    for (uint32_t i = 0; i < 4096 / 4; ++i) ((uint32_t *)td)[i] = 0;
    qh->head = 1U;
    qh->element = 1U;
    for (uint32_t i = 0; i < 8; ++i) setup_copy[i] = setup[i];
    if (length && (setup[0] & 0x80U) == 0)
        for (uint32_t i = 0; i < length; ++i) data_copy[i] = ((uint8_t *)data)[i];
    uint32_t data_count = length ? ((length + 63U) / 64U) : 0;
    td[0].link = (uint32_t)(td_frame + sizeof(uhci_td_t)) | 0U;
    td[0].status = uhci_td_status(0);
    td[0].token = uhci_token(0x2d, address, endpoint, 0, 8);
    td[0].buffer = (uint32_t)setup_frame;
    for (uint32_t i = 0; i < data_count; ++i) {
        uhci_td_t *current = &td[1U + i];
        uint16_t chunk = (uint16_t)(length - i * 64U);
        if (chunk > 64U) chunk = 64U;
        current->link = (uint32_t)(td_frame + (2U + i) * sizeof(uhci_td_t));
        current->status = uhci_td_status(0);
        current->token = uhci_token((setup[0] & 0x80U) ? 0x69 : 0xe1,
                                    address, endpoint, (uint8_t)(1U ^ (i & 1U)), chunk);
        current->buffer = (uint32_t)(data_frame + i * 64U);
    }
    uhci_td_t *status_td = &td[1U + data_count];
    status_td->link = 1U;
    status_td->status = uhci_td_status(1);
    status_td->token = uhci_token((setup[0] & 0x80U) ? 0xe1 : 0x69,
                                  address, endpoint, 1, 0);
    status_td->buffer = 0;
    qh->element = (uint32_t)td_frame;
    volatile uint32_t *frame_list = (volatile uint32_t *)(uintptr_t)controller_frame_list;
    if (!uhci_set_running(0)) {
        uhci_release_transfer_frames(qh_frame, td_frame, setup_frame, 1, data_frame);
        return 0;
    }
    uhci_acknowledge_status(uhci_status() & 0x001fU);
    for (uint32_t i = 0; i < 1024; ++i) frame_list[i] = (uint32_t)qh_frame | 2U;
    __asm__ volatile ("outl %0, %1" :: "a"((uint32_t)controller_frame_list),
                      "Nd"((uint16_t)(controller_base + UHCI_FLBASEADD)));
    if (!uhci_set_running(1)) {
        uhci_release_transfer_frames(qh_frame, td_frame, setup_frame, 1, data_frame);
        return 0;
    }
    int complete = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        uhci_dma_read_barrier();
        if ((status_td->status & UHCI_TD_ACTIVE) == 0) {
            complete = uhci_td_complete(&td[0]) && uhci_td_complete(status_td);
            for (uint32_t i = 0; complete && i < data_count; ++i)
                complete = uhci_td_complete(&td[1U + i]);
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!uhci_set_running(0)) {
        uhci_io_disabled = 1;
        __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)0),
                          "Nd"((uint16_t)(controller_base + UHCI_USBINTR)));
        return 0;
    }
    uint16_t controller_status = uhci_status();
    __atomic_store_n(&uhci_last_status_value, controller_status,
                     __ATOMIC_RELEASE);
    complete = complete && (controller_status & (UHCI_STATUS_ERROR |
                        UHCI_STATUS_HOST_SYSTEM_ERROR |
                        UHCI_STATUS_PROCESS_ERROR)) == 0;
    for (uint32_t i = 0; i < 1024; ++i) frame_list[i] = 1U;
    if (!uhci_set_running(1)) complete = 0;
    if (complete && length && (setup[0] & 0x80U))
        for (uint32_t i = 0; i < length; ++i) ((uint8_t *)data)[i] = data_copy[i];
    uhci_release_transfer_frames(qh_frame, td_frame, setup_frame, 1, data_frame);
    return complete;
}

static int uhci_data_transfer_locked(uint8_t address, uint8_t endpoint, void *data,
                            uint16_t length, uint16_t max_packet,
                            uint8_t *toggle, int bulk) {
    if (uhci_io_disabled || !controller_base || !controller_frame_list ||
        (!data && length != 0) || address > 127 ||
        (endpoint & 0x7fU) > 15 || (!bulk && length == 0) || length > 4096 ||
        max_packet == 0 ||
        max_packet > 64 || !toggle || *toggle > 1) return 0;
    uint32_t packet_count = length == 0 ? 1U :
        (length + max_packet - 1U) / max_packet;
    uint64_t qh_frame = physical_alloc_frame();
    uint32_t td_pages = (uint32_t)(((uint64_t)packet_count * sizeof(uhci_td_t) +
                                    4095U) / 4096U);
    uint64_t td_frame = physical_alloc_frames(td_pages);
    uint64_t data_frame = physical_alloc_frame();
    if (!qh_frame || !td_frame || !data_frame) {
        uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
        return 0;
    }
    uhci_qh_t *qh = (uhci_qh_t *)(uintptr_t)qh_frame;
    uhci_td_t *td = (uhci_td_t *)(uintptr_t)td_frame;
    uint8_t *transfer = (uint8_t *)(uintptr_t)data_frame;
    int input = (endpoint & 0x80U) != 0;
    if (!input)
        for (uint16_t i = 0; i < length; ++i) transfer[i] = ((uint8_t *)data)[i];
    for (uint32_t i = 0; i < td_pages * 4096U / 4U; ++i)
        ((uint32_t *)td)[i] = 0;
    qh->head = 1U;
    qh->element = (uint32_t)td_frame;
    uint8_t current_toggle = *toggle;
    for (uint32_t i = 0; i < packet_count; ++i) {
        uint16_t chunk = (uint16_t)(length - i * max_packet);
        if (chunk > max_packet) chunk = max_packet;
        td[i].link = i + 1U < packet_count ?
            (uint32_t)(td_frame + (i + 1U) * sizeof(uhci_td_t)) : 1U;
        td[i].status = uhci_td_status(i + 1U == packet_count);
        td[i].token = uhci_token(input ? 0x69 : 0xe1, address,
                                 endpoint & 0x0fU, current_toggle, chunk);
        td[i].buffer = length == 0 ? 0U :
            (uint32_t)(data_frame + i * max_packet);
        current_toggle ^= 1U;
    }
    if (bulk && (uhci_async_pending || !uhci_bulk_anchor_frame)) {
        uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
        return 0;
    }
    volatile uint32_t *frame_list =
        (volatile uint32_t *)(uintptr_t)controller_frame_list;
    if (!uhci_set_running(0)) {
        uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
        return 0;
    }
    if (bulk) {
        uhci_qh_t *anchor =
            (uhci_qh_t *)(uintptr_t)uhci_bulk_anchor_frame;
        anchor->head = (uint32_t)qh_frame | 2U;
        anchor->element = 1U;
        for (uint32_t i = 0; i < 1024; ++i)
            frame_list[i] = (uint32_t)uhci_bulk_anchor_frame | 2U;
    } else {
        for (uint32_t i = 0; i < 1024; ++i)
            frame_list[i] = (uint32_t)qh_frame | 2U;
    }
    __asm__ volatile ("outl %0, %1" :: "a"((uint32_t)controller_frame_list),
                      "Nd"((uint16_t)(controller_base + UHCI_FLBASEADD)));
    if (!uhci_set_running(1)) {
        uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
        return 0;
    }
    int complete = 0;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        uhci_dma_read_barrier();
        if ((td[packet_count - 1U].status & UHCI_TD_ACTIVE) == 0) {
            complete = 1;
            for (uint32_t i = 0; i < packet_count; ++i)
                complete = complete && uhci_td_complete(&td[i]);
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!uhci_set_running(0)) {
        uhci_io_disabled = 1;
        __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)0),
                          "Nd"((uint16_t)(controller_base + UHCI_USBINTR)));
        return 0;
    }
    uint16_t controller_status = uhci_status();
    __atomic_store_n(&uhci_last_td_status_value, td[packet_count - 1U].status,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&uhci_last_status_value, controller_status,
                     __ATOMIC_RELEASE);
    complete = complete && (controller_status & (UHCI_STATUS_ERROR |
                        UHCI_STATUS_HOST_SYSTEM_ERROR |
                        UHCI_STATUS_PROCESS_ERROR)) == 0;
    if (bulk) {
        uhci_qh_t *anchor =
            (uhci_qh_t *)(uintptr_t)uhci_bulk_anchor_frame;
        anchor->head = 1U;
        for (uint32_t i = 0; i < 1024; ++i)
            frame_list[i] = (uint32_t)uhci_bulk_anchor_frame | 2U;
    } else {
        for (uint32_t i = 0; i < 1024; ++i) frame_list[i] = 1U;
    }
    if (!uhci_set_running(1)) complete = 0;
    if (complete) {
        if (input)
            for (uint16_t i = 0; i < length; ++i) ((uint8_t *)data)[i] = transfer[i];
        *toggle = current_toggle;
    }
    uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
    return complete;
}

static int uhci_bulk_transfer_locked(uint8_t address, uint8_t endpoint, void *data,
                                     uint16_t length, uint16_t max_packet,
                                     uint8_t *toggle) {
    if (uhci_io_disabled || !controller_base || !controller_frame_list ||
        (!data && length != 0) || address > 127 ||
        (endpoint & 0x7fU) > 15 || length > 4096 ||
        max_packet == 0 || max_packet > 64 || !toggle || *toggle > 1)
        return 0;

    return uhci_data_transfer_locked(address, endpoint, data, length,
                                     max_packet, toggle, 1);
}

static int uhci_interrupt_submit_locked(uint8_t address, uint8_t endpoint,
                                        void *data, uint16_t length,
                                        uint16_t max_packet, uint8_t interval,
                                        uint8_t *toggle) {
    if (uhci_async_pending || uhci_io_disabled || !controller_base ||
        !controller_frame_list || !data || address > 127 ||
        (endpoint & 0x7fU) > 15 || length == 0 || length > 4096 ||
        max_packet == 0 || max_packet > 64 || interval == 0 ||
        !toggle || *toggle > 1)
        return 0;
    uint32_t packet_count = (length + max_packet - 1U) / max_packet;
    uint64_t qh_frame = physical_alloc_frame();
    uint32_t td_pages = (uint32_t)(((uint64_t)packet_count * sizeof(uhci_td_t) +
                                    4095U) / 4096U);
    uint64_t td_frame = physical_alloc_frames(td_pages);
    uint64_t data_frame = physical_alloc_frame();
    if (!qh_frame || !td_frame || !data_frame) {
        uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
        return 0;
    }
    uhci_qh_t *qh = (uhci_qh_t *)(uintptr_t)qh_frame;
    uhci_td_t *td = (uhci_td_t *)(uintptr_t)td_frame;
    for (uint32_t i = 0; i < td_pages * 4096U / 4U; ++i)
        ((uint32_t *)td)[i] = 0;
    int input = (endpoint & 0x80U) != 0;
    if (!input)
        for (uint16_t i = 0; i < length; ++i)
            ((uint8_t *)(uintptr_t)data_frame)[i] = ((uint8_t *)data)[i];
    qh->head = 1U; qh->element = (uint32_t)td_frame;
    uint8_t current_toggle = *toggle;
    for (uint32_t i = 0; i < packet_count; ++i) {
        uint16_t chunk = (uint16_t)(length - i * max_packet);
        if (chunk > max_packet) chunk = max_packet;
        td[i].link = i + 1U < packet_count ?
            (uint32_t)(td_frame + (i + 1U) * sizeof(uhci_td_t)) : 1U;
        td[i].status = uhci_td_status(i + 1U == packet_count);
        td[i].token = uhci_token(input ? 0x69 : 0xe1, address,
                                 endpoint & 0x0fU, current_toggle, chunk);
        td[i].buffer = (uint32_t)(data_frame + i * max_packet);
        current_toggle ^= 1U;
    }
    if (!uhci_set_running(0)) goto fail;
    volatile uint32_t *frame_list =
        (volatile uint32_t *)(uintptr_t)controller_frame_list;
    for (uint32_t i = 0; i < 1024; ++i)
        frame_list[i] = i % interval == 0 ? (uint32_t)qh_frame | 2U : 1U;
    __asm__ volatile ("outl %0, %1" :: "a"((uint32_t)controller_frame_list),
                      "Nd"((uint16_t)(controller_base + UHCI_FLBASEADD)));
    if (!uhci_set_running(1)) goto fail;
    uhci_async_qh_frame = qh_frame; uhci_async_td_frame = td_frame;
    uhci_async_td_pages = td_pages;
    uhci_async_data_frame = data_frame; uhci_async_data = (uint8_t *)data;
    uhci_async_length = length;
    uhci_async_toggle = toggle; uhci_async_packet_count = (uint16_t)packet_count;
    uhci_async_input = (uint8_t)input;
    uhci_async_completion = 0;
    uhci_async_pending = 1;
    return 1;
fail:
    uhci_release_transfer_frames(qh_frame, td_frame, 0, td_pages, data_frame);
    return 0;
}

static int uhci_interrupt_poll_locked(void) {
    if (!uhci_async_pending) return 0;
    uhci_td_t *td = (uhci_td_t *)(uintptr_t)uhci_async_td_frame;
    uhci_dma_read_barrier();
    uint32_t last_td_status = td[uhci_async_packet_count - 1U].status;
    __atomic_store_n(&uhci_last_td_status_value, last_td_status,
                     __ATOMIC_RELEASE);
    if (last_td_status & UHCI_TD_ACTIVE) return 0;
    int complete = 1;
    for (uint32_t i = 0; i < uhci_async_packet_count; ++i)
        complete = complete && uhci_td_complete(&td[i]);
    uint16_t controller_status = uhci_status();
    __atomic_store_n(&uhci_last_status_value, controller_status,
                     __ATOMIC_RELEASE);
    complete = complete && !(controller_status & (UHCI_STATUS_ERROR |
        UHCI_STATUS_HOST_SYSTEM_ERROR | UHCI_STATUS_PROCESS_ERROR));
    if (!uhci_set_running(0)) { uhci_io_disabled = 1; return -1; }
    volatile uint32_t *frame_list =
        (volatile uint32_t *)(uintptr_t)controller_frame_list;
    for (uint32_t i = 0; i < 1024; ++i) frame_list[i] = 1U;
    int restarted = uhci_set_running(1);
    if (complete && restarted && uhci_async_input) {
        uint8_t *transfer = (uint8_t *)(uintptr_t)uhci_async_data_frame;
        for (uint16_t i = 0; i < uhci_async_length; ++i)
            uhci_async_data[i] = transfer[i];
        uint8_t next_toggle = *uhci_async_toggle;
        for (uint32_t i = 0; i < uhci_async_packet_count; ++i) next_toggle ^= 1U;
        *uhci_async_toggle = next_toggle;
    }
    uhci_release_transfer_frames(uhci_async_qh_frame, uhci_async_td_frame,
                                 0, uhci_async_td_pages, uhci_async_data_frame);
    uhci_async_pending = 0;
    return complete && restarted ? 1 : -1;
}

int uhci_initialize(void) {
    controllers = 0;
    root_ports = 0;
    controller_base = 0;
    controller_frame_list = 0;
    uhci_bulk_anchor_frame = 0;
    uhci_low_speed = 0;
    uhci_io_disabled = 0;
    uhci_irq_enabled = 0;
    uhci_interrupts = 0;
    uhci_last_td_status_value = 0;
    uhci_last_status_value = 0;
    uhci_async_pending = 0;
    uhci_async_completion = 0;
    spinlock_init(&uhci_lock);
    uhci_driver.name = "uhci";
    uhci_driver.bus = DEVICE_BUS_PCI;
    uhci_driver.match = uhci_match;
    uhci_driver.probe = uhci_probe;
    return device_driver_register(&uhci_driver) && device_bind_drivers();
}

uint32_t uhci_controller_count(void) { return controllers; }
uint32_t uhci_root_port_count(void) { return root_ports; }
int uhci_interrupt_enabled(void) { return uhci_irq_enabled; }
uint32_t uhci_interrupt_count(void) { return uhci_interrupts; }
uint32_t uhci_last_transfer_td_status(void) {
    return __atomic_load_n(&uhci_last_td_status_value, __ATOMIC_ACQUIRE);
}
uint16_t uhci_last_controller_status(void) {
    return __atomic_load_n(&uhci_last_status_value, __ATOMIC_ACQUIRE);
}
int uhci_interrupt_submit(uint8_t address, uint8_t endpoint, void *data,
                          uint16_t length, uint16_t max_packet, uint8_t interval,
                          uint8_t *toggle) {
    uint64_t flags = spinlock_lock_irqsave(&uhci_lock);
    int result = uhci_interrupt_submit_locked(address, endpoint, data, length,
                                               max_packet, interval, toggle);
    spinlock_unlock_irqrestore(&uhci_lock, flags);
    return result;
}

int uhci_interrupt_poll(void) {
    uint64_t flags = spinlock_lock_irqsave(&uhci_lock);
    int result = uhci_async_pending ? uhci_interrupt_poll_locked()
                                    : uhci_async_completion;
    if (!uhci_async_pending) uhci_async_completion = 0;
    spinlock_unlock_irqrestore(&uhci_lock, flags);
    return result;
}
int uhci_control_transfer(uint8_t address, uint8_t endpoint,
                          const uint8_t setup[8], void *data, uint16_t length) {
    uint64_t flags = spinlock_lock_irqsave(&uhci_lock);
    int result = uhci_control_transfer_locked(address, endpoint, setup, data, length);
    spinlock_unlock_irqrestore(&uhci_lock, flags);
    return result;
}

int uhci_interrupt_transfer(uint8_t address, uint8_t endpoint, void *data,
                            uint16_t length, uint16_t max_packet,
                            uint8_t *toggle) {
    uint64_t flags = spinlock_lock_irqsave(&uhci_lock);
    int result = uhci_data_transfer_locked(address, endpoint, data, length,
                                           max_packet, toggle, 0);
    spinlock_unlock_irqrestore(&uhci_lock, flags);
    return result;
}

int uhci_bulk_transfer(uint8_t address, uint8_t endpoint, void *data,
                       uint16_t length, uint16_t max_packet, uint8_t *toggle) {
    uint64_t flags = spinlock_lock_irqsave(&uhci_lock);
    int result = uhci_bulk_transfer_locked(address, endpoint, data, length,
                                           max_packet, toggle);
    spinlock_unlock_irqrestore(&uhci_lock, flags);
    return result;
}

void uhci_interrupt_handler(void) {
    uint64_t flags = spinlock_lock_irqsave(&uhci_lock);
    if (!controller_base) {
        spinlock_unlock_irqrestore(&uhci_lock, flags);
        return;
    }
    uint16_t status = uhci_status();
    if ((status & UHCI_INTR_MASK) != 0) {
        __atomic_fetch_add(&uhci_interrupts, 1U, __ATOMIC_RELAXED);
        uhci_acknowledge_status(status & 0x001fU);
        if (uhci_async_pending) {
            int completion = uhci_interrupt_poll_locked();
            if (completion != 0) uhci_async_completion = completion;
        }
    }
    spinlock_unlock_irqrestore(&uhci_lock, flags);
}
