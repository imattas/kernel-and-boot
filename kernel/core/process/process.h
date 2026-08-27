#ifndef OS_CORE_PROCESS_H
#define OS_CORE_PROCESS_H

#include <stdint.h>
#include "user_image.h"
#include "../../security/credentials.h"
#include "handle.h"
#include "../../core/sync/spinlock.h"
#include "../../core/task/wait_queue.h"
#include "../../fs/vfs/vfs.h"

typedef struct process_thread process_thread_t;

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_EXITED
} process_state_t;

#define PROCESS_SIGNAL_MAX 32U
#define PROCESS_MAX 64U
#define PROCESS_USER_STACK_PAGES 8U

typedef struct process {
    spinlock_t lock;
    uint32_t references;
    uint64_t id;
    process_state_t state;
    address_space_t address_space;
    user_image_t image;
    security_context_t security;
    process_thread_t *threads;
    uint32_t thread_count;
    uint32_t retained_thread_references;
    uint64_t user_stack_pages[PROCESS_USER_STACK_PAGES];
    uint32_t user_stack_page_count;
    uint64_t user_stack_top;
    uint32_t pending_signals;
    uint32_t blocked_signals;
    task_wait_queue_t signal_waiters;
    task_wait_queue_t exit_waiters;
    int32_t exit_status;
    process_handle_table_t handles;
    vfs_node_t *root_directory;
    vfs_node_t *working_directory;
} process_t;

process_t *process_create(uint64_t id);
process_t *process_create_user(uint64_t id, const void *image, uint64_t image_size,
                               uint64_t stack_base, uint32_t thread_id,
                               uint64_t kernel_stack_size);
int process_initialize(void);
int process_set_namespace(process_t *process, vfs_node_t *root,
                          vfs_node_t *working_directory);
process_t *process_lookup(uint64_t id);
process_t *process_lookup_retain(uint64_t id);
void process_release(process_t *process);
int process_load_image(process_t *process, const void *image, uint64_t size);
int process_map_user_stack(process_t *process, uint64_t page_address);
int process_activate(process_t *process);
int process_destroy(process_t *process);
process_t *process_current(void);
int process_send_signal(process_t *process, uint32_t signal);
int process_can_signal(const process_t *caller, const process_t *target);
int process_set_signal_mask(process_t *process, uint32_t mask);
int process_take_signal(process_t *process, uint32_t *signal);
int process_wait_signal(process_t *process, uint32_t *signal);
int process_wait(process_t *process, int32_t *status);
int process_terminate(process_t *process, int32_t status);

#endif
