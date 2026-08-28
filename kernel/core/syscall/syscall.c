#include "syscall.h"
#include "../../syscall/abi.h"
#include "../../time/clock.h"
#include "../process/process.h"
#include "../../arch/x86_64/cpu/tables.h"
#include "../../arch/x86_64/time/timer.h"
#include "../../mm/virtual/address_space.h"
#include "../../mm/heap/heap.h"
#include "../../fs/vfs/file.h"
#include "../../ipc/endpoint.h"
#include "../../ipc/pipe.h"
#include "../../sched/core/scheduler.h"
#include "../printk/serial.h"
#include "../../drivers/input/input.h"
#include "../process/thread.h"

#define SYSCALL_SPAWN_MAX_IMAGE (1024U * 1024U)

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

static vfs_node_t *syscall_parent(process_t *process, char *path,
                                  uint64_t length, char leaf[32],
                                  security_context_t *security) {
    if (!process || !path || !leaf || !security || length == 0 ||
        length > OS_SYSCALL_MAX_PATH) return 0;
    for (uint32_t i = 0; i < length; ++i)
        if (path[i] == '\0') return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    vfs_node_t *root = process->root_directory;
    vfs_node_t *working = process->working_directory;
    *security = process->security;
    if (root) vfs_node_retain(root);
    if (working) vfs_node_retain(working);
    spinlock_unlock_irqrestore(&process->lock, flags);
    if (!root || !working) {
        if (working) vfs_node_release(working);
        if (root) vfs_node_release(root);
        return 0;
    }
    uint32_t slash = UINT32_MAX;
    for (uint32_t i = 0; i < length; ++i)
        if (path[i] == '/') slash = i;
    uint32_t leaf_start = slash == UINT32_MAX ? 0 : slash + 1U;
    uint32_t leaf_length = (uint32_t)length - leaf_start;
    if (leaf_length == 0 || leaf_length >= 32U ||
        (leaf_length == 1 && path[leaf_start] == '.') ||
        (leaf_length == 2 && path[leaf_start] == '.' &&
         path[leaf_start + 1U] == '.')) {
        vfs_node_release(working); vfs_node_release(root); return 0;
    }
    for (uint32_t i = 0; i < leaf_length; ++i) leaf[i] = path[leaf_start + i];
    leaf[leaf_length] = '\0';
    if (slash == UINT32_MAX) {
        path[0] = '.'; path[1] = '\0';
    } else if (slash == 0) {
        path[0] = '/'; path[1] = '\0';
    } else {
        path[slash] = '\0';
    }
    vfs_node_t *parent = vfs_lookup_path_at_access(root, working, path, security);
    if (!parent || !vfs_node_access(parent, security, 3)) {
        if (parent) vfs_node_release(parent);
        vfs_node_release(working); vfs_node_release(root); return 0;
    }
    vfs_node_release(working); vfs_node_release(root);
    return parent;
}

int syscall_copy_from_user(void *destination, uint64_t source, uint64_t size) {
    return address_space_copy_from_user(destination, source, size);
}

int syscall_copy_to_user(uint64_t destination, const void *source, uint64_t size) {
    return address_space_copy_to_user(destination, source, size);
}

static int syscall_copy_path(char *destination, uint64_t source,
                             uint64_t length) {
    if (!destination || length == 0 || length > OS_SYSCALL_MAX_PATH ||
        !syscall_copy_from_user(destination, source, length)) return 0;
    for (uint32_t i = 0; i < length; ++i)
        if (destination[i] == '\0') return 0;
    destination[length] = '\0';
    return 1;
}

static int syscall_copy_string(char *destination, uint64_t source,
                               uint32_t capacity) {
    if (!destination || capacity < 2U) return 0;
    for (uint32_t index = 0; index < capacity; ++index) {
        if (!syscall_copy_from_user(&destination[index], source + index, 1))
            return 0;
        if (destination[index] == '\0') return 1;
    }
    return 0;
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
        case OS_SYSCALL_PROCESS_LIST: {
            if (arg2 == 0 || arg2 > PROCESS_MAX || arg3 != 0 ||
                arg2 > UINT64_MAX / sizeof(uint64_t) ||
                !user_range(arg1, arg2 * sizeof(uint64_t), 1))
                return OS_SYSCALL_ERROR;
            uint64_t ids[PROCESS_MAX];
            uint32_t count = process_snapshot(ids, (uint32_t)arg2);
            return syscall_copy_to_user(arg1, ids,
                                        count * sizeof(uint64_t)) ? count :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_PROCESS_STATUS: {
            process_t *target = process_lookup_retain(arg1);
            if (!target || !user_range(arg2, sizeof(os_syscall_process_info_t), 1)) {
                process_release(target);
                return OS_SYSCALL_ERROR;
            }
            uint64_t flags = spinlock_lock_irqsave(&target->lock);
            os_syscall_process_info_t info = {
                target->id, target->parent ? target->parent->id : 0,
                (uint32_t)target->state, target->exit_status
            };
            spinlock_unlock_irqrestore(&target->lock, flags);
            process_release(target);
            return syscall_copy_to_user(arg2, &info, sizeof(info)) ? 0 :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_GETUID:
        case OS_SYSCALL_GETGID: {
            process_t *process = process_current();
            if (!process) return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            uint64_t value = number == OS_SYSCALL_GETUID ?
                             process->security.uid : process->security.gid;
            spinlock_unlock_irqrestore(&process->lock, flags);
            return value;
        }
        case OS_SYSCALL_GETPPID: {
            process_t *process = process_current();
            if (!process) return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            uint64_t value = process->parent ? process->parent->id : 0;
            spinlock_unlock_irqrestore(&process->lock, flags);
            return value;
        }
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
            if (!user_range(arg2, sizeof(int32_t), 1)) return OS_SYSCALL_ERROR;
            process_t *target = process_lookup_retain(arg1);
            int32_t status = 0;
            int valid = caller && target && target != caller &&
                        process_wait_child(caller, target, &status) &&
                        process_activate(caller) &&
                        syscall_copy_to_user(arg2, &status, sizeof(status));
            process_release(target);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_PROCESS_REAP: {
            process_t *parent = process_current();
            process_t *target = process_lookup_retain(arg1);
            int valid = parent && target && parent != target;
            if (valid) {
                uint64_t flags = spinlock_lock_irqsave(&target->lock);
                valid = target->parent == parent &&
                        target->state == PROCESS_EXITED;
                spinlock_unlock_irqrestore(&target->lock, flags);
            }
            if (valid) valid = process_destroy(target);
            process_release(target);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_GETENV: {
            process_t *process = process_current();
            if (!process || arg1 == 0 || arg2 == 0 || arg3 == 0 ||
                arg3 > PROCESS_ENVIRONMENT_SIZE || !user_range(arg2, arg3, 1))
                return OS_SYSCALL_ERROR;
            char key[33];
            char value[PROCESS_ENVIRONMENT_SIZE];
            if (!syscall_copy_string(key, arg1, sizeof(key)))
                return OS_SYSCALL_ERROR;
            uint32_t key_length = 0;
            while (key[key_length]) ++key_length;
            int length = process_environment_get(process, key, key_length,
                                                 value, (uint32_t)arg3);
            return length >= 0 && syscall_copy_to_user(arg2, value,
                                                        (uint32_t)length + 1U) ?
                   (uint64_t)length : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_SETENV: {
            process_t *process = process_current();
            char key[33];
            char value[PROCESS_ENVIRONMENT_SIZE];
            if (!process || arg1 == 0 || arg2 == 0 ||
                !syscall_copy_string(key, arg1, sizeof(key)) ||
                !syscall_copy_string(value, arg2, sizeof(value)))
                return OS_SYSCALL_ERROR;
            uint32_t key_length = 0;
            uint32_t value_length = 0;
            while (key[key_length]) ++key_length;
            while (value[value_length]) ++value_length;
            return process_environment_set(process, key, key_length, value,
                                           value_length) ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_UNSETENV: {
            process_t *process = process_current();
            char key[33];
            if (!process || arg1 == 0 ||
                !syscall_copy_string(key, arg1, sizeof(key)))
                return OS_SYSCALL_ERROR;
            uint32_t key_length = 0;
            while (key[key_length]) ++key_length;
            return process_environment_unset(process, key, key_length) ?
                   0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_SPAWN: {
            process_t *parent = process_current();
            if (!parent || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH)
                return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            if (!syscall_copy_path(path, arg1, arg2)) return OS_SYSCALL_ERROR;
            char arguments[257] = {0};
            if (arg3 && !syscall_copy_string(arguments, arg3, sizeof(arguments)))
                return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&parent->lock);
            vfs_node_t *root = parent->root_directory;
            vfs_node_t *working = parent->working_directory;
            security_context_t security = parent->security;
            if (root) vfs_node_retain(root);
            if (working) vfs_node_retain(working);
            spinlock_unlock_irqrestore(&parent->lock, flags);
            vfs_node_t *node = root && working ?
                vfs_lookup_path_at_access(root, working, path, &security) : 0;
            int accessible = node && node->type == VFS_NODE_REGULAR &&
                              vfs_node_access(node, &security, 4U);
            vfs_file_t *file = accessible ? vfs_file_open(node, VFS_FILE_READ) : 0;
            if (node) vfs_node_release(node);
            if (working) vfs_node_release(working);
            if (root) vfs_node_release(root);
            if (!file) return OS_SYSCALL_ERROR;
            uint8_t *image = (uint8_t *)kmalloc(SYSCALL_SPAWN_MAX_IMAGE);
            uint32_t image_size = 0;
            if (image) {
                while (image_size < SYSCALL_SPAWN_MAX_IMAGE) {
                    uint32_t capacity = SYSCALL_SPAWN_MAX_IMAGE - image_size;
                    if (capacity > 4096U) capacity = 4096U;
                    int count = vfs_file_read(file, image + image_size, capacity);
                    if (count <= 0) break;
                    image_size += (uint32_t)count;
                }
            }
            vfs_file_release(file);
            if (!image || image_size == 0 || image_size == SYSCALL_SPAWN_MAX_IMAGE) {
                if (image) kfree(image);
                return OS_SYSCALL_ERROR;
            }
            process_t *child = process_create_auto();
            process_thread_t *thread = 0;
            uint64_t stack_pointer = 0;
            int valid = child && process_set_parent(child, parent) &&
                        process_inherit_namespace(child, parent) &&
                        process_inherit_handles(child, parent) &&
                        process_inherit_environment(child, parent) &&
                        process_load_image(child, image, image_size) &&
                        process_map_user_stack(child, 0x8000100000ULL +
                            (child->id * 0x10000ULL)) &&
                        process_prepare_user_stack(child, path, arguments,
                                                   &stack_pointer) &&
                        (thread = process_thread_create_user(child, (uint32_t)child->id,
                            child->image.entry, stack_pointer, 4096)) != 0 &&
                        process_thread_start(thread);
            kfree(image);
            if (!valid) {
                if (child) (void)process_destroy(child);
                return OS_SYSCALL_ERROR;
            }
            return child->id;
        }
        case OS_SYSCALL_OPEN: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH ||
                arg3 == 0 || (arg3 & ~(VFS_FILE_READ | VFS_FILE_WRITE)) != 0)
                return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            if (!syscall_copy_path(path, arg1, arg2)) return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            vfs_node_t *root = process->root_directory;
            vfs_node_t *working = process->working_directory;
            security_context_t security = process->security;
            if (root) vfs_node_retain(root);
            if (working) vfs_node_retain(working);
            spinlock_unlock_irqrestore(&process->lock, flags);
            vfs_node_t *node = root && working ?
                vfs_lookup_path_at_access(root, working, path, &security) : 0;
            uint32_t requested = (arg3 & VFS_FILE_READ ? 4U : 0U) |
                                 (arg3 & VFS_FILE_WRITE ? 2U : 0U);
            int access = node && vfs_node_access(node, &security, requested);
            int handle = access ? vfs_file_open_handle(&process->handles, node,
                                                        (uint32_t)arg3) : 0;
            if (node) vfs_node_release(node);
            if (working) vfs_node_release(working);
            if (root) vfs_node_release(root);
            return handle ? (uint64_t)(uint32_t)handle : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_READ:
        case OS_SYSCALL_WRITE_FILE: {
            process_t *process = process_current();
            if (!process || arg3 == 0 || arg3 > OS_SYSCALL_MAX_WRITE) return OS_SYSCALL_ERROR;
            if (number == OS_SYSCALL_READ && arg1 == 0) {
                uint8_t input[OS_SYSCALL_MAX_WRITE];
                uint32_t count = input_read_standard(input, (uint32_t)arg3);
                return count != 0 && syscall_copy_to_user(arg2, input, count) ? count : 0;
            }
            if (number == OS_SYSCALL_READ && !user_range(arg2, arg3, 1))
                return OS_SYSCALL_ERROR;
            process_handle_ref_t ref = {0};
            uint32_t rights = number == OS_SYSCALL_READ ? PROCESS_HANDLE_READ :
                              PROCESS_HANDLE_WRITE;
            if (!process_handle_get_retain(&process->handles, (uint32_t)arg1,
                                           rights, &ref)) return OS_SYSCALL_ERROR;
            uint8_t buffer[OS_SYSCALL_MAX_WRITE];
            int result = number == OS_SYSCALL_READ ?
                (ref.kind == PROCESS_HANDLE_OBJECT_PIPE_READ ?
                 pipe_endpoint_read((pipe_endpoint_t *)ref.object, buffer,
                                    (uint32_t)arg3) :
                 vfs_file_read((vfs_file_t *)ref.object, buffer, (uint32_t)arg3)) : 0;
            if (number == OS_SYSCALL_READ && result > 0 &&
                !syscall_copy_to_user(arg2, buffer, (uint32_t)result)) result = 0;
            if (number == OS_SYSCALL_WRITE_FILE) {
                result = syscall_copy_from_user(buffer, arg2, arg3) ?
                    (ref.kind == PROCESS_HANDLE_OBJECT_PIPE_WRITE ?
                     pipe_endpoint_write((pipe_endpoint_t *)ref.object, buffer,
                                          (uint32_t)arg3) :
                     vfs_file_write((vfs_file_t *)ref.object, buffer,
                                    (uint32_t)arg3)) : 0;
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
            int retained = process &&
                (process_handle_get_retain(&process->handles, (uint32_t)arg1,
                                           PROCESS_HANDLE_READ, &ref) ||
                 process_handle_get_retain(&process->handles, (uint32_t)arg1,
                                           PROCESS_HANDLE_WRITE, &ref));
            if (!retained ||
                !vfs_file_seek((vfs_file_t *)ref.object, arg2)) {
                process_handle_release_ref(&ref);
                return OS_SYSCALL_ERROR;
            }
            process_handle_release_ref(&ref);
            return 0;
        }
        case OS_SYSCALL_TRUNCATE: {
            process_t *process = process_current();
            process_handle_ref_t ref = {0};
            int valid = process && process_handle_get_retain(&process->handles,
                         (uint32_t)arg1, PROCESS_HANDLE_WRITE, &ref) &&
                        arg2 <= UINT32_MAX &&
                        vfs_file_truncate((vfs_file_t *)ref.object,
                                          (uint32_t)arg2);
            process_handle_release_ref(&ref);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_READDIR: {
            process_t *process = process_current();
            process_handle_ref_t ref = {0};
            vfs_dirent_t entry = {0};
            if (!process || !user_range(arg2, sizeof(entry), 1) ||
                !process_handle_get_retain(&process->handles,
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
        case OS_SYSCALL_PIPE: {
            process_t *process = process_current();
            os_syscall_pipe_t result = {0};
            pipe_endpoint_t *reader = 0;
            pipe_endpoint_t *writer = 0;
            if (!process || !user_range(arg1, sizeof(result), 1) ||
                !pipe_create(&reader, &writer)) return OS_SYSCALL_ERROR;
            int read_handle = process_handle_open_owned_kind(
                &process->handles, reader, PROCESS_HANDLE_READ,
                (process_handle_release_fn)pipe_endpoint_release,
                (process_handle_retain_fn)pipe_endpoint_retain,
                PROCESS_HANDLE_OBJECT_PIPE_READ);
            int write_handle = read_handle ? process_handle_open_owned_kind(
                &process->handles, writer, PROCESS_HANDLE_WRITE,
                (process_handle_release_fn)pipe_endpoint_release,
                (process_handle_retain_fn)pipe_endpoint_retain,
                PROCESS_HANDLE_OBJECT_PIPE_WRITE) : 0;
            if (!read_handle || !write_handle) {
                if (read_handle) (void)process_handle_close(&process->handles,
                                                             (uint32_t)read_handle);
                else pipe_endpoint_release(reader);
                if (write_handle) (void)process_handle_close(&process->handles,
                                                              (uint32_t)write_handle);
                else pipe_endpoint_release(writer);
                return OS_SYSCALL_ERROR;
            }
            result.read_handle = (uint32_t)read_handle;
            result.write_handle = (uint32_t)write_handle;
            if (!syscall_copy_to_user(arg1, &result, sizeof(result))) {
                (void)process_handle_close(&process->handles, result.read_handle);
                (void)process_handle_close(&process->handles, result.write_handle);
                return OS_SYSCALL_ERROR;
            }
            return 0;
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
            if (!syscall_copy_path(path, arg1, arg2) || path[0] == '\0')
                return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            vfs_node_t *root = process->root_directory;
            security_context_t security = process->security;
            if (root) vfs_node_retain(root);
            spinlock_unlock_irqrestore(&process->lock, flags);
            vfs_node_t *directory = root ?
                vfs_lookup_path_at_access(root, root, path, &security) : 0;
            int accessible = directory && directory->type == VFS_NODE_DIRECTORY &&
                             vfs_node_access(directory, &security, 1);
            if (root) vfs_node_release(root);
            int valid = accessible && process_set_working_directory(process, directory);
            if (directory) vfs_node_release(directory);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_GETCWD: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH) return OS_SYSCALL_ERROR;
            vfs_node_t *root = 0;
            vfs_node_t *current = 0;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            if (process->root_directory) {
                root = process->root_directory;
                current = process->working_directory;
                vfs_node_retain(root);
                if (current) vfs_node_retain(current);
            }
            spinlock_unlock_irqrestore(&process->lock, flags);
            if (!root || !current) {
                if (root) vfs_node_release(root);
                if (current) vfs_node_release(current);
                return OS_SYSCALL_ERROR;
            }
            char path[OS_SYSCALL_MAX_PATH + 1];
            uint32_t end = sizeof(path);
            path[--end] = '\0';
            while (current != root) {
                char name[sizeof(current->name)];
                vfs_node_t *parent = 0;
                uint64_t node_flags = spinlock_lock_irqsave(&current->lock);
                parent = current->parent;
                if (parent) vfs_node_retain(parent);
                for (uint32_t i = 0; i < sizeof(name); ++i) name[i] = current->name[i];
                spinlock_unlock_irqrestore(&current->lock, node_flags);
                if (!parent) {
                    vfs_node_release(current);
                    vfs_node_release(root);
                    return OS_SYSCALL_ERROR;
                }
                uint32_t length = 0;
                while (length < sizeof(name) && name[length] != '\0') ++length;
                if (length == 0 || end < length + 1) {
                    vfs_node_release(parent);
                    vfs_node_release(current);
                    vfs_node_release(root);
                    return OS_SYSCALL_ERROR;
                }
                end -= length;
                for (uint32_t i = 0; i < length; ++i) path[end + i] = name[i];
                path[--end] = '/';
                vfs_node_release(current);
                current = parent;
            }
            if (end == sizeof(path) - 1) path[--end] = '/';
            uint32_t length = sizeof(path) - end;
            int valid = length <= arg2 && syscall_copy_to_user(arg1, &path[end], length);
            vfs_node_release(current);
            vfs_node_release(root);
            return valid ? length - 1 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_FSTAT: {
            process_t *process = process_current();
            if (!process || !user_range(arg2, sizeof(os_syscall_stat_t), 1))
                return OS_SYSCALL_ERROR;
            process_handle_ref_t ref = {0};
            vfs_stat_t stat = {0};
            int valid = process_handle_get_retain(&process->handles,
                                                  (uint32_t)arg1, 0, &ref) &&
                        vfs_file_stat((vfs_file_t *)ref.object, &stat);
            if (valid) {
                os_syscall_stat_t result = {stat.owner_uid, stat.owner_gid,
                                            stat.mode, (uint32_t)stat.type};
                valid = syscall_copy_to_user(arg2, &result, sizeof(result));
            }
            process_handle_release_ref(&ref);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_CHMOD: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH ||
                (arg3 & ~0777U) != 0) return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            if (!syscall_copy_path(path, arg1, arg2)) return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            vfs_node_t *root = process->root_directory;
            vfs_node_t *working = process->working_directory;
            security_context_t security = process->security;
            if (root) vfs_node_retain(root);
            if (working) vfs_node_retain(working);
            spinlock_unlock_irqrestore(&process->lock, flags);
            vfs_node_t *node = root && working ?
                vfs_lookup_path_at_access(root, working, path, &security) : 0;
            int valid = node && vfs_node_set_mode(node, &security, (uint32_t)arg3);
            if (node) vfs_node_release(node);
            if (working) vfs_node_release(working);
            if (root) vfs_node_release(root);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_STAT: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH ||
                !user_range(arg3, sizeof(os_syscall_stat_t), 1))
                return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            if (!syscall_copy_path(path, arg1, arg2)) return OS_SYSCALL_ERROR;
            uint64_t flags = spinlock_lock_irqsave(&process->lock);
            vfs_node_t *root = process->root_directory;
            vfs_node_t *working = process->working_directory;
            security_context_t security = process->security;
            if (root) vfs_node_retain(root);
            if (working) vfs_node_retain(working);
            spinlock_unlock_irqrestore(&process->lock, flags);
            vfs_node_t *node = root && working ?
                vfs_lookup_path_at_access(root, working, path, &security) : 0;
            vfs_stat_t stat = {0};
            int valid = node && vfs_node_stat(node, &stat);
            if (valid) {
                os_syscall_stat_t result = {stat.owner_uid, stat.owner_gid,
                                            stat.mode, (uint32_t)stat.type};
                valid = syscall_copy_to_user(arg3, &result, sizeof(result));
            }
            if (node) vfs_node_release(node);
            if (working) vfs_node_release(working);
            if (root) vfs_node_release(root);
            return valid ? 0 : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_DUP: {
            process_t *process = process_current();
            if (!process || arg2 > 7U) return OS_SYSCALL_ERROR;
            int handle = process_handle_duplicate(&process->handles,
                                                  (uint32_t)arg1,
                                                  (uint32_t)arg2);
            return handle ? (uint64_t)(uint32_t)handle : OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_MAP_ANONYMOUS: {
            process_t *process = process_current();
            if (!process || arg1 < (1ULL << 39) ||
                (arg1 & 0xfffULL) != 0 || arg2 == 0 ||
                arg2 > ADDRESS_SPACE_MAX_ANONYMOUS_PAGES ||
                (arg3 & ~(ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_EXECUTABLE)) != 0)
                return OS_SYSCALL_ERROR;
            return address_space_map_anonymous(&process->address_space, arg1,
                                               (uint32_t)arg2, arg3) ? arg1 :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_UNMAP_ANONYMOUS: {
            process_t *process = process_current();
            if (!process || arg1 < (1ULL << 39) ||
                (arg1 & 0xfffULL) != 0 || arg2 == 0 ||
                arg2 > ADDRESS_SPACE_MAX_ANONYMOUS_PAGES)
                return OS_SYSCALL_ERROR;
            return address_space_unmap_anonymous(&process->address_space, arg1,
                                                 (uint32_t)arg2) ? 0 :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_PROTECT_MEMORY: {
            process_t *process = process_current();
            if (!process || arg1 < (1ULL << 39) ||
                (arg1 & 0xfffULL) != 0 || arg2 == 0 ||
                arg2 > ADDRESS_SPACE_MAX_ANONYMOUS_PAGES ||
                (arg3 & ~(ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_EXECUTABLE)) != 0)
                return OS_SYSCALL_ERROR;
            return address_space_protect_range(&process->address_space, arg1,
                                               (uint32_t)arg2, arg3) ? 0 :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_SET_INHERITABLE: {
            process_t *process = process_current();
            if (!process || arg2 > 1) return OS_SYSCALL_ERROR;
            return process_handle_set_inheritable(&process->handles,
                                                  (uint32_t)arg1,
                                                  (int)arg2) ? 0 :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_CREATE:
        case OS_SYSCALL_MKDIR:
        case OS_SYSCALL_UNLINK:
        case OS_SYSCALL_RMDIR: {
            process_t *process = process_current();
            if (!process || arg2 == 0 || arg2 > OS_SYSCALL_MAX_PATH ||
                (number == OS_SYSCALL_CREATE &&
                 (arg3 == 0 || (arg3 & ~(VFS_FILE_READ | VFS_FILE_WRITE)) != 0)) ||
                (number == OS_SYSCALL_MKDIR && (arg3 & ~0777U) != 0) ||
                ((number == OS_SYSCALL_UNLINK || number == OS_SYSCALL_RMDIR) &&
                 arg3 != 0))
                return OS_SYSCALL_ERROR;
            char path[OS_SYSCALL_MAX_PATH + 1];
            char leaf[32];
            security_context_t security;
            if (!syscall_copy_path(path, arg1, arg2)) return OS_SYSCALL_ERROR;
            vfs_node_t *parent = syscall_parent(process, path, arg2, leaf, &security);
            if (!parent) return OS_SYSCALL_ERROR;
            if (number == OS_SYSCALL_UNLINK) {
                vfs_node_t *child = vfs_node_lookup(parent, leaf);
                int result = child && child->type == VFS_NODE_REGULAR &&
                             vfs_node_remove(parent, child);
                if (child) vfs_node_release(child);
                vfs_node_release(parent);
                return result ? 0 : OS_SYSCALL_ERROR;
            }
            if (number == OS_SYSCALL_RMDIR) {
                vfs_node_t *child = vfs_node_lookup(parent, leaf);
                int result = child && child->type == VFS_NODE_DIRECTORY &&
                             vfs_node_remove(parent, child);
                if (child) vfs_node_release(child);
                vfs_node_release(parent);
                return result ? 0 : OS_SYSCALL_ERROR;
            }
            vfs_node_type_t type = number == OS_SYSCALL_MKDIR ?
                VFS_NODE_DIRECTORY : VFS_NODE_REGULAR;
            uint32_t mode = number == OS_SYSCALL_MKDIR ? (uint32_t)arg3 : 0666U;
            vfs_node_t *node = vfs_node_create(leaf, type, security.uid,
                                                security.gid, mode);
            int result = node && vfs_node_add_child(parent, node);
            if (result && number == OS_SYSCALL_CREATE) {
                result = vfs_file_open_handle(&process->handles, node,
                                              (uint32_t)arg3);
                if (!result) (void)vfs_node_remove(parent, node);
            }
            if (node) vfs_node_release(node);
            vfs_node_release(parent);
            return result ? (number == OS_SYSCALL_CREATE ?
                             (uint64_t)(uint32_t)result : 0) :
                   OS_SYSCALL_ERROR;
        }
        case OS_SYSCALL_RENAME: {
            process_t *process = process_current();
            uint32_t old_length = (uint32_t)arg3;
            uint32_t new_length = (uint32_t)(arg3 >> 32);
            if (!process || old_length == 0 || old_length > OS_SYSCALL_MAX_PATH ||
                new_length == 0 || new_length > OS_SYSCALL_MAX_PATH)
                return OS_SYSCALL_ERROR;
            char old_path[OS_SYSCALL_MAX_PATH + 1];
            char new_path[OS_SYSCALL_MAX_PATH + 1];
            char old_leaf[32], new_leaf[32];
            security_context_t old_security, new_security;
            if (!syscall_copy_path(old_path, arg1, old_length) ||
                !syscall_copy_path(new_path, arg2, new_length))
                return OS_SYSCALL_ERROR;
            vfs_node_t *old_parent = syscall_parent(process, old_path, old_length,
                                                     old_leaf, &old_security);
            vfs_node_t *new_parent = syscall_parent(process, new_path, new_length,
                                                     new_leaf, &new_security);
            if (!old_parent || !new_parent) {
                if (new_parent) vfs_node_release(new_parent);
                if (old_parent) vfs_node_release(old_parent);
                return OS_SYSCALL_ERROR;
            }
            vfs_node_t *child = vfs_node_lookup(old_parent, old_leaf);
            int result = child && (old_parent == new_parent ?
                         vfs_node_rename(old_parent, child, new_leaf) :
                         vfs_node_move(old_parent, new_parent, child, new_leaf));
            if (child) vfs_node_release(child);
            vfs_node_release(new_parent); vfs_node_release(old_parent);
            return result ? 0 : OS_SYSCALL_ERROR;
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
