#include "handle.h"
void process_handle_table_initialize(process_handle_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i) {
        table->entries[i].object = 0; table->entries[i].rights = 0;
    }
}
int process_handle_open(process_handle_table_t *table, void *object, uint32_t rights) {
    if (!table || !object || rights == 0 || (rights & ~7U) != 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i)
        if (!table->entries[i].object) {
            table->entries[i].object = object; table->entries[i].rights = rights;
            spinlock_unlock_irqrestore(&table->lock, flags);
            return (int)(i + 1U);
        }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 0;
}
void *process_handle_get(const process_handle_table_t *table, uint32_t handle,
                         uint32_t required_rights) {
    if (!table || handle == 0 || handle > PROCESS_HANDLE_CAPACITY || (required_rights & ~7U) != 0)
        return 0;
    process_handle_table_t *mutable_table = (process_handle_table_t *)table;
    uint64_t flags = spinlock_lock_irqsave(&mutable_table->lock);
    const process_handle_t *entry = &table->entries[handle - 1U];
    void *object = entry->object &&
                   (entry->rights & required_rights) == required_rights ?
                   entry->object : 0;
    spinlock_unlock_irqrestore(&mutable_table->lock, flags);
    return object;
}
int process_handle_close(process_handle_table_t *table, uint32_t handle) {
    if (!table || handle == 0 || handle > PROCESS_HANDLE_CAPACITY)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    if (!table->entries[handle - 1U].object) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    table->entries[handle - 1U].object = 0; table->entries[handle - 1U].rights = 0;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}
void process_handle_table_close_all(process_handle_table_t *table) {
    if (!table) return;
    for (uint32_t i = 1; i <= PROCESS_HANDLE_CAPACITY; ++i) process_handle_close(table, i);
}
