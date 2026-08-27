#include "syscall.h"
#include "../../syscall/abi.h"
#include "../../time/clock.h"
#include "../process/process.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../../arch/x86_64/time/timer.h"
#include "../../mm/virtual/address_space.h"
#include "../../fs/vfs/file.h"
#include "../../ipc/endpoint.h"
#include "../../sched/core/scheduler.h"
#include "../printk/serial.h"

extern void arch_syscall_interrupt(void);

void syscall_initialize(void) {
    arch_set_user_interrupt_gate(0x80, arch_syscall_interrupt);
}

static int user_range(uint64_t address, uint64_t size, int writable) {
    return address_space_user_range_valid(address_space_active(), address,
                                           size, writable);
}

static int valid_signal_number(uint64_t value) {
    return value >= 1 && value <= PROCESS_SIGNAL_MAX;
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
            return arg1 > UINT32_MAX ||
                   !process_set_signal_mask(process_current(), (uint32_t)arg1) ?
                   OS_SYSCALL_ERROR : 0;
        case OS_SYSCALL_SIGNAL_SEND:
            return !valid_signal_number(arg1) ||
                   !process_send_signal(process_current(), (uint32_t)arg1) ?
                   OS_SYSCALL_ERROR : 0;
        case OS_SYSCALL_SIGNAL_SEND_TO: {
            process_t *caller = process_current();
            process_t *target = process_lookup_retain(arg1);
            int valid = process_can_signal(caller, target) &&
                        valid_signal_number(arg2) &&
                        process_send_signal(target, (uint32_t)arg2);
            process_release(target);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_PROCESS_WAIT: {
            process_t *caller = process_current();
            process_t *target = process_lookup_retain(arg1);
            int32_t status = 0;
            int valid = caller && target && target != caller &&
                        process_wait(target, &status) &&
                        syscall_copy_to_user(arg2, &status, sizeof(status));
            process_release(target);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_OPEN: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH ||
                arg3 == 0 || (arg3 & ~(VFS_FILE_READ | VFS_FILE_WRITE)) != 0)
                return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            if (!syscall_copy_from_user(path, arg1, arg2)) return OS_SYSCALL_ERROR;
            path[arg2] = '\0';
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            vfs_node_t *root = process->root_directory;
            vfs_node_t *working = process->working_directory;
            if (root) vfs_node_retain(root);
            if (working) vfs_node_retain(working);
            spinlock_unlock_irqrestore(&process->lock, flags);
            vfs_node_t *base = working ? working : root;
            int handle = base ? vfs_file_open_path_handle(&process->handles,
                                                          base, path, (uint32_t)arg3) : 0;
            if (working) vfs_node_release(working);
            if (root) vfs_node_release(root);
            return handle ? (uint64_t)(uint32_t)handle : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_READ:
        case OS_SYSCALL_WRITE_FILE: {
            process_t *process = process_current();
            if (!process || arg3 == 0 || arg3 > OS_SYSCALL_MAX_WRITE) return OS_SYSCALL_ERROR;
            process_handle_ref_t ref = {0};
            uint32_t rights = number == OS_SYSCALL_READ ? PROCESS_HANDLE_READ :
                              PROCESS_HANDLE_WRITE;
            if (!process_handle_get_retain(&process->handles, (uint32_t)arg1,
                                           rights, &ref)) return OS_SYSCALL_ERROR;
            uint8_t buffer[OS_SYSCALL_MAX_WRITE];
            int result = number == OS_SYSCALL_READ ?
                vfs_file_read((vfs_file_t *)ref.object, buffer, (uint32_t)arg3) : 0;
            if (number == OS_SYSCALL_READ && result > 0 &&
                !syscall_copy_to_user(arg2, buffer, (uint32_t)result)) result = 0;
            if (number == OS_SYSCALL_WRITE_FILE) {
                result = syscall_copy_from_user(buffer, arg2, arg3) ?
                    vfs_file_write((vfs_file_t *)ref.object, buffer, (uint32_t)arg3) : 0;
            }
            process_handle_release_ref(&ref);
            return result > 0 ? (uint64_t)(uint32_t)result : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_CLOSE:
            return process_current() && process_handle_close(&process_current()->handles,
                                                             (uint32_t)arg1) ? 0 :
                   OS_SYSCALL_ERROR;
        case OS_SYSCALL_SEEK: {
            process_t *process = process_current();
            process_handle_ref_t ref = {0};
            if (!process || !process_handle_get_retain(&process->handles,
                    (uint32_t)arg1, PROCESS_HANDLE_READ, &ref) ||
                !vfs_file_seek((vfs_file_t *)ref.object, arg2)) {
                process_handle_release_ref(&ref);
                return OS_SYSCALL_ERROR;
            }
            process_handle_release_ref(&ref);
            return 0;
        }
        case OS_SYSCALL_READDIR: {
            process_t *process = process_current();
            process_handle_ref_t ref = {0};
            vfs_dirent_t entry = {0};
            if (!process || !process_handle_get_retain(&process->handles,
                    (uint32_t)arg1, PROCESS_HANDLE_READ, &ref) ||
                !vfs_file_readdir((vfs_file_t *)ref.object, &entry) ||
                !syscall_copy_to_user(arg2, &entry, sizeof(entry))) {
                process_handle_release_ref(&ref);
                return OS_SYSCALL_ERROR;
            }
            process_handle_release_ref(&ref);
            return 1;
        }
        case OS_SYSCALL_EXIT:
            if (arg1 > UINT32_MAX) return OS_SYSCALL_ERROR;
            process_exit_current((int32_t)(uint32_t)arg1);
        case OS_SYSCALL_CHANNEL_CREATE: {
            process_t *process = process_current();
            ipc_endpoint_t *endpoint = ipc_endpoint_create();
            if (!process || !endpoint) {
                if (endpoint) ipc_endpoint_release(endpoint);
                return OS_SYSCALL_ERROR;
            }
            int handle = process_handle_open_owned(&process->handles, endpoint,
                PROCESS_HANDLE_READ | PROCESS_HANDLE_WRITE,
                (process_handle_release_fn)ipc_endpoint_release);
            if (!handle) ipc_endpoint_release(endpoint);
            return handle ? (uint64_t)(uint32_t)handle : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_CHANNEL_SEND:
        case OS_SYSCALL_CHANNEL_RECEIVE:
        case OS_SYSCALL_CHANNEL_SEND_WAIT:
        case OS_SYSCALL_CHANNEL_RECEIVE_WAIT: {
            process_t *process = process_current();
            if (!process || arg3 == 0 || arg3 > OS_SYSCALL_MAX_MESSAGE) return OS_SYSCALL_ERROR;
            process_handle_ref_t ref = {0};
            uint32_t rights = (number == OS_SYSCALL_CHANNEL_SEND ||
                               number == OS_SYSCALL_CHANNEL_SEND_WAIT) ?
                              PROCESS_HANDLE_WRITE : PROCESS_HANDLE_READ;
            if (!process_handle_get_retain(&process->handles, (uint32_t)arg1,
                                           rights, &ref)) return OS_SYSCALL_ERROR;
            uint8_t buffer[OS_SYSCALL_MAX_MESSAGE];
            int result = 0;
            if (number == OS_SYSCALL_CHANNEL_SEND ||
                number == OS_SYSCALL_CHANNEL_SEND_WAIT) {
                if (syscall_copy_from_user(buffer, arg2, arg3))
                    result = ((number == OS_SYSCALL_CHANNEL_SEND_WAIT ?
                               ipc_endpoint_send_wait : ipc_endpoint_send)(
                               (ipc_endpoint_t *)ref.object, process->id, buffer,
                               (uint32_t)arg3)) ?
                             (int)arg3 : 0;
            } else if (user_range(arg2, arg3, 1)) {
                uint32_t received = 0;
                int received_ok = number == OS_SYSCALL_CHANNEL_RECEIVE_WAIT ?
                    ipc_endpoint_receive_wait((ipc_endpoint_t *)ref.object,
                                              process->id, buffer, (uint32_t)arg3,
                                              &received) :
                    ipc_endpoint_receive((ipc_endpoint_t *)ref.object,
                                          process->id, buffer, (uint32_t)arg3,
                                          &received);
                if (received_ok &&
                    syscall_copy_to_user(arg2, buffer, received))
                    result = (int)received;
            }
            process_handle_release_ref(&ref);
            return result > 0 ? (uint64_t)(uint32_t)result : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_YIELD:
            scheduler_yield();
            return 0;
        case OS_SYSCALL_CHDIR: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH) return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            if (!syscall_copy_from_user(path, arg1, arg2) || path[0] != '/')
                return OS_SYSCALL_ERROR;
            path[arg2] = '\0';
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            vfs_node_t *root = process->root_directory;
            if (root) vfs_node_retain(root);
            spinlock_unlock_irqrestore(&process->lock, flags);
            vfs_node_t *directory = root ? vfs_lookup_path(root, path) : 0;
            if (root) vfs_node_release(root);
            int valid = directory && process_set_working_directory(process, directory);
            if (directory) vfs_node_release(directory);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_SIGNAL_NEXT: {
            uint32_t signal = 0;
            if (!user_range(arg1, sizeof(signal), 1) ||
                !process_take_signal(process_current(), &signal) ||
                !syscall_copy_to_user(arg1, &signal, sizeof(signal))) return OS_SYSCALL_ERROR;
            return signal;
        }
        default:
            return OS_SYSCALL_ERROR;
    }
}
