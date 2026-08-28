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
#include "../../../ipc/endpoint.h"
#include "../../../ipc/pipe.h"
#include "../../../security/credentials.h"
#include "../../../fs/vfs/vfs.h"
#include "../../../fs/vfs/file.h"
#include "../../../fs/vfs/mount.h"
#include "../../../fs/vfs/probe.h"
#include "../../../fs/block/block.h"
#include "../../../fs/block/storage_block.h"
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
#include "../../../drivers/network/tcp.h"
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
#include "../../../drivers/time/rtc.h"
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
static volatile uint32_t owned_handle_release_count;
static volatile uint32_t inherited_handle_retain_count;

static void owned_handle_release_probe(void *object) {
    if (object) ++owned_handle_release_count;
}
static void inherited_handle_retain_probe(void *object) {
    if (object) ++inherited_handle_retain_count;
}
typedef struct {
    network_packet_queue_t *queue;
    arp_cache_t *arp_cache;
    ipv4_reassembly_table_t *reassembly;
    udp_endpoint_table_t *udp;
    tcp_endpoint_table_t *tcp;
    uint8_t *local_hardware;
    const uint8_t *local_protocol;
    uint8_t *reassembly_output;
} network_runtime_context_t;
static network_runtime_context_t network_runtime;
static input_queue_t *input_runtime_queue;
static uint8_t input_runtime_endpoint;
static uint16_t input_runtime_packet;
static uint8_t input_runtime_interval;
static uint8_t input_runtime_toggle;
static uint8_t input_runtime_report[64];
static usb_hid_keyboard_state_t input_runtime_hid_state;
static int input_runtime_mouse;
static int input_runtime_ready;
static int input_runtime_pending;

static void network_runtime_task(void *argument) {
    network_runtime_context_t *runtime = (network_runtime_context_t *)argument;
    for (;;) {
        (void)network_service(runtime->queue, runtime->local_hardware,
                              runtime->local_protocol, runtime->arp_cache,
                              runtime->udp, runtime->tcp, timer_ticks(), 8,
                              runtime->reassembly, runtime->reassembly_output,
                              IPV4_REASSEMBLY_MAX_PAYLOAD);
        scheduler_yield();
    }
}

static void input_runtime_task(void *argument) {
    (void)argument;
    for (;;) {
        char serial_input[16];
        uint32_t serial_count = serial_poll_input(serial_input,
                                                  sizeof(serial_input));
        if (serial_count != 0)
            (void)input_queue_push_text(input_runtime_queue, serial_input,
                                        serial_count, timer_ticks());
        if (input_runtime_ready && input_runtime_pending) {
            int completed = uhci_interrupt_poll();
            if (completed > 0) {
            input_event_t events[20];
            uint32_t event_count = 0;
            int decoded = input_runtime_mouse ?
                usb_hid_mouse_decode(input_runtime_report,
                                     input_runtime_packet, events,
                                     &event_count) :
                usb_hid_keyboard_decode_state(input_runtime_report,
                                              input_runtime_packet,
                                              &input_runtime_hid_state,
                                              events, &event_count);
            if (decoded)
                (void)input_queue_push_batch(input_runtime_queue, events,
                                             event_count);
            input_runtime_pending = 0;
            } else if (completed < 0) {
                input_runtime_pending = 0;
            }
        }
        if (input_runtime_ready && !input_runtime_pending)
            input_runtime_pending = uhci_interrupt_submit(
                1, input_runtime_endpoint, input_runtime_report,
                input_runtime_packet, input_runtime_packet,
                input_runtime_interval,
                &input_runtime_toggle);
        scheduler_yield();
    }
}
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
static int block_probe_flush(void *context) {
    return context != 0;
}
static int vfs_probe_write(vfs_node_t *node, uint64_t offset,
                           const void *buffer, uint32_t size) {
    uint8_t *storage = (uint8_t *)node->private_data;
    if (!storage || offset > 16 || size > 16 - offset) return 0;
    for (uint32_t i = 0; i < size; ++i) storage[offset + i] = ((const uint8_t *)buffer)[i];
    return (int)size;
}
static int vfs_probe_read(vfs_node_t *node, uint64_t offset,
                          void *buffer, uint32_t size) {
    uint8_t *storage = node ? (uint8_t *)node->private_data : 0;
    if (!storage || !buffer || offset > 16 || size > 16 - offset) return 0;
    for (uint32_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = storage[offset + i];
    return (int)size;
}
static int vfs_probe_truncate(vfs_node_t *node, uint32_t size) {
    uint8_t *storage = node ? (uint8_t *)node->private_data : 0;
    if (!storage || size > 16) return 0;
    for (uint32_t i = size; i < 16; ++i) storage[i] = 0;
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
    probe64(&user_image_probe[88], 0x8000001000ULL); probe64(&user_image_probe[96], 34);
    probe64(&user_image_probe[104], 4096); probe64(&user_image_probe[112], 1);
    user_image_probe[120] = 0xb8; user_image_probe[121] = 1; user_image_probe[122] = 0;
    user_image_probe[123] = 0; user_image_probe[124] = 0;
    user_image_probe[125] = 0xbf; user_image_probe[126] = 1; user_image_probe[127] = 0;
    user_image_probe[128] = 0; user_image_probe[129] = 0;
    user_image_probe[130] = 0x48; user_image_probe[131] = 0x8d; user_image_probe[132] = 0x35;
    user_image_probe[133] = 15; user_image_probe[134] = 0; user_image_probe[135] = 0;
    user_image_probe[136] = 0;
    user_image_probe[137] = 0xba; user_image_probe[138] = 2; user_image_probe[139] = 0;
    user_image_probe[140] = 0; user_image_probe[141] = 0; user_image_probe[142] = 0xcd;
    user_image_probe[143] = 0x80; user_image_probe[144] = 0xb8; user_image_probe[145] = 15;
    user_image_probe[146] = 0; user_image_probe[147] = 0; user_image_probe[148] = 0;
    user_image_probe[149] = 0xcd; user_image_probe[150] = 0x80; user_image_probe[151] = 0xf4;
    user_image_probe[152] = 'o'; user_image_probe[153] = 'k';
    probe32(&user_image_probe[176], 1); probe32(&user_image_probe[180], 4);
    probe64(&user_image_probe[184], 232); probe64(&user_image_probe[192], 0x8000001800ULL);
    probe64(&user_image_probe[200], 0x8000001800ULL); probe64(&user_image_probe[208], 4);
    probe64(&user_image_probe[216], 16); probe64(&user_image_probe[224], 1);
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
    serial_write("serial input bridge ready\r\n");
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
    uint64_t zero_frame = physical_alloc_frame();
    if (!zero_frame) {
        serial_write("physical allocation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    volatile uint8_t *zero_memory = (volatile uint8_t *)(uintptr_t)zero_frame;
    zero_memory[0] = 0xa5; zero_memory[4095] = 0x5a;
    physical_free_frame(zero_frame);
    uint64_t recycled_frame = physical_alloc_frame();
    if (!recycled_frame || ((volatile uint8_t *)(uintptr_t)recycled_frame)[0] != 0 ||
        ((volatile uint8_t *)(uintptr_t)recycled_frame)[4095] != 0) {
        if (recycled_frame) physical_free_frame(recycled_frame);
        serial_write("physical zeroing failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    physical_free_frame(recycled_frame);
    serial_write("physical frame hygiene ready\r\n");
    uint64_t contiguous_frames = physical_alloc_frames(2);
    if (!contiguous_frames || physical_alloc_frames(0) != 0 ||
        physical_alloc_frames(UINT32_MAX) != 0 ||
        (contiguous_frames & 0xfffULL) != 0 ||
        ((volatile uint8_t *)(uintptr_t)contiguous_frames)[0] != 0 ||
        ((volatile uint8_t *)(uintptr_t)contiguous_frames)[4096] != 0) {
        if (contiguous_frames) physical_free_frames(contiguous_frames, 2);
        serial_write("physical contiguous allocation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    physical_free_frames(contiguous_frames, 2);
    serial_write("physical contiguous frames ready\r\n");
    if (!virtual_memory_initialize()) {
        serial_write("virtual memory initialization failed\r\n");
        for (;;) __asm__ volatile ("hlt" ::: "memory");
    }
    serial_write("virtual memory root="); serial_write_hex(virtual_memory_root()); serial_write("\r\n");
    static address_space_t process_space;
    process_space.root = 0;
    process_space.owned_count = 0;
    uint64_t process_page = physical_alloc_frame();
    static user_image_t rejected_image;
    static user_image_t loaded_image;
    static uint8_t invalid_image[sizeof(user_image_probe)];
    static uint8_t invalid_alignment_image[sizeof(user_image_probe)];
    build_user_image_probe();
    for (uint32_t i = 0; i < sizeof(invalid_image); ++i)
        invalid_image[i] = user_image_probe[i];
    probe32(&invalid_image[68], 0x85);
    for (uint32_t i = 0; i < sizeof(invalid_alignment_image); ++i)
        invalid_alignment_image[i] = user_image_probe[i];
    probe64(&invalid_alignment_image[112], 3);
    if (!address_space_create(&process_space) ||
        user_image_load(&process_space, 0, 0, &rejected_image) ||
        user_image_load(&process_space, invalid_image, sizeof(invalid_image),
                        &rejected_image) ||
        user_image_load(&process_space, invalid_alignment_image,
                        sizeof(invalid_alignment_image), &rejected_image) ||
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
    if (!address_space_unmap_page(&process_space, 0x8000000000ULL) ||
        process_space.owned_count != 2 ||
        !address_space_map_page(&process_space, 0x8000000000ULL, process_page,
                                ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_USER)) {
        serial_write("address space table reclamation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!address_space_unmap_page(&process_space, 0x8000000000ULL) ||
        process_space.owned_count != 2) {
        serial_write("address space table reclamation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    static address_space_t cloned_image_space;
    static user_image_t cloned_image;
    cloned_image_space = (address_space_t){0};
    cloned_image = (user_image_t){0};
    if (!user_image_load(&process_space, user_image_probe,
                         sizeof(user_image_probe), &loaded_image) ||
        !user_image_clone(&cloned_image_space, &loaded_image, &cloned_image) ||
        cloned_image.entry != loaded_image.entry ||
        cloned_image.page_count != loaded_image.page_count ||
        cloned_image.pages[0] == loaded_image.pages[0] ||
        ((volatile uint8_t *)(uintptr_t)cloned_image.pages[0])[0] !=
            ((volatile uint8_t *)(uintptr_t)loaded_image.pages[0])[0]) {
        serial_write("user image clone failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    user_image_destroy(&cloned_image_space, &cloned_image);
    if (!address_space_destroy(&cloned_image_space)) {
        serial_write("user image clone teardown failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    user_image_destroy(&process_space, &loaded_image);
    static address_space_t cloned_space;
    cloned_space = (address_space_t){0};
    if (!address_space_map_anonymous(&process_space, 0x8000003000ULL, 1,
                                     ADDRESS_SPACE_WRITABLE) ||
        process_space.anonymous_count != 1 ||
        !address_space_clone_anonymous(&cloned_space, &process_space) ||
        cloned_space.anonymous_count != 1 ||
        cloned_space.anonymous_frames[0].virtual_address != 0x8000003000ULL ||
        cloned_space.anonymous_frames[0].physical_address ==
            process_space.anonymous_frames[0].physical_address) {
        serial_write("address space clone failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    volatile uint8_t *source_clone_byte = (volatile uint8_t *)(uintptr_t)
        process_space.anonymous_frames[0].physical_address;
    volatile uint8_t *destination_clone_byte = (volatile uint8_t *)(uintptr_t)
        cloned_space.anonymous_frames[0].physical_address;
    source_clone_byte[0] = 0x5a;
    if (destination_clone_byte[0] != 0) {
        serial_write("address space clone isolation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!address_space_destroy(&cloned_space) ||
        !address_space_unmap_anonymous(&process_space, 0x8000003000ULL, 1)) {
        serial_write("address space clone teardown failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
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
    device_t invalid_resource_device = {0};
    invalid_resource_device.bus = DEVICE_BUS_PCI;
    invalid_resource_device.resources[0].address = 0x1000;
    invalid_resource_device.resources[0].size = 0x100;
    invalid_resource_device.resources[1].address = 0x1080;
    invalid_resource_device.resources[1].size = 0x80;
    if (device_register(&invalid_resource_device)) {
        serial_write("device resource validation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
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
                    input_runtime_interval = endpoint->interval;
                }
            }
            offset = (uint16_t)(offset + descriptor_length);
        }
    }
    if (uhci_root_port_count() != 0 && uhci_interrupt_endpoint != 0) {
        serial_write("UHCI HID interrupt endpoint ready\r\n");
    }
    uint8_t uhci_bulk_probe = 0;
    uint8_t uhci_bulk_toggle = 0;
    if (uhci_controller_count() != 0 &&
        uhci_bulk_transfer(128, 0x81, &uhci_bulk_probe, 0, 64,
                           &uhci_bulk_toggle)) {
        serial_write("UHCI bulk validation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (uhci_controller_count() != 0)
        serial_write("UHCI bulk transfer ready\r\n");
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
        serial_write("NVMe flush I/O ready\r\n");
    }
    static uint8_t nvme_multi_write[16384];
    static uint8_t nvme_multi_read[16384];
    for (uint32_t i = 0; i < sizeof(nvme_multi_write); ++i)
        nvme_multi_write[i] = (uint8_t)(0x5aU ^ i);
    if (nvme_controller_count() != 0 &&
        (!nvme_write_sectors(122, 32, nvme_multi_write) ||
         !nvme_read_sectors(122, 32, nvme_multi_read) ||
         nvme_last_io_page_count() != 4U)) {
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
    if (nvme_controller_count() != 0 && nvme_error_count() != 0) {
        serial_write("NVMe completion error accounting failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
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
    static const uint8_t tcp_probe_payload[3] = {0x11, 0x22, 0x33};
    static uint8_t tcp_probe_packet[TCP_MAX_PACKET_SIZE];
    tcp_segment_view_t tcp_probe_view;
    tcp_connection_t tcp_probe_connection;
    tcp_connection_result_t tcp_probe_result;
    uint16_t tcp_probe_length = 0;
    tcp_connection_initialize(&tcp_probe_connection, 6001, 4096);
    if (!tcp_connection_listen(&tcp_probe_connection) ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 100, 0, TCP_FLAG_SYN, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_probe_view) ||
        !tcp_connection_receive(&tcp_probe_connection, &tcp_probe_view,
                                &tcp_probe_result) ||
        tcp_probe_connection.state != TCP_CONNECTION_SYN_RECEIVED ||
        tcp_probe_result.response_flags != (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 1, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_probe_view) ||
        !tcp_connection_receive(&tcp_probe_connection, &tcp_probe_view,
                                &tcp_probe_result) ||
        tcp_probe_connection.state != TCP_CONNECTION_ESTABLISHED ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 1, TCP_FLAG_ACK | TCP_FLAG_PSH, 4096,
                           tcp_probe_payload, sizeof(tcp_probe_payload),
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_probe_view) ||
        !tcp_connection_receive(&tcp_probe_connection, &tcp_probe_view,
                                &tcp_probe_result) ||
        tcp_probe_result.accepted_payload != sizeof(tcp_probe_payload) ||
        tcp_probe_connection.receive_next != 104) {
        serial_write("TCP transport failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    tcp_probe_connection.peer_window = 2;
    if (tcp_connection_build(&tcp_probe_connection, ipv4_probe_source,
                             ipv4_probe_destination, 0, tcp_probe_payload,
                             sizeof(tcp_probe_payload), tcp_probe_packet,
                             sizeof(tcp_probe_packet), &tcp_probe_length)) {
        serial_write("TCP window enforcement failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    tcp_probe_connection.peer_window = 4096;
    if (!tcp_connection_build(&tcp_probe_connection, ipv4_probe_source,
                               ipv4_probe_destination, 0, tcp_probe_payload,
                               sizeof(tcp_probe_payload), tcp_probe_packet,
                               sizeof(tcp_probe_packet), &tcp_probe_length) ||
        !tcp_connection_retransmit_due(&tcp_probe_connection, 100, 100,
                                       tcp_probe_packet,
                                       TCP_RETRANSMIT_MAX_SIZE,
                                       &tcp_probe_length) ||
        tcp_probe_length != TCP_HEADER_SIZE + sizeof(tcp_probe_payload)) {
        serial_write("TCP retransmission failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 104, 4, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_probe_view) ||
        !tcp_connection_receive(&tcp_probe_connection, &tcp_probe_view,
                                &tcp_probe_result) ||
        tcp_connection_retransmit_due(&tcp_probe_connection, 200, 100,
                                      tcp_probe_packet,
                                      sizeof(tcp_probe_packet),
                                      &tcp_probe_length)) {
        serial_write("TCP retransmission ACK failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 4, TCP_FLAG_ACK | TCP_FLAG_PSH, 4096,
                           tcp_probe_payload, sizeof(tcp_probe_payload),
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_probe_view) ||
        !tcp_connection_receive(&tcp_probe_connection, &tcp_probe_view,
                                &tcp_probe_result) ||
        tcp_probe_result.response_flags != TCP_FLAG_ACK ||
        tcp_probe_result.response_acknowledgment != 104 ||
        tcp_probe_result.accepted_payload != 0 ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 105, 4, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_probe_view) ||
        !tcp_connection_receive(&tcp_probe_connection, &tcp_probe_view,
                                &tcp_probe_result) ||
        tcp_probe_result.response_acknowledgment != 104) {
        serial_write("TCP duplicate ACK failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    tcp_connection_t tcp_close_probe;
    tcp_connection_result_t tcp_close_result;
    tcp_segment_view_t tcp_close_view;
    tcp_connection_initialize(&tcp_close_probe, 6001, 4096);
    if (!tcp_connection_listen(&tcp_close_probe) ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 100, 0, TCP_FLAG_SYN, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_close_view) ||
        !tcp_connection_receive(&tcp_close_probe, &tcp_close_view,
                                &tcp_close_result) ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 1, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_close_view) ||
        !tcp_connection_receive(&tcp_close_probe, &tcp_close_view,
                                &tcp_close_result) ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 1, TCP_FLAG_FIN | TCP_FLAG_ACK, 4096,
                           0, 0, &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_close_view) ||
        !tcp_connection_receive(&tcp_close_probe, &tcp_close_view,
                                &tcp_close_result) ||
        tcp_close_probe.state != TCP_CONNECTION_CLOSE_WAIT ||
        !tcp_connection_close(&tcp_close_probe) ||
        !tcp_connection_build(&tcp_close_probe, ipv4_probe_destination,
                              ipv4_probe_source, TCP_FLAG_FIN, 0, 0,
                              tcp_probe_packet, sizeof(tcp_probe_packet),
                              &tcp_probe_length) ||
        tcp_close_probe.state != TCP_CONNECTION_LAST_ACK ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_destination, ipv4_probe_source,
                           &tcp_close_view) ||
        (tcp_close_view.flags & (TCP_FLAG_FIN | TCP_FLAG_ACK)) !=
            (TCP_FLAG_FIN | TCP_FLAG_ACK) ||
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 102, 2, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_close_view) ||
        !tcp_connection_receive(&tcp_close_probe, &tcp_close_view,
                                &tcp_close_result) ||
        tcp_close_probe.state != TCP_CONNECTION_CLOSED) {
        serial_write("TCP close lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("TCP transport ready\r\n");
    static tcp_endpoint_table_t tcp_endpoint_probe_table;
    static uint8_t tcp_endpoint_probe_ip[ETHERNET_MAX_PAYLOAD_SIZE];
    static uint8_t tcp_endpoint_probe_frame[ETHERNET_MAX_FRAME_SIZE];
    static uint8_t tcp_endpoint_probe_payload[8];
    static uint8_t tcp_endpoint_probe_outbound[TCP_MAX_PACKET_SIZE];
    static uint8_t tcp_endpoint_probe_source_address[4];
    tcp_endpoint_handle_t tcp_endpoint_probe_handle = 0;
    tcp_connection_result_t tcp_endpoint_probe_result;
    uint16_t tcp_endpoint_probe_tcp_length = 0;
    uint16_t tcp_endpoint_probe_ip_length = 0;
    uint16_t tcp_endpoint_probe_frame_length = 0;
    uint16_t tcp_endpoint_probe_outbound_length = 0;
    tcp_endpoint_table_initialize(&tcp_endpoint_probe_table);
    if (!tcp_endpoint_listen(&tcp_endpoint_probe_table, ipv4_probe_destination,
                             6001, 4096, &tcp_endpoint_probe_handle)) {
        serial_write("TCP endpoint failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 100, 0, TCP_FLAG_SYN, 4096, 0, 0,
                           &tcp_endpoint_probe_tcp_length) ||
        !ipv4_packet_build(tcp_endpoint_probe_ip, sizeof(tcp_endpoint_probe_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x5321, tcp_probe_packet,
                           tcp_endpoint_probe_tcp_length,
                           &tcp_endpoint_probe_ip_length) ||
        !ethernet_frame_build(tcp_endpoint_probe_frame,
                              sizeof(tcp_endpoint_probe_frame),
                              ethernet_probe_destination,
                              ethernet_probe_source, 0x0800,
                              tcp_endpoint_probe_ip, tcp_endpoint_probe_ip_length,
                              &tcp_endpoint_probe_frame_length) ||
        !network_deliver_tcp_frame(tcp_endpoint_probe_frame,
                                   tcp_endpoint_probe_frame_length,
                                   &tcp_endpoint_probe_table,
                                   &tcp_endpoint_probe_result) ||
        tcp_endpoint_probe_result.response_flags !=
            (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        serial_write("TCP endpoint failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    tcp_endpoint_handle_t tcp_listener_probe_handle = tcp_endpoint_probe_handle;
    tcp_endpoint_probe_handle = tcp_endpoint_probe_result.endpoint_handle;
    tcp_connection_result_t tcp_duplicate_result;
    tcp_segment_view_t tcp_duplicate_view;
    if (!tcp_segment_parse(tcp_probe_packet, tcp_endpoint_probe_tcp_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_duplicate_view) ||
        !tcp_endpoint_deliver(&tcp_endpoint_probe_table,
                              ipv4_probe_destination, ipv4_probe_source,
                              &tcp_duplicate_view, &tcp_duplicate_result) ||
        tcp_duplicate_result.endpoint_handle != tcp_endpoint_probe_handle ||
        tcp_duplicate_result.response_flags != (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        serial_write("TCP duplicate SYN failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    tcp_connection_result_t tcp_second_result;
    tcp_segment_view_t tcp_second_view;
    if (!tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6002,
                           6001, 200, 0, TCP_FLAG_SYN, 4096, 0, 0,
                           &tcp_probe_length) ||
        !tcp_segment_parse(tcp_probe_packet, tcp_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_second_view) ||
        !tcp_endpoint_deliver(&tcp_endpoint_probe_table,
                              ipv4_probe_destination, ipv4_probe_source,
                              &tcp_second_view, &tcp_second_result) ||
        tcp_second_result.endpoint_handle == 0 ||
        tcp_second_result.endpoint_handle == tcp_listener_probe_handle ||
        tcp_second_result.endpoint_handle == tcp_endpoint_probe_handle ||
        tcp_second_result.response_flags != (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        serial_write("TCP listener backlog failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 1, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_endpoint_probe_tcp_length) ||
        !ipv4_packet_build(tcp_endpoint_probe_ip, sizeof(tcp_endpoint_probe_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x5322, tcp_probe_packet,
                           tcp_endpoint_probe_tcp_length,
                           &tcp_endpoint_probe_ip_length) ||
        !ethernet_frame_build(tcp_endpoint_probe_frame,
                              sizeof(tcp_endpoint_probe_frame),
                              ethernet_probe_destination,
                              ethernet_probe_source, 0x0800,
                              tcp_endpoint_probe_ip, tcp_endpoint_probe_ip_length,
                              &tcp_endpoint_probe_frame_length) ||
        !network_deliver_tcp_frame(tcp_endpoint_probe_frame,
                                   tcp_endpoint_probe_frame_length,
                                   &tcp_endpoint_probe_table,
                                   &tcp_endpoint_probe_result)) {
        serial_write("TCP endpoint failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (
        !tcp_segment_build(tcp_probe_packet, sizeof(tcp_probe_packet),
                           ipv4_probe_source, ipv4_probe_destination, 6000,
                           6001, 101, 1, TCP_FLAG_ACK | TCP_FLAG_PSH, 4096,
                           tcp_endpoint_probe_payload,
                           sizeof(tcp_endpoint_probe_payload),
                           &tcp_endpoint_probe_tcp_length) ||
        !ipv4_packet_build(tcp_endpoint_probe_ip, sizeof(tcp_endpoint_probe_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x5323, tcp_probe_packet,
                           tcp_endpoint_probe_tcp_length,
                           &tcp_endpoint_probe_ip_length) ||
        !ethernet_frame_build(tcp_endpoint_probe_frame,
                              sizeof(tcp_endpoint_probe_frame),
                              ethernet_probe_destination,
                              ethernet_probe_source, 0x0800,
                              tcp_endpoint_probe_ip, tcp_endpoint_probe_ip_length,
                              &tcp_endpoint_probe_frame_length) ||
        !network_deliver_tcp_frame(tcp_endpoint_probe_frame,
                                   tcp_endpoint_probe_frame_length,
                                   &tcp_endpoint_probe_table,
                                   &tcp_endpoint_probe_result) ||
        !tcp_endpoint_receive(&tcp_endpoint_probe_table,
                              tcp_endpoint_probe_handle,
                              tcp_endpoint_probe_source_address,
                              tcp_endpoint_probe_payload,
                              sizeof(tcp_endpoint_probe_payload),
                              &tcp_endpoint_probe_tcp_length) ||
        tcp_endpoint_probe_tcp_length != sizeof(tcp_endpoint_probe_payload)) {
        serial_write("TCP endpoint data failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    network_frame_view_t tcp_response_probe_view;
    if (!network_build_tcp_response(tcp_endpoint_probe_frame,
                                    tcp_endpoint_probe_frame_length,
                                    ethernet_probe_source,
                                    ipv4_probe_destination,
                                    &tcp_endpoint_probe_result,
                                    tcp_endpoint_probe_outbound,
                                    sizeof(tcp_endpoint_probe_outbound),
                                    &tcp_endpoint_probe_outbound_length) ||
        !network_decode_frame(tcp_endpoint_probe_outbound,
                              tcp_endpoint_probe_outbound_length,
                              &tcp_response_probe_view) ||
        tcp_response_probe_view.kind != NETWORK_FRAME_TCP ||
        tcp_response_probe_view.tcp.flags != TCP_FLAG_ACK) {
        serial_write("TCP response framing failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    tcp_segment_view_t tcp_endpoint_probe_outbound_view;
    if (!tcp_endpoint_send_segment(&tcp_endpoint_probe_table,
                                   tcp_endpoint_probe_handle,
                                   tcp_endpoint_probe_payload,
                                   sizeof(tcp_endpoint_probe_payload), 0,
                                   tcp_endpoint_probe_outbound,
                                   sizeof(tcp_endpoint_probe_outbound),
                                   &tcp_endpoint_probe_outbound_length) ||
        !tcp_segment_parse(tcp_endpoint_probe_outbound,
                           tcp_endpoint_probe_outbound_length,
                           ipv4_probe_destination, ipv4_probe_source,
                           &tcp_endpoint_probe_outbound_view) ||
        tcp_endpoint_probe_outbound_view.sequence != 1 ||
        tcp_endpoint_probe_outbound_view.acknowledgment != 109 ||
        tcp_endpoint_probe_outbound_view.payload_length !=
            sizeof(tcp_endpoint_probe_payload)) {
        serial_write("TCP endpoint transmit failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!tcp_endpoint_send_segment(&tcp_endpoint_probe_table,
                                   tcp_endpoint_probe_handle, 0, 0,
                                   TCP_FLAG_FIN, tcp_endpoint_probe_outbound,
                                   sizeof(tcp_endpoint_probe_outbound),
                                   &tcp_endpoint_probe_outbound_length) ||
        !tcp_segment_parse(tcp_endpoint_probe_outbound,
                           tcp_endpoint_probe_outbound_length,
                           ipv4_probe_destination, ipv4_probe_source,
                           &tcp_endpoint_probe_outbound_view) ||
        tcp_endpoint_probe_outbound_view.sequence != 9 ||
        (tcp_endpoint_probe_outbound_view.flags &
         (TCP_FLAG_FIN | TCP_FLAG_ACK)) != (TCP_FLAG_FIN | TCP_FLAG_ACK)) {
        serial_write("TCP endpoint close failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("TCP endpoint ready\r\n");
    static uint8_t tcp_reset_segment[TCP_MAX_PACKET_SIZE];
    static uint8_t tcp_reset_ip[ETHERNET_MAX_PAYLOAD_SIZE];
    static uint8_t tcp_reset_frame[ETHERNET_MAX_FRAME_SIZE];
    static uint8_t tcp_reset_reply[ETHERNET_MAX_FRAME_SIZE];
    uint16_t tcp_reset_segment_length = 0, tcp_reset_ip_length = 0;
    uint16_t tcp_reset_frame_length = 0, tcp_reset_reply_length = 0;
    network_frame_view_t tcp_reset_view;
    int tcp_reset_valid = tcp_segment_build(tcp_reset_segment, sizeof(tcp_reset_segment),
                           ipv4_probe_source, ipv4_probe_destination, 6500,
                           6501, 50, 77, TCP_FLAG_ACK, 4096, 0, 0,
                           &tcp_reset_segment_length) &&
        ipv4_packet_build(tcp_reset_ip, sizeof(tcp_reset_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x6501, tcp_reset_segment, tcp_reset_segment_length,
                           &tcp_reset_ip_length) &&
        ethernet_frame_build(tcp_reset_frame, sizeof(tcp_reset_frame),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, tcp_reset_ip, tcp_reset_ip_length,
                              &tcp_reset_frame_length) &&
        network_build_tcp_reset(tcp_reset_frame, tcp_reset_frame_length,
                                 ethernet_probe_source, ipv4_probe_destination,
                                 tcp_reset_reply, sizeof(tcp_reset_reply),
                                 &tcp_reset_reply_length) &&
        network_decode_frame(tcp_reset_reply, tcp_reset_reply_length,
                              &tcp_reset_view);
    if (!tcp_reset_valid || tcp_reset_view.kind !=
                              NETWORK_FRAME_TCP || tcp_reset_view.tcp.flags !=
                              TCP_FLAG_RST || tcp_reset_view.tcp.sequence != 77 ||
                              tcp_reset_view.tcp.acknowledgment != 0) {
        serial_write("TCP reset response failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!tcp_segment_build(tcp_reset_segment, sizeof(tcp_reset_segment),
                           ipv4_probe_source, ipv4_probe_destination, 6500,
                           6501, 50, 0, TCP_FLAG_SYN, 4096, 0, 0,
                           &tcp_reset_segment_length) ||
        !ipv4_packet_build(tcp_reset_ip, sizeof(tcp_reset_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x6502, tcp_reset_segment, tcp_reset_segment_length,
                           &tcp_reset_ip_length) ||
        !ethernet_frame_build(tcp_reset_frame, sizeof(tcp_reset_frame),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, tcp_reset_ip, tcp_reset_ip_length,
                              &tcp_reset_frame_length) ||
        !network_build_tcp_reset(tcp_reset_frame, tcp_reset_frame_length,
                                 ethernet_probe_source, ipv4_probe_destination,
                                 tcp_reset_reply, sizeof(tcp_reset_reply),
                                 &tcp_reset_reply_length) ||
        !network_decode_frame(tcp_reset_reply, tcp_reset_reply_length,
                              &tcp_reset_view) || tcp_reset_view.tcp.flags !=
                              (TCP_FLAG_RST | TCP_FLAG_ACK) ||
                              tcp_reset_view.tcp.sequence != 0 ||
                              tcp_reset_view.tcp.acknowledgment != 51) {
        serial_write("TCP SYN reset response failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("TCP reset response ready\r\n");
    static tcp_endpoint_table_t tcp_active_probe_table;
    static uint8_t tcp_active_probe_syn[TCP_HEADER_SIZE];
    static uint8_t tcp_active_probe_synack[TCP_HEADER_SIZE];
    tcp_endpoint_handle_t tcp_active_probe_handle = 0;
    tcp_segment_view_t tcp_active_probe_view;
    tcp_connection_result_t tcp_active_probe_result;
    uint16_t tcp_active_probe_length = 0;
    tcp_endpoint_table_initialize(&tcp_active_probe_table);
    if (!tcp_endpoint_connect(&tcp_active_probe_table,
                              ipv4_probe_destination, 6100,
                              ipv4_probe_source, 6101, 500, 4096,
                              &tcp_active_probe_handle, tcp_active_probe_syn,
                              sizeof(tcp_active_probe_syn),
                              &tcp_active_probe_length) ||
        !tcp_segment_parse(tcp_active_probe_syn, tcp_active_probe_length,
                           ipv4_probe_destination, ipv4_probe_source,
                           &tcp_active_probe_view) ||
        tcp_active_probe_view.sequence != 500 ||
        tcp_active_probe_view.flags != TCP_FLAG_SYN ||
        !tcp_segment_build(tcp_active_probe_synack, sizeof(tcp_active_probe_synack),
                           ipv4_probe_source, ipv4_probe_destination, 6101,
                           6100, 700, 501, TCP_FLAG_SYN | TCP_FLAG_ACK,
                           4096, 0, 0, &tcp_active_probe_length) ||
        !tcp_segment_parse(tcp_active_probe_synack, tcp_active_probe_length,
                           ipv4_probe_source, ipv4_probe_destination,
                           &tcp_active_probe_view) ||
        !tcp_endpoint_deliver(&tcp_active_probe_table,
                              ipv4_probe_destination, ipv4_probe_source,
                              &tcp_active_probe_view, &tcp_active_probe_result) ||
        tcp_active_probe_result.response_flags != TCP_FLAG_ACK ||
        tcp_active_probe_result.response_sequence != 501 ||
        tcp_active_probe_result.response_acknowledgment != 701) {
        serial_write("TCP active endpoint failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("TCP active endpoint ready\r\n");
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
    static network_packet_queue_t network_service_probe_queue;
    static arp_cache_t network_service_probe_cache;
    static ipv4_reassembly_table_t network_reassembly_probe_table;
    static uint8_t network_reassembly_probe_output[IPV4_REASSEMBLY_MAX_PAYLOAD];
    network_runtime.queue = &network_service_probe_queue;
    network_runtime.arp_cache = &network_service_probe_cache;
    network_runtime.reassembly = &network_reassembly_probe_table;
    network_runtime.udp = &udp_endpoint_probe_table;
    static tcp_endpoint_table_t network_tcp_runtime_table;
    network_runtime.tcp = &network_tcp_runtime_table;
    network_runtime.local_hardware = ethernet_probe_source;
    network_runtime.local_protocol = ipv4_probe_destination;
    network_runtime.reassembly_output = network_reassembly_probe_output;
    network_packet_queue_initialize(&network_service_probe_queue);
    arp_cache_initialize(&network_service_probe_cache);
    ipv4_reassembly_initialize(&network_reassembly_probe_table);
    udp_endpoint_table_initialize(&udp_endpoint_probe_table);
    tcp_endpoint_table_initialize(&network_tcp_runtime_table);
    tcp_endpoint_handle_t network_tcp_probe_handle = 0;
    static uint8_t network_tcp_probe_payload[2] = {0x7a, 0x7b};
    static uint8_t network_tcp_probe_segment[TCP_MAX_PACKET_SIZE];
    uint16_t network_tcp_probe_segment_length = 0;
    uint16_t network_tcp_probe_ip_length = 0;
    uint16_t network_tcp_probe_frame_length = 0;
    if (!tcp_endpoint_listen(&network_tcp_runtime_table,
                             ipv4_probe_destination, 6201, 4096,
                             &network_tcp_probe_handle) ||
        !tcp_segment_build(network_tcp_probe_segment,
                           sizeof(network_tcp_probe_segment),
                           ipv4_probe_source, ipv4_probe_destination, 6200,
                           6201, 300, 0, TCP_FLAG_SYN, 4096, 0, 0,
                           &network_tcp_probe_segment_length) ||
        !ipv4_packet_build(tcp_endpoint_probe_ip, sizeof(tcp_endpoint_probe_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x6201, network_tcp_probe_segment,
                           network_tcp_probe_segment_length,
                           &network_tcp_probe_ip_length) ||
        !ethernet_frame_build(tcp_endpoint_probe_frame,
                              sizeof(tcp_endpoint_probe_frame),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, tcp_endpoint_probe_ip,
                              network_tcp_probe_ip_length,
                              &network_tcp_probe_frame_length) ||
        !network_packet_queue_push(&network_service_probe_queue,
                                   tcp_endpoint_probe_frame,
                                   network_tcp_probe_frame_length) ||
        network_service(&network_service_probe_queue, ethernet_probe_source,
                        ipv4_probe_destination, &network_service_probe_cache,
                        &udp_endpoint_probe_table, &network_tcp_runtime_table,
                        3, 4, &network_reassembly_probe_table,
                        network_reassembly_probe_output,
                        sizeof(network_reassembly_probe_output)) != 1 ||
        !tcp_endpoint_accept(&network_tcp_runtime_table,
                             network_tcp_probe_handle,
                             &network_tcp_probe_handle) ||
        !tcp_segment_build(network_tcp_probe_segment,
                           sizeof(network_tcp_probe_segment),
                           ipv4_probe_source, ipv4_probe_destination, 6200,
                           6201, 301, 1, TCP_FLAG_ACK, 4096, 0, 0,
                           &network_tcp_probe_segment_length) ||
        !ipv4_packet_build(tcp_endpoint_probe_ip, sizeof(tcp_endpoint_probe_ip),
                           ipv4_probe_source, ipv4_probe_destination, 6, 64,
                           0x6202, network_tcp_probe_segment,
                           network_tcp_probe_segment_length,
                           &network_tcp_probe_ip_length) ||
        !ethernet_frame_build(tcp_endpoint_probe_frame,
                              sizeof(tcp_endpoint_probe_frame),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, tcp_endpoint_probe_ip,
                              network_tcp_probe_ip_length,
                              &network_tcp_probe_frame_length) ||
        !network_packet_queue_push(&network_service_probe_queue,
                                   tcp_endpoint_probe_frame,
                                   network_tcp_probe_frame_length) ||
        network_service(&network_service_probe_queue, ethernet_probe_source,
                        ipv4_probe_destination, &network_service_probe_cache,
                        &udp_endpoint_probe_table, &network_tcp_runtime_table,
                        4, 4, &network_reassembly_probe_table,
                        network_reassembly_probe_output,
                        sizeof(network_reassembly_probe_output)) != 1 ||
        !tcp_endpoint_send_segment(&network_tcp_runtime_table,
                                   network_tcp_probe_handle,
                                   network_tcp_probe_payload,
                                   sizeof(network_tcp_probe_payload), 0,
                                   network_tcp_probe_segment,
                                   sizeof(network_tcp_probe_segment),
                                   &network_tcp_probe_segment_length)) {
        serial_write("network TCP service failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("network TCP service ready\r\n");
    if (!udp_endpoint_bind(&udp_endpoint_probe_table, ipv4_probe_destination,
                           6001, &udp_endpoint_any) ||
        !network_packet_queue_push(&network_service_probe_queue,
                                   network_dispatch_frame,
                                   network_dispatch_frame_length)) {
        serial_write("network service setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (network_service(&network_service_probe_queue, ethernet_probe_source,
                        ipv4_probe_destination, &network_service_probe_cache,
                        &udp_endpoint_probe_table, &network_tcp_runtime_table,
                        1, 4,
                        &network_reassembly_probe_table,
                        network_reassembly_probe_output,
                        sizeof(network_reassembly_probe_output)) != 1 ||
        network_packet_queue_count(&network_service_probe_queue) != 0 ||
        !udp_endpoint_receive(&udp_endpoint_probe_table, udp_endpoint_any,
                              udp_endpoint_source, &udp_endpoint_source_port,
                              udp_endpoint_output, sizeof(udp_endpoint_output),
                              &udp_endpoint_output_length) ||
        udp_endpoint_source_port != 6000 || udp_endpoint_output_length != 3 ||
        udp_endpoint_output[0] != 0xa1) {
        serial_write("network service failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("network service ready\r\n");
    static uint8_t fragment_one_ip[IPV4_MIN_HEADER_SIZE + 8];
    static uint8_t fragment_two_ip[IPV4_MIN_HEADER_SIZE + sizeof(network_dispatch_udp) - 8];
    static uint8_t fragment_one_frame[ETHERNET_MAX_FRAME_SIZE];
    static uint8_t fragment_two_frame[ETHERNET_MAX_FRAME_SIZE];
    uint16_t fragment_one_ip_length = 0, fragment_two_ip_length = 0;
    uint16_t fragment_one_frame_length = 0, fragment_two_frame_length = 0;
    if (!ipv4_packet_build(fragment_one_ip, sizeof(fragment_one_ip),
                           ipv4_probe_source, ipv4_probe_destination, 17, 64,
                           0x4321, network_dispatch_udp, 8,
                           &fragment_one_ip_length) ||
        !ipv4_packet_build(fragment_two_ip, sizeof(fragment_two_ip),
                           ipv4_probe_source, ipv4_probe_destination, 17, 64,
                           0x4321, network_dispatch_udp + 8,
                           sizeof(network_dispatch_udp) - 8,
                           &fragment_two_ip_length)) {
        serial_write("network fragment setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    fragment_one_ip[6] = 0x20; fragment_one_ip[7] = 0;
    fragment_two_ip[6] = 0; fragment_two_ip[7] = 1;
    for (uint32_t fragment = 0; fragment < 2; ++fragment) {
        uint8_t *ip = fragment == 0 ? fragment_one_ip : fragment_two_ip;
        ip[10] = 0; ip[11] = 0;
        uint16_t checksum = ipv4_checksum(ip, IPV4_MIN_HEADER_SIZE);
        ip[10] = (uint8_t)(checksum >> 8); ip[11] = (uint8_t)checksum;
    }
    if (!ethernet_frame_build(fragment_one_frame, sizeof(fragment_one_frame),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, fragment_one_ip, fragment_one_ip_length,
                              &fragment_one_frame_length) ||
        !ethernet_frame_build(fragment_two_frame, sizeof(fragment_two_frame),
                              ethernet_probe_destination, ethernet_probe_source,
                              0x0800, fragment_two_ip, fragment_two_ip_length,
                              &fragment_two_frame_length)) {
        serial_write("network fragment frame failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    network_packet_queue_initialize(&network_service_probe_queue);
    ipv4_reassembly_initialize(&network_reassembly_probe_table);
    udp_endpoint_table_initialize(&udp_endpoint_probe_table);
    if (!udp_endpoint_bind(&udp_endpoint_probe_table, ipv4_probe_destination,
                           6001, &udp_endpoint_any) ||
        !network_packet_queue_push(&network_service_probe_queue,
                                   fragment_two_frame, fragment_two_frame_length) ||
        !network_packet_queue_push(&network_service_probe_queue,
                                   fragment_one_frame, fragment_one_frame_length) ||
        network_service(&network_service_probe_queue, ethernet_probe_source,
                        ipv4_probe_destination, &network_service_probe_cache,
                        &udp_endpoint_probe_table, &network_tcp_runtime_table,
                        2, 4,
                        &network_reassembly_probe_table,
                        network_reassembly_probe_output,
                        sizeof(network_reassembly_probe_output)) != 2 ||
        !udp_endpoint_receive(&udp_endpoint_probe_table, udp_endpoint_any,
                              udp_endpoint_source, &udp_endpoint_source_port,
                              udp_endpoint_output, sizeof(udp_endpoint_output),
                              &udp_endpoint_output_length) ||
        udp_endpoint_output_length != 3 || udp_endpoint_output[0] != 0xa1) {
        serial_write("network fragment delivery failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("network fragment reassembly ready\r\n");
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
    int ahci_initialized = ahci_initialize();
    if (ahci_initialized) {
        serial_write("AHCI driver ready controllers=");
        serial_write_hex(ahci_controller_count());
        serial_write(" ports=");
        serial_write_hex(ahci_port_mask());
        serial_write(" ready=");
        serial_write_hex(ahci_ready_port_count());
        serial_write(" mask=");
        serial_write_hex(ahci_ready_port_mask());
        serial_write("\r\n");
    } else {
        serial_write("AHCI driver unavailable\r\n");
    }
    serial_write(acpi_reset_available() ? "ACPI reset service ready\r\n" :
                 "ACPI reset service unavailable\r\n");
    int ahci_storage_available = ahci_initialized && ahci_ready_port_count() != 0;
    if (ahci_storage_available) {
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
    uint8_t ahci_multi_write[8192];
    uint8_t ahci_multi_read[8192];
    for (uint32_t i = 0; i < sizeof(ahci_multi_write); ++i)
        ahci_multi_write[i] = (uint8_t)(0xc3U ^ i);
    if (!ahci_write_sectors(122, 16, ahci_multi_write) ||
        !ahci_read_sectors(122, 16, ahci_multi_read) ||
        ahci_last_io_prdt_length() != 2U) {
        serial_write("AHCI multi-sector I/O failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t i = 0; i < sizeof(ahci_multi_write); ++i)
        if (ahci_multi_read[i] != ahci_multi_write[i]) {
            serial_write("AHCI multi-sector verification failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
    serial_write("AHCI multi-sector I/O ready\r\n");
    if (ahci_error_count() != 0) {
        serial_write("AHCI completion error accounting failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    }
    storage_initialize();
    if (ahci_storage_available && !ahci_register_storage_devices()) {
        serial_write("AHCI storage registration failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write(ahci_storage_available ? "AHCI storage backend ready\r\n" :
                 "AHCI storage backend unavailable\r\n");
    if (nvme_controller_count() != 0) {
        storage_device_t nvme_storage = {
            .name = "nvme0", .block_size = 512,
            .block_count = nvme_namespace_sectors,
            .read = nvme_read_sectors, .write = nvme_write_sectors,
            .flush = nvme_flush_device
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
    uint8_t ata_multi_write[1024];
    uint8_t ata_multi_read[1024];
    for (uint32_t byte = 0; byte < sizeof(ata_multi_write); ++byte)
        ata_multi_write[byte] = (uint8_t)(0xc3U ^ byte);
    if (!ata_write_sectors(121, 2, ata_multi_write) ||
        !ata_read_sectors(121, 2, ata_multi_read)) {
        serial_write("ATA multi-sector I/O failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (uint32_t byte = 0; byte < sizeof(ata_multi_write); ++byte)
        if (ata_multi_write[byte] != ata_multi_read[byte]) {
            serial_write("ATA multi-sector verification failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
    serial_write("ATA multi-sector I/O ready\r\n");
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
    if (ahci_ready_port_count() > 1) {
        for (uint32_t byte = 0; byte < sizeof(storage_scratch); ++byte)
            storage_scratch[byte] = (uint8_t)(0x37U ^ byte);
        if (!storage_read(1, 1, 1, storage_verify) ||
            !storage_write(1, 120, 1, storage_scratch) ||
            !storage_read(1, 120, 1, storage_verify)) {
            serial_write("AHCI secondary storage failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        for (uint32_t byte = 0; byte < sizeof(storage_scratch); ++byte)
            if (storage_scratch[byte] != storage_verify[byte]) {
                serial_write("AHCI secondary storage verification failure\r\n");
                for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
            }
        serial_write("AHCI multi-disk storage ready\r\n");
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
    vfs_filesystem_type_t detected_filesystem;
    if (!vfs_probe_filesystem(0, &detected_filesystem) ||
        detected_filesystem != VFS_FILESYSTEM_FAT32) {
        serial_write("filesystem probe dispatch failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("filesystem probe dispatch ready\r\n");
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
    static const char args_short_name[11] = {
        'A','R','G','S',' ',' ',' ',' ','E','L','F'
    };
    if (!fat32_vfs_attach_file(&fat32, vfs_root, args_short_name,
                               "args.elf")) {
        serial_write("userland args VFS attach failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("userland args executable ready\r\n");
    static const char env_short_name[11] = {
        'E','N','V',' ',' ',' ',' ',' ','E','L','F'
    };
    if (!fat32_vfs_attach_file(&fat32, vfs_root, env_short_name,
                               "env.elf")) {
        serial_write("userland env VFS attach failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("userland env executable ready\r\n");
    vfs_node_t *vfs_foreign = vfs_node_create("foreign", VFS_NODE_REGULAR,
                                               0, 0, 0444);
    int vfs_remove_result = vfs_foreign ?
        vfs_node_remove(vfs_root, vfs_foreign) : 0;
    vfs_node_t *vfs_after_remove = vfs_node_lookup(vfs_root, "dev");
    if (!vfs_foreign || vfs_remove_result || !vfs_after_remove) {
        if (vfs_foreign) vfs_node_release(vfs_foreign);
        if (vfs_after_remove) vfs_node_release(vfs_after_remove);
        serial_write("VFS remove failure-path deadlock\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_after_remove);
    vfs_node_release(vfs_foreign);
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
        vfs_mount(&vfs_mounts, vfs_dev, vfs_root) ||
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
    vfs_node_t *vfs_mount_escape =
        vfs_lookup_path_mounted(&vfs_mounts, vfs_root, "/dev/../etc");
    if (!vfs_mount_escape || vfs_mount_escape->name[0] != 'e') {
        if (vfs_mount_escape) vfs_node_release(vfs_mount_escape);
        serial_write("VFS mount escape failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_mount_escape);
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
    vfs_node_t *vfs_first_child = vfs_node_child(vfs_root, 0);
    if (!vfs_first_child) {
        serial_write("VFS directory iteration failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_first_child);
    serial_write("VFS directory iteration ready\r\n");
    vfs_node_t *vfs_private_directory = vfs_node_create("private", VFS_NODE_DIRECTORY,
                                                        0, 0, 0700);
    if (!vfs_private_directory || !vfs_node_add_child(vfs_root, vfs_private_directory)) {
        if (vfs_private_directory) vfs_node_release(vfs_private_directory);
        serial_write("VFS permissions setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_private_directory);
    vfs_node_t *vfs_user_directory = vfs_node_create("user", VFS_NODE_DIRECTORY,
                                                     1000, 1000, 0700);
    if (!vfs_user_directory || !vfs_node_add_child(vfs_root, vfs_user_directory)) {
        if (vfs_user_directory) vfs_node_release(vfs_user_directory);
        serial_write("VFS user directory setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_user_directory);
    uint8_t vfs_write_storage[16] = {0};
    static const uint8_t vfs_write_data[4] = {'v', 'f', 's', '!'};
    vfs_node_t *vfs_write_node = vfs_node_create("write_probe", VFS_NODE_REGULAR,
                                                  1000, 1000, 0666);
    if (!vfs_write_node || !vfs_node_set_write(vfs_write_node, vfs_probe_write,
                                               vfs_write_storage) ||
        !vfs_node_set_read(vfs_write_node, vfs_probe_read, vfs_write_storage) ||
        !vfs_node_set_truncate(vfs_write_node, vfs_probe_truncate) ||
        !vfs_node_add_child(vfs_root, vfs_write_node) ||
        vfs_node_write(vfs_write_node, 4, vfs_write_data,
                       sizeof(vfs_write_data)) != sizeof(vfs_write_data) ||
        vfs_write_storage[4] != 'v' || vfs_write_storage[7] != '!') {
        if (vfs_write_node) vfs_node_release(vfs_write_node);
        serial_write("VFS write failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_node_release(vfs_write_node);
    serial_write("VFS write ready\r\n");
    serial_write("VFS permissions ready\r\n");
    vfs_node_t *proc_root = procfs_create(42);
    vfs_node_t *proc_pid = proc_root ?
        vfs_lookup_path(proc_root, "/self/pid") : 0;
    char proc_pid_text[4] = {0};
    if (!proc_root || !proc_pid || vfs_node_read(proc_pid, 0, proc_pid_text, 3) != 3 ||
        proc_pid_text[0] != '4' || proc_pid_text[1] != '2' || proc_pid_text[2] != '\n') {
        serial_write("procfs read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    vfs_file_t *proc_file = vfs_file_open(proc_pid, VFS_FILE_READ);
    vfs_file_t *proc_directory = vfs_file_open(proc_root, VFS_FILE_READ);
    process_handle_table_t file_handles;
    process_handle_ref_t file_ref = {0};
    process_handle_table_initialize(&file_handles);
    int file_handle = vfs_file_open_path_handle(&file_handles, proc_root,
                                                "/self/pid", VFS_FILE_READ);
    char file_pid_text[4] = {0};
    vfs_dirent_t directory_entry = {0};
    if (!proc_file || vfs_file_read(proc_file, file_pid_text, 3) != 3 ||
        file_pid_text[0] != '4' || file_pid_text[1] != '2' ||
        file_pid_text[2] != '\n' || vfs_file_offset(proc_file) != 3 ||
        !vfs_file_seek(proc_file, 0) || vfs_file_write(proc_file, file_pid_text, 1) ||
        !proc_directory || !vfs_file_readdir(proc_directory, &directory_entry) ||
        directory_entry.type != VFS_NODE_DIRECTORY || directory_entry.name[0] != 's' ||
        vfs_file_readdir(proc_directory, &directory_entry) ||
        !file_handle || !process_handle_get_retain(&file_handles,
            (uint32_t)file_handle, PROCESS_HANDLE_READ, &file_ref) ||
        !process_handle_close(&file_handles, (uint32_t)file_handle)) {
        if (proc_file) vfs_file_release(proc_file);
        if (proc_directory) vfs_file_release(proc_directory);
        serial_write("VFS file description failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_handle_release_ref(&file_ref);
    vfs_file_release(proc_file);
    vfs_file_release(proc_directory);
    serial_write("VFS file descriptions ready\r\n");
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
    block_probe.flush = block_probe_flush;
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
    block_registry_t storage_block_registry;
    block_device_t storage_block = {0};
    storage_block_context_t storage_block_context = {0};
    uint8_t storage_block_probe[512];
    block_registry_initialize(&storage_block_registry);
    if (!storage_block_bind(&storage_block, &storage_block_context, "disk0", 0) ||
        !block_registry_register(&storage_block_registry, &storage_block) ||
        !block_registry_read(&storage_block_registry, 0, 0, 1,
                             storage_block_probe) ||
        storage_block_probe[82] != 'F' || storage_block_probe[83] != 'A' ||
        storage_block_probe[84] != 'T' || storage_block_probe[85] != '3' ||
        storage_block_probe[86] != '2') {
        serial_write("storage block adapter failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("storage block adapter ready\r\n");
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
    if (!block_cache_flush(&block_cache, &block_registry, 0)) {
        serial_write("block cache flush failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("block cache ready\r\n");
    block_cache_invalidate_device(&block_cache, &block_registry, 0);
    if (!block_cache_read(&block_cache, &block_registry, 0, 0,
                          block_probe_data, sizeof(block_probe_data))) {
        serial_write("block cache lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("block cache lifecycle ready\r\n");
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
    input_runtime_queue = &input_queue;
    input_set_standard_queue(&input_queue);
    input_event_t input_event = {
        .type = INPUT_EVENT_KEY, .code = 30, .value = 1, .timestamp = 7
    };
    input_event_t invalid_input_event = {0};
    invalid_input_event.type = (input_event_type_t)-1;
    input_event_t input_out;
    input_queue_initialize(&input_queue);
    if (input_queue_push(&input_queue, &invalid_input_event) ||
        !input_queue_push(&input_queue, &input_event)) {
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
    input_event_t batch[2] = {input_event,
                              {.type = INPUT_EVENT_AXIS, .code = 2,
                               .value = -1, .timestamp = 8}};
    if (!input_queue_push_batch(&input_queue, batch, 2) ||
        input_queue_count(&input_queue) != 2 ||
        !input_queue_pop(&input_queue, &input_out) || input_out.code != 30 ||
        !input_queue_pop(&input_queue, &input_out) || input_out.code != 2 ||
        input_queue_count(&input_queue) != 0) {
        serial_write("input batch failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    input_event_t standard_input_event = {
        .type = INPUT_EVENT_KEY, .code = 4, .value = 1, .timestamp = 9
    };
    uint8_t standard_input_probe[2] = {0};
    if (!input_queue_push(&input_queue, &standard_input_event) ||
        input_read_standard(standard_input_probe, 1) != 1 ||
        standard_input_probe[0] != 'a') {
        serial_write("standard input failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("standard input ready\r\n");
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
    uint64_t firmware_pixels = (uint64_t)info->framebuffer_pitch * info->framebuffer_height;
    if (!info->framebuffer_base || info->framebuffer_base >= (1ULL << 32) ||
        firmware_pixels > UINT64_MAX / sizeof(uint32_t) ||
        firmware_pixels * sizeof(uint32_t) > (1ULL << 32) - info->framebuffer_base ||
        info->framebuffer_size < firmware_pixels * sizeof(uint32_t) ||
        !framebuffer_initialize(&firmware_framebuffer,
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
    static const uint8_t usb_invalid_bulk_endpoint[7] = {7, 5, 0x02, 2, 3, 0, 1};
    usb_device_t usb_device;
    uint8_t invalid_usb_device_descriptor[18];
    for (uint32_t descriptor_byte = 0; descriptor_byte < 18; ++descriptor_byte)
        invalid_usb_device_descriptor[descriptor_byte] = usb_device_descriptor[descriptor_byte];
    invalid_usb_device_descriptor[7] = 7;
    if (!usb_device_parse_descriptor(&usb_device, usb_device_descriptor,
                                     sizeof(usb_device_descriptor)) ||
        usb_device_parse_descriptor(&usb_device, invalid_usb_device_descriptor,
                                    sizeof(invalid_usb_device_descriptor)) ||
        usb_device.vendor_id != 0x1234 ||
        !usb_device_add_endpoint(&usb_device, usb_endpoint_descriptor,
                                  sizeof(usb_endpoint_descriptor)) ||
        usb_device_add_endpoint(&usb_device, usb_invalid_bulk_endpoint,
                                sizeof(usb_invalid_bulk_endpoint)) ||
        usb_device_add_endpoint(&usb_device, usb_endpoint_descriptor,
                                sizeof(usb_endpoint_descriptor)) ||
        usb_device.endpoint_count != 1 ||
        usb_device.endpoints[0].max_packet_size != 64) {
        serial_write("USB descriptor failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("USB descriptor layer ready\r\n");
    static const uint8_t hid_probe_report[8] = {0x02, 0, 0x04, 0x05, 0, 0, 0, 0};
    input_event_t hid_probe_events[6];
    uint32_t hid_probe_count = 0;
    if (!usb_hid_keyboard_decode_report(hid_probe_report, sizeof(hid_probe_report),
                                        hid_probe_events, &hid_probe_count) ||
        hid_probe_count != 2 || hid_probe_events[0].code != 0x04 ||
        hid_probe_events[1].code != 0x05 || hid_probe_events[0].value != 1 ||
        usb_hid_keyboard_decode_report((const uint8_t[]){0, 0, 4, 4, 0, 0, 0, 0},
                                        sizeof(hid_probe_report), hid_probe_events,
                                        &hid_probe_count)) {
        serial_write("USB HID keyboard failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    usb_hid_keyboard_state_t hid_probe_state;
    input_event_t hid_transition_events[20];
    uint32_t hid_transition_count = 0;
    usb_hid_keyboard_state_initialize(&hid_probe_state);
    static const uint8_t hid_release_report[8] = {0, 0, 0x05, 0, 0, 0, 0, 0};
    if (!usb_hid_keyboard_decode_state(hid_probe_report, sizeof(hid_probe_report),
                                       &hid_probe_state, hid_transition_events,
                                       &hid_transition_count) ||
        hid_transition_count != 3 ||
        hid_transition_events[0].code != 0xe1 ||
        hid_transition_events[0].value != 1 ||
        !usb_hid_keyboard_decode_state(hid_release_report,
                                       sizeof(hid_release_report),
                                       &hid_probe_state, hid_transition_events,
                                       &hid_transition_count) ||
        hid_transition_count != 2 || hid_transition_events[0].code != 0xe1 ||
        hid_transition_events[0].value != 0 ||
        hid_transition_events[1].code != 0x04 ||
        hid_transition_events[1].value != 0) {
        serial_write("USB HID keyboard state failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    usb_hid_keyboard_state_t hid_capacity_state = { {4, 5, 6, 7, 8, 9}, 0xff };
    static const uint8_t hid_capacity_report[8] = {0, 0, 10, 11, 12, 13, 14, 15};
    if (!usb_hid_keyboard_decode_state(hid_capacity_report,
                                       sizeof(hid_capacity_report),
                                       &hid_capacity_state, hid_transition_events,
                                       &hid_transition_count) ||
        hid_transition_count != 20) {
        serial_write("USB HID keyboard capacity failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("USB HID keyboard ready\r\n");
    static const uint8_t hid_mouse_probe_report[3] = {0x05, 0x04, 0xfb};
    input_event_t hid_mouse_probe_events[3];
    uint32_t hid_mouse_probe_count = 0;
    if (!usb_hid_mouse_decode(hid_mouse_probe_report,
                              sizeof(hid_mouse_probe_report),
                              hid_mouse_probe_events,
                              &hid_mouse_probe_count) ||
        hid_mouse_probe_count != 3 ||
        hid_mouse_probe_events[0].type != INPUT_EVENT_BUTTON ||
        hid_mouse_probe_events[0].value != 5 ||
        hid_mouse_probe_events[1].value != 4 ||
        hid_mouse_probe_events[2].code != 1 ||
        hid_mouse_probe_events[2].value != -5) {
        serial_write("USB HID mouse failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    static const uint8_t hid_mouse_wheel_report[4] = {0x01, 0xff, 0x02, 0xfe};
    input_event_t hid_mouse_wheel_events[4];
    uint32_t hid_mouse_wheel_count = 0;
    if (!usb_hid_mouse_decode(hid_mouse_wheel_report,
                              sizeof(hid_mouse_wheel_report),
                              hid_mouse_wheel_events,
                              &hid_mouse_wheel_count) ||
        hid_mouse_wheel_count != 4 || hid_mouse_wheel_events[3].code != 2 ||
        hid_mouse_wheel_events[3].value != -2) {
        serial_write("USB HID mouse wheel failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("USB HID mouse ready\r\n");
    if (uhci_root_port_count() != 0 && uhci_interrupt_endpoint != 0) {
        for (uint32_t i = 0; i < sizeof(input_runtime_report); ++i)
            input_runtime_report[i] = 0;
        input_runtime_toggle = 0;
        input_runtime_endpoint = uhci_interrupt_endpoint;
        input_runtime_packet = uhci_interrupt_packet;
        input_runtime_mouse = input_runtime_packet == 3 ||
                              input_runtime_packet == 4;
        input_runtime_ready = 1;
        input_runtime_pending = uhci_interrupt_submit(
            1, input_runtime_endpoint, input_runtime_report,
            input_runtime_packet, input_runtime_packet,
            input_runtime_interval,
            &input_runtime_toggle);
        usb_hid_keyboard_state_initialize(&input_runtime_hid_state);
        serial_write(input_runtime_pending ? "UHCI HID interrupt transfer scheduled\r\n" :
                     "UHCI HID interrupt scheduling failure\r\n");
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
    static const uint8_t ps2_mouse_wheel_packet[4] = {0x08, 1, 0xff, 0x0f};
    input_event_t ps2_mouse_wheel_events[4];
    uint32_t ps2_mouse_wheel_count = 0;
    if (!ps2_mouse_decode_wheel(ps2_mouse_wheel_packet,
                                ps2_mouse_wheel_events,
                                &ps2_mouse_wheel_count) ||
        ps2_mouse_wheel_count != 4 || ps2_mouse_wheel_events[3].code != 2 ||
        ps2_mouse_wheel_events[3].value != -1) {
        serial_write("PS2 mouse wheel failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    static const uint8_t ps2_mouse_explorer_packet[4] = {0x09, 1, 0xfe, 0x31};
    input_event_t ps2_mouse_explorer_events[4];
    uint32_t ps2_mouse_explorer_count = 0;
    if (!ps2_mouse_decode_explorer(ps2_mouse_explorer_packet,
                                   ps2_mouse_explorer_events,
                                   &ps2_mouse_explorer_count) ||
        ps2_mouse_explorer_count != 4 ||
        ps2_mouse_explorer_events[0].value != 0x19 ||
        ps2_mouse_explorer_events[3].value != 1) {
        serial_write("PS2 explorer mouse failure\r\n");
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
    process_thread_t *looked_up_thread = thread_process ?
        process_thread_lookup(thread_process, 200) : 0;
    if (!thread_probe || looked_up_thread != thread_probe ||
        process_thread_create(thread_process, 200, process_thread_probe, 0, 4096) ||
        process_thread_create(thread_process, 0, process_thread_probe, 0, 4096) ||
        thread_process->thread_count != 1 ||
        !process_thread_start(thread_probe) || scheduler_ready_count() != 1 ||
        !process_thread_destroy(thread_probe) || thread_process->thread_count != 0 ||
        process_destroy(thread_process)) {
        serial_write("process thread lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_thread_release(looked_up_thread);
    if (!process_destroy(thread_process)) {
        serial_write("process thread release failure\r\n");
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
    task_wait_node_initialize(&task_demo_waiter_a, 0);
    task_wait_node_initialize(&task_demo_waiter_b, 0);
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
        if (!uhci_control_transfer(1, 0, uhci_irq_probe_setup,
                                   uhci_irq_probe_descriptor,
                                   sizeof(uhci_irq_probe_descriptor))) {
            serial_write("UHCI interrupt delivery failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("UHCI interrupt path configured\r\n");
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
        (void)e1000_service();
        if (e1000_tx_error_count() != 0 || e1000_rx_error_count() != 0) {
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
    rtc_datetime_t rtc_now;
    rtc_initialize();
    if (!rtc_read_datetime(&rtc_now)) {
        serial_write("RTC read failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("RTC ready\r\n");
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
        !process_map_user_stack(runtime_process, 0x8000002000ULL) ||
        !process_set_namespace(runtime_process, vfs_root, vfs_root) ||
        !address_space_user_range_valid(&runtime_process->address_space,
                                        0x8000002000ULL, 8 * 0x1000ULL, 1)) {
        serial_write("user process setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    static const uint64_t anonymous_probe_address = 0x8000100000ULL;
    if (!address_space_map_anonymous(&runtime_process->address_space,
                                      anonymous_probe_address, 2,
                                      ADDRESS_SPACE_WRITABLE) ||
        !address_space_user_range_valid(&runtime_process->address_space,
                                        anonymous_probe_address, 2 * 0x1000ULL, 1) ||
        !address_space_protect_range(&runtime_process->address_space,
                                      anonymous_probe_address, 1,
                                      ADDRESS_SPACE_EXECUTABLE) ||
        address_space_user_range_valid(&runtime_process->address_space,
                                       anonymous_probe_address, 0x1000, 1) ||
        !address_space_page_executable(&runtime_process->address_space,
                                       anonymous_probe_address) ||
        !address_space_protect_range(&runtime_process->address_space,
                                     anonymous_probe_address, 1,
                                     ADDRESS_SPACE_WRITABLE) ||
        !address_space_user_range_valid(&runtime_process->address_space,
                                        anonymous_probe_address, 0x1000, 1) ||
        address_space_protect_range(&runtime_process->address_space,
                                    anonymous_probe_address, 3,
                                    ADDRESS_SPACE_EXECUTABLE) ||
        !address_space_user_range_valid(&runtime_process->address_space,
                                        anonymous_probe_address, 0x1000, 1) ||
        !address_space_unmap_anonymous(&runtime_process->address_space,
                                       anonymous_probe_address, 2) ||
        address_space_user_range_valid(&runtime_process->address_space,
                                       anonymous_probe_address, 2 * 0x1000ULL, 1) ||
        !address_space_map_anonymous(&runtime_process->address_space,
                                     anonymous_probe_address, 1,
                                     ADDRESS_SPACE_WRITABLE)) {
        serial_write("anonymous memory lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    volatile uint8_t *anonymous_source = (volatile uint8_t *)(uintptr_t)
        runtime_process->address_space.anonymous_frames[0].physical_address;
    anonymous_source[0] = 0x5a;
    serial_write("anonymous memory lifecycle ready\r\n");
    process_t *constructed_process = process_create_user(
        7, user_image_probe, sizeof(user_image_probe), 0x8000010000ULL,
        70, 4096);
    if (!constructed_process || constructed_process->state != PROCESS_READY ||
        constructed_process->thread_count != 1 ||
        !process_destroy(constructed_process)) {
        serial_write("user process construction failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *cloned_process = process_clone_user(runtime_process, 8, 80, 4096);
    int32_t cloned_status = 0;
    if (!cloned_process || cloned_process->state != PROCESS_READY ||
        cloned_process->thread_count != 1 ||
        cloned_process->root_directory != runtime_process->root_directory ||
        cloned_process->image.pages[0] == runtime_process->image.pages[0] ||
        cloned_process->user_stack_pages[0] == runtime_process->user_stack_pages[0] ||
        cloned_process->address_space.anonymous_count != 1 ||
        cloned_process->address_space.anonymous_frames[0].physical_address ==
            runtime_process->address_space.anonymous_frames[0].physical_address ||
        ((volatile uint8_t *)(uintptr_t)cloned_process->address_space.anonymous_frames[0].physical_address)[0] != 0x5a ||
        !process_terminate(cloned_process, 17) ||
        !process_wait_child(runtime_process, cloned_process, &cloned_status) ||
        cloned_status != 17 || !process_destroy(cloned_process)) {
        serial_write("user process clone failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!address_space_unmap_anonymous(&runtime_process->address_space,
                                       anonymous_probe_address, 1)) {
        serial_write("anonymous clone cleanup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("user process clone ready\r\n");
    process_thread_t *user_context_probe = process_thread_create_user(
        runtime_process, 2, runtime_process->image.entry,
        runtime_process->user_stack_top, 4096);
    if (!user_context_probe ||
        user_context_probe->task->context.r12 != (uint64_t)(uintptr_t)runtime_process ||
        user_context_probe->task->context.r13 !=
            (uint64_t)(uintptr_t)&runtime_process->address_space ||
        user_context_probe->task->context.r14 != runtime_process->image.entry ||
        user_context_probe->task->context.r15 != runtime_process->user_stack_top ||
        process_thread_create_user(runtime_process, 3,
                                   runtime_process->user_stack_top,
                                   runtime_process->user_stack_top, 4096) ||
        process_thread_create_user(runtime_process, 4,
                                   runtime_process->image.entry,
                                   1ULL << 48, 4096) ||
        !process_thread_destroy(user_context_probe)) {
        serial_write("user task context failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_thread_t *ring3_probe_thread = process_thread_create_user(
        runtime_process, 5, runtime_process->image.entry,
        runtime_process->user_stack_top, 4096);
    if (!ring3_probe_thread) {
        serial_write("user task transition setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("process lifecycle ready\r\n");
    process_t *signal_process = process_create(2);
    static uint8_t handle_probe_object;
    static uint8_t handle_replacement_object;
    int handle_probe = signal_process ? process_handle_open(&signal_process->handles,
        &handle_probe_object, PROCESS_HANDLE_READ | PROCESS_HANDLE_WRITE) : 0;
    int replacement_handle = 0;
    process_handle_ref_t owned_handle_ref = {0};
    owned_handle_release_count = 0;
    int owned_handle = signal_process ? process_handle_open_owned(
        &signal_process->handles, &handle_probe_object, PROCESS_HANDLE_READ,
        owned_handle_release_probe) : 0;
    uint32_t signal = 0;
    if (!signal_process || process_lookup(2) != signal_process || !handle_probe ||
        !owned_handle ||
        !process_handle_get_retain(&signal_process->handles, (uint32_t)owned_handle,
                                   PROCESS_HANDLE_READ, &owned_handle_ref) ||
        !process_handle_close(&signal_process->handles, (uint32_t)owned_handle) ||
        process_handle_get(&signal_process->handles, (uint32_t)owned_handle,
                           PROCESS_HANDLE_READ) != 0 ||
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
        signal_process->exit_status != 42) {
        serial_write("process signal lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (process_destroy(signal_process) || owned_handle_release_count != 0) {
        serial_write("process handle lifetime failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_handle_release_ref(&owned_handle_ref);
    if (!process_destroy(signal_process) || owned_handle_release_count != 1) {
        serial_write("process signal lifecycle failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (process_lookup(2) != 0) {
        serial_write("process registry reap failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *wait_syscall_process = process_create(6);
    if (!wait_syscall_process ||
        !process_set_parent(wait_syscall_process, runtime_process) ||
        !process_terminate(wait_syscall_process, 11)) {
        serial_write("process wait setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *retained_process = process_create(5);
    process_t *retained_lookup = process_lookup_retain(5);
    if (!retained_process || retained_lookup != retained_process ||
        !process_terminate(retained_process, 9) ||
        !process_destroy(retained_process) || process_lookup_retain(5) != 0) {
        serial_write("process retained lookup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_release(retained_lookup);
    process_t *terminate_process = process_create(3);
    process_thread_t *terminate_thread = terminate_process ?
        process_thread_create(terminate_process, 202, process_thread_probe, 0, 4096) : 0;
    int32_t waited_status = 0;
    if (!terminate_thread || !process_terminate(terminate_process, 7) ||
        terminate_process->thread_count != 0 || terminate_process->state != PROCESS_EXITED ||
        terminate_process->exit_status != 7 ||
        !process_wait(terminate_process, &waited_status) || waited_status != 7 ||
        !process_destroy(terminate_process)) {
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
    process_t *automatic_process = process_create_auto();
    if (!automatic_process || automatic_process->id == 0 ||
        !process_destroy(automatic_process)) {
        serial_write("automatic process allocation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("automatic process allocation ready\r\n");
    process_t *namespace_child = process_create_auto();
    if (!namespace_child || !process_inherit_namespace(namespace_child,
                                                        runtime_process) ||
        namespace_child->root_directory != vfs_root ||
        namespace_child->working_directory != vfs_root ||
        !process_destroy(namespace_child)) {
        serial_write("process namespace inheritance failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("process namespace inheritance ready\r\n");
    static uint32_t inherited_handle_object;
    inherited_handle_retain_count = 0;
    owned_handle_release_count = 0;
    int inherited_handle = process_handle_open_owned_retain(
        &runtime_process->handles, &inherited_handle_object,
        PROCESS_HANDLE_READ, owned_handle_release_probe,
        inherited_handle_retain_probe);
    process_t *handle_child = process_create_auto();
    if (!inherited_handle || !handle_child ||
        !process_inherit_handles(handle_child, runtime_process) ||
        !process_handle_get(&handle_child->handles, (uint32_t)inherited_handle,
                            PROCESS_HANDLE_READ) ||
        inherited_handle_retain_count != 1 ||
        !process_destroy(handle_child) ||
        !process_handle_close(&runtime_process->handles,
                              (uint32_t)inherited_handle) ||
        owned_handle_release_count != 2) {
        serial_write("process handle inheritance failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("process handle inheritance ready\r\n");
    syscall_initialize();
    if (!process_activate(runtime_process)) {
        serial_write("user address space activation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    static uint8_t noninheritable_object;
    int noninheritable_handle = process_handle_open(&runtime_process->handles,
                                                    &noninheritable_object,
                                                    PROCESS_HANDLE_READ);
    process_t *noninheritable_child = process_create_auto();
    if (!noninheritable_handle || !noninheritable_child ||
        syscall_dispatch(OS_SYSCALL_SET_INHERITABLE,
                         (uint64_t)(uint32_t)noninheritable_handle, 0, 0) != 0 ||
        !process_inherit_handles(noninheritable_child, runtime_process) ||
        process_handle_get(&noninheritable_child->handles,
                           (uint32_t)noninheritable_handle,
                           PROCESS_HANDLE_READ) != 0 ||
        !process_destroy(noninheritable_child) ||
        !process_handle_close(&runtime_process->handles,
                              (uint32_t)noninheritable_handle)) {
        serial_write("descriptor inheritance policy failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (syscall_dispatch(OS_SYSCALL_MAP_ANONYMOUS, anonymous_probe_address,
                         2, ADDRESS_SPACE_WRITABLE) != anonymous_probe_address ||
        syscall_dispatch(OS_SYSCALL_PROTECT_MEMORY, anonymous_probe_address,
                         1, ADDRESS_SPACE_EXECUTABLE) != 0 ||
        address_space_user_range_valid(&runtime_process->address_space,
                                       anonymous_probe_address, 0x1000, 1) ||
        syscall_dispatch(OS_SYSCALL_PROTECT_MEMORY, anonymous_probe_address,
                         3, ADDRESS_SPACE_EXECUTABLE) != OS_SYSCALL_ERROR ||
        address_space_user_range_valid(&runtime_process->address_space,
                                       anonymous_probe_address, 0x1000, 1) ||
        syscall_dispatch(OS_SYSCALL_PROTECT_MEMORY, anonymous_probe_address,
                         1, ADDRESS_SPACE_WRITABLE) != 0 ||
        syscall_dispatch(OS_SYSCALL_UNMAP_ANONYMOUS, anonymous_probe_address,
                         2, 0) != 0) {
        serial_write("memory protection syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("memory protection syscall ready\r\n");
    static const char syscall_marker[] = "ok";
    char syscall_copy[sizeof(syscall_marker)] = {0};
    if (!syscall_copy_to_user(0x8000002000ULL, syscall_marker,
                              sizeof(syscall_marker)) ||
        !syscall_copy_from_user(syscall_copy, 0x8000002000ULL,
                                sizeof(syscall_marker)) ||
        !syscall_copy_from_user(0, 0, 0) || !syscall_copy_to_user(0, 0, 0) ||
        syscall_copy_from_user(syscall_copy, 0x8000009fffULL, 2) ||
        syscall_copy_to_user(0x8000009fffULL, syscall_marker, 2) ||
        syscall_copy[0] != 'o' || syscall_copy[1] != 'k' ||
        syscall_copy[2] != '\0' || process_lookup(1) != runtime_process ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND_TO, 1, 3, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND, 0x100000001ULL, 0, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_MASK, 0x100000000ULL, 0, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_SIGNAL_SEND_TO, 1, 0x100000001ULL, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_EXIT, 0x100000000ULL, 0, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_PROCESS_WAIT, 6, 0x8000009fffULL, 0) !=
            OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_PROCESS_WAIT, 6, 0x8000002000ULL, 0) != 0 ||
        !syscall_copy_from_user(&waited_status, 0x8000002000ULL,
                                sizeof(waited_status)) || waited_status != 11 ||
        syscall_dispatch(OS_SYSCALL_PROCESS_WAIT, 1, 0x8000002000ULL, 0) !=
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
        syscall_dispatch(OS_SYSCALL_SIGNAL_NEXT, 0x8000009fffULL, 0, 0) !=
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
    if (!process_destroy(wait_syscall_process)) {
        serial_write("process wait reap failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("signal syscalls ready\r\n");
    static const char file_syscall_path[] = "/write_probe";
    static const char file_syscall_data[] = "file";
    char file_syscall_read[sizeof(file_syscall_data)] = {0};
    uint64_t file_syscall_handle = OS_SYSCALL_ERROR;
    if (!syscall_copy_to_user(0x8000002000ULL, file_syscall_path,
                              sizeof(file_syscall_path)) ||
        !syscall_copy_to_user(0x8000003000ULL, file_syscall_data,
                              sizeof(file_syscall_data) - 1) ||
        (file_syscall_handle = syscall_dispatch(OS_SYSCALL_OPEN,
            0x8000002000ULL, sizeof(file_syscall_path) - 1,
            VFS_FILE_WRITE)) == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_WRITE_FILE, file_syscall_handle,
                         0x8000003000ULL, sizeof(file_syscall_data) - 1) !=
            sizeof(file_syscall_data) - 1 ||
        syscall_dispatch(OS_SYSCALL_TRUNCATE, file_syscall_handle, 2, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_SEEK, file_syscall_handle, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_CLOSE, file_syscall_handle, 0, 0) != 0 ||
        (file_syscall_handle = syscall_dispatch(OS_SYSCALL_OPEN,
            0x8000002000ULL, sizeof(file_syscall_path) - 1,
            VFS_FILE_READ)) == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_READ, file_syscall_handle,
                         0x8000009fffULL, 2) != OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_READ, file_syscall_handle,
                         0x8000004000ULL, 2) != 2 ||
        syscall_dispatch(OS_SYSCALL_CLOSE, file_syscall_handle, 0, 0) != 0 ||
        !syscall_copy_from_user(file_syscall_read, 0x8000004000ULL,
                                sizeof(file_syscall_read) - 1) ||
        file_syscall_read[0] != 'f' || file_syscall_read[1] != 'i' ||
        vfs_write_storage[0] != 'f' || vfs_write_storage[2] != 0) {
        serial_write("file syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("file syscalls ready\r\n");
    static const char create_syscall_path[] = "/user/syscall_create_probe";
    static const char rename_syscall_path[] = "/user/syscall_rename_probe";
    static const char mkdir_syscall_path[] = "/user/syscall_dir_probe";
    static const char rename_target_path[] =
        "/user/syscall_dir_probe/syscall_rename_probe";
    uint64_t create_syscall_handle = OS_SYSCALL_ERROR;
    if (!syscall_copy_to_user(0x8000002000ULL, create_syscall_path,
                              sizeof(create_syscall_path)) ||
        !syscall_copy_to_user(0x8000006000ULL, rename_syscall_path,
                              sizeof(rename_syscall_path)) ||
        !syscall_copy_to_user(0x8000003000ULL, mkdir_syscall_path,
                              sizeof(mkdir_syscall_path)) ||
        !syscall_copy_to_user(0x8000007000ULL, rename_target_path,
                              sizeof(rename_target_path)) ||
        (create_syscall_handle = syscall_dispatch(OS_SYSCALL_CREATE,
            0x8000002000ULL, sizeof(create_syscall_path) - 1,
            VFS_FILE_READ | VFS_FILE_WRITE)) == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_CLOSE, create_syscall_handle, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_MKDIR, 0x8000003000ULL,
                         sizeof(mkdir_syscall_path) - 1, 0755) != 0 ||
        syscall_dispatch(OS_SYSCALL_RENAME,
                         0x8000002000ULL, 0x8000007000ULL,
                         ((uint64_t)(sizeof(rename_target_path) - 1) << 32) |
                         (sizeof(create_syscall_path) - 1)) == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_UNLINK, 0x8000007000ULL,
                         sizeof(rename_target_path) - 1, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_UNLINK, 0x8000003000ULL,
                         sizeof(mkdir_syscall_path) - 1, 0) != OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_RMDIR, 0x8000003000ULL,
                         sizeof(mkdir_syscall_path) - 1, 0) != 0) {
        serial_write("filesystem mutation syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("filesystem rename syscall ready\r\n");
    serial_write("filesystem mutation syscalls ready\r\n");
    if (!syscall_copy_to_user(0x8000002000ULL, file_syscall_path,
                              sizeof(file_syscall_path))) {
        serial_write("filesystem syscall path restore failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (syscall_dispatch(OS_SYSCALL_CHMOD, 0x8000002000ULL,
                         sizeof(file_syscall_path) - 1, 0640) != 0) {
        serial_write("chmod syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    os_syscall_stat_t file_stat = {0};
    if ((file_syscall_handle = syscall_dispatch(OS_SYSCALL_OPEN,
                         0x8000002000ULL, sizeof(file_syscall_path) - 1,
                         VFS_FILE_READ)) == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_FSTAT, file_syscall_handle,
                         0x8000005000ULL, 0) != 0 ||
        !syscall_copy_from_user(&file_stat, 0x8000005000ULL,
                                sizeof(file_stat)) ||
        file_stat.owner_uid != 1000 || file_stat.owner_gid != 1000 ||
        file_stat.mode != 0640 || file_stat.type != VFS_NODE_REGULAR ||
        syscall_dispatch(OS_SYSCALL_CLOSE, file_syscall_handle, 0, 0) != 0) {
        serial_write("fstat syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (syscall_dispatch(OS_SYSCALL_CHMOD, 0x8000002000ULL,
                         sizeof(file_syscall_path) - 1, 0666) != 0) {
        serial_write("chmod syscall restore failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("chmod syscall ready\r\n");
    os_syscall_stat_t path_stat = {0};
    if (syscall_dispatch(OS_SYSCALL_STAT, 0x8000002000ULL,
                         sizeof(file_syscall_path) - 1, 0x8000005000ULL) != 0 ||
        !syscall_copy_from_user(&path_stat, 0x8000005000ULL,
                                sizeof(path_stat)) ||
        path_stat.owner_uid != 1000 || path_stat.owner_gid != 1000 ||
        path_stat.mode != 0666 || path_stat.type != VFS_NODE_REGULAR) {
        serial_write("stat syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (syscall_dispatch(OS_SYSCALL_GETUID, 0, 0, 0) != 1000 ||
        syscall_dispatch(OS_SYSCALL_GETGID, 0, 0, 0) != 1000 ||
        syscall_dispatch(OS_SYSCALL_GETPPID, 0, 0, 0) != 0) {
        serial_write("identity syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("stat and identity syscalls ready\r\n");
    serial_write("fstat syscall ready\r\n");
    uint64_t duplicate_source = syscall_dispatch(OS_SYSCALL_OPEN,
        0x8000002000ULL, sizeof(file_syscall_path) - 1, VFS_FILE_READ);
    uint64_t duplicate_handle = duplicate_source == OS_SYSCALL_ERROR ?
                                OS_SYSCALL_ERROR :
        syscall_dispatch(OS_SYSCALL_DUP, duplicate_source, VFS_FILE_READ, 0);
    uint8_t duplicate_read[sizeof(file_syscall_data)] = {0};
    if (duplicate_source == OS_SYSCALL_ERROR || duplicate_handle == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_CLOSE, duplicate_source, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_READ, (uint32_t)duplicate_handle,
                         0x8000004000ULL, 2) != 2 ||
        !syscall_copy_from_user(duplicate_read, 0x8000004000ULL,
                                sizeof(duplicate_read) - 1) ||
        duplicate_read[0] != 'f' || duplicate_read[1] != 'i' ||
        duplicate_read[2] != 0) {
        serial_write("dup syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("dup syscall ready\r\n");
    uint64_t directory_syscall_handle = syscall_dispatch(OS_SYSCALL_OPEN,
        0x8000002000ULL, 1, VFS_FILE_READ);
    os_syscall_dirent_t syscall_dirent = {0};
    if (directory_syscall_handle == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_READDIR, directory_syscall_handle,
                         0x8000009fffULL, 0) != OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_READDIR, directory_syscall_handle,
                         0x8000005000ULL, 0) != 1 ||
        !syscall_copy_from_user(&syscall_dirent, 0x8000005000ULL,
                                sizeof(syscall_dirent)) ||
        syscall_dirent.type != VFS_NODE_REGULAR || syscall_dirent.name[0] != 'w' ||
        syscall_dispatch(OS_SYSCALL_SEEK, directory_syscall_handle, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_READ, directory_syscall_handle,
                         0x8000004000ULL, 1) != OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_CLOSE, directory_syscall_handle, 0, 0) != 0) {
        serial_write("directory syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("directory syscalls ready\r\n");
    uint64_t channel_syscall_handle = syscall_dispatch(OS_SYSCALL_CHANNEL_CREATE,
                                                        0, 0, 0);
    static const char channel_message[] = "ipc";
    char channel_result[sizeof(channel_message)] = {0};
    if (channel_syscall_handle == OS_SYSCALL_ERROR ||
        !syscall_copy_to_user(0x8000003000ULL, channel_message,
                              sizeof(channel_message) - 1) ||
        syscall_dispatch(OS_SYSCALL_CHANNEL_SEND, channel_syscall_handle,
                         0x8000003000ULL, sizeof(channel_message) - 1) !=
            sizeof(channel_message) - 1 ||
        syscall_dispatch(OS_SYSCALL_CHANNEL_RECEIVE, channel_syscall_handle,
                         0x8000005000ULL, sizeof(channel_result) - 1) !=
            sizeof(channel_message) - 1 ||
        !syscall_copy_from_user(channel_result, 0x8000005000ULL,
                                sizeof(channel_result) - 1) ||
        channel_result[0] != 'i' || channel_result[2] != 'c' ||
        syscall_dispatch(OS_SYSCALL_CLOSE, channel_syscall_handle, 0, 0) != 0) {
        serial_write("IPC syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("IPC syscalls ready\r\n");
    uint64_t blocking_channel_handle = syscall_dispatch(OS_SYSCALL_CHANNEL_CREATE,
                                                         0, 0, 0);
    char blocking_channel_result[sizeof(channel_message)] = {0};
    if (blocking_channel_handle == OS_SYSCALL_ERROR ||
        syscall_dispatch(OS_SYSCALL_CHANNEL_SEND_WAIT, blocking_channel_handle,
                         0x8000003000ULL, sizeof(channel_message) - 1) !=
            sizeof(channel_message) - 1 ||
        syscall_dispatch(OS_SYSCALL_CHANNEL_RECEIVE_WAIT,
                         blocking_channel_handle, 0x8000005000ULL,
                         sizeof(blocking_channel_result) - 1) !=
            sizeof(channel_message) - 1 ||
        !syscall_copy_from_user(blocking_channel_result, 0x8000005000ULL,
                                sizeof(blocking_channel_result) - 1) ||
        blocking_channel_result[0] != 'i' || blocking_channel_result[2] != 'c' ||
        syscall_dispatch(OS_SYSCALL_CLOSE, blocking_channel_handle, 0, 0) != 0) {
        serial_write("blocking IPC syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("blocking IPC syscalls ready\r\n");
    os_syscall_pipe_t pipe_result = {0};
    char pipe_result_data[sizeof(channel_message)] = {0};
    uint64_t pipe_create_status = syscall_dispatch(OS_SYSCALL_PIPE,
                                                    0x8000005000ULL, 0, 0);
    int pipe_copy_ok = syscall_copy_from_user(&pipe_result, 0x8000005000ULL,
                                              sizeof(pipe_result));
    uint64_t pipe_write_status = syscall_dispatch(
        OS_SYSCALL_WRITE_FILE, pipe_result.write_handle,
        0x8000003000ULL, sizeof(channel_message) - 1);
    uint64_t pipe_read_status = syscall_dispatch(
        OS_SYSCALL_READ, pipe_result.read_handle, 0x8000005000ULL,
        sizeof(pipe_result_data) - 1);
    int pipe_data_ok = syscall_copy_from_user(pipe_result_data,
                                               0x8000005000ULL,
                                               sizeof(pipe_result_data) - 1);
    if (pipe_create_status != 0 || !pipe_copy_ok ||
        pipe_write_status != sizeof(channel_message) - 1 ||
        pipe_read_status != sizeof(channel_message) - 1 || !pipe_data_ok ||
        pipe_result_data[0] != 'i' || pipe_result_data[2] != 'c' ||
        syscall_dispatch(OS_SYSCALL_CLOSE, pipe_result.read_handle, 0, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_CLOSE, pipe_result.write_handle, 0, 0) != 0) {
        serial_write("pipe syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("pipe syscalls ready\r\n");
    if (syscall_dispatch(OS_SYSCALL_YIELD, 0, 0, 0) != 0) {
        serial_write("yield syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("yield syscall ready\r\n");
    static const char root_path[] = "/";
    static const char file_path[] = "/write_probe";
    if (!syscall_copy_to_user(0x8000002000ULL, root_path, sizeof(root_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(root_path) - 1, 0) != 0 ||
        !syscall_copy_to_user(0x8000002000ULL, file_path, sizeof(file_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(file_path) - 1, 0) != OS_SYSCALL_ERROR) {
        serial_write("chdir syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("chdir syscall ready\r\n");
    static const char dev_path[] = "/dev";
    char cwd_path[8] = {0};
    if (!syscall_copy_to_user(0x8000002000ULL, dev_path, sizeof(dev_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(dev_path) - 1, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_GETCWD, 0x8000005000ULL,
                         sizeof(cwd_path), 0) != 4 ||
        !syscall_copy_from_user(cwd_path, 0x8000005000ULL,
                                sizeof(cwd_path)) || cwd_path[0] != '/' ||
        cwd_path[1] != 'd' || cwd_path[3] != 'v' || cwd_path[4] != '\0' ||
        !syscall_copy_to_user(0x8000002000ULL, root_path, sizeof(root_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(root_path) - 1, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_GETCWD, 0x8000005000ULL, 2, 0) != 1) {
        serial_write("getcwd syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("getcwd syscall ready\r\n");
    static const char relative_dev_path[] = "dev";
    if (!syscall_copy_to_user(0x8000002000ULL, relative_dev_path,
                              sizeof(relative_dev_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(relative_dev_path) - 1, 0) != 0 ||
        syscall_dispatch(OS_SYSCALL_GETCWD, 0x8000005000ULL,
                         sizeof(cwd_path), 0) != 4 ||
        !syscall_copy_to_user(0x8000002000ULL, root_path, sizeof(root_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(root_path) - 1, 0) != 0) {
        serial_write("relative path syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("relative path syscalls ready\r\n");
    static const char private_path[] = "/private";
    static const char device_path[] = "/dev/pci0";
    if (!syscall_copy_to_user(0x8000002000ULL, private_path, sizeof(private_path)) ||
        syscall_dispatch(OS_SYSCALL_CHDIR, 0x8000002000ULL,
                         sizeof(private_path) - 1, 0) != OS_SYSCALL_ERROR ||
        !syscall_copy_to_user(0x8000002000ULL, device_path, sizeof(device_path)) ||
        syscall_dispatch(OS_SYSCALL_OPEN, 0x8000002000ULL,
                         sizeof(device_path) - 1, VFS_FILE_WRITE) != OS_SYSCALL_ERROR) {
        serial_write("VFS permission syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("VFS permission syscalls ready\r\n");
    if (!process_thread_start(ring3_probe_thread)) {
        serial_write("ring3 thread start failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!scheduler_start()) {
        serial_write("ring3 scheduler return failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (runtime_process->state != PROCESS_EXITED) {
        serial_write("ring3 exit syscall failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (process_current() != 0) {
        serial_write("dead process remained current\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!process_thread_destroy(ring3_probe_thread) ||
        !address_space_activate_kernel() ||
        !process_destroy(runtime_process)) {
        serial_write("ring3 transition failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("ring3 transition ready\r\n");
    if (!info || !info->init_image || !info->init_image_size) {
        serial_write("external userland image missing\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *init_process = process_create(7);
    process_thread_t *init_thread = 0;
    if (!init_process) {
        serial_write("external userland process creation failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!process_load_image(init_process, (const void *)(uintptr_t)info->init_image,
                            info->init_image_size)) {
        serial_write("external userland image load failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!process_map_user_stack(init_process, 0x8000008000ULL) ||
        !process_set_namespace(init_process, vfs_root, vfs_root) ||
        !(init_thread = process_thread_create_user(init_process, 7,
            init_process->image.entry, init_process->user_stack_top, 4096))) {
        serial_write("external userland setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!process_thread_start(init_thread)) {
        serial_write("external userland thread start failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!scheduler_start()) {
        serial_write("external userland scheduler failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (init_process->state != PROCESS_EXITED) {
        serial_write("external userland exit failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!process_thread_destroy(init_thread)) {
        serial_write("external userland thread reap failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!address_space_activate_kernel()) {
        serial_write("external userland kernel address-space failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    if (!process_destroy(init_process)) {
        serial_write("external userland process reap failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("external userland init ready\r\n");
    if (!kernel_init_state_advance(&init_state, KERNEL_INIT_SERVICES))
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    serial_write("userland runtime ready\r\n");
    if (e1000_controller_count() != 0) {
        task_t *network_task = task_create_kernel(500, network_runtime_task,
                                                   &network_runtime, 16384);
        if (!network_task || !scheduler_enqueue(network_task)) {
            serial_write("network runtime task setup failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("network runtime service ready\r\n");
    }
    if (input_runtime_ready) {
        task_t *input_task = task_create_kernel(501, input_runtime_task, 0, 16384);
        if (!input_task || !scheduler_enqueue(input_task)) {
            serial_write("input runtime task setup failure\r\n");
            for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
        }
        serial_write("input runtime service ready\r\n");
    }
    if (e1000_controller_count() != 0 || input_runtime_ready)
        scheduler_enable_preemption(1);
    if (!info->shell_image || !info->shell_image_size) {
        serial_write("external shell image missing\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    process_t *shell_process = process_create(8);
    process_thread_t *shell_thread = 0;
    if (!shell_process ||
        !process_load_image(shell_process, (const void *)(uintptr_t)info->shell_image,
                            info->shell_image_size) ||
        !process_map_user_stack(shell_process, 0x8000030000ULL) ||
        !process_set_namespace(shell_process, vfs_root, vfs_root) ||
        !(shell_thread = process_thread_create_user(shell_process, 8,
            shell_process->image.entry, shell_process->user_stack_top, 4096)) ||
        !process_thread_start(shell_thread)) {
        serial_write("external shell setup failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    serial_write("interactive shell ready\r\n");
    if (!scheduler_start()) {
        serial_write("userland scheduler start failure\r\n");
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    for (;;) {
        __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
}
