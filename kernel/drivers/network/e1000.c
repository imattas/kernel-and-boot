#include "e1000.h"
#include "../../device/device.h"
#include "../pci/pci.h"
#include "../../mm/physical/frame.h"
#include "../../core/sync/spinlock.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../../arch/x86_64/interrupts/apic.h"

#define E1000_BAR 0
#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008
#define E1000_STATUS_LINK_UP (1U << 1)
#define E1000_TCTL 0x0400
#define E1000_TIPG 0x0410
#define E1000_RCTL 0x0100
#define E1000_ICR 0x00c0
#define E1000_IMS 0x00d0
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818
#define E1000_RAL 0x5400
#define E1000_RAH 0x5404
#define E1000_RAH_AV (1U << 31)
#define E1000_REQUIRED_MMIO_SIZE 0x5408U
#define E1000_CTRL_RST (1U << 26)
#define E1000_CTRL_ASDE (1U << 5)
#define E1000_TCTL_EN (1U << 1)
#define E1000_TCTL_PSP (1U << 3)
#define E1000_RCTL_EN (1U << 1)
#define E1000_RCTL_BAM (1U << 15)
#define E1000_RCTL_BSEX (1U << 25)
#define E1000_RING_COUNT 64U
#define E1000_TX_CMD_EOP 0x01U
#define E1000_TX_CMD_IFCS 0x02U
#define E1000_TX_CMD_RS 0x08U
#define E1000_DESC_DONE 0x01U
#define E1000_DESC_ERROR_MASK 0x0eU
#define E1000_DESC_EOP 0x02U
#define E1000_IMS_RXDMT0 (1U << 4)
#define E1000_IMS_RXT0 (1U << 7)
#define E1000_IMS_TXDW (1U << 0)
#define E1000_IMS_LSC (1U << 2)
#define E1000_IMS_RXO (1U << 6)
#define E1000_INTERRUPT_MASK (E1000_IMS_RXDMT0 | E1000_IMS_RXT0 | E1000_IMS_TXDW | \
                              E1000_IMS_LSC | E1000_IMS_RXO)
#define E1000_IRQ_VECTOR 0x51

extern void arch_pci_shared_irq_stub(void);

static void e1000_dma_write_barrier(void) {
    __asm__ volatile ("sfence" ::: "memory");
}

static void e1000_dma_read_barrier(void) {
    __asm__ volatile ("lfence" ::: "memory");
}

typedef struct { uint64_t address; uint16_t length; uint8_t cso, command, status, css; uint16_t special; } __attribute__((packed)) e1000_tx_desc_t;
typedef struct { uint64_t address; uint16_t length, checksum; uint8_t status, errors; uint16_t special; } __attribute__((packed)) e1000_rx_desc_t;

static uint32_t controllers;
static device_driver_t e1000_driver;
static uint64_t e1000_rx_buffers[E1000_RING_COUNT];
static uint64_t e1000_tx_buffers[E1000_RING_COUNT];
static volatile uint32_t *e1000_regs;
static volatile e1000_tx_desc_t *e1000_tx_ring;
static volatile e1000_rx_desc_t *e1000_rx_ring;
static uint32_t e1000_tx_index;
static uint32_t e1000_tx_reclaim_index;
static uint32_t e1000_tx_pending;
static uint32_t e1000_rx_index;
static spinlock_t e1000_lock;
static int e1000_msi_enabled;
static uint8_t e1000_mac[6];
static volatile uint32_t e1000_interrupts;
static volatile uint32_t e1000_pending_causes;
static volatile uint32_t e1000_tx_errors;
static volatile uint32_t e1000_rx_errors;
static volatile uint32_t e1000_link_events;
static volatile uint32_t e1000_rx_overruns;

static int e1000_match(const device_t *device) {
    if (!device || device->bus != DEVICE_BUS_PCI || device->class_code != 0x02 ||
        device->vendor_id != 0x8086) return 0;
    /* This driver implements the 82540/82545 legacy register and descriptor
       layout. Do not bind newer Intel NICs until their contracts are added. */
    return device->device_id == 0x100e || device->device_id == 0x100f ||
           device->device_id == 0x1010 || device->device_id == 0x1011 ||
           device->device_id == 0x1026;
}

static int e1000_probe(device_t *device) {
    if (controllers != 0) return 0;
    if (!device || device->resources[E1000_BAR].size < E1000_REQUIRED_MMIO_SIZE ||
        device->resources[E1000_BAR].address == 0 ||
        device->resources[E1000_BAR].address >= 0x100000000ULL ||
        device->resources[E1000_BAR].size > 0x100000000ULL -
            device->resources[E1000_BAR].address ||
        (device->resources[E1000_BAR].flags & 1U) != 0 ||
        !device_claim_resource(device, E1000_BAR, &e1000_driver)) return 0;
    uint64_t tx_frame = 0;
    uint64_t rx_frame = 0;
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)device->resources[E1000_BAR].address;
    regs[E1000_CTRL / 4] |= E1000_CTRL_RST;
    int reset_complete = 0;
    for (uint32_t i = 0; i < 100000; ++i)
        if ((regs[E1000_CTRL / 4] & E1000_CTRL_RST) == 0) {
            reset_complete = 1;
            break;
        }
    if (!reset_complete) goto fail;
    tx_frame = physical_alloc_frame();
    rx_frame = physical_alloc_frame();
    if (!tx_frame || !rx_frame) goto fail;
    e1000_tx_desc_t *tx = (e1000_tx_desc_t *)(uintptr_t)tx_frame;
    e1000_rx_desc_t *rx = (e1000_rx_desc_t *)(uintptr_t)rx_frame;
    uint32_t receive_address_low = regs[E1000_RAL / 4];
    uint32_t receive_address_high = regs[E1000_RAH / 4];
    if ((receive_address_high & E1000_RAH_AV) == 0 ||
        (receive_address_low & 1U) != 0 ||
        (receive_address_low == 0 &&
         (receive_address_high & 0xffffU) == 0)) goto fail;
    e1000_mac[0] = (uint8_t)receive_address_low;
    e1000_mac[1] = (uint8_t)(receive_address_low >> 8);
    e1000_mac[2] = (uint8_t)(receive_address_low >> 16);
    e1000_mac[3] = (uint8_t)(receive_address_low >> 24);
    e1000_mac[4] = (uint8_t)receive_address_high;
    e1000_mac[5] = (uint8_t)(receive_address_high >> 8);
    for (uint32_t i = 0; i < 64; ++i) {
        e1000_tx_buffers[i] = physical_alloc_frame();
        if (!e1000_tx_buffers[i]) goto fail;
        tx[i].address = e1000_tx_buffers[i]; tx[i].length = 0; tx[i].command = 0; tx[i].status = E1000_DESC_DONE;
        e1000_rx_buffers[i] = physical_alloc_frame();
        if (!e1000_rx_buffers[i]) goto fail;
        rx[i].address = e1000_rx_buffers[i]; rx[i].status = 0;
    }
    regs[E1000_TDBAL / 4] = (uint32_t)tx_frame; regs[E1000_TDBAH / 4] = 0;
    regs[E1000_TDLEN / 4] = 64 * sizeof(e1000_tx_desc_t); regs[E1000_TDH / 4] = 0; regs[E1000_TDT / 4] = 0;
    regs[E1000_RDBAL / 4] = (uint32_t)rx_frame; regs[E1000_RDBAH / 4] = 0;
    regs[E1000_RDLEN / 4] = 64 * sizeof(e1000_rx_desc_t); regs[E1000_RDH / 4] = 0; regs[E1000_RDT / 4] = 63;
    regs[E1000_TIPG / 4] = 0x0060200aU;
    regs[E1000_TCTL / 4] = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10U << 4) | (0x40U << 12);
    /* BSIZE=00 with BSEX selects the 4096-byte receive buffers above. */
    regs[E1000_RCTL / 4] = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSEX;
    regs[E1000_CTRL / 4] |= E1000_CTRL_ASDE;
    e1000_dma_write_barrier();
    (void)regs[E1000_ICR / 4];
    regs[E1000_IMS / 4] = E1000_INTERRUPT_MASK;
    e1000_regs = regs; e1000_tx_ring = tx; e1000_rx_ring = rx;
    e1000_tx_index = 0; e1000_tx_reclaim_index = 0;
    e1000_tx_pending = 0; e1000_rx_index = 0;
    arch_set_interrupt_gate(E1000_IRQ_VECTOR, arch_pci_shared_irq_stub);
    e1000_msi_enabled = pci_enable_msix(device, E1000_IRQ_VECTOR);
    if (!e1000_msi_enabled) e1000_msi_enabled = pci_enable_msi(device, E1000_IRQ_VECTOR);
    if (!e1000_msi_enabled) e1000_msi_enabled = pci_enable_legacy_irq(device, E1000_IRQ_VECTOR);
    ++controllers;
    return 1;
fail:
    if (tx_frame) physical_free_frame(tx_frame);
    if (rx_frame) physical_free_frame(rx_frame);
    for (uint32_t i = 0; i < E1000_RING_COUNT; ++i) {
        if (e1000_rx_buffers[i]) physical_free_frame(e1000_rx_buffers[i]);
        if (e1000_tx_buffers[i]) physical_free_frame(e1000_tx_buffers[i]);
        e1000_rx_buffers[i] = 0;
        e1000_tx_buffers[i] = 0;
    }
    device_release_resource(device, E1000_BAR, &e1000_driver);
    return 0;
}

int e1000_initialize(void) {
    controllers = 0; e1000_regs = 0; e1000_tx_ring = 0; e1000_rx_ring = 0;
    e1000_tx_pending = 0;
    e1000_msi_enabled = 0;
    e1000_interrupts = 0;
    e1000_pending_causes = 0;
    e1000_tx_errors = 0;
    e1000_rx_errors = 0;
    e1000_link_events = 0;
    e1000_rx_overruns = 0;
    for (uint32_t i = 0; i < 6; ++i) e1000_mac[i] = 0;
    spinlock_init(&e1000_lock);
    for (uint32_t i = 0; i < E1000_RING_COUNT; ++i) {
        e1000_rx_buffers[i] = 0;
        e1000_tx_buffers[i] = 0;
    }
    e1000_driver.name = "e1000"; e1000_driver.bus = DEVICE_BUS_PCI;
    e1000_driver.match = e1000_match; e1000_driver.probe = e1000_probe;
    return device_driver_register(&e1000_driver) && device_bind_drivers();
}
uint32_t e1000_controller_count(void) { return controllers; }
int e1000_link_up(void) {
    return e1000_regs && (e1000_regs[E1000_STATUS / 4] & E1000_STATUS_LINK_UP) != 0;
}
int e1000_mac_address(uint8_t address[6]) {
    if (!address || !e1000_regs || e1000_controller_count() == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&e1000_lock);
    for (uint32_t i = 0; i < 6; ++i) address[i] = e1000_mac[i];
    spinlock_unlock_irqrestore(&e1000_lock, flags);
    return 1;
}
int e1000_interrupt_enabled(void) { return e1000_msi_enabled; }
uint32_t e1000_interrupt_count(void) {
    return __atomic_load_n(&e1000_interrupts, __ATOMIC_ACQUIRE);
}
uint32_t e1000_tx_error_count(void) {
    return __atomic_load_n(&e1000_tx_errors, __ATOMIC_ACQUIRE);
}
uint32_t e1000_rx_error_count(void) {
    return __atomic_load_n(&e1000_rx_errors, __ATOMIC_ACQUIRE);
}
uint32_t e1000_link_event_count(void) {
    return __atomic_load_n(&e1000_link_events, __ATOMIC_ACQUIRE);
}
uint32_t e1000_rx_overrun_count(void) {
    return __atomic_load_n(&e1000_rx_overruns, __ATOMIC_ACQUIRE);
}
void e1000_interrupt_handler(void) {
    if (!e1000_regs) return;
    uint32_t causes = e1000_regs[E1000_ICR / 4];
    __atomic_fetch_or(&e1000_pending_causes, causes, __ATOMIC_RELEASE);
    if ((causes & E1000_INTERRUPT_MASK) != 0)
        __atomic_fetch_add(&e1000_interrupts, 1U, __ATOMIC_RELAXED);
}

int e1000_transmit(const void *data, uint16_t length) {
    if (!e1000_regs || !e1000_tx_ring || !data || length == 0 || length > 2048)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&e1000_lock);
    if ((e1000_regs[E1000_STATUS / 4] & E1000_STATUS_LINK_UP) == 0) {
        spinlock_unlock_irqrestore(&e1000_lock, flags);
        return 0;
    }
    volatile e1000_tx_desc_t *descriptor = &e1000_tx_ring[e1000_tx_index];
    if (e1000_tx_pending == E1000_RING_COUNT ||
        (descriptor->status & E1000_DESC_DONE) == 0) {
        spinlock_unlock_irqrestore(&e1000_lock, flags);
        return 0;
    }
    uint8_t *destination = (uint8_t *)(uintptr_t)e1000_tx_buffers[e1000_tx_index];
    const uint8_t *source = (const uint8_t *)data;
    for (uint16_t i = 0; i < length; ++i) destination[i] = source[i];
    descriptor->length = length;
    descriptor->command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    descriptor->status = 0;
    e1000_tx_index = (e1000_tx_index + 1U) % E1000_RING_COUNT;
    ++e1000_tx_pending;
    e1000_dma_write_barrier();
    e1000_regs[E1000_TDT / 4] = e1000_tx_index;
    spinlock_unlock_irqrestore(&e1000_lock, flags);
    return 1;
}

uint32_t e1000_service(void) {
    if (!e1000_regs || !e1000_tx_ring) return 0;
    uint64_t flags = spinlock_lock_irqsave(&e1000_lock);
    uint32_t causes = __atomic_exchange_n(&e1000_pending_causes, 0,
                                          __ATOMIC_ACQUIRE);
    causes |= e1000_regs[E1000_ICR / 4];
    if ((causes & E1000_IMS_LSC) != 0)
        __atomic_fetch_add(&e1000_link_events, 1U, __ATOMIC_RELAXED);
    if ((causes & E1000_IMS_RXO) != 0)
        __atomic_fetch_add(&e1000_rx_overruns, 1U, __ATOMIC_RELAXED);
    e1000_dma_read_barrier();
    while (e1000_tx_pending != 0 &&
           (e1000_tx_ring[e1000_tx_reclaim_index].status & E1000_DESC_DONE) != 0) {
        if ((e1000_tx_ring[e1000_tx_reclaim_index].status &
             E1000_DESC_ERROR_MASK) != 0)
            __atomic_fetch_add(&e1000_tx_errors, 1U, __ATOMIC_RELAXED);
        e1000_tx_reclaim_index =
            (e1000_tx_reclaim_index + 1U) % E1000_RING_COUNT;
        --e1000_tx_pending;
    }
    spinlock_unlock_irqrestore(&e1000_lock, flags);
    return causes;
}

int e1000_receive(void *data, uint16_t capacity, uint16_t *length) {
    if (!e1000_regs || !e1000_rx_ring || !data || !length || capacity == 0)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&e1000_lock);
    uint32_t index = e1000_rx_index;
    uint32_t total = 0;
    uint32_t descriptors = 0;
    int invalid = 0;
    e1000_dma_read_barrier();
    for (;;) {
        volatile e1000_rx_desc_t *descriptor = &e1000_rx_ring[index];
        if ((descriptor->status & E1000_DESC_DONE) == 0) {
            if (descriptors == 0) {
                spinlock_unlock_irqrestore(&e1000_lock, flags);
                return 0;
            }
            spinlock_unlock_irqrestore(&e1000_lock, flags);
            return 0;
        }
        uint32_t packet_length = descriptor->length;
        if (descriptor->errors != 0 || packet_length == 0 ||
            packet_length > 4096 || total > UINT32_MAX - packet_length ||
            total + packet_length > capacity) {
            invalid = 1;
        }
        if (total <= UINT32_MAX - packet_length)
            total += packet_length;
        else
            total = UINT32_MAX;
        ++descriptors;
        uint8_t end = descriptor->status & E1000_DESC_EOP;
        index = (index + 1U) % E1000_RING_COUNT;
        if (end || descriptors == E1000_RING_COUNT) {
            if (!end) invalid = 1;
            break;
        }
    }
    if (invalid || total == 0 || total > UINT16_MAX) {
        __atomic_fetch_add(&e1000_rx_errors, 1U, __ATOMIC_RELAXED);
        uint32_t recycle = e1000_rx_index;
        for (uint32_t consumed = 0; consumed < descriptors; ++consumed) {
            e1000_rx_ring[recycle].status = 0;
            e1000_rx_ring[recycle].errors = 0;
            recycle = (recycle + 1U) % E1000_RING_COUNT;
        }
        e1000_rx_index = recycle;
        e1000_dma_write_barrier();
        e1000_regs[E1000_RDT / 4] =
            (e1000_rx_index + E1000_RING_COUNT - 1U) % E1000_RING_COUNT;
        spinlock_unlock_irqrestore(&e1000_lock, flags);
        return 0;
    }
    index = e1000_rx_index;
    uint32_t copied = 0;
    for (uint32_t consumed = 0; consumed < descriptors; ++consumed) {
        volatile e1000_rx_desc_t *descriptor = &e1000_rx_ring[index];
        const uint8_t *source =
            (const uint8_t *)(uintptr_t)e1000_rx_buffers[index];
        uint8_t *destination = (uint8_t *)data;
        for (uint32_t i = 0; i < descriptor->length; ++i)
            destination[copied + i] = source[i];
        copied += descriptor->length;
        descriptor->status = 0;
        descriptor->errors = 0;
        index = (index + 1U) % E1000_RING_COUNT;
    }
    e1000_rx_index = index;
    e1000_dma_write_barrier();
    e1000_regs[E1000_RDT / 4] =
        (index + E1000_RING_COUNT - 1U) % E1000_RING_COUNT;
    *length = (uint16_t)total;
    spinlock_unlock_irqrestore(&e1000_lock, flags);
    return 1;
}
