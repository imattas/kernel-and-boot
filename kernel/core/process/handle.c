#include "handle.h"
void process_handle_table_initialize(process_handle_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i) {
        table->entries[i].object = 0; table->entries[i].rights = 0;
        table->entries[i].release = 0;
        table->generations[i] = 1;
    }
}

static uint32_t make_handle(uint32_t slot, uint16_t generation) {
    return ((uint32_t)generation << PROCESS_HANDLE_SLOT_BITS) | (slot + 1U);
}

static int decode_handle(uint32_t handle, uint32_t *slot, uint16_t *generation) {
    uint32_t encoded_slot = handle & PROCESS_HANDLE_SLOT_MASK;
    uint32_t encoded_generation = handle >> PROCESS_HANDLE_SLOT_BITS;
    if (!handle || encoded_slot == 0 || encoded_generation == 0 ||
        encoded_slot > PROCESS_HANDLE_CAPACITY || encoded_generation > UINT16_MAX)
        return 0;
    *slot = encoded_slot - 1U;
    *generation = (uint16_t)encoded_generation;
    return 1;
}
int process_handle_open(process_handle_table_t *table, void *object, uint32_t rights) {
    return process_handle_open_owned(table, object, rights, 0);
}

int process_handle_open_owned(process_handle_table_t *table, void *object,
                              uint32_t rights, process_handle_release_fn release) {
    if (!table || !object || rights == 0 || (rights & ~7U) != 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i)
        if (!table->entries[i].object) {
            table->entries[i].object = object; table->entries[i].rights = rights;
            table->entries[i].release = release;
            int handle = (int)make_handle(i, table->generations[i]);
            spinlock_unlock_irqrestore(&table->lock, flags);
            return handle;
        }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 0;
}
void *process_handle_get(const process_handle_table_t *table, uint32_t handle,
                         uint32_t required_rights) {
    uint32_t slot; uint16_t generation;
    if (!table || !decode_handle(handle, &slot, &generation) ||
        (required_rights & ~7U) != 0)
        return 0;
    process_handle_table_t *mutable_table = (process_handle_table_t *)table;
    uint64_t flags = spinlock_lock_irqsave(&mutable_table->lock);
    const process_handle_t *entry = &table->entries[slot];
    void *object = table->generations[slot] == generation && entry->object &&
                   (entry->rights & required_rights) == required_rights ?
                   entry->object : 0;
    spinlock_unlock_irqrestore(&mutable_table->lock, flags);
    return object;
}
int process_handle_close(process_handle_table_t *table, uint32_t handle) {
    uint32_t slot; uint16_t generation;
    if (!table || !decode_handle(handle, &slot, &generation))
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    if (table->generations[slot] != generation || !table->entries[slot].object) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    void *object = table->entries[slot].object;
    process_handle_release_fn release = table->entries[slot].release;
    table->entries[slot].object = 0; table->entries[slot].rights = 0;
    table->entries[slot].release = 0;
    table->generations[slot] = table->generations[slot] == UINT16_MAX ?
        1 : (uint16_t)(table->generations[slot] + 1U);
    spinlock_unlock_irqrestore(&table->lock, flags);
    if (release) release(object);
    return 1;
}
void process_handle_table_close_all(process_handle_table_t *table) {
    if (!table) return;
    void *objects[PROCESS_HANDLE_CAPACITY];
    process_handle_release_fn releases[PROCESS_HANDLE_CAPACITY];
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i) {
        objects[i] = table->entries[i].object;
        releases[i] = table->entries[i].release;
        table->entries[i].object = 0; table->entries[i].rights = 0;
        table->entries[i].release = 0;
        table->generations[i] = table->generations[i] == UINT16_MAX ?
            1 : (uint16_t)(table->generations[i] + 1U);
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i)
        if (releases[i]) releases[i](objects[i]);
}
