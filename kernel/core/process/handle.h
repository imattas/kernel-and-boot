#ifndef OS_KERNEL_CORE_PROCESS_HANDLE_H
#define OS_KERNEL_CORE_PROCESS_HANDLE_H
#include <stdint.h>
#include "../sync/spinlock.h"
#define PROCESS_HANDLE_CAPACITY 32U
#define PROCESS_HANDLE_READ 0x01U
#define PROCESS_HANDLE_WRITE 0x02U
#define PROCESS_HANDLE_EXEC 0x04U
#define PROCESS_HANDLE_SLOT_BITS 5U
#define PROCESS_HANDLE_SLOT_MASK (PROCESS_HANDLE_CAPACITY - 1U)
typedef void (*process_handle_release_fn)(void *object);
typedef void (*process_handle_retain_fn)(void *object);
typedef struct process_handle_table process_handle_table_t;
typedef struct {
    void *object;
    uint32_t rights;
    process_handle_release_fn release;
    process_handle_retain_fn retain;
    uint32_t retained_references;
    uint8_t closing;
    uint8_t inheritable;
} process_handle_t;
typedef struct {
    process_handle_table_t *table;
    process_handle_t *entry;
    void *object;
    process_handle_release_fn release;
    uint8_t active;
} process_handle_ref_t;
struct process_handle_table {
    spinlock_t lock;
    process_handle_t entries[PROCESS_HANDLE_CAPACITY];
    uint16_t generations[PROCESS_HANDLE_CAPACITY];
    uint32_t retained_references;
};
void process_handle_table_initialize(process_handle_table_t *table);
int process_handle_table_inherit(process_handle_table_t *destination,
                                 process_handle_table_t *source);
int process_handle_open(process_handle_table_t *table, void *object, uint32_t rights);
int process_handle_open_owned(process_handle_table_t *table, void *object,
                              uint32_t rights, process_handle_release_fn release);
int process_handle_open_owned_retain(process_handle_table_t *table, void *object,
                                     uint32_t rights,
                                     process_handle_release_fn release,
                                     process_handle_retain_fn retain);
int process_handle_duplicate(process_handle_table_t *table, uint32_t handle,
                             uint32_t rights);
int process_handle_set_inheritable(process_handle_table_t *table, uint32_t handle,
                                   int inheritable);
void *process_handle_get(const process_handle_table_t *table, uint32_t handle, uint32_t required_rights);
int process_handle_get_retain(process_handle_table_t *table, uint32_t handle,
                              uint32_t required_rights, process_handle_ref_t *ref);
void process_handle_release_ref(process_handle_ref_t *ref);
int process_handle_table_has_retained(const process_handle_table_t *table);
int process_handle_close(process_handle_table_t *table, uint32_t handle);
void process_handle_table_close_all(process_handle_table_t *table);
#endif
