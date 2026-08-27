#ifndef OS_KERNEL_CORE_PROCESS_HANDLE_H
#define OS_KERNEL_CORE_PROCESS_HANDLE_H
#include <stdint.h>
#include "../sync/spinlock.h"
#define PROCESS_HANDLE_CAPACITY 32U
#define PROCESS_HANDLE_READ 0x01U
#define PROCESS_HANDLE_WRITE 0x02U
#define PROCESS_HANDLE_EXEC 0x04U
typedef struct { void *object; uint32_t rights; } process_handle_t;
typedef struct {
    spinlock_t lock;
    process_handle_t entries[PROCESS_HANDLE_CAPACITY];
} process_handle_table_t;
void process_handle_table_initialize(process_handle_table_t *table);
int process_handle_open(process_handle_table_t *table, void *object, uint32_t rights);
void *process_handle_get(const process_handle_table_t *table, uint32_t handle, uint32_t required_rights);
int process_handle_close(process_handle_table_t *table, uint32_t handle);
void process_handle_table_close_all(process_handle_table_t *table);
#endif
