#ifndef OS_CORE_PROCESS_H
#define OS_CORE_PROCESS_H

#include <stdint.h>
#include "user_image.h"
#include "../../security/credentials.h"
#include "handle.h"
#include "../../core/sync/spinlock.h"
#include "../../core/task/wait_queue.h"

typedef struct process_thread process_thread_t;

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_EXITED
} process_state_t;

#define PROCESS_SIGNAL_MAX 32U
#define PROCESS_MAX 64U

typedef struct process {
    spinlock_t lock;
    uint64_t id;
    process_state_t state;
    address_space_t address_space;
    user_image_t image;
    security_context_t security;
    process_thread_t *threads;
    uint32_t thread_count;
    uint64_t user_stack_page;
    uint64_t user_stack_top;
    uint32_t pending_signals;
    uint32_t blocked_signals;
    task_wait_queue_t signal_waiters;
    int32_t exit_status;
    process_handle_table_t handles;
} process_t;

process_t *process_create(uint64_t id);
int process_initialize(void);
process_t *process_lookup(uint64_t id);
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
int process_terminate(process_t *process, int32_t status);

#endif
