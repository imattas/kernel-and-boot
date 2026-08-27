#include "syscall.h"
#include "../../syscall/abi.h"
#include "../../time/clock.h"
#include "../process/process.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../../arch/x86_64/time/timer.h"
#include "../../mm/virtual/address_space.h"
#include "../printk/serial.h"

extern void arch_syscall_interrupt(void);

void syscall_initialize(void) {
    arch_set_user_interrupt_gate(0x80, arch_syscall_interrupt);
}

static int user_range(uint64_t address, uint64_t size, int writable) {
    return address_space_user_range_valid(address_space_active(), address,
                                           size, writable);
}

int syscall_copy_from_user(void *destination, uint64_t source, uint64_t size) {
    if (size == 0) return 1;
    if (!destination || !user_range(source, size, 0)) return 0;
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)(uintptr_t)source;
    for (uint64_t i = 0; i < size; ++i) out[i] = in[i];
    return 1;
}

int syscall_copy_to_user(uint64_t destination, const void *source, uint64_t size) {
    if (size == 0) return 1;
    if (!source || !user_range(destination, size, 1)) return 0;
    uint8_t *out = (uint8_t *)(uintptr_t)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint64_t i = 0; i < size; ++i) out[i] = in[i];
    return 1;
}

uint64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3) {
    switch (number) {
        case OS_SYSCALL_DEBUG:
            serial_write("syscall handled\r\n");
            return 0;
        case OS_SYSCALL_WRITE: {
            if (arg1 != 1 || arg3 == 0 || arg3 > OS_SYSCALL_MAX_WRITE) return OS_SYSCALL_ERROR;
            char buffer[257];
            if (!syscall_copy_from_user(buffer, arg2, arg3)) return OS_SYSCALL_ERROR;
            buffer[arg3] = '\0';
            serial_write(buffer);
            return arg3;
        }
        case OS_SYSCALL_CLOCK_MONOTONIC:
            return clock_monotonic_ns();
        case OS_SYSCALL_GETPID:
            return process_current() ? process_current()->id : OS_SYSCALL_ERROR;
        case OS_SYSCALL_SIGNAL_MASK:
            return process_set_signal_mask(process_current(), (uint32_t)arg1) ? 0 : OS_SYSCALL_ERROR;
        case OS_SYSCALL_SIGNAL_SEND:
            return process_send_signal(process_current(), (uint32_t)arg1) ? 0 : OS_SYSCALL_ERROR;
        case OS_SYSCALL_SIGNAL_SEND_TO: {
            process_t *target = process_lookup(arg1);
            return process_send_signal(target, (uint32_t)arg2) ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_SIGNAL_NEXT: {
            uint32_t signal = 0;
            if (!process_take_signal(process_current(), &signal) ||
                !syscall_copy_to_user(arg1, &signal, sizeof(signal))) return OS_SYSCALL_ERROR;
            return signal;
        }
        default:
            return OS_SYSCALL_ERROR;
    }
}
