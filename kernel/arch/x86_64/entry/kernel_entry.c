#include <stdint.h>
#include "../../../../boot/UEFI/core/boot_info.h"
#include "../cpu/tables.h"
#include "../cpu/cpu.h"
#include "../../../core/printk/serial.h"
#include "../../../mm/physical/frame.h"
#include "../../../mm/virtual/address_space.h"
#include "../../../mm/heap/heap.h"
#include "../interrupts/irq.h"
#include "../interrupts/apic.h"
#include "../platform/acpi.h"
#include "../smp/percpu.h"
#include "../time/timer.h"
#include "../../../core/sync/spinlock.h"
#include "../../../ipc/channel.h"
#include "../../../security/credentials.h"
#include "../../../fs/vfs/vfs.h"
#include "../../../fs/vfs/mount.h"
#include "../../../fs/block/block.h"
#include "../../../fs/cache/cache.h"
#include "../../../fs/devfs/devfs.h"
#include "../../../fs/procfs/procfs.h"
#include "../../../mm/slab/slab.h"
#include "../../../fs/fat/fat32.h"
#include "../../../fs/fat/fat32_vfs.h"
#include "../../../drivers/input/input.h"
#include "../../../drivers/input/ps2.h"
#include "../../../drivers/display/framebuffer.h"
#include "../../../drivers/usb/usb.h"
#include "../../../drivers/usb/hid.h"
#include "../../../drivers/usb/uhci.h"
#include "../../../drivers/ahci/ahci.h"
#include "../../../drivers/nvme/nvme.h"
#include "../../../drivers/network/e1000.h"
#include "../../../drivers/network/ethernet.h"
#include "../../../drivers/network/arp.h"
#include "../../../drivers/network/arp_cache.h"
#include "../../../drivers/network/ipv4.h"
#include "../../../drivers/network/udp.h"
#include "../../../drivers/network/icmp.h"
#include "../../../drivers/network/route.h"
#include "../../../drivers/network/packet_queue.h"
#include "../../../drivers/network/network.h"
#include "../../../drivers/network/reassembly.h"
#include "../../../drivers/network/udp_endpoint.h"
#include "../../../time/clock.h"
#include "../../../debug/assert.h"
#include "../../../core/task/context.h"
#include "../../../core/task/wait_queue.h"
#include "../../../core/task/scheduler.h"
#include "../../../device/device.h"
#include "../../../drivers/pci/pci.h"
#include "../../../drivers/storage/storage.h"
#include "../../../drivers/storage/ata.h"
#include "../../../core/process/user_image.h"
#include "../../../core/process/process.h"
#include "../../../core/process/thread.h"
#include "../../../core/syscall/syscall.h"
#include "../../../syscall/abi.h"
#include "../../../core/init/init.h"
#include "../../../drivers/firmware/firmware.h"

static task_context_t task_demo_main;
static task_context_t task_demo_worker;
static uint8_t task_demo_stack[4096] __attribute__((aligned(16)));
static task_wait_queue_t task_demo_waiters;
static task_wait_node_t task_demo_waiter_a;
static task_wait_node_t task_demo_waiter_b;
static task_t scheduler_demo_task_a;
static task_t scheduler_demo_task_b;
static task_t scheduler_demo_idle;
static task_t *kernel_thread_active;
static uint8_t kernel_thread_ran;
static address_space_t kernel_space;
static uint8_t user_image_probe[256];
static volatile uint64_t preempt_task_a_ticks;
static volatile uint64_t preempt_task_b_ticks;
static ipc_channel_t ipc_block_probe_channel;
static volatile uint8_t ipc_block_probe_result;
static process_t *signal_wait_probe_process;
static volatile uint8_t signal_wait_probe_result;
static uint8_t ahci_read_probe[512];
static void process_thread_probe(void *argument) { (void)argument; }
static int block_probe_read(void *context, uint64_t sector, uint32_t count,
                            void *buffer) {
    uint8_t *source = (uint8_t *)context;
    uint8_t *destination = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count * 16U; ++i)
        destination[i] = source[sector * 16U + i];
    return 1;
}
static int block_probe_write(void *context, uint64_t sector, uint32_t count,
                             const void *buffer) {
    uint8_t *destination = (uint8_t *)context;
    const uint8_t *source = (const uint8_t *)buffer;
    for (uint32_t i = 0; i < count * 16U; ++i)
        destination[sector * 16U + i] = source[i];
    return 1;
}

static void preempt_task_a(void *argument) {
    uint64_t deadline = *(uint64_t *)argument;
    while (timer_ticks() < deadline) {
        ++preempt_task_a_ticks;
    }
    scheduler_task_exit();
}

static void preempt_task_b(void *argument) {
    uint64_t deadline = *(uint64_t *)argument;
    while (timer_ticks() < deadline) {
        ++preempt_task_b_ticks;
    }
    scheduler_task_exit();
}

static void ipc_block_receiver(void *argument) {
    (void)argument;
    char data[4] = {0};
    uint64_t sender = 0;
    uint32_t size = 0;
    if (ipc_channel_receive_wait(&ipc_block_probe_channel, 2, data,
                                 sizeof(data), &sender, &size) &&
        sender == 1 && size == 4 && data[0] == 'p' && data[1] == 'i' &&
        data[2] == 'n' && data[3] == 'g') {
        ipc_block_probe_result |= 2U;
    }
    scheduler_task_exit();
}

static void ipc_block_sender(void *argument) {
    (void)argument;
    static const char message[4] = {'p', 'i', 'n', 'g'};
    if (ipc_channel_send_wait(&ipc_block_probe_channel, 1, message,
                              sizeof(message))) ipc_block_probe_result |= 1U;
    scheduler_task_exit();
}

static void signal_wait_receiver(void *argument) {
    (void)argument;
    uint32_t signal = 0;
    if (process_wait_signal(signal_wait_probe_process, &signal) && signal == 6)
        signal_wait_probe_result |= 2U;
    scheduler_task_exit();
}

static void signal_wait_sender(void *argument) {
    (void)argument;
    if (process_send_signal(signal_wait_probe_process, 6))
        signal_wait_probe_result |= 1U;
    scheduler_task_exit();
}

static void probe16(uint8_t *at, uint16_t value) {
    at[0] = (uint8_t)value; at[1] = (uint8_t)(value >> 8);
}
static void probe32(uint8_t *at, uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i) at[i] = (uint8_t)(value >> (i * 8));
}
static void probe64(uint8_t *at, uint64_t value) {
    for (uint32_t i = 0; i < 8; ++i) at[i] = (uint8_t)(value >> (i * 8));
}
static void build_user_image_probe(void) {
    for (uint32_t i = 0; i < sizeof(user_image_probe); ++i) user_image_probe[i] = 0;
    user_image_probe[0] = 0x7f; user_image_probe[1] = 'E';
    user_image_probe[2] = 'L'; user_image_probe[3] = 'F';
    user_image_probe[4] = 2; user_image_probe[5] = 1; user_image_probe[6] = 1;
    probe16(&user_image_probe[16], 2); probe16(&user_image_probe[18], 62);
    probe32(&user_image_probe[20], 1); probe64(&user_image_probe[24], 0x8000001000ULL);
    probe64(&user_image_probe[32], 64); probe16(&user_image_probe[52], 64);
    probe16(&user_image_probe[54], 56); probe16(&user_image_probe[56], 2);
    probe32(&user_image_probe[64], 1); probe32(&user_image_probe[68], 5);
    probe64(&user_image_probe[72], 120); probe64(&user_image_probe[80], 0x8000001000ULL);
    probe64(&user_image_probe[88], 0x8000001000ULL); probe64(&user_image_probe[96], 8);
    probe64(&user_image_probe[104], 4096); probe64(&user_image_probe[112], 4096);
    user_image_probe[120] = 0xb8; user_image_probe[121] = 0; user_image_probe[122] = 0;
    user_image_probe[123] = 0; user_image_probe[124] = 0; user_image_probe[125] = 0xcd;
    user_image_probe[126] = 0x80; user_image_probe[127] = 0xf4;
    probe32(&user_image_probe[176], 1); probe32(&user_image_probe[180], 4);
    probe64(&user_image_probe[184], 232); probe64(&user_image_probe[192], 0x8000001800ULL);
    probe64(&user_image_probe[200], 0x8000001800ULL); probe64(&user_image_probe[208], 4);
    probe64(&user_image_probe[216], 16); probe64(&user_image_probe[224], 4096);
    user_image_probe[232] = 0x90; user_image_probe[233] = 0x90;
    user_image_probe[234] = 0x90; user_image_probe[235] = 0x90;
}

static void task_object_probe(void *argument) {
    (void)argument;
    kernel_thread_ran = 1;
    serial_write("kernel task entered\r\n");
    task_context_switch(&kernel_thread_active->context, &task_demo_main);
}

static void task_demo_entry(void *argument) {
    (void)argument;
    serial_write("task context entered\r\n");
    task_context_switch(&task_demo_worker, &task_demo_main);
}

void kernel_main(void *boot_info) {
    const os_boot_info_t *info = boot_info;
    kernel_init_state_t init_state;
    kernel_init_state_initialize(&init_state);
    serial_init();
    serial_write("serial driver ready\r\n");
    kernel_init_state_t invalid_init;
    kernel_init_state_initialize(&invalid_init);
    if (kernel_init_state_advance(&invalid_init, KERNEL_INIT_MEMORY)) {
        serial_write("kernel initialization ordering failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!kernel_init_state_advance(&init_state, KERNEL_INIT_EARLY))
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    serial_write("os kernel architectural entry\r\n");
    arch_init_tables();
    const arch_cpu_info_t *cpu = arch_cpu_initialize();
    serial_write("cpu="); serial_write(cpu->vendor); serial_write("\r\n");
    serial_write("acpi RSDP="); serial_write_hex(info ? info->acpi_rsdp : 0); serial_write("\r\n");
    if (!arch_apic_initialize(cpu)) {
        serial_write("local APIC initialization failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("local APIC ready id="); serial_write_hex(arch_apic_id()); serial_write("\r\n");
    if (!firmware_boot_contract_valid(info)) {
        serial_write("os kernel contract invalid\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    os_boot_info_t invalid_contract = *info;
    invalid_contract.memory_map_size -= 1;
    if (firmware_boot_contract_valid(&invalid_contract)) {
        serial_write("firmware contract rejection failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("firmware boot contract ready\r\n");
    if (!physical_init(info)) {
        serial_write("physical memory initialization failed\r\n");
        for (;;) __asm__ volatile ("hlt" ::: "memory");
    }
    serial_write("physical free frames="); serial_write_hex(physical_stats()->free_frames); serial_write("\r\n");
    uint64_t physical_free_before = physical_stats()->free_frames;
    physical_free_frame(info->kernel_base & ~0xfffULL);
    if (physical_stats()->free_frames != physical_free_before) {
        serial_write("physical ownership failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!virtual_memory_initialize()) {
        serial_write("virtual memory initialization failed\r\n");
        for (;;) __asm__ volatile ("hlt" ::: "memory");
    }
    serial_write("virtual memory root="); serial_write_hex(virtual_memory_root()); serial_write("\r\n");
    address_space_t process_space;
    process_space.root = 0;
    process_space.owned_count = 0;
    uint64_t process_page = physical_alloc_frame();
    user_image_t rejected_image;
    user_image_t loaded_image;
    build_user_image_probe();
    if (!address_space_create(&process_space) ||
        user_image_load(&process_space, 0, 0, &rejected_image) ||
        !user_image_load(&process_space, user_image_probe, sizeof(user_image_probe), &loaded_image) ||
        loaded_image.entry != 0x8000001000ULL || loaded_image.page_count != 1 ||
        address_space_user_range_valid(&process_space, 0x8000001000ULL, 1, 1) ||
        !address_space_page_executable(&process_space, 0x8000001000ULL) ||
        !process_page || !address_space_map_page(&process_space, 0x8000000000ULL,
                                                  process_page, ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_USER) ||
        address_space_map_page(&process_space, 0x8000001000ULL, process_page, 0x100ULL) ||
        address_space_map_page(&process_space, 0x8000000000ULL, process_page,
                               ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_USER) ||
        address_space_page_executable(&process_space, 0x8000000000ULL) ||
        process_space.root == virtual_memory_root()) {
        serial_write("address space foundation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    user_image_destroy(&process_space, &loaded_image);
    if (!address_space_activate(&process_space) ||
        (kernel_space.root = virtual_memory_root(), !address_space_activate(&kernel_space)) ||
        !address_space_destroy(&process_space)) {
        serial_write("address space foundation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("user image loader ready\r\n");
    physical_free_frame(process_page);
    serial_write("address space foundation ready\r\n");
    if (!kernel_init_state_advance(&init_state, KERNEL_INIT_MEMORY))
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    if (!acpi_initialize(info->acpi_rsdp)) {
        serial_write("ACPI MADT initialization failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ACPI MADT CPUs="); serial_write_hex(acpi_cpu_count());
    serial_write(" LAPIC="); serial_write_hex(acpi_lapic_base()); serial_write("\r\n");
    if (!arch_percpu_initialize()) {
        serial_write("per-CPU initialization failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("per-CPU BSP ready online="); serial_write_hex(arch_percpu_online_count());
    serial_write(" present="); serial_write_hex(arch_percpu_present_count()); serial_write("\r\n");
    if (!arch_percpu_bringup()) {
        serial_write("AP startup failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write_hex_line("SMP online=", arch_percpu_online_count());
    if (!kernel_init_state_advance(&init_state, KERNEL_INIT_PLATFORM))
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    device_registry_initialize();
    if (!pci_initialize() || pci_device_count() != device_count() ||
        pci_resource_count() == 0 ||
        !device_bind_drivers() || !device_at(0) ||
        device_at(0)->bus != DEVICE_BUS_PCI) {
        serial_write("PCI device model failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("device model ready\r\n");
    if (!kernel_init_state_advance(&init_state, KERNEL_INIT_DRIVERS))
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    if (!uhci_initialize()) {
        serial_write("UHCI initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("UHCI driver ready controllers=");
    serial_write_hex_line("", uhci_controller_count());
    serial_write("UHCI root hub ready ports=");
    serial_write_hex_line("", uhci_root_port_count());
    static const uint8_t uhci_descriptor_setup[8] = {0x80, 0x06, 0, 1, 0, 0, 18, 0};
    uint8_t uhci_descriptor[18];
    if ((uhci_root_port_count() != 0 &&
         !uhci_control_transfer(0, 0, uhci_descriptor_setup,
                                uhci_descriptor, sizeof(uhci_descriptor))) ||
        (uhci_root_port_count() != 0 &&
         (uhci_descriptor[0] != 18 || uhci_descriptor[1] != 1))) {
        serial_write("UHCI control transfer failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (uhci_root_port_count() != 0) serial_write("UHCI control transfer ready\r\n");
    static const uint8_t uhci_set_address_setup[8] = {0, 5, 1, 0, 0, 0, 0, 0};
    static const uint8_t uhci_device_setup[8] = {0x80, 6, 0, 1, 0, 0, 18, 0};
    static const uint8_t uhci_config_setup[8] = {0x80, 6, 0, 2, 0, 0, 64, 0};
    uint8_t uhci_set_config_setup[8] = {0, 9, 1, 0, 0, 0, 0, 0};
    uint8_t uhci_config_descriptor[64];
    if (uhci_root_port_count() != 0 &&
        (!uhci_control_transfer(0, 0, uhci_set_address_setup, 0, 0) ||
         !uhci_control_transfer(1, 0, uhci_device_setup, uhci_descriptor,
                                sizeof(uhci_descriptor)) ||
         uhci_descriptor[0] != 18 || uhci_descriptor[1] != 1 ||
         !uhci_control_transfer(1, 0, uhci_config_setup, uhci_config_descriptor,
                                sizeof(uhci_config_descriptor)) ||
         uhci_config_descriptor[0] != 9 || uhci_config_descriptor[1] != 2 ||
         uhci_config_descriptor[5] == 0 ||
         (((uint16_t)uhci_config_descriptor[2] |
           ((uint16_t)uhci_config_descriptor[3] << 8)) >
          (uint16_t)sizeof(uhci_config_descriptor)))) {
        serial_write("UHCI device enumeration failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    uhci_set_config_setup[2] = uhci_config_descriptor[5];
    if (uhci_root_port_count() != 0 &&
        !uhci_control_transfer(1, 0, uhci_set_config_setup, 0, 0)) {
        serial_write("UHCI device enumeration failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (uhci_root_port_count() != 0) serial_write("UHCI device enumeration ready\r\n");
    usb_device_t uhci_keyboard;
    uint8_t uhci_interrupt_endpoint = 0;
    uint16_t uhci_interrupt_packet = 0;
    if (uhci_root_port_count() != 0 &&
        usb_device_parse_descriptor(&uhci_keyboard, uhci_descriptor,
                                    sizeof(uhci_descriptor))) {
        uint16_t total = (uint16_t)uhci_config_descriptor[2] |
                         ((uint16_t)uhci_config_descriptor[3] << 8);
        for (uint16_t offset = 0; offset + 2 <= total;) {
            uint8_t descriptor_length = uhci_config_descriptor[offset];
            if (descriptor_length < 2 || offset + descriptor_length > total) break;
            if (uhci_config_descriptor[offset + 1] == 5 &&
                usb_device_add_endpoint(&uhci_keyboard,
                                        &uhci_config_descriptor[offset],
                                        descriptor_length)) {
                usb_endpoint_t *endpoint =
                    &uhci_keyboard.endpoints[uhci_keyboard.endpoint_count - 1U];
                if ((endpoint->address & 0x80U) != 0 &&
                    (endpoint->attributes & 3U) == 3U && !uhci_interrupt_endpoint) {
                    uhci_interrupt_endpoint = endpoint->address;
                    uhci_interrupt_packet = endpoint->max_packet_size;
                }
            }
            offset = (uint16_t)(offset + descriptor_length);
        }
    }
    if (uhci_root_port_count() != 0 && uhci_interrupt_endpoint != 0) {
        serial_write("UHCI HID interrupt endpoint ready\r\n");
    }
    if (!nvme_initialize()) {
        serial_write("NVMe initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("NVMe driver ready controllers=");
    serial_write_hex_line("", nvme_controller_count());
    static uint8_t nvme_identify_probe[4096];
    if (nvme_controller_count() != 0 &&
        (!nvme_identify_controller(nvme_identify_probe) ||
         (nvme_identify_probe[0] == 0xff && nvme_identify_probe[1] == 0xff))) {
        serial_write("NVMe identify failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (nvme_controller_count() != 0) serial_write("NVMe admin I/O ready\r\n");
    static uint8_t nvme_namespace_probe[4096];
    uint64_t nvme_namespace_sectors = 0;
    if (nvme_controller_count() != 0 &&
        (!nvme_identify_namespace(nvme_namespace_probe) ||
         (nvme_namespace_probe[0] == 0 && nvme_namespace_probe[1] == 0 &&
          nvme_namespace_probe[2] == 0 && nvme_namespace_probe[3] == 0))) {
        serial_write("NVMe namespace identify failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (nvme_controller_count() != 0) {
        for (uint32_t i = 0; i < 8; ++i)
            nvme_namespace_sectors |= (uint64_t)nvme_namespace_probe[i] << (i * 8U);
        if (nvme_namespace_sectors == 0) {
            serial_write("NVMe namespace capacity failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("NVMe namespace ready\r\n");
    }
    if (nvme_controller_count() != 0 &&
        (!nvme_read_sector(0, nvme_identify_probe) ||
         nvme_identify_probe[82] != 'F' || nvme_identify_probe[83] != 'A' ||
         nvme_identify_probe[84] != 'T' || nvme_identify_probe[85] != '3' ||
         nvme_identify_probe[86] != '2' || nvme_identify_probe[510] != 0x55 ||
         nvme_identify_probe[511] != 0xaa)) {
        serial_write("NVMe sector read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (nvme_controller_count() != 0) serial_write("NVMe sector I/O ready\r\n");
    static uint8_t nvme_write_probe[512];
    static uint8_t nvme_write_back[512];
    for (uint32_t i = 0; i < 512; ++i) nvme_write_probe[i] = (uint8_t)(i ^ 0xa5U);
    if (nvme_controller_count() != 0 &&
        (!nvme_write_sector(120, nvme_write_probe) ||
         !nvme_read_sector(120, nvme_write_back))) {
        serial_write("NVMe sector write failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (nvme_controller_count() != 0) {
        for (uint32_t i = 0; i < 512; ++i)
            if (nvme_write_back[i] != nvme_write_probe[i]) {
                serial_write("NVMe sector verify failure\r\n");
                for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
            }
        serial_write("NVMe sector write I/O ready\r\n");
    }
    static uint8_t nvme_multi_write[1024];
    static uint8_t nvme_multi_read[1024];
    for (uint32_t i = 0; i < sizeof(nvme_multi_write); ++i)
        nvme_multi_write[i] = (uint8_t)(0x5aU ^ i);
    if (nvme_controller_count() != 0 &&
        (!nvme_write_sectors(122, 2, nvme_multi_write) ||
         !nvme_read_sectors(122, 2, nvme_multi_read))) {
        serial_write("NVMe multi-sector I/O failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (nvme_controller_count() != 0) {
        for (uint32_t i = 0; i < sizeof(nvme_multi_write); ++i)
            if (nvme_multi_write[i] != nvme_multi_read[i]) {
                serial_write("NVMe multi-sector verification failure\r\n");
                for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
            }
        serial_write("NVMe multi-sector I/O ready\r\n");
    }
    if (!e1000_initialize()) {
        serial_write("e1000 initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("e1000 driver ready controllers=");
    serial_write_hex_line("", e1000_controller_count());
    static const uint8_t ethernet_probe_destination[6] =
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static uint8_t ethernet_probe_source[6] =
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    static const uint8_t ethernet_probe_payload[4] =
        {0xde, 0xad, 0xbe, 0xef};
    static uint8_t ethernet_probe_frame[ETHERNET_MAX_FRAME_SIZE];
    uint16_t ethernet_probe_length = 0;
    ethernet_frame_view_t ethernet_probe_view;
    if (e1000_controller_count() != 0 &&
        !e1000_mac_address(ethernet_probe_source)) {
        serial_write("e1000 MAC failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!ethernet_frame_build(ethernet_probe_frame,
                              sizeof(ethernet_probe_frame),
                              ethernet_probe_destination,
                              ethernet_probe_source, 0x88b5,
                              ethernet_probe_payload,
                              sizeof(ethernet_probe_payload),
                              &ethernet_probe_length) ||
        ethernet_probe_length != ETHERNET_MIN_FRAME_SIZE ||
        !ethernet_frame_parse(ethernet_probe_frame, ethernet_probe_length,
                              &ethernet_probe_view) ||
        ethernet_probe_view.ether_type != 0x88b5 ||
        ethernet_probe_view.payload_length !=
            ETHERNET_MIN_FRAME_SIZE - ETHERNET_HEADER_SIZE ||
        !ethernet_address_is_broadcast(ethernet_probe_view.destination) ||
        ethernet_probe_view.payload[0] != 0xde ||
        ethernet_frame_build(ethernet_probe_frame, 59,
                             ethernet_probe_destination, ethernet_probe_source,
                             0x88b5, ethernet_probe_payload,
                             sizeof(ethernet_probe_payload),
                             &ethernet_probe_length)) {
        serial_write("Ethernet frame failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("Ethernet frame ready\r\n");
    static const uint8_t arp_probe_sender_hardware[6] =
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    static const uint8_t arp_probe_sender_protocol[4] = {192, 168, 0, 2};
    static const uint8_t arp_probe_target_hardware[6] = {0};
    static const uint8_t arp_probe_target_protocol[4] = {192, 168, 0, 1};
    static uint8_t arp_probe_packet[ARP_PACKET_SIZE];
    uint16_t arp_probe_length = 0;
    arp_packet_view_t arp_probe_view;
    if (!arp_packet_build(arp_probe_packet, sizeof(arp_probe_packet),
                          ARP_OPERATION_REQUEST, arp_probe_sender_hardware,
                          arp_probe_sender_protocol,
                          arp_probe_target_hardware,
                          arp_probe_target_protocol, &arp_probe_length) ||
        arp_probe_length != ARP_PACKET_SIZE ||
        !arp_packet_parse(arp_probe_packet, arp_probe_length,
                          &arp_probe_view) ||
        arp_probe_view.operation != ARP_OPERATION_REQUEST ||
        arp_probe_view.sender_protocol[0] != 192 ||
        arp_probe_view.target_protocol[3] != 1 ||
        arp_packet_parse(arp_probe_packet, ARP_PACKET_SIZE - 1,
                         &arp_probe_view)) {
        serial_write("ARP packet failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ARP packet ready\r\n");
    arp_cache_t arp_probe_cache;
    static const uint8_t arp_cache_ip[4] = {192, 168, 0, 1};
    static const uint8_t arp_cache_mac[6] =
        {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    static const uint8_t arp_cache_replacement_mac[6] =
        {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    uint8_t arp_cache_result[6] = {0};
    arp_cache_initialize(&arp_probe_cache);
    if (!arp_cache_update(&arp_probe_cache, arp_cache_ip, arp_cache_mac, 10) ||
        !arp_cache_lookup(&arp_probe_cache, arp_cache_ip, arp_cache_result) ||
        arp_cache_result[1] != 0x11 ||
        !arp_cache_update(&arp_probe_cache, arp_cache_ip,
                          arp_cache_replacement_mac, 20) ||
        !arp_cache_lookup(&arp_probe_cache, arp_cache_ip, arp_cache_result) ||
        arp_cache_result[1] != 0xaa ||
        arp_cache_expire(&arp_probe_cache, 29, 10) != 0 ||
        arp_cache_expire(&arp_probe_cache, 30, 10) != 1 ||
        arp_cache_lookup(&arp_probe_cache, arp_cache_ip, arp_cache_result)) {
        serial_write("ARP cache failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ARP cache ready\r\n");
    static const uint8_t ipv4_probe_source[4] = {192, 168, 0, 2};
    static const uint8_t ipv4_probe_destination[4] = {192, 168, 0, 1};
    static const uint8_t ipv4_probe_payload[4] = {1, 2, 3, 4};
    static uint8_t ipv4_probe_packet[IPV4_MIN_HEADER_SIZE + 4];
    uint16_t ipv4_probe_length = 0;
    ipv4_packet_view_t ipv4_probe_view;
    if (!ipv4_packet_build(ipv4_probe_packet, sizeof(ipv4_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 17, 64,
                           0x1234, ipv4_probe_payload,
                           sizeof(ipv4_probe_payload), &ipv4_probe_length) ||
        ipv4_probe_length != sizeof(ipv4_probe_packet) ||
        !ipv4_packet_parse(ipv4_probe_packet, ipv4_probe_length,
                           &ipv4_probe_view) || ipv4_probe_view.ihl != 5 ||
        ipv4_probe_view.protocol != 17 || ipv4_probe_view.ttl != 64 ||
        ipv4_probe_view.payload_length != sizeof(ipv4_probe_payload) ||
        ipv4_probe_view.payload[3] != 4) {
        serial_write("IPv4 packet failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    ipv4_probe_packet[10] ^= 1U;
    if (ipv4_packet_parse(ipv4_probe_packet, ipv4_probe_length,
                          &ipv4_probe_view)) {
        serial_write("IPv4 checksum failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("IPv4 packet ready\r\n");
    static const uint8_t udp_probe_payload[5] = {5, 4, 3, 2, 1};
    static uint8_t udp_probe_packet[UDP_HEADER_SIZE + sizeof(udp_probe_payload)];
    uint16_t udp_probe_length = 0;
    udp_packet_view_t udp_probe_view;
    if (!udp_packet_build(udp_probe_packet, sizeof(udp_probe_packet),
                          ipv4_probe_source, ipv4_probe_destination,
                          4000, 5000, udp_probe_payload,
                          sizeof(udp_probe_payload), &udp_probe_length) ||
        udp_probe_length != sizeof(udp_probe_packet) ||
        !udp_packet_parse(udp_probe_packet, udp_probe_length,
                          ipv4_probe_source, ipv4_probe_destination,
                          &udp_probe_view) || udp_probe_view.source_port != 4000 ||
        udp_probe_view.destination_port != 5000 ||
        udp_probe_view.payload_length != sizeof(udp_probe_payload) ||
        udp_probe_view.payload[0] != 5) {
        serial_write("UDP packet failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    udp_probe_packet[7] ^= 1U;
    if (udp_packet_parse(udp_probe_packet, udp_probe_length,
                         ipv4_probe_source, ipv4_probe_destination,
                         &udp_probe_view)) {
        serial_write("UDP checksum failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("UDP packet ready\r\n");
    static const uint8_t icmp_probe_payload[5] = {9, 8, 7, 6, 5};
    static uint8_t icmp_probe_packet[ICMP_ECHO_HEADER_SIZE +
                                     sizeof(icmp_probe_payload)];
    uint16_t icmp_probe_length = 0;
    icmp_echo_view_t icmp_probe_view;
    if (!icmp_echo_build(icmp_probe_packet, sizeof(icmp_probe_packet),
                         ICMP_TYPE_ECHO_REQUEST, 0x1234, 7,
                         icmp_probe_payload, sizeof(icmp_probe_payload),
                         &icmp_probe_length) ||
        !icmp_echo_parse(icmp_probe_packet, icmp_probe_length,
                         &icmp_probe_view) ||
        icmp_probe_view.identifier != 0x1234 ||
        icmp_probe_view.sequence != 7 ||
        icmp_probe_view.payload_length != sizeof(icmp_probe_payload) ||
        icmp_probe_view.payload[4] != 5) {
        serial_write("ICMP packet failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    icmp_probe_packet[2] ^= 1U;
    if (icmp_echo_parse(icmp_probe_packet, icmp_probe_length,
                        &icmp_probe_view)) {
        serial_write("ICMP checksum failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ICMP packet ready\r\n");
    ipv4_route_table_t route_probe_table;
    ipv4_route_entry_t route_probe_result;
    ipv4_route_table_initialize(&route_probe_table);
    if (!ipv4_route_add(&route_probe_table, 0, 0, 0x01010101, 1, 100) ||
        !ipv4_route_add(&route_probe_table, 0x0a000000, 8, 0x02020202, 1, 50) ||
        !ipv4_route_add(&route_probe_table, 0x0a010000, 16, 0x03030303, 2, 100) ||
        !ipv4_route_lookup(&route_probe_table, 0x0a010203, &route_probe_result) ||
        route_probe_result.interface_id != 2 ||
        route_probe_result.gateway != 0x03030303 ||
        !ipv4_route_add(&route_probe_table, 0x0a010000, 16, 0x04040404, 3, 10) ||
        !ipv4_route_lookup(&route_probe_table, 0x0a010203, &route_probe_result) ||
        route_probe_result.interface_id != 3 ||
        !ipv4_route_remove(&route_probe_table, 0x0a010000, 16, 3) ||
        !ipv4_route_lookup(&route_probe_table, 0x0a010203, &route_probe_result) ||
        route_probe_result.interface_id != 2 ||
        ipv4_route_lookup(&route_probe_table, 0xc0a80001, &route_probe_result) == 0 ||
        !ipv4_route_remove(&route_probe_table, 0, 0, 1)) {
        serial_write("IPv4 route failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("IPv4 route ready\r\n");
    static network_packet_queue_t network_probe_queue;
    static uint8_t network_probe_received[ETHERNET_MAX_FRAME_SIZE];
    network_packet_queue_initialize(&network_probe_queue);
    if (!network_packet_queue_push(&network_probe_queue, ethernet_probe_frame,
                                   ethernet_probe_length) ||
        network_packet_queue_count(&network_probe_queue) != 1 ||
        !network_packet_queue_pop(&network_probe_queue, network_probe_received,
                                  sizeof(network_probe_received),
                                  &ethernet_probe_length) ||
        ethernet_probe_length != ETHERNET_MIN_FRAME_SIZE ||
        network_probe_received[0] != 0xff ||
        network_packet_queue_count(&network_probe_queue) != 0) {
        serial_write("network packet queue failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t i = 0; i < NETWORK_PACKET_QUEUE_CAPACITY; ++i)
        (void)network_packet_queue_push(&network_probe_queue,
                                        ethernet_probe_frame,
                                        ETHERNET_MIN_FRAME_SIZE);
    if (network_packet_queue_push(&network_probe_queue, ethernet_probe_frame,
                                  ETHERNET_MIN_FRAME_SIZE) ||
        network_packet_queue_dropped(&network_probe_queue) != 1) {
        serial_write("network packet queue overflow failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("network packet queue ready\r\n");
    network_packet_queue_initialize(&network_probe_queue);
    if (network_e1000_poll(&network_probe_queue, 4) != 0 ||
        network_packet_queue_count(&network_probe_queue) != 0) {
        serial_write("network interface receive failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (e1000_controller_count() != 0 &&
        !network_e1000_transmit(ethernet_probe_frame, ethernet_probe_length)) {
        serial_write("e1000 transmit failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (e1000_controller_count() != 0) {
        serial_write("network interface ready\r\n");
        serial_write("e1000 network I/O ready\r\n");
    }
    static const uint8_t network_dispatch_payload[3] = {0xa1, 0xb2, 0xc3};
    static uint8_t network_dispatch_udp[UDP_HEADER_SIZE +
                                        sizeof(network_dispatch_payload)];
    static uint8_t network_dispatch_ipv4[IPV4_MIN_HEADER_SIZE +
                                         sizeof(network_dispatch_udp)];
    static uint8_t network_dispatch_frame[ETHERNET_MAX_FRAME_SIZE];
    uint16_t network_dispatch_udp_length = 0;
    uint16_t network_dispatch_ipv4_length = 0;
    uint16_t network_dispatch_frame_length = 0;
    network_frame_view_t network_dispatch_view;
    if (!udp_packet_build(network_dispatch_udp, sizeof(network_dispatch_udp),
                          ipv4_probe_source, ipv4_probe_destination, 6000,
                          6001, network_dispatch_payload,
                          sizeof(network_dispatch_payload),
                          &network_dispatch_udp_length) ||
        !ipv4_packet_build(network_dispatch_ipv4, sizeof(network_dispatch_ipv4),
                           ipv4_probe_source, ipv4_probe_destination, 17, 64,
                           0x4321, network_dispatch_udp,
                           network_dispatch_udp_length,
                           &network_dispatch_ipv4_length) ||
        !ethernet_frame_build(network_dispatch_frame,
                              sizeof(network_dispatch_frame),
                              ethernet_probe_destination,
                              ethernet_probe_source, 0x0800,
                              network_dispatch_ipv4,
                              network_dispatch_ipv4_length,
                              &network_dispatch_frame_length) ||
        !network_decode_frame(network_dispatch_frame,
                              network_dispatch_frame_length,
                              &network_dispatch_view) ||
        network_dispatch_view.kind != NETWORK_FRAME_UDP ||
        network_dispatch_view.udp.destination_port != 6001 ||
        network_dispatch_view.udp.payload[2] != 0xc3) {
        serial_write("network frame decode failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("network frame decode ready\r\n");
    static uint8_t arp_reply_probe_request[ETHERNET_MAX_FRAME_SIZE];
    static uint8_t arp_reply_probe_reply[ETHERNET_MAX_FRAME_SIZE];
    uint16_t arp_reply_probe_request_length = 0;
    uint16_t arp_reply_probe_reply_length = 0;
    network_frame_view_t arp_reply_probe_view;
    if (!ethernet_frame_build(arp_reply_probe_request,
                              sizeof(arp_reply_probe_request),
                              ethernet_probe_destination,
                              arp_probe_sender_hardware, 0x0806U,
                              arp_probe_packet, arp_probe_length,
                              &arp_reply_probe_request_length) ||
        !network_build_arp_reply(arp_reply_probe_request,
                                 arp_reply_probe_request_length,
                                 ethernet_probe_source,
                                 arp_probe_target_protocol,
                                 arp_reply_probe_reply,
                                 sizeof(arp_reply_probe_reply),
                                 &arp_reply_probe_reply_length) ||
        !network_decode_frame(arp_reply_probe_reply,
                              arp_reply_probe_reply_length,
                              &arp_reply_probe_view) ||
        arp_reply_probe_view.kind != NETWORK_FRAME_ARP ||
        arp_reply_probe_view.arp.operation != ARP_OPERATION_REPLY ||
        arp_reply_probe_view.arp.sender_protocol[3] != 1 ||
        arp_reply_probe_view.arp.target_protocol[3] != 2 ||
        arp_reply_probe_view.ethernet.destination[0] !=
            arp_probe_sender_hardware[0]) {
        serial_write("ARP reply failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ARP reply ready\r\n");
    static const uint8_t icmp_reply_probe_payload[4] =
        {0x10, 0x20, 0x30, 0x40};
    static uint8_t icmp_reply_probe_packet[ICMP_ECHO_HEADER_SIZE +
                                           sizeof(icmp_reply_probe_payload)];
    static uint8_t icmp_reply_probe_ipv4[IPV4_MIN_HEADER_SIZE +
                                         sizeof(icmp_reply_probe_packet)];
    static uint8_t icmp_reply_probe_request[ETHERNET_MAX_FRAME_SIZE];
    static uint8_t icmp_reply_probe_reply[ETHERNET_MAX_FRAME_SIZE];
    uint16_t icmp_reply_probe_packet_length = 0;
    uint16_t icmp_reply_probe_ipv4_length = 0;
    uint16_t icmp_reply_probe_request_length = 0;
    uint16_t icmp_reply_probe_reply_length = 0;
    network_frame_view_t icmp_reply_probe_view;
    if (!icmp_echo_build(icmp_reply_probe_packet,
                         sizeof(icmp_reply_probe_packet),
                         ICMP_TYPE_ECHO_REQUEST, 0x5678, 9,
                         icmp_reply_probe_payload,
                         sizeof(icmp_reply_probe_payload),
                         &icmp_reply_probe_packet_length) ||
        !ipv4_packet_build(icmp_reply_probe_ipv4,
                           sizeof(icmp_reply_probe_ipv4),
                           ipv4_probe_source, ipv4_probe_destination, 1, 64,
                           0x2468, icmp_reply_probe_packet,
                           icmp_reply_probe_packet_length,
                           &icmp_reply_probe_ipv4_length) ||
        !ethernet_frame_build(icmp_reply_probe_request,
                              sizeof(icmp_reply_probe_request),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, icmp_reply_probe_ipv4,
                              icmp_reply_probe_ipv4_length,
                              &icmp_reply_probe_request_length) ||
        !network_build_icmp_echo_reply(icmp_reply_probe_request,
                                       icmp_reply_probe_request_length,
                                       icmp_reply_probe_reply,
                                       sizeof(icmp_reply_probe_reply),
                                       &icmp_reply_probe_reply_length) ||
        !network_decode_frame(icmp_reply_probe_reply,
                              icmp_reply_probe_reply_length,
                              &icmp_reply_probe_view) ||
        icmp_reply_probe_view.kind != NETWORK_FRAME_ICMP ||
        icmp_reply_probe_view.icmp.type != ICMP_TYPE_ECHO_REPLY ||
        icmp_reply_probe_view.icmp.identifier != 0x5678 ||
        icmp_reply_probe_view.ethernet.destination[0] != ethernet_probe_source[0] ||
        icmp_reply_probe_view.ipv4.destination[3] != 2 ||
        icmp_reply_probe_view.icmp.payload[3] != 0x40) {
        serial_write("ICMP echo reply failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ICMP echo reply ready\r\n");
    static ipv4_reassembly_table_t reassembly_probe_table;
    static const uint8_t reassembly_probe_data[16] =
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    static uint8_t reassembly_probe_output[16];
    uint16_t reassembly_probe_length = 0;
    ipv4_reassembly_initialize(&reassembly_probe_table);
    if (ipv4_reassembly_add(&reassembly_probe_table, 0x2222,
                            ipv4_probe_source, ipv4_probe_destination, 17,
                            8, 0, reassembly_probe_data + 8, 8, 10, 100,
                            reassembly_probe_output,
                            sizeof(reassembly_probe_output),
                            &reassembly_probe_length) ||
        !ipv4_reassembly_add(&reassembly_probe_table, 0x2222,
                             ipv4_probe_source, ipv4_probe_destination, 17,
                             0, 1, reassembly_probe_data, 8, 20, 100,
                             reassembly_probe_output,
                             sizeof(reassembly_probe_output),
                             &reassembly_probe_length) ||
        reassembly_probe_length != sizeof(reassembly_probe_data) ||
        reassembly_probe_output[15] != 15 ||
        ipv4_reassembly_add(&reassembly_probe_table, 0x3333,
                            ipv4_probe_source, ipv4_probe_destination, 17,
                            0, 1, reassembly_probe_data, 8, 30, 100,
                            reassembly_probe_output,
                            sizeof(reassembly_probe_output),
                            &reassembly_probe_length) ||
        ipv4_reassembly_add(&reassembly_probe_table, 0x3333,
                            ipv4_probe_source, ipv4_probe_destination, 17,
                            4, 0, reassembly_probe_data + 4, 8, 31, 100,
                            reassembly_probe_output,
                            sizeof(reassembly_probe_output),
                            &reassembly_probe_length)) {
        serial_write("IPv4 reassembly failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("IPv4 reassembly ready\r\n");
    static udp_endpoint_table_t udp_endpoint_probe_table;
    static const uint8_t udp_endpoint_local[4] = {10, 0, 0, 2};
    static const uint8_t udp_endpoint_other[4] = {10, 0, 0, 3};
    static const uint8_t udp_endpoint_wildcard[4] = {0, 0, 0, 0};
    static const uint8_t udp_endpoint_payload[3] = {0x31, 0x32, 0x33};
    static uint8_t udp_endpoint_output[UDP_ENDPOINT_PAYLOAD_MAX];
    uint8_t udp_endpoint_source[4] = {0};
    uint16_t udp_endpoint_source_port = 0;
    uint16_t udp_endpoint_output_length = 0;
    udp_endpoint_handle_t udp_endpoint_exact = 0;
    udp_endpoint_handle_t udp_endpoint_any = 0;
    udp_endpoint_table_initialize(&udp_endpoint_probe_table);
    if (!udp_endpoint_bind(&udp_endpoint_probe_table, udp_endpoint_local, 7000,
                           &udp_endpoint_exact) ||
        !udp_endpoint_bind(&udp_endpoint_probe_table, udp_endpoint_wildcard,
                           7001, &udp_endpoint_any) ||
        !udp_endpoint_deliver(&udp_endpoint_probe_table, udp_endpoint_local,
                              7000, udp_endpoint_other, 8000,
                              udp_endpoint_payload,
                              sizeof(udp_endpoint_payload)) ||
        !udp_endpoint_deliver(&udp_endpoint_probe_table, udp_endpoint_other,
                              7001, udp_endpoint_local, 8001,
                              udp_endpoint_payload,
                              sizeof(udp_endpoint_payload)) ||
        udp_endpoint_deliver(&udp_endpoint_probe_table, udp_endpoint_other,
                             7002, udp_endpoint_local, 8001,
                             udp_endpoint_payload,
                             sizeof(udp_endpoint_payload)) ||
        !udp_endpoint_receive(&udp_endpoint_probe_table, udp_endpoint_exact,
                              udp_endpoint_source, &udp_endpoint_source_port,
                              udp_endpoint_output,
                              sizeof(udp_endpoint_output),
                              &udp_endpoint_output_length) ||
        udp_endpoint_source_port != 8000 || udp_endpoint_output_length != 3 ||
        udp_endpoint_output[2] != 0x33 ||
        !udp_endpoint_receive(&udp_endpoint_probe_table, udp_endpoint_any,
                              udp_endpoint_source, &udp_endpoint_source_port,
                              udp_endpoint_output,
                              sizeof(udp_endpoint_output),
                              &udp_endpoint_output_length) ||
        udp_endpoint_source_port != 8001 ||
        !udp_endpoint_unbind(&udp_endpoint_probe_table, udp_endpoint_exact) ||
        udp_endpoint_receive(&udp_endpoint_probe_table, udp_endpoint_exact,
                             udp_endpoint_source, &udp_endpoint_source_port,
                             udp_endpoint_output,
                             sizeof(udp_endpoint_output),
                             &udp_endpoint_output_length)) {
        serial_write("UDP endpoint failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("UDP endpoint ready\r\n");
    udp_endpoint_table_initialize(&udp_endpoint_probe_table);
    if (!udp_endpoint_bind(&udp_endpoint_probe_table, ipv4_probe_destination,
                           6001, &udp_endpoint_any) ||
        !network_deliver_frame(network_dispatch_frame,
                                network_dispatch_frame_length,
                                &udp_endpoint_probe_table) ||
        !udp_endpoint_receive(&udp_endpoint_probe_table, udp_endpoint_any,
                              udp_endpoint_source, &udp_endpoint_source_port,
                              udp_endpoint_output,
                              sizeof(udp_endpoint_output),
                              &udp_endpoint_output_length) ||
        udp_endpoint_source_port != 6000 || udp_endpoint_output_length != 3 ||
        udp_endpoint_output[0] != 0xa1) {
        serial_write("network UDP delivery failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("network UDP delivery ready\r\n");
    if (e1000_controller_count() != 0)
        serial_write(e1000_link_up() ? "e1000 link ready\r\n" :
                     "e1000 link down\r\n");
    if (e1000_controller_count() != 0) {
        (void)e1000_service();
        serial_write("e1000 completion service ready\r\n");
        serial_write(e1000_interrupt_enabled() ?
                     "e1000 interrupt path ready\r\n" :
                     "e1000 polling fallback ready\r\n");
    }
    if (!ahci_initialize()) {
        serial_write("AHCI initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("AHCI driver ready controllers=");
    serial_write_hex(ahci_controller_count());
    serial_write(" ports=");
    serial_write_hex(ahci_port_mask());
    serial_write(" ready=");
    serial_write_hex(ahci_ready_port_count());
    serial_write("\r\n");
    static uint16_t ahci_identify_words[256];
    if (!ahci_identify(ahci_identify_words)) {
        serial_write("AHCI identify failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("AHCI identify ready\r\n");
    if (!ahci_read_sector(0, ahci_read_probe) ||
        ahci_read_probe[82] != 'F' || ahci_read_probe[83] != 'A' ||
        ahci_read_probe[84] != 'T' || ahci_read_probe[85] != '3' ||
        ahci_read_probe[86] != '2' || ahci_read_probe[510] != 0x55 ||
        ahci_read_probe[511] != 0xaa) {
        serial_write("AHCI sector read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("AHCI sector read ready\r\n");
    uint8_t ahci_write_probe[512];
    for (uint32_t i = 0; i < sizeof(ahci_write_probe); ++i)
        ahci_write_probe[i] = (uint8_t)(i ^ 0x5aU);
    if (!ahci_write_sector(120, ahci_write_probe) ||
        !ahci_read_sector(120, ahci_read_probe)) {
        serial_write("AHCI sector write failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t i = 0; i < sizeof(ahci_write_probe); ++i)
        if (ahci_read_probe[i] != ahci_write_probe[i]) {
            serial_write("AHCI sector write verification failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
    serial_write("AHCI sector write ready\r\n");
    uint8_t ahci_multi_write[1024];
    uint8_t ahci_multi_read[1024];
    for (uint32_t i = 0; i < sizeof(ahci_multi_write); ++i)
        ahci_multi_write[i] = (uint8_t)(0xc3U ^ i);
    if (!ahci_write_sectors(122, 2, ahci_multi_write) ||
        !ahci_read_sectors(122, 2, ahci_multi_read)) {
        serial_write("AHCI multi-sector I/O failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t i = 0; i < sizeof(ahci_multi_write); ++i)
        if (ahci_multi_read[i] != ahci_multi_write[i]) {
            serial_write("AHCI multi-sector verification failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
    serial_write("AHCI multi-sector I/O ready\r\n");
    storage_initialize();
    uint64_t ahci_sector_count = (uint64_t)ahci_identify_words[100] |
        ((uint64_t)ahci_identify_words[101] << 16) |
        ((uint64_t)ahci_identify_words[102] << 32) |
        ((uint64_t)ahci_identify_words[103] << 48);
    storage_device_t ahci_storage = {
        "ahci0", 512, ahci_sector_count, ahci_read_sectors, ahci_write_sectors
    };
    if (!storage_register(&ahci_storage)) {
        serial_write("AHCI storage registration failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("AHCI storage backend ready\r\n");
    if (nvme_controller_count() != 0) {
        storage_device_t nvme_storage = {
            "nvme0", 512, nvme_namespace_sectors,
            nvme_read_sectors, nvme_write_sectors
        };
        if (!storage_register(&nvme_storage)) {
            serial_write("NVMe storage registration failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("NVMe storage backend ready\r\n");
    }
    if (!ata_initialize()) {
        serial_write("ATA storage initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    int ata_resources_valid = 0;
    for (uint32_t device_index = 0; device_index < device_count(); ++device_index) {
        const device_t *device = device_at(device_index);
        if (device && device->driver && device->class_code == 0x01 &&
            device->subclass == 0x01) {
            ata_resources_valid = 1;
            for (uint32_t resource = 0; resource < 2; ++resource)
                if (device->resources[resource].size != 0 &&
                    device_resource_owner(device, resource) != device->driver)
                    ata_resources_valid = 0;
        }
    }
    if (!ata_resources_valid) {
        serial_write("driver resource ownership failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("driver resource ownership ready\r\n");
    uint8_t boot_sector[512];
    if (!ata_read_boot_sector(boot_sector) || boot_sector[510] != 0x55 ||
        boot_sector[511] != 0xaa) {
        serial_write("ATA boot-sector read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("storage ready\r\n");
    uint8_t storage_scratch[512];
    uint8_t storage_verify[512];
    if (!storage_device_at(0) || storage_device_at(0)->block_count < 2 ||
        !storage_read(0, 1, 1, storage_scratch) ||
        !storage_write(0, 1, 1, storage_scratch) ||
        !storage_read(0, 1, 1, storage_verify)) {
        serial_write("storage write path failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t byte = 0; byte < sizeof(storage_scratch); ++byte) {
        if (storage_scratch[byte] != storage_verify[byte]) {
            serial_write("storage write verification failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
    }
    serial_write("storage read-write ready\r\n");
    if (!heap_initialize()) {
        serial_write("kernel heap initialization failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    void *heap_probe = kmalloc(128);
    if (!heap_probe) {
        serial_write("kernel heap allocation failed\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    kfree(heap_probe);
    kfree(heap_probe);
    uint8_t invalid_heap_pointer;
    kfree(&invalid_heap_pointer);
    serial_write("kernel heap ready\r\n");
    fat32_fs_t fat32;
    uint32_t kernel_cluster;
    uint32_t kernel_size;
    uint8_t kernel_file_probe[1024];
    static const char kernel_short_name[11] = {
        'K','E','R','N','E','L',' ',' ','E','L','F'
    };
    if (!fat32_mount(&fat32, 0) ||
        !fat32_lookup(&fat32, kernel_short_name, &kernel_cluster, &kernel_size) ||
        kernel_size < 1024 || !fat32_read_file(&fat32, kernel_short_name, 0,
                                                kernel_file_probe, 1024) ||
        kernel_file_probe[0] != 0x7f || kernel_file_probe[1] != 'E' ||
        kernel_file_probe[2] != 'L' || kernel_file_probe[3] != 'F') {
        serial_write("FAT32 filesystem failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("FAT32 filesystem ready\r\n");
    vfs_node_t *fat_vfs_root = vfs_node_create("fat", VFS_NODE_DIRECTORY,
                                               0, 0, 0555);
    uint8_t fat_vfs_probe[4];
    vfs_node_t *fat_vfs_file = 0;
    if (!fat_vfs_root) {
        serial_write("FAT32 VFS root failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!fat32_vfs_attach_file(&fat32, fat_vfs_root, kernel_short_name,
                               "kernel.elf")) {
        serial_write("FAT32 VFS attach failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    fat_vfs_file = vfs_lookup_path(fat_vfs_root, "/kernel.elf");
    if (!fat_vfs_file) {
        serial_write("FAT32 VFS lookup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (vfs_node_read(fat_vfs_file, 0, fat_vfs_probe, sizeof(fat_vfs_probe)) != 4 ||
        fat_vfs_probe[0] != 0x7f || fat_vfs_probe[1] != 'E' ||
        fat_vfs_probe[2] != 'L' || fat_vfs_probe[3] != 'F') {
        serial_write("FAT32 VFS read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(fat_vfs_file);
    serial_write("FAT32 VFS adapter ready\r\n");
    spinlock_t boot_lock;
    spinlock_init(&boot_lock);
    spinlock_lock(&boot_lock);
    spinlock_unlock(&boot_lock);
    uint64_t boot_flags = spinlock_lock_irqsave(&boot_lock);
    spinlock_unlock_irqrestore(&boot_lock, boot_flags);
    serial_write("synchronization primitives ready\r\n");
    ipc_channel_t boot_channel;
    ipc_channel_initialize(&boot_channel);
    static const char ipc_one[] = "one";
    static const char ipc_two[] = "two";
    char ipc_buffer[sizeof(ipc_one)];
    uint64_t ipc_sender;
    uint32_t ipc_size;
    if (!ipc_channel_send(&boot_channel, 11, ipc_one, sizeof(ipc_one)) ||
        !ipc_channel_send(&boot_channel, 22, ipc_two, sizeof(ipc_two)) ||
        ipc_channel_count(&boot_channel) != 2 ||
        !ipc_channel_receive(&boot_channel, 99, ipc_buffer, sizeof(ipc_buffer),
                             &ipc_sender, &ipc_size) ||
        ipc_sender != 11 || ipc_size != sizeof(ipc_one) ||
        ipc_buffer[0] != 'o' || ipc_buffer[1] != 'n' || ipc_buffer[2] != 'e' ||
        !ipc_channel_receive(&boot_channel, 99, ipc_buffer, sizeof(ipc_buffer),
                             &ipc_sender, &ipc_size) ||
        ipc_sender != 22 || ipc_size != sizeof(ipc_two) ||
        ipc_buffer[0] != 't' || ipc_buffer[1] != 'w' || ipc_buffer[2] != 'o' ||
        ipc_channel_count(&boot_channel) != 0 ||
        !ipc_channel_close(&boot_channel) ||
        ipc_channel_send(&boot_channel, 33, ipc_one, sizeof(ipc_one)) ||
        ipc_channel_receive(&boot_channel, 99, ipc_buffer, 2,
                            &ipc_sender, &ipc_size) ||
        ipc_channel_close(&boot_channel)) {
        serial_write("IPC channel failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ipc channels ready\r\n");
    security_context_t root_security;
    security_context_t user_security;
    security_context_initialize(&root_security, 0, 0,
                                SECURITY_CAP_SYS_ADMIN | SECURITY_CAP_SYS_RAWIO);
    security_context_initialize(&user_security, 1000, 1000, 0);
    if (!security_has_capability(&root_security, SECURITY_CAP_SYS_ADMIN) ||
        security_has_capability(&user_security, SECURITY_CAP_SYS_ADMIN) ||
        !security_can_access(&root_security, 1000, 1000, 0000, 7) ||
        !security_can_access(&user_security, 1000, 1000, 0600, 6) ||
        security_can_access(&user_security, 2000, 2000, 0600, 4) ||
        security_can_access(&user_security, 1000, 1000, 0600, 1)) {
        serial_write("security policy failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("security policy ready\r\n");
    vfs_node_t *vfs_root = vfs_node_create("root", VFS_NODE_DIRECTORY,
                                           0, 0, 0755);
    vfs_node_t *vfs_dev = vfs_node_create("dev", VFS_NODE_DIRECTORY,
                                          0, 0, 0755);
    vfs_node_t *vfs_console = vfs_node_create("console", VFS_NODE_DEVICE,
                                              0, 0, 0600);
    vfs_node_t *vfs_etc = vfs_node_create("etc", VFS_NODE_DIRECTORY,
                                          0, 0, 0755);
    vfs_node_t *vfs_outer = vfs_node_create("outer", VFS_NODE_DIRECTORY,
                                            0, 0, 0755);
    if (!vfs_root || !vfs_dev || !vfs_console || !vfs_etc ||
        !vfs_outer ||
        !vfs_node_add_child(vfs_root, vfs_dev) ||
        !vfs_node_add_child(vfs_root, vfs_etc) ||
        !vfs_node_add_child(vfs_dev, vfs_console) ||
        !vfs_node_add_child(vfs_outer, vfs_root) ||
        vfs_node_add_child(vfs_root, vfs_dev) ||
        vfs_node_add_child(vfs_etc, vfs_dev) ||
        vfs_node_add_child(vfs_root, vfs_outer)) {
        serial_write("VFS hierarchy failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_dev);
    vfs_node_release(vfs_console);
    vfs_node_release(vfs_etc);
    vfs_node_release(vfs_outer);
    vfs_node_t *vfs_confined = vfs_lookup_path(vfs_root, "/..");
    if (!vfs_confined || vfs_confined != vfs_root) {
        if (vfs_confined) vfs_node_release(vfs_confined);
        serial_write("VFS root confinement failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_confined);
    vfs_mount_table_t vfs_mounts;
    vfs_node_t *vfs_mounted_root = vfs_node_create("mounted", VFS_NODE_DIRECTORY,
                                                   0, 0, 0755);
    vfs_node_t *vfs_mounted_file = vfs_node_create("hello", VFS_NODE_REGULAR,
                                                   0, 0, 0644);
    vfs_mount_table_initialize(&vfs_mounts);
    if (!vfs_mounted_root || !vfs_mounted_file ||
        !vfs_node_add_child(vfs_mounted_root, vfs_mounted_file) ||
        !vfs_mount(&vfs_mounts, vfs_dev, vfs_mounted_root)) {
        serial_write("VFS mount failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_mounted_file);
    vfs_node_t *vfs_mounted_found =
        vfs_lookup_path_mounted(&vfs_mounts, vfs_root, "/dev/hello");
    vfs_node_t *vfs_mountpoint_root =
        vfs_lookup_path_mounted(&vfs_mounts, vfs_root, "/dev");
    if (!vfs_mounted_found || vfs_mounted_found->type != VFS_NODE_REGULAR ||
        !vfs_mountpoint_root || vfs_mountpoint_root->type != VFS_NODE_DIRECTORY ||
        vfs_mountpoint_root->name[0] != 'm') {
        if (vfs_mountpoint_root) vfs_node_release(vfs_mountpoint_root);
        serial_write("VFS mount traversal failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_mountpoint_root);
    if (!vfs_unmount(&vfs_mounts, vfs_dev)) {
        serial_write("VFS mounted path failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_mounted_found);
    vfs_node_t *vfs_mounted_child = vfs_node_lookup(vfs_mounted_root, "hello");
    if (!vfs_mounted_child || !vfs_node_remove(vfs_mounted_root, vfs_mounted_child)) {
        serial_write("VFS mount cleanup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_mounted_child);
    vfs_node_release(vfs_mounted_root);
    uint32_t devfs_count = devfs_populate(vfs_dev);
    vfs_node_t *devfs_probe = vfs_lookup_path(vfs_root, "/dev/pci0");
    if (devfs_count == 0 || !devfs_probe || devfs_probe->type != VFS_NODE_DEVICE) {
        serial_write("devfs population failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(devfs_probe);
    serial_write("devfs ready\r\n");
    vfs_node_t *vfs_found = vfs_lookup_path(vfs_root, "/dev/console");
    vfs_node_t *vfs_parent = vfs_lookup_path(vfs_root, "/dev/../etc/.");
    if (!vfs_found || !vfs_parent || vfs_found->type != VFS_NODE_DEVICE ||
        vfs_parent->type != VFS_NODE_DIRECTORY ||
        !vfs_node_remove(vfs_found->parent, vfs_found)) {
        serial_write("VFS lookup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_found);
    vfs_node_release(vfs_parent);
    vfs_node_t *vfs_dev_handle = vfs_node_lookup(vfs_root, "dev");
    vfs_node_t *vfs_etc_handle = vfs_node_lookup(vfs_root, "etc");
    if (!vfs_dev_handle || !vfs_etc_handle ||
        !vfs_node_remove(vfs_root, vfs_etc_handle)) {
        serial_write("VFS cleanup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_dev_handle);
    vfs_node_release(vfs_etc_handle);
    vfs_node_release(vfs_root);
    serial_write("VFS core ready\r\n");
    vfs_node_t *proc_root = procfs_create(42);
    vfs_node_t *proc_pid = proc_root ?
        vfs_lookup_path(proc_root, "/self/pid") : 0;
    char proc_pid_text[4] = {0};
    if (!proc_root || !proc_pid || vfs_node_read(proc_pid, 0, proc_pid_text, 3) != 3 ||
        proc_pid_text[0] != '4' || proc_pid_text[1] != '2' || proc_pid_text[2] != '\n') {
        serial_write("procfs read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_t *proc_self = vfs_node_lookup(proc_root, "self");
    if (!proc_self || !vfs_node_remove(proc_self, proc_pid) ||
        !vfs_node_remove(proc_root, proc_self)) {
        serial_write("procfs cleanup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(proc_pid);
    vfs_node_release(proc_self);
    vfs_node_release(proc_root);
    serial_write("procfs ready\r\n");
    uint8_t block_probe_storage[32] = {0};
    uint8_t block_probe_data[16];
    block_device_t block_probe = {0};
    block_registry_t block_registry;
    block_registry_initialize(&block_registry);
    block_probe.name[0] = 'r'; block_probe.name[1] = 'a';
    block_probe.name[2] = 'm'; block_probe.name[3] = '0';
    block_probe.sector_count = 2;
    block_probe.sector_size = 16;
    block_probe.context = block_probe_storage;
    block_probe.read = block_probe_read;
    block_probe.write = block_probe_write;
    for (uint32_t i = 0; i < sizeof(block_probe_data); ++i)
        block_probe_data[i] = (uint8_t)(i + 1);
    if (!block_registry_register(&block_registry, &block_probe) ||
        !block_registry_at(&block_registry, 0) ||
        !block_registry_write(&block_registry, 0, 1, 1, block_probe_data) ||
        !block_registry_read(&block_registry, 0, 1, 1, block_probe_data) ||
        block_probe_storage[16] != 1 || block_probe_storage[31] != 16 ||
        block_registry_read(&block_registry, 0, 2, 1, block_probe_data)) {
        serial_write("block interface failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("block interface ready\r\n");
    block_cache_t block_cache;
    uint8_t cache_data[16];
    block_cache_initialize(&block_cache);
    for (uint32_t i = 0; i < sizeof(cache_data); ++i)
        cache_data[i] = (uint8_t)(0xa0 + i);
    if (!block_cache_write(&block_cache, &block_registry, 0, 0,
                           cache_data, sizeof(cache_data)) ||
        block_probe_storage[0] != 0xa0 || block_probe_storage[15] != 0xaf ||
        !block_cache_read(&block_cache, &block_registry, 0, 0,
                          block_probe_data, sizeof(block_probe_data)) ||
        block_probe_data[0] != 0xa0 || block_probe_data[15] != 0xaf) {
        serial_write("block cache failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("block cache ready\r\n");
    uint8_t slab_storage[64] __attribute__((aligned(16)));
    uint8_t slab_used[2];
    slab_cache_t slab;
    slab_cache_initialize(&slab, slab_storage, 32, 2, slab_used);
    void *slab_first = slab_cache_allocate(&slab);
    void *slab_second = slab_cache_allocate(&slab);
    if (!slab_first || !slab_second || slab_cache_allocate(&slab) ||
        slab_cache_available(&slab) != 0 ||
        slab_cache_free(&slab, (void *)(slab_storage + 1)) ||
        !slab_cache_free(&slab, slab_first) ||
        slab_cache_free(&slab, slab_first) || slab_cache_available(&slab) != 1) {
        serial_write("slab cache failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!slab_cache_free(&slab, slab_second)) {
        serial_write("slab cache release failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("slab cache ready\r\n");
    input_queue_t input_queue;
    input_event_t input_event = {
        .type = INPUT_EVENT_KEY, .code = 30, .value = 1, .timestamp = 7
    };
    input_event_t input_out;
    input_queue_initialize(&input_queue);
    if (!input_queue_push(&input_queue, &input_event)) {
        serial_write("input queue setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t event_index = 1; event_index < INPUT_EVENT_CAPACITY; ++event_index)
        if (!input_queue_push(&input_queue, &input_event)) {
            serial_write("input queue fill failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
    if (input_queue_push(&input_queue, &input_event) ||
        input_queue_count(&input_queue) != INPUT_EVENT_CAPACITY ||
        !input_queue_pop(&input_queue, &input_out) || input_out.code != 30 ||
        input_out.timestamp != 7 || input_queue_count(&input_queue) != 63) {
        serial_write("input queue failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("input event queue ready\r\n");
    while (input_queue_pop(&input_queue, &input_out)) { }
    uint32_t framebuffer_storage[50];
    framebuffer_t framebuffer;
    if (!framebuffer_initialize(&framebuffer, framebuffer_storage, 8, 5, 10)) {
        serial_write("framebuffer setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    framebuffer_clear(&framebuffer, 0x11223344);
    if (!framebuffer_put_pixel(&framebuffer, 7, 4, 0xaabbccdd) ||
        framebuffer_put_pixel(&framebuffer, 8, 4, 0) ||
        !framebuffer_fill_rect(&framebuffer, 1, 1, 3, 2, 0x55667788) ||
        framebuffer_fill_rect(&framebuffer, 7, 4, 2, 1, 0)) {
        serial_write("framebuffer operation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (framebuffer_storage[0] != 0x11223344 ||
        framebuffer_storage[1 + 10] != 0x55667788 ||
        framebuffer_storage[4 * 10 + 7] != 0xaabbccdd) {
        serial_write("framebuffer pixel failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("framebuffer surface ready\r\n");
    framebuffer_t firmware_framebuffer;
    if (!info->framebuffer_base || !framebuffer_initialize(&firmware_framebuffer,
            (void *)(uintptr_t)info->framebuffer_base, info->framebuffer_width,
            info->framebuffer_height, info->framebuffer_pitch) ||
        !framebuffer_put_pixel(&firmware_framebuffer, 0, 0, 0)) {
        serial_write("firmware framebuffer failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("firmware framebuffer ready\r\n");
    static const uint8_t usb_device_descriptor[18] = {
        18, 1, 0, 2, 0, 0, 0, 64, 0x34, 0x12, 0x78, 0x56, 0, 1, 1, 2, 3, 1
    };
    static const uint8_t usb_endpoint_descriptor[7] = {7, 5, 0x81, 2, 64, 0, 1};
    usb_device_t usb_device;
    if (!usb_device_parse_descriptor(&usb_device, usb_device_descriptor,
                                     sizeof(usb_device_descriptor)) ||
        usb_device.vendor_id != 0x1234 ||
        !usb_device_add_endpoint(&usb_device, usb_endpoint_descriptor,
                                  sizeof(usb_endpoint_descriptor)) ||
        usb_device.endpoint_count != 1 ||
        usb_device.endpoints[0].max_packet_size != 64) {
        serial_write("USB descriptor failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("USB descriptor layer ready\r\n");
    static const uint8_t hid_probe_report[8] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
    input_event_t hid_probe_event;
    if (!usb_hid_keyboard_decode(hid_probe_report, sizeof(hid_probe_report),
                                 &hid_probe_event) ||
        hid_probe_event.type != INPUT_EVENT_KEY || hid_probe_event.code != 0x04 ||
        hid_probe_event.value != 0x02) {
        serial_write("USB HID keyboard failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("USB HID keyboard ready\r\n");
    if (uhci_root_port_count() != 0 && uhci_interrupt_endpoint != 0) {
        uint8_t uhci_report[64] = {0};
        uint8_t uhci_toggle = 0;
        int uhci_report_ready = uhci_interrupt_transfer(
            1, uhci_interrupt_endpoint, uhci_report, uhci_interrupt_packet,
            uhci_interrupt_packet, &uhci_toggle);
        input_event_t uhci_event;
        if (uhci_report_ready &&
            usb_hid_keyboard_decode(uhci_report, uhci_interrupt_packet,
                                    &uhci_event) &&
            !input_queue_push(&input_queue, &uhci_event)) {
            serial_write("UHCI HID input delivery failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write(uhci_report_ready ? "UHCI HID input delivery ready\r\n" :
                     "UHCI HID polling ready\r\n");
    }
    if (!ps2_keyboard_initialize(&input_queue)) {
        serial_write("PS2 keyboard initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("PS2 keyboard ready\r\n");
    static const uint8_t ps2_mouse_probe_packet[3] = {0x09, 0x05, 0xfb};
    input_event_t ps2_mouse_probe_events[3];
    uint32_t ps2_mouse_probe_count = 0;
    if (!ps2_mouse_decode(ps2_mouse_probe_packet, ps2_mouse_probe_events,
                          &ps2_mouse_probe_count) ||
        ps2_mouse_probe_count != 3 ||
        ps2_mouse_probe_events[0].type != INPUT_EVENT_BUTTON ||
        ps2_mouse_probe_events[0].value != 1 ||
        ps2_mouse_probe_events[1].type != INPUT_EVENT_AXIS ||
        ps2_mouse_probe_events[1].value != 5 ||
        ps2_mouse_probe_events[2].code != 1 ||
        ps2_mouse_probe_events[2].value != -5) {
        serial_write("PS2 mouse packet failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("PS2 mouse packet ready\r\n");
    if (ps2_mouse_initialize(&input_queue))
        serial_write("PS2 mouse controller ready\r\n");
    else
        serial_write("PS2 mouse unavailable\r\n");
    kernel_assert(kernel_debug_range_valid(0x100, 0x200, 0x1000),
                  "debug range assertion failure");
    kernel_assert(!kernel_debug_range_valid(0x1000, 1, 0x1000),
                  "debug overflow assertion failure");
    serial_write("kernel debug ready\r\n");
    scheduler_initialize();
    if (!process_initialize()) {
        serial_write("process initialization failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *thread_process = process_create(2);
    process_thread_t *thread_probe = thread_process ?
        process_thread_create(thread_process, 200, process_thread_probe, 0, 4096) : 0;
    if (!thread_probe || process_thread_lookup(thread_process, 200) != thread_probe ||
        process_thread_create(thread_process, 200, process_thread_probe, 0, 4096) ||
        process_thread_create(thread_process, 0, process_thread_probe, 0, 4096) ||
        thread_process->thread_count != 1 ||
        !process_thread_start(thread_probe) || scheduler_ready_count() != 1 ||
        !process_thread_destroy(thread_probe) || thread_process->thread_count != 0 ||
        !process_destroy(thread_process)) {
        serial_write("process thread lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *owned_thread_process = process_create(3);
    process_thread_t *owned_thread = owned_thread_process ?
        process_thread_create(owned_thread_process, 201, process_thread_probe, 0, 4096) : 0;
    if (!owned_thread || owned_thread_process->thread_count != 1 ||
        !process_destroy(owned_thread_process)) {
        serial_write("process-owned thread teardown failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("process thread lifecycle ready\r\n");
    task_wait_queue_initialize(&task_demo_waiters);
    if (!task_wait_queue_enqueue(&task_demo_waiters, &task_demo_waiter_a) ||
        !task_wait_queue_enqueue(&task_demo_waiters, &task_demo_waiter_b) ||
        task_wait_queue_count(&task_demo_waiters) != 2 ||
        task_wait_queue_dequeue(&task_demo_waiters) != &task_demo_waiter_a ||
        task_wait_queue_dequeue(&task_demo_waiters) != &task_demo_waiter_b ||
        task_wait_queue_count(&task_demo_waiters) != 0) {
        serial_write("task wait queue failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("task wait queues ready\r\n");
    task_initialize(&scheduler_demo_task_a, 1);
    task_initialize(&scheduler_demo_task_b, 2);
    if (!scheduler_enqueue(&scheduler_demo_task_a) ||
        !scheduler_enqueue(&scheduler_demo_task_b) ||
        scheduler_ready_count() != 2 ||
        scheduler_next() != &scheduler_demo_task_a ||
        scheduler_next() != &scheduler_demo_task_b ||
        scheduler_ready_count() != 0) {
        serial_write("scheduler core failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    scheduler_set_current(&scheduler_demo_task_a);
    if (scheduler_current() != &scheduler_demo_task_a ||
        scheduler_demo_task_a.state != TASK_RUNNING) {
        serial_write("scheduler dispatch failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!scheduler_block(&scheduler_demo_task_a, &task_demo_waiters) ||
        scheduler_current() != 0 || scheduler_demo_task_a.state != TASK_BLOCKED ||
        scheduler_wake_one(&task_demo_waiters) != &scheduler_demo_task_a ||
        scheduler_demo_task_a.state != TASK_READY || scheduler_ready_count() != 1) {
        serial_write("scheduler block wake failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (scheduler_next() != &scheduler_demo_task_a) {
        serial_write("scheduler ready dispatch failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    task_initialize(&scheduler_demo_idle, 0);
    scheduler_set_idle(&scheduler_demo_idle);
    if (scheduler_next() != &scheduler_demo_idle ||
        scheduler_demo_idle.state != TASK_RUNNING) {
        serial_write("scheduler idle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    task_t *kernel_thread = task_create_kernel(3, task_object_probe, 0, 4096);
    kernel_thread_active = kernel_thread;
    if (!kernel_thread || kernel_thread->state != TASK_READY ||
        kernel_thread->context.rip == 0) {
        serial_write("kernel task creation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    task_context_switch(&task_demo_main, &kernel_thread->context);
    kernel_thread->state = TASK_TERMINATED;
    if (!kernel_thread_ran || !task_destroy_kernel(kernel_thread)) {
        serial_write("kernel task execution failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("scheduler core ready\r\n");
    serial_write("scheduler policy ready\r\n");
    task_context_initialize(&task_demo_worker, &task_demo_stack[sizeof(task_demo_stack)],
                            task_demo_entry, 0);
    task_context_switch(&task_demo_main, &task_demo_worker);
    serial_write("task context returned\r\n");
    scheduler_set_idle(0);
    ipc_channel_initialize(&ipc_block_probe_channel);
    ipc_block_probe_result = 0;
    task_t *ipc_receiver = task_create_kernel(300, ipc_block_receiver, 0, 4096);
    task_t *ipc_sender_task = task_create_kernel(301, ipc_block_sender, 0, 4096);
    if (!ipc_receiver || !ipc_sender_task ||
        !scheduler_enqueue(ipc_receiver) || !scheduler_enqueue(ipc_sender_task) ||
        !scheduler_start() || ipc_block_probe_result != 3 ||
        !task_destroy_kernel(ipc_receiver) || !task_destroy_kernel(ipc_sender_task)) {
        serial_write("IPC blocking failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!ipc_channel_close(&ipc_block_probe_channel)) {
        serial_write("IPC blocking close failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ipc blocking ready\r\n");
    interrupts_initialize();
    if (uhci_controller_count() != 0 && uhci_interrupt_enabled() != 0) {
        static const uint8_t uhci_irq_probe_setup[8] =
            {0x80, 0x06, 0, 1, 0, 0, 8, 0};
        static uint8_t uhci_irq_probe_descriptor[8];
        uint32_t before = uhci_interrupt_count();
        if (!uhci_control_transfer(1, 0, uhci_irq_probe_setup,
                                   uhci_irq_probe_descriptor,
                                   sizeof(uhci_irq_probe_descriptor)) ||
            uhci_interrupt_count() == before) {
            serial_write("UHCI interrupt delivery failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("UHCI interrupt delivery ready\r\n");
    }
    if (nvme_controller_count() != 0 && nvme_interrupt_enabled() != 0) {
        static uint8_t nvme_interrupt_probe[512];
        if (!nvme_read_sector(0, nvme_interrupt_probe)) {
            serial_write("NVMe interrupt probe read failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        if (nvme_interrupt_count() == 0) {
            serial_write("NVMe interrupt delivery failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("NVMe interrupt delivery ready\r\n");
    }
    if (ahci_controller_count() != 0 && ahci_interrupt_enabled() != 0) {
        static uint8_t ahci_interrupt_probe[512];
        if (!ahci_read_sector(0, ahci_interrupt_probe)) {
            serial_write("AHCI interrupt probe read failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        if (ahci_interrupt_count() == 0) {
            serial_write("AHCI interrupt delivery failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("AHCI interrupt delivery ready\r\n");
    }
    if (e1000_controller_count() != 0) {
        static const uint8_t e1000_interrupt_packet[60] = {0};
        if (!e1000_transmit(e1000_interrupt_packet,
                            sizeof(e1000_interrupt_packet))) {
            serial_write("e1000 interrupt probe transmit failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        timer_wait(10);
        if (e1000_interrupt_count() == 0) {
            serial_write("e1000 interrupt delivery failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        if (e1000_tx_error_count() != 0) {
            serial_write("e1000 completion error failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("e1000 interrupt delivery ready\r\n");
    }
    timer_wait(10);
    serial_write("timer ticks="); serial_write_hex(timer_ticks()); serial_write("\r\n");
    serial_write("time ns="); serial_write_hex(timer_now_ns()); serial_write(" hz=");
    serial_write_hex(timer_frequency_hz()); serial_write("\r\n");
    if (clock_monotonic_ns() == 0 || clock_frequency_hz() == 0) {
        serial_write("generic clock failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("generic clock ready\r\n");
    uint64_t preempt_deadline = timer_ticks() + 20;
    preempt_task_a_ticks = 0;
    preempt_task_b_ticks = 0;
    scheduler_set_idle(0);
    task_t *preempt_a = task_create_kernel(100, preempt_task_a,
                                            (void *)&preempt_deadline, 4096);
    task_t *preempt_b = task_create_kernel(101, preempt_task_b,
                                            (void *)&preempt_deadline, 4096);
    if (!preempt_a || !preempt_b || !scheduler_enqueue(preempt_a) ||
        !scheduler_enqueue(preempt_b)) {
        serial_write("scheduler preemption setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    scheduler_enable_preemption(1);
    if (!scheduler_start() || preempt_task_a_ticks == 0 ||
        preempt_task_b_ticks == 0 || scheduler_preemption_count() == 0) {
        serial_write("scheduler preemption failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    scheduler_enable_preemption(0);
    if (!task_destroy_kernel(preempt_a) || !task_destroy_kernel(preempt_b)) {
        serial_write("scheduler task reap failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("scheduler preemption ready\r\n");
    serial_write("os kernel entry ok\r\n");
    process_t *runtime_process = process_create(1);
    build_user_image_probe();
    if (!runtime_process ||
        !process_load_image(runtime_process, user_image_probe,
                            sizeof(user_image_probe)) ||
        !process_map_user_stack(runtime_process, 0x8000002000ULL)) {
        serial_write("user process setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("process lifecycle ready\r\n");
    process_t *signal_process = process_create(2);
    static uint8_t handle_probe_object;
    static uint8_t handle_replacement_object;
    int handle_probe = signal_process ? process_handle_open(&signal_process->handles,
        &handle_probe_object, PROCESS_HANDLE_READ | PROCESS_HANDLE_WRITE) : 0;
    int replacement_handle = 0;
    uint32_t signal = 0;
    if (!signal_process || process_lookup(2) != signal_process || !handle_probe ||
        process_handle_get(&signal_process->handles, (uint32_t)handle_probe,
                           PROCESS_HANDLE_READ) != &handle_probe_object ||
        process_handle_get(&signal_process->handles, (uint32_t)handle_probe,
                           PROCESS_HANDLE_EXEC) != 0 ||
        !process_handle_close(&signal_process->handles, (uint32_t)handle_probe) ||
        process_handle_get(&signal_process->handles, (uint32_t)handle_probe, 0) != 0 ||
        !(replacement_handle = process_handle_open(&signal_process->handles,
            &handle_replacement_object, PROCESS_HANDLE_READ)) ||
        process_handle_get(&signal_process->handles, (uint32_t)handle_probe, 0) != 0 ||
        process_handle_get(&signal_process->handles, (uint32_t)replacement_handle,
                           PROCESS_HANDLE_READ) != &handle_replacement_object ||
        !process_handle_close(&signal_process->handles, (uint32_t)replacement_handle) ||
        !process_send_signal(signal_process, 2) ||
        !process_set_signal_mask(signal_process, 1U << 4) ||
        !process_take_signal(signal_process, &signal) || signal != 2 ||
        process_take_signal(signal_process, &signal) ||
        !process_terminate(signal_process, 42) ||
        signal_process->exit_status != 42 || !process_destroy(signal_process)) {
        serial_write("process signal lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (process_lookup(2) != 0) {
        serial_write("process registry reap failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *terminate_process = process_create(3);
    process_thread_t *terminate_thread = terminate_process ?
        process_thread_create(terminate_process, 202, process_thread_probe, 0, 4096) : 0;
    if (!terminate_thread || !process_terminate(terminate_process, 7) ||
        terminate_process->thread_count != 0 || terminate_process->state != PROCESS_EXITED ||
        terminate_process->exit_status != 7 || !process_destroy(terminate_process)) {
        serial_write("process termination teardown failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    signal_wait_probe_process = process_create(4);
    signal_wait_probe_result = 0;
    scheduler_set_idle(0);
    task_t *signal_waiter = task_create_kernel(400, signal_wait_receiver, 0, 4096);
    task_t *signal_sender = task_create_kernel(401, signal_wait_sender, 0, 4096);
    if (!signal_wait_probe_process || !signal_waiter || !signal_sender ||
        !scheduler_enqueue(signal_waiter) || !scheduler_enqueue(signal_sender) ||
        !scheduler_start() || signal_wait_probe_result != 3 ||
        !process_destroy(signal_wait_probe_process) ||
        !task_destroy_kernel(signal_waiter) || !task_destroy_kernel(signal_sender)) {
        serial_write("signal blocking failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("signal blocking ready\r\n");
    serial_write("process signals ready\r\n");
    serial_write("process handles ready\r\n");
    syscall_initialize();
    if (!process_activate(runtime_process)) {
        serial_write("user address space activation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    static const char syscall_marker[] = "ok";
    char syscall_copy[sizeof(syscall_marker)] = {0};
    if (!syscall_copy_to_user(0x8000002000ULL, syscall_marker,
                              sizeof(syscall_marker)) ||
        !syscall_copy_from_user(syscall_copy, 0x8000002000ULL,
                                sizeof(syscall_marker)) ||
        !syscall_copy_from_user(0, 0, 0) || !syscall_copy_to_user(0, 0, 0) ||
        syscall_copy_from_user(syscall_copy, 0x8000002fffULL, 2) ||
        syscall_copy_to_user(0x8000002fffULL, syscall_marker, 2) ||
        syscall_copy[0] != 'o' || syscall_copy[1] != 'k' ||
        syscall_copy[2] != '\0' || process_lookup(1) != runtime_process ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND_TO, 1, 3, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND, 0x100000001ULL, 0, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_MASK, 0x100000000ULL, 0, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND_TO, 1, 0x100000001ULL, 0) !=
            OS_SYSCALL_ERROR ||
        !process_take_signal(runtime_process, &signal) || signal != 3 ||
        syscall_dispatch(OS_SYSCALL_GETPID, 0, 0, 0) != 1 ||
        syscall_dispatch(99, 0, 0, 0) != OS_SYSCALL_ERROR) {
        serial_write("syscall ABI failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("syscall ABI ready\r\n");
    uint32_t signal_result = 0;
    if (syscall_dispatch(OS_SYSCALL_SIGNAL_SEND, 3, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_NEXT, 0x8000002fffULL, 0, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_NEXT, 0x8000002000ULL, 0, 0) != 3 ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_MASK, 1U << 4, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND, 2, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_NEXT, 0x8000002000ULL, 0, 0) != 2 ||
        !syscall_copy_from_user(&signal_result, 0x8000002000ULL, sizeof(signal_result)) ||
        signal_result != 2) {
        serial_write("signal syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("signal syscalls ready\r\n");
    if (!kernel_init_state_advance(&init_state, KERNEL_INIT_SERVICES))
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    serial_write("user mode deferred until kernel completion\r\n");
    for (;;) {
        __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
}
