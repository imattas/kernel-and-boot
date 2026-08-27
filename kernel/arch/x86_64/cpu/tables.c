#include <stdint.h>
#include "tables.h"

struct descriptor_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed, aligned(16)));
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2, reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

extern void arch_default_interrupt(void);
extern void arch_load_idt(const void *idt_ptr);
extern void arch_load_gdt(const void *gdt_ptr);
typedef void (*interrupt_stub_t)(void);
#define EXTERN_EXCEPTION(n) extern void arch_exception_stub_##n(void)
EXTERN_EXCEPTION(0); EXTERN_EXCEPTION(1); EXTERN_EXCEPTION(2); EXTERN_EXCEPTION(3);
EXTERN_EXCEPTION(4); EXTERN_EXCEPTION(5); EXTERN_EXCEPTION(6); EXTERN_EXCEPTION(7);
EXTERN_EXCEPTION(8); EXTERN_EXCEPTION(9); EXTERN_EXCEPTION(10); EXTERN_EXCEPTION(11);
EXTERN_EXCEPTION(12); EXTERN_EXCEPTION(13); EXTERN_EXCEPTION(14); EXTERN_EXCEPTION(15);
EXTERN_EXCEPTION(16); EXTERN_EXCEPTION(17); EXTERN_EXCEPTION(18); EXTERN_EXCEPTION(19);
EXTERN_EXCEPTION(20); EXTERN_EXCEPTION(21); EXTERN_EXCEPTION(22); EXTERN_EXCEPTION(23);
EXTERN_EXCEPTION(24); EXTERN_EXCEPTION(25); EXTERN_EXCEPTION(26); EXTERN_EXCEPTION(27);
EXTERN_EXCEPTION(28); EXTERN_EXCEPTION(29); EXTERN_EXCEPTION(30); EXTERN_EXCEPTION(31);
static struct idt_gate idt[64][256] __attribute__((aligned(16)));
static uint8_t idt_initialized[64];
static uint64_t gdt[64][9] __attribute__((aligned(8)));
static struct tss64 tss[64] __attribute__((aligned(16)));
static uint8_t tss_stacks[64][16384] __attribute__((aligned(16)));

#define TSS_SELECTOR 0x38

static void set_gate_with_attributes(struct idt_gate *gate, uintptr_t address,
                                     uint8_t attributes, uint16_t selector) {
    gate->offset_low = (uint16_t)address;
    gate->selector = selector;
    gate->ist = 0;
    gate->type_attributes = attributes;
    gate->offset_middle = (uint16_t)(address >> 16);
    gate->offset_high = (uint32_t)(address >> 32);
    gate->reserved = 0;
}

static void set_gate(struct idt_gate *gate, uintptr_t address, uint16_t selector) {
    set_gate_with_attributes(gate, address, 0x8e, selector);
}

static void initialize_gdt(uint32_t logical_id) {
    struct tss64 *cpu_tss = &tss[logical_id];
    uint8_t *tss_bytes = (uint8_t *)cpu_tss;
    for (uint64_t i = 0; i < sizeof(*cpu_tss); ++i) tss_bytes[i] = 0;
    cpu_tss->rsp0 = (uint64_t)(uintptr_t)&tss_stacks[logical_id][sizeof(tss_stacks[0])];
    cpu_tss->iomap_base = sizeof(*cpu_tss);
    gdt[logical_id][0] = 0;
    gdt[logical_id][1] = 0x00af9a000000ffffULL;
    gdt[logical_id][2] = 0x00cf92000000ffffULL;
    gdt[logical_id][3] = 0x00af9a000000ffffULL;
    gdt[logical_id][4] = 0x00cf92000000ffffULL;
    gdt[logical_id][5] = 0x00affa000000ffffULL;
    gdt[logical_id][6] = 0x00cff2000000ffffULL;
    uint64_t base = (uint64_t)(uintptr_t)cpu_tss;
    uint64_t limit = sizeof(*cpu_tss) - 1;
    gdt[logical_id][7] = (limit & 0xffff) | ((base & 0xffffff) << 16) |
                         (0x89ULL << 40) | ((limit & 0xf0000) << 32) |
                         (((base >> 24) & 0xff) << 56);
    gdt[logical_id][8] = base >> 32;
    struct descriptor_ptr gdt_ptr;
    gdt_ptr.limit = (uint16_t)(sizeof(gdt[0]) - 1);
    gdt_ptr.base = (uint64_t)(uintptr_t)&gdt[logical_id][0];
    arch_load_gdt(&gdt_ptr);
    __asm__ volatile ("ltr %0" :: "r"((uint16_t)TSS_SELECTOR) : "memory");
}

void arch_init_tables_for_cpu(uint32_t logical_id) {
    if (logical_id >= 64) return;
    struct idt_gate *table = idt[logical_id];
    static const interrupt_stub_t exceptions[32] = {
        arch_exception_stub_0, arch_exception_stub_1, arch_exception_stub_2, arch_exception_stub_3,
        arch_exception_stub_4, arch_exception_stub_5, arch_exception_stub_6, arch_exception_stub_7,
        arch_exception_stub_8, arch_exception_stub_9, arch_exception_stub_10, arch_exception_stub_11,
        arch_exception_stub_12, arch_exception_stub_13, arch_exception_stub_14, arch_exception_stub_15,
        arch_exception_stub_16, arch_exception_stub_17, arch_exception_stub_18, arch_exception_stub_19,
        arch_exception_stub_20, arch_exception_stub_21, arch_exception_stub_22, arch_exception_stub_23,
        arch_exception_stub_24, arch_exception_stub_25, arch_exception_stub_26, arch_exception_stub_27,
        arch_exception_stub_28, arch_exception_stub_29, arch_exception_stub_30, arch_exception_stub_31,
    };
    uint16_t selector = logical_id == 0 ? 0x08 : 0x18;
    for (uint64_t i = 0; i < 256; ++i)
        set_gate(&table[i], (uintptr_t)arch_default_interrupt, selector);
    for (uint64_t i = 0; i < 32; ++i)
        set_gate(&table[i], (uintptr_t)exceptions[i], selector);
    idt_initialized[logical_id] = 1;
    struct descriptor_ptr idt_ptr;
    idt_ptr.limit = (uint16_t)(sizeof(idt[0]) - 1);
    idt_ptr.base = (uint64_t)(uintptr_t)&table[0];
    arch_load_idt(&idt_ptr);
    /* Every CPU needs its own GDT and TSS after leaving the AP trampoline. */
    initialize_gdt(logical_id);
}

void arch_init_tables(void) { arch_init_tables_for_cpu(0); }

void arch_set_interrupt_gate(uint8_t vector, void (*handler)(void)) {
    for (uint32_t cpu = 0; cpu < 64; ++cpu) {
        if (idt_initialized[cpu])
            set_gate(&idt[cpu][vector], (uintptr_t)handler,
                     cpu == 0 ? 0x08 : 0x18);
    }
}

void arch_set_user_interrupt_gate(uint8_t vector, void (*handler)(void)) {
    for (uint32_t cpu = 0; cpu < 64; ++cpu) {
        if (idt_initialized[cpu])
            set_gate_with_attributes(&idt[cpu][vector], (uintptr_t)handler, 0xee,
                                     cpu == 0 ? 0x08 : 0x18);
    }
}

void arch_set_kernel_stack(uint32_t logical_id, uint64_t stack_top) {
    if (logical_id >= 64 || stack_top < 4096 || (stack_top & 0xfULL) != 0)
        return;
    tss[logical_id].rsp0 = stack_top;
}

uint16_t arch_user_code_selector(void) { return 5 * 8 + 3; }
uint16_t arch_user_data_selector(void) { return 6 * 8 + 3; }
