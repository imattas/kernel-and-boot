#include "process.h"
#include "thread.h"
#include "../../mm/heap/heap.h"
#include "../../mm/physical/frame.h"
#include "../../exec/exec.h"
#include "handle.h"
#include "../sync/spinlock.h"
#include "../../sched/core/scheduler.h"

#define PAGE_SIZE 0x1000ULL

static process_t *current_process;
static process_t *process_table[PROCESS_MAX];
static spinlock_t process_table_lock;

static int process_retain_namespace(vfs_node_t *root,
                                    vfs_node_t *working_directory) {
    if (!root || !working_directory || !vfs_node_retain(root)) return 0;
    if (!vfs_node_retain(working_directory)) {
        vfs_node_release(root);
        return 0;
    }
    return 1;
}

static int process_retain_existing(process_t *process) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    int retained = 0;
    for (uint32_t i = 0; i < PROCESS_MAX; ++i)
        if (process_table[i] == process) {
            if (process->references != UINT32_MAX) {
                ++process->references;
                retained = 1;
            }
            break;
        }
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    return retained;
}

static void wake_all_signal_waiters(process_t *process) {
    while (scheduler_wake_one(&process->signal_waiters)) { }
}

static void process_reparent_children(process_t *parent) {
    if (!parent) return;
    process_t *children[PROCESS_MAX];
    uint32_t child_count = 0;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    for (uint32_t index = 0; index < PROCESS_MAX; ++index) {
        process_t *candidate = process_table[index];
        if (!candidate || candidate == parent ||
            candidate->references == UINT32_MAX || child_count == PROCESS_MAX)
            continue;
        ++candidate->references;
        children[child_count++] = candidate;
    }
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    for (uint32_t index = 0; index < child_count; ++index) {
        process_t *child = children[index];
        process_t *replacement = parent->id == 7 ? 0 : process_lookup_retain(7);
        int orphaned = 0;
        int adopted = 0;
        if (replacement && replacement != parent) {
            uint64_t replacement_flags = spinlock_lock_irqsave(&replacement->lock);
            adopted = replacement->state != PROCESS_EXITED;
            spinlock_unlock_irqrestore(&replacement->lock, replacement_flags);
        }
        flags = spinlock_lock_irqsave(&child->lock);
        if (child->parent == parent) {
            if (adopted) child->parent = replacement;
            else child->parent = 0;
            orphaned = 1;
        }
        spinlock_unlock_irqrestore(&child->lock, flags);
        if (orphaned) {
            process_release(parent);
            if (adopted) replacement = 0;
        }
        if (replacement) process_release(replacement);
        process_release(child);
    }
}

__attribute__((noreturn)) void process_exit_current(int32_t status) {
    process_t *process = process_current();
    task_t *task = scheduler_current();
    if (!process || !task) {
        for (;;) __asm__ volatile ("cli\n\t hlt" ::: "memory");
    }
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        scheduler_task_exit();
    }
    process->exit_status = status;
    process->state = PROCESS_EXITED;
    spinlock_unlock_irqrestore(&process->lock, flags);
    process_reparent_children(process);
    flags = spinlock_lock_irqsave(&process_table_lock);
    if (current_process == process)
        current_process = process->parent;
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    wake_all_signal_waiters(process);
    while (scheduler_wake_one(&process->exit_waiters)) { }
    scheduler_task_exit();
}

int process_initialize(void) {
    spinlock_init(&process_table_lock);
    for (uint32_t i = 0; i < PROCESS_MAX; ++i) process_table[i] = 0;
    current_process = 0;
    return 1;
}

process_t *process_create(uint64_t id) {
    if (id == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    for (uint32_t i = 0; i < PROCESS_MAX; ++i)
        if (process_table[i] && process_table[i]->id == id) {
            spinlock_unlock_irqrestore(&process_table_lock, flags);
            return 0;
        }
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    process_t *process = (process_t *)kmalloc(sizeof(*process));
    if (!process) return 0;
    spinlock_init(&process->lock);
    process->references = 1;
    process->id = id;
    process->parent = 0;
    process->state = PROCESS_NEW;
    process->address_space.root = 0;
    process->address_space.owned_count = 0;
    process->address_space.anonymous_count = 0;
    process->image.entry = 0;
    process->image.page_count = 0;
    security_context_initialize(&process->security, 1000, 1000, 0);
    for (uint32_t index = 0; index < PROCESS_ENVIRONMENT_SIZE; ++index)
        process->environment[index] = 0;
    const char default_environment[] = "PATH=/:/bin:/usr/bin";
    for (uint32_t index = 0; index < sizeof(default_environment); ++index)
        process->environment[index] = default_environment[index];
    for (uint32_t page = 0; page < PROCESS_USER_STACK_PAGES; ++page)
        process->user_stack_pages[page] = 0;
    process->user_stack_page_count = 0;
    process->user_stack_top = 0;
    process->pending_signals = 0;
    process->blocked_signals = 0;
    task_wait_queue_initialize(&process->signal_waiters);
    task_wait_queue_initialize(&process->exit_waiters);
    process->exit_status = 0;
    process_handle_table_initialize(&process->handles);
    process->standard_input_handle = 0;
    process->standard_output_handle = 0;
    process->root_directory = 0;
    process->working_directory = 0;
    process->threads = 0;
    process->thread_count = 0;
    process->retained_thread_references = 0;
    if (!address_space_create(&process->address_space)) {
        kfree(process);
        return 0;
    }
    flags = spinlock_lock_irqsave(&process_table_lock);
    for (uint32_t i = 0; i < PROCESS_MAX; ++i) {
        if (process_table[i] && process_table[i]->id == id) {
            spinlock_unlock_irqrestore(&process_table_lock, flags);
            address_space_destroy(&process->address_space);
            kfree(process);
            return 0;
        }
        if (!process_table[i]) {
            process_table[i] = process;
            spinlock_unlock_irqrestore(&process_table_lock, flags);
            return process;
        }
    }
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    address_space_destroy(&process->address_space);
    kfree(process);
    return 0;
}

process_t *process_create_auto(void) {
    for (uint32_t attempt = 0; attempt < PROCESS_MAX; ++attempt) {
        uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
        uint32_t candidate = 0;
        for (uint32_t id = 1; id <= PROCESS_MAX; ++id) {
            uint8_t used = 0;
            for (uint32_t index = 0; index < PROCESS_MAX; ++index)
                if (process_table[index] && process_table[index]->id == id) {
                    used = 1;
                    break;
                }
            if (!used) {
                candidate = id;
                break;
            }
        }
        spinlock_unlock_irqrestore(&process_table_lock, flags);
        if (!candidate) return 0;
        process_t *process = process_create(candidate);
        if (process) return process;
    }
    return 0;
}

uint32_t process_snapshot(uint64_t *ids, uint32_t capacity) {
    if (!ids || capacity == 0) return 0;
    uint32_t count = 0;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    for (uint32_t index = 0; index < PROCESS_MAX && count < capacity; ++index)
        if (process_table[index]) ids[count++] = process_table[index]->id;
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    return count;
}

int process_set_namespace(process_t *process, vfs_node_t *root,
                          vfs_node_t *working_directory) {
    if (!process || !root || !working_directory ||
        root->type != VFS_NODE_DIRECTORY ||
        working_directory->type != VFS_NODE_DIRECTORY) return 0;
    if (!process_retain_namespace(root, working_directory)) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        vfs_node_release(working_directory);
        vfs_node_release(root);
        return 0;
    }
    vfs_node_t *old_root = process->root_directory;
    vfs_node_t *old_working = process->working_directory;
    process->root_directory = root;
    process->working_directory = working_directory;
    spinlock_unlock_irqrestore(&process->lock, flags);
    if (old_working) vfs_node_release(old_working);
    if (old_root) vfs_node_release(old_root);
    return 1;
}

int process_inherit_namespace(process_t *child, process_t *parent) {
    if (!child || !parent || child == parent) return 0;
    vfs_node_t *root;
    vfs_node_t *working;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    if (parent->state == PROCESS_EXITED || !parent->root_directory ||
        !parent->working_directory) {
        spinlock_unlock_irqrestore(&parent->lock, flags);
        return 0;
    }
    root = parent->root_directory;
    working = parent->working_directory;
    if (!process_retain_namespace(root, working)) {
        spinlock_unlock_irqrestore(&parent->lock, flags);
        return 0;
    }
    spinlock_unlock_irqrestore(&parent->lock, flags);

    flags = spinlock_lock_irqsave(&child->lock);
    if (child->state != PROCESS_NEW) {
        spinlock_unlock_irqrestore(&child->lock, flags);
        vfs_node_release(working);
        vfs_node_release(root);
        return 0;
    }
    child->root_directory = root;
    child->working_directory = working;
    spinlock_unlock_irqrestore(&child->lock, flags);
    return 1;
}

int process_inherit_handles(process_t *child, process_t *parent) {
    if (!child || !parent || child == parent) return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    int parent_live = parent->state != PROCESS_EXITED;
    spinlock_unlock_irqrestore(&parent->lock, flags);
    if (!parent_live) return 0;
    flags = spinlock_lock_irqsave(&child->lock);
    int child_new = child->state == PROCESS_NEW;
    spinlock_unlock_irqrestore(&child->lock, flags);
    if (!child_new || !process_handle_table_inherit(&child->handles,
                                                    &parent->handles)) return 0;
    flags = spinlock_lock_irqsave(&parent->lock);
    uint32_t input = parent->standard_input_handle;
    uint32_t output = parent->standard_output_handle;
    spinlock_unlock_irqrestore(&parent->lock, flags);
    flags = spinlock_lock_irqsave(&child->lock);
    child->standard_input_handle = input;
    child->standard_output_handle = output;
    spinlock_unlock_irqrestore(&child->lock, flags);
    return 1;
}

int process_set_standard_handles(process_t *process, uint32_t input_handle,
                                 uint32_t output_handle) {
    if (!process || (input_handle &&
        !process_handle_get(&process->handles, input_handle,
                            PROCESS_HANDLE_READ)) ||
        (output_handle &&
         !process_handle_get(&process->handles, output_handle,
                             PROCESS_HANDLE_WRITE))) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    process->standard_input_handle = input_handle;
    process->standard_output_handle = output_handle;
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int process_inherit_environment(process_t *child, process_t *parent) {
    if (!child || !parent || child == parent) return 0;
    char environment[PROCESS_ENVIRONMENT_SIZE];
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    if (parent->state == PROCESS_EXITED) {
        spinlock_unlock_irqrestore(&parent->lock, flags);
        return 0;
    }
    for (uint32_t index = 0; index < sizeof(environment); ++index)
        environment[index] = parent->environment[index];
    spinlock_unlock_irqrestore(&parent->lock, flags);
    flags = spinlock_lock_irqsave(&child->lock);
    if (child->state != PROCESS_NEW) {
        spinlock_unlock_irqrestore(&child->lock, flags);
        return 0;
    }
    for (uint32_t index = 0; index < sizeof(environment); ++index)
        child->environment[index] = environment[index];
    spinlock_unlock_irqrestore(&child->lock, flags);
    return 1;
}

static int environment_key_matches(const char *entry, uint32_t entry_length,
                                   const char *key, uint32_t key_length) {
    uint32_t separator = 0;
    while (separator < entry_length && entry[separator] != '=') ++separator;
    if (separator != key_length) return 0;
    for (uint32_t index = 0; index < key_length; ++index)
        if (entry[index] != key[index]) return 0;
    return 1;
}

int process_environment_get(process_t *process, const char *key,
                            uint32_t key_length, char *value,
                            uint32_t capacity) {
    if (!process || !key || !value || key_length == 0 || capacity == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    uint32_t offset = 0;
    while (offset < sizeof(process->environment) &&
           process->environment[offset]) {
        uint32_t end = offset;
        while (end < sizeof(process->environment) &&
               process->environment[end]) ++end;
        if (environment_key_matches(&process->environment[offset],
                                     end - offset, key, key_length)) {
            uint32_t separator = offset;
            while (separator < end && process->environment[separator] != '=')
                ++separator;
            if (separator == end) break;
            uint32_t length = end - separator - 1U;
            if (length + 1U > capacity) break;
            for (uint32_t index = 0; index <= length; ++index)
                value[index] = process->environment[separator + 1U + index];
            spinlock_unlock_irqrestore(&process->lock, flags);
            return (int)length;
        }
        offset = end + 1U;
    }
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 0;
}

int process_environment_set(process_t *process, const char *key,
                            uint32_t key_length, const char *value,
                            uint32_t value_length) {
    if (!process || !key || !value || key_length == 0 || key_length > 32U ||
        value_length > PROCESS_ENVIRONMENT_SIZE - 2U) return 0;
    for (uint32_t index = 0; index < key_length; ++index)
        if (key[index] == '=' || key[index] == 0) return 0;
    char updated[PROCESS_ENVIRONMENT_SIZE] = {0};
    uint32_t output = 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    uint32_t offset = 0;
    while (offset < sizeof(process->environment) &&
           process->environment[offset]) {
        uint32_t end = offset;
        while (end < sizeof(process->environment) &&
               process->environment[end]) ++end;
        if (!environment_key_matches(&process->environment[offset],
                                     end - offset, key, key_length)) {
            uint32_t length = end - offset;
            if (output + length + 1U >= sizeof(updated)) {
                spinlock_unlock_irqrestore(&process->lock, flags);
                return 0;
            }
            for (uint32_t index = 0; index <= length; ++index)
                updated[output + index] = process->environment[offset + index];
            output += length + 1U;
        }
        offset = end + 1U;
    }
    uint32_t entry_length = key_length + 1U + value_length;
    if (output + entry_length + 1U >= sizeof(updated)) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    for (uint32_t index = 0; index < key_length; ++index)
        updated[output + index] = key[index];
    updated[output + key_length] = '=';
    for (uint32_t index = 0; index < value_length; ++index)
        updated[output + key_length + 1U + index] = value[index];
    output += entry_length + 1U;
    for (uint32_t index = 0; index < sizeof(updated); ++index)
        process->environment[index] = updated[index];
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int process_environment_unset(process_t *process, const char *key,
                              uint32_t key_length) {
    if (!process || !key || key_length == 0 || key_length > 32U) return 0;
    char updated[PROCESS_ENVIRONMENT_SIZE] = {0};
    uint32_t output = 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    uint32_t offset = 0;
    while (offset < sizeof(process->environment) &&
           process->environment[offset]) {
        uint32_t end = offset;
        while (end < sizeof(process->environment) &&
               process->environment[end]) ++end;
        if (!environment_key_matches(&process->environment[offset],
                                     end - offset, key, key_length)) {
            uint32_t length = end - offset;
            if (output + length + 1U >= sizeof(updated)) {
                spinlock_unlock_irqrestore(&process->lock, flags);
                return 0;
            }
            for (uint32_t index = 0; index <= length; ++index)
                updated[output + index] = process->environment[offset + index];
            output += length + 1U;
        }
        offset = end + 1U;
    }
    for (uint32_t index = 0; index < sizeof(updated); ++index)
        process->environment[index] = updated[index];
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int process_set_working_directory(process_t *process, vfs_node_t *directory) {
    if (!process || !directory || directory->type != VFS_NODE_DIRECTORY) return 0;
    if (!vfs_node_retain(directory)) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED || !process->root_directory) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        vfs_node_release(directory);
        return 0;
    }
    vfs_node_t *old = process->working_directory;
    process->working_directory = directory;
    spinlock_unlock_irqrestore(&process->lock, flags);
    if (old) vfs_node_release(old);
    return 1;
}

int process_set_parent(process_t *child, process_t *parent) {
    if (!child || !parent || child == parent || !process_retain_existing(parent))
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&parent->lock);
    int parent_live = parent->state != PROCESS_EXITED;
    spinlock_unlock_irqrestore(&parent->lock, flags);
    flags = spinlock_lock_irqsave(&child->lock);
    int available = parent_live && child->state == PROCESS_NEW &&
                    child->parent == 0;
    if (available) child->parent = parent;
    spinlock_unlock_irqrestore(&child->lock, flags);
    if (!available) process_release(parent);
    return available;
}

process_t *process_create_user(uint64_t id, const void *image,
                               uint64_t image_size, uint64_t stack_base,
                               uint32_t thread_id, uint64_t kernel_stack_size) {
    if (thread_id == 0 || kernel_stack_size < PAGE_SIZE) return 0;
    process_t *process = process_create(id);
    if (!process || !process_load_image(process, image, image_size) ||
        !process_map_user_stack(process, stack_base) ||
        !process_thread_create_user(process, thread_id, process->image.entry,
                                    process->user_stack_top, kernel_stack_size)) {
        if (process) process_destroy(process);
        return 0;
    }
    return process;
}

process_t *process_clone_user(process_t *parent, uint64_t id,
                               uint32_t thread_id, uint64_t kernel_stack_size) {
    if (!parent || id == 0 || thread_id == 0 || kernel_stack_size < PAGE_SIZE)
        return 0;
    uint64_t stack_frames[PROCESS_USER_STACK_PAGES];
    uint64_t stack_top = 0;
    security_context_t security;
    vfs_node_t *root = 0;
    vfs_node_t *working = 0;
    address_space_t cloned_space = {0};
    user_image_t cloned_image = {0};
    uint64_t parent_flags = spinlock_lock_irqsave(&parent->lock);
    if (parent->state == PROCESS_EXITED ||
        parent->user_stack_page_count != PROCESS_USER_STACK_PAGES ||
        parent->image.page_count == 0) {
        spinlock_unlock_irqrestore(&parent->lock, parent_flags);
        return 0;
    }
    for (uint32_t page = 0; page < PROCESS_USER_STACK_PAGES; ++page)
        stack_frames[page] = parent->user_stack_pages[page];
    stack_top = parent->user_stack_top;
    security = parent->security;
    root = parent->root_directory;
    working = parent->working_directory;
    if (!root || !working) {
        spinlock_unlock_irqrestore(&parent->lock, parent_flags);
        return 0;
    }
    if (!process_retain_namespace(root, working)) {
        spinlock_unlock_irqrestore(&parent->lock, parent_flags);
        return 0;
    }
    if (!process_retain_existing(parent)) {
        spinlock_unlock_irqrestore(&parent->lock, parent_flags);
        vfs_node_release(working);
        vfs_node_release(root);
        return 0;
    }
    process_t *child = process_create(id);
    if (child) child->parent = parent;
    if (!child || !user_image_clone(&cloned_space, &parent->image, &cloned_image) ||
        !address_space_clone_anonymous(&cloned_space, &parent->address_space)) {
        spinlock_unlock_irqrestore(&parent->lock, parent_flags);
        vfs_node_release(working);
        vfs_node_release(root);
        if (cloned_image.page_count != 0)
            user_image_destroy(&cloned_space, &cloned_image);
        else if (cloned_space.root != 0)
            (void)address_space_destroy(&cloned_space);
        if (child) (void)process_destroy(child);
        else process_release(parent);
        return 0;
    }
    spinlock_unlock_irqrestore(&parent->lock, parent_flags);
    if (!address_space_destroy(&child->address_space)) {
        user_image_destroy(&cloned_space, &cloned_image);
        vfs_node_release(working);
        vfs_node_release(root);
        (void)process_destroy(child);
        return 0;
    }
    child->address_space = cloned_space;
    child->image = cloned_image;
    child->security = security;
    child->root_directory = root;
    child->working_directory = working;
    for (uint32_t page = 0; page < PROCESS_USER_STACK_PAGES; ++page) {
        uint64_t frame = physical_alloc_frame();
        uint64_t virtual_page = stack_top -
            (uint64_t)(PROCESS_USER_STACK_PAGES - page) * PAGE_SIZE;
        if (!frame || !address_space_map_page(&child->address_space, virtual_page,
                                               frame, ADDRESS_SPACE_WRITABLE |
                                               ADDRESS_SPACE_USER)) {
            if (frame) physical_free_frame(frame);
            (void)process_destroy(child);
            return 0;
        }
        volatile uint8_t *from = (volatile uint8_t *)(uintptr_t)stack_frames[page];
        volatile uint8_t *to = (volatile uint8_t *)(uintptr_t)frame;
        for (uint32_t byte = 0; byte < PAGE_SIZE; ++byte) to[byte] = from[byte];
        child->user_stack_pages[page] = frame;
    }
    child->user_stack_page_count = PROCESS_USER_STACK_PAGES;
    child->user_stack_top = stack_top;
    if (!process_inherit_handles(child, parent)) {
        (void)process_destroy(child);
        return 0;
    }
    child->state = PROCESS_READY;
    if (!process_thread_create_user(child, thread_id, child->image.entry,
                                    child->user_stack_top, kernel_stack_size)) {
        (void)process_destroy(child);
        return 0;
    }
    return child;
}

process_t *process_lookup(uint64_t id) {
    if (id == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    process_t *result = 0;
    for (uint32_t i = 0; i < PROCESS_MAX; ++i)
        if (process_table[i] && process_table[i]->id == id) { result = process_table[i]; break; }
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    return result;
}

process_t *process_lookup_retain(uint64_t id) {
    if (id == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    process_t *result = 0;
    for (uint32_t i = 0; i < PROCESS_MAX; ++i)
        if (process_table[i] && process_table[i]->id == id) {
            if (process_table[i]->references == UINT32_MAX) break;
            ++process_table[i]->references;
            result = process_table[i];
            break;
        }
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    return result;
}

void process_release(process_t *process) {
    if (!process) return;
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    if (process->references == 0) {
        spinlock_unlock_irqrestore(&process_table_lock, flags);
        return;
    }
    int free_process = --process->references == 0;
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    if (free_process) kfree(process);
}

int process_load_image(process_t *process, const void *image, uint64_t size) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state != PROCESS_NEW ||
        !exec_load_image(&process->address_space, image, size, &process->image)) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    process->state = PROCESS_READY;
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int process_map_user_stack(process_t *process, uint64_t page_address) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state != PROCESS_READY ||
        (page_address & (PAGE_SIZE - 1)) != 0 ||
        page_address < (1ULL << 39) ||
        page_address >= (1ULL << 48) -
                       PROCESS_USER_STACK_PAGES * PAGE_SIZE ||
        process->user_stack_page_count != 0) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    for (uint32_t page = 0; page < PROCESS_USER_STACK_PAGES; ++page) {
        uint64_t frame = physical_alloc_frame();
        uint64_t address = page_address + (uint64_t)page * PAGE_SIZE;
        if (!frame || !address_space_map_page(&process->address_space, address,
                                              frame, ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_USER)) {
            if (frame) physical_free_frame(frame);
            while (page != 0) {
                --page;
                address_space_unmap_page(&process->address_space,
                                         page_address + (uint64_t)page * PAGE_SIZE);
                physical_free_frame(process->user_stack_pages[page]);
                process->user_stack_pages[page] = 0;
            }
            spinlock_unlock_irqrestore(&process->lock, flags);
            return 0;
        }
        process->user_stack_pages[page] = frame;
    }
    process->user_stack_page_count = PROCESS_USER_STACK_PAGES;
    process->user_stack_top = page_address +
                              PROCESS_USER_STACK_PAGES * PAGE_SIZE;
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 1;
}

static int process_user_stack_write(const process_t *process, uint64_t address,
                                    const void *source, uint32_t size) {
    if (!process || !source || address < process->user_stack_top -
        PROCESS_USER_STACK_PAGES * PAGE_SIZE ||
        address > process->user_stack_top ||
        size > process->user_stack_top - address) return 0;
    const uint8_t *from = (const uint8_t *)source;
    uint64_t base = process->user_stack_top - PROCESS_USER_STACK_PAGES * PAGE_SIZE;
    for (uint32_t copied = 0; copied < size;) {
        uint64_t offset = address + copied - base;
        uint32_t page = (uint32_t)(offset / PAGE_SIZE);
        uint32_t in_page = (uint32_t)(offset % PAGE_SIZE);
        uint32_t count = PAGE_SIZE - in_page;
        if (count > size - copied) count = size - copied;
        volatile uint8_t *to = (volatile uint8_t *)(uintptr_t)
            (process->user_stack_pages[page] + in_page);
        for (uint32_t index = 0; index < count; ++index)
            to[index] = from[copied + index];
        copied += count;
    }
    return 1;
}

typedef struct {
    uint32_t environment_starts[16];
    uint32_t environment_lengths[16];
    char argument_tokens[8][257];
    uint64_t argument_addresses[8];
    uint64_t environment_addresses[16];
    uint64_t values[32];
} process_stack_build_t;

int process_prepare_user_stack(process_t *process, const char *path,
                               const char *arguments, uint64_t *stack_pointer) {
    if (!process || !path || !arguments || !stack_pointer ||
        process->user_stack_page_count != PROCESS_USER_STACK_PAGES) return 0;
    uint32_t path_length = 0;
    uint32_t argument_length = 0;
    while (path[path_length] && path_length < 256U) ++path_length;
    while (arguments[argument_length] && argument_length < 256U) ++argument_length;
    if (path[path_length] || arguments[argument_length]) return 0;
    process_stack_build_t *build = (process_stack_build_t *)kmalloc(sizeof(*build));
    if (!build) return 0;
    uint32_t *environment_starts = build->environment_starts;
    uint32_t *environment_lengths = build->environment_lengths;
    char (*argument_tokens)[257] = build->argument_tokens;
    uint64_t *argument_addresses = build->argument_addresses;
    uint64_t *environment_addresses = build->environment_addresses;
    uint64_t *values = build->values;
    for (uint32_t index = 0; index < sizeof(*build) / sizeof(uint64_t); ++index)
        ((uint64_t *)build)[index] = 0;
    uint32_t environment_count = 0;
    for (uint32_t index = 0; index < PROCESS_ENVIRONMENT_SIZE &&
         process->environment[index];) {
        if (environment_count == 16) return 0;
        environment_starts[environment_count] = index;
        while (index < PROCESS_ENVIRONMENT_SIZE && process->environment[index])
            ++index;
        environment_lengths[environment_count] = index -
                                                   environment_starts[environment_count];
        ++environment_count;
        ++index;
    }
    uint32_t argument_count = 0;
    for (uint32_t index = 0; index < argument_length;) {
        while (index < argument_length && (arguments[index] == ' ' ||
                                            arguments[index] == '\t')) ++index;
        if (index == argument_length) break;
        if (argument_count == 8) goto argument_fail;
        uint32_t token_length = 0;
        char quote = 0;
        int escaped = 0;
        int token_started = 0;
        while (index < argument_length) {
            char value = arguments[index++];
            if (escaped) {
                if (token_length == sizeof(argument_tokens[0]) - 1U) goto argument_fail;
                argument_tokens[argument_count][token_length++] = value;
                escaped = 0;
                token_started = 1;
                continue;
            }
            if (value == '\\') {
                escaped = 1;
                token_started = 1;
                continue;
            }
            if (quote) {
                if (value == quote) quote = 0;
                else {
                    if (token_length == sizeof(argument_tokens[0]) - 1U) goto argument_fail;
                    argument_tokens[argument_count][token_length++] = value;
                }
                token_started = 1;
                continue;
            }
            if (value == '\'' || value == '"') {
                quote = value;
                token_started = 1;
                continue;
            }
            if (value == ' ' || value == '\t') break;
            if (token_length == sizeof(argument_tokens[0]) - 1U) goto argument_fail;
            argument_tokens[argument_count][token_length++] = value;
            token_started = 1;
        }
        if (quote || escaped || !token_started) goto argument_fail;
        argument_tokens[argument_count][token_length] = 0;
        ++argument_count;
    }
    uint64_t cursor = process->user_stack_top;
    for (uint32_t index = argument_count; index != 0; --index) {
        uint32_t argument = index - 1U;
        uint32_t token_length = 0;
        while (argument_tokens[argument][token_length]) ++token_length;
        cursor -= token_length + 1U;
        argument_addresses[argument] = cursor;
        if (!process_user_stack_write(process, cursor, argument_tokens[argument],
                                      token_length + 1U)) goto argument_fail;
    }
    cursor -= path_length + 1U;
    uint64_t path_address = cursor;
    if (!process_user_stack_write(process, cursor, path, path_length + 1U))
        goto argument_fail;
    for (uint32_t index = environment_count; index != 0; --index) {
        uint32_t environment = index - 1U;
        cursor -= environment_lengths[environment] + 1U;
        environment_addresses[environment] = cursor;
        if (!process_user_stack_write(process, cursor,
                                      &process->environment[environment_starts[environment]],
                                      environment_lengths[environment] + 1U)) goto argument_fail;
    }
    uint64_t value_count = 4U + argument_count + environment_count;
    cursor = (cursor - value_count * sizeof(uint64_t)) & ~0xfULL;
    values[0] = 1U + argument_count;
    values[1] = path_address;
    for (uint32_t index = 0; index < argument_count; ++index)
        values[2U + index] = argument_addresses[index];
    values[2U + argument_count] = 0;
    for (uint32_t index = 0; index < environment_count; ++index)
        values[3U + argument_count + index] = environment_addresses[index];
    values[3U + argument_count + environment_count] = 0;
    if (!process_user_stack_write(process, cursor, values,
                                  value_count * sizeof(uint64_t)))
        goto argument_fail;
    *stack_pointer = cursor;
    kfree(build);
    return 1;

argument_fail:
    kfree(build);
    return 0;
}

int process_activate(process_t *process) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if ((process->state != PROCESS_READY && process->state != PROCESS_RUNNING) ||
        process->user_stack_page_count == 0 ||
        !address_space_activate(&process->address_space)) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    process->state = PROCESS_RUNNING;
    uint64_t table_flags = spinlock_lock_irqsave(&process_table_lock);
    current_process = process;
    spinlock_unlock_irqrestore(&process_table_lock, table_flags);
    spinlock_unlock_irqrestore(&process->lock, flags);
    return 1;
}

int process_destroy(process_t *process) {
    if (!process) return 0;
    uint64_t process_flags = spinlock_lock_irqsave(&process->lock);
    for (process_thread_t *thread = process->threads; thread;
         thread = thread->next)
        if (thread->references > 1) {
            spinlock_unlock_irqrestore(&process->lock, process_flags);
            return 0;
        }
    if (process->retained_thread_references != 0) {
        spinlock_unlock_irqrestore(&process->lock, process_flags);
        return 0;
    }
    if (process_handle_table_has_retained(&process->handles)) {
        spinlock_unlock_irqrestore(&process->lock, process_flags);
        return 0;
    }
    if (process_current() == process || process->state == PROCESS_RUNNING ||
        task_wait_queue_count(&process->signal_waiters) != 0 ||
        task_wait_queue_count(&process->exit_waiters) != 0 ||
        !process_thread_destroy_all_locked(process)) {
        spinlock_unlock_irqrestore(&process->lock, process_flags);
        return 0;
    }
    process_reparent_children(process);
    exec_unload_image(&process->address_space, &process->image);
    if (!address_space_destroy(&process->address_space)) {
        spinlock_unlock_irqrestore(&process->lock, process_flags);
        return 0;
    }
    for (uint32_t page = 0; page < process->user_stack_page_count; ++page)
        physical_free_frame(process->user_stack_pages[page]);
    process->user_stack_page_count = 0;
    process->state = PROCESS_EXITED;
    process_handle_table_close_all(&process->handles);
    vfs_node_t *root = process->root_directory;
    vfs_node_t *working = process->working_directory;
    process_t *parent = process->parent;
    process->parent = 0;
    process->root_directory = 0;
    process->working_directory = 0;
    spinlock_unlock_irqrestore(&process->lock, process_flags);
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    for (uint32_t i = 0; i < PROCESS_MAX; ++i)
        if (process_table[i] == process) process_table[i] = 0;
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    if (working) vfs_node_release(working);
    if (root) vfs_node_release(root);
    if (parent) process_release(parent);
    process_release(process);
    return 1;
}

process_t *process_current(void) {
    uint64_t flags = spinlock_lock_irqsave(&process_table_lock);
    process_t *process = current_process;
    spinlock_unlock_irqrestore(&process_table_lock, flags);
    return process;
}

int process_send_signal(process_t *process, uint32_t signal) {
    if (!process || signal == 0 || signal > PROCESS_SIGNAL_MAX) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    __atomic_fetch_or(&process->pending_signals, 1U << (signal - 1U), __ATOMIC_RELEASE);
    spinlock_unlock_irqrestore(&process->lock, flags);
    (void)scheduler_wake_one(&process->signal_waiters);
    return 1;
}

int process_can_signal(const process_t *caller, const process_t *target) {
    if (!caller || !target) return 0;
    return caller == target || caller->security.uid == target->security.uid ||
           security_has_capability(&caller->security, SECURITY_CAP_SYS_ADMIN);
}

int process_set_signal_mask(process_t *process, uint32_t mask) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    __atomic_store_n(&process->blocked_signals, mask, __ATOMIC_RELEASE);
    spinlock_unlock_irqrestore(&process->lock, flags);
    (void)scheduler_wake_one(&process->signal_waiters);
    return 1;
}

static int take_signal_locked(process_t *process, uint32_t *signal) {
    for (;;) {
        uint32_t pending = __atomic_load_n(&process->pending_signals, __ATOMIC_ACQUIRE);
        uint32_t blocked = __atomic_load_n(&process->blocked_signals, __ATOMIC_ACQUIRE);
        uint32_t available = pending & ~blocked;
        if (!available) return 0;
        uint32_t bit = 0;
        while ((available & (1U << bit)) == 0) ++bit;
        uint32_t updated = pending & ~(1U << bit);
        if (__atomic_compare_exchange_n(&process->pending_signals, &pending, updated,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            *signal = bit + 1U;
            return 1;
        }
    }
}

int process_take_signal(process_t *process, uint32_t *signal) {
    if (!process || !signal) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    int result = take_signal_locked(process, signal);
    spinlock_unlock_irqrestore(&process->lock, flags);
    return result;
}

int process_wait_signal(process_t *process, uint32_t *signal) {
    if (!process || !signal) return 0;
    for (;;) {
        uint64_t flags = spinlock_lock_irqsave(&process->lock);
        if (take_signal_locked(process, signal)) {
            spinlock_unlock_irqrestore(&process->lock, flags);
            return 1;
        }
        if (process->state == PROCESS_EXITED ||
            !scheduler_block_current_with_lock(&process->signal_waiters,
                                               &process->lock, flags)) return 0;
    }
}

int process_wait(process_t *process, int32_t *status) {
    if (!process || !status) return 0;
    for (;;) {
        uint64_t flags = spinlock_lock_irqsave(&process->lock);
        if (process->state == PROCESS_EXITED) {
            *status = process->exit_status;
            spinlock_unlock_irqrestore(&process->lock, flags);
            return 1;
        }
        if (scheduler_block_current_with_lock(&process->exit_waiters,
                                              &process->lock, flags))
            continue;
        return 0;
    }
}

int process_wait_child(process_t *parent, process_t *child, int32_t *status) {
    if (!parent || !child || parent == child) return 0;
    uint64_t flags = spinlock_lock_irqsave(&child->lock);
    int related = child->parent == parent;
    spinlock_unlock_irqrestore(&child->lock, flags);
    return related && process_wait(child, status);
}

int process_terminate(process_t *process, int32_t status) {
    if (!process) return 0;
    uint64_t flags = spinlock_lock_irqsave(&process->lock);
    if (process->state == PROCESS_EXITED || process == process_current()) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    for (process_thread_t *thread = process->threads; thread; thread = thread->next)
        if (!thread->task || thread->task->state == TASK_RUNNING) {
            spinlock_unlock_irqrestore(&process->lock, flags);
            return 0;
        }
    if (!process_thread_destroy_all_locked(process)) {
        spinlock_unlock_irqrestore(&process->lock, flags);
        return 0;
    }
    process->exit_status = status;
    process->state = PROCESS_EXITED;
    spinlock_unlock_irqrestore(&process->lock, flags);
    wake_all_signal_waiters(process);
    while (scheduler_wake_one(&process->exit_waiters)) { }
    return 1;
}
