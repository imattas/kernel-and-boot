#include "handle.h"
void process_handle_table_initialize(process_handle_table_t *table) {
    if (!table) return;
    spinlock_init(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i) {
        table->entries[i].object = 0; table->entries[i].rights = 0;
        table->entries[i].release = 0;
        table->entries[i].retain = 0;
        table->entries[i].retained_references = 0;
        table->entries[i].closing = 0;
        table->generations[i] = 1;
    }
    table->retained_references = 0;
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
    return process_handle_open_owned_retain(table, object, rights, release, 0);
}

int process_handle_open_owned_retain(process_handle_table_t *table, void *object,
                                     uint32_t rights,
                                     process_handle_release_fn release,
                                     process_handle_retain_fn retain) {
    if (!table || !object || rights == 0 || (rights & ~7U) != 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i)
        if (!table->entries[i].object) {
            table->entries[i].object = object; table->entries[i].rights = rights;
            table->entries[i].release = release;
            table->entries[i].retain = retain;
            table->entries[i].closing = 0;
            int handle = (int)make_handle(i, table->generations[i]);
            spinlock_unlock_irqrestore(&table->lock, flags);
            return handle;
        }
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 0;
}
int process_handle_duplicate(process_handle_table_t *table, uint32_t handle,
                             uint32_t rights) {
    uint32_t slot; uint16_t generation;
    if (!table || !decode_handle(handle, &slot, &generation)) return 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    process_handle_t *source = &table->entries[slot];
    if (table->generations[slot] != generation || source->closing ||
        !source->object || !source->retain ||
        (rights && (rights & ~source->rights) != 0)) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    uint32_t duplicated_rights = rights ? rights : source->rights;
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i) {
        process_handle_t *entry = &table->entries[i];
        if (!entry->object) {
            source->retain(source->object);
            entry->object = source->object;
            entry->rights = duplicated_rights;
            entry->release = source->release;
            entry->retain = source->retain;
            entry->closing = 0;
            int result = (int)make_handle(i, table->generations[i]);
            spinlock_unlock_irqrestore(&table->lock, flags);
            return result;
        }
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
    void *object = table->generations[slot] == generation && !entry->closing &&
                   entry->object &&
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
    if (table->generations[slot] != generation || table->entries[slot].closing ||
        !table->entries[slot].object) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    process_handle_t *entry = &table->entries[slot];
    void *object = entry->object;
    process_handle_release_fn release = entry->release;
    entry->rights = 0;
    entry->closing = 1;
    if (entry->retained_references == 0) {
        entry->object = 0;
        entry->release = 0;
        entry->retain = 0;
    } else {
        release = 0;
    }
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
        table->entries[i].rights = 0;
        table->entries[i].closing = 1;
        if (table->entries[i].retained_references == 0) {
            table->entries[i].object = 0;
            table->entries[i].release = 0;
            table->entries[i].retain = 0;
        } else {
            objects[i] = 0;
            releases[i] = 0;
        }
        table->generations[i] = table->generations[i] == UINT16_MAX ?
            1 : (uint16_t)(table->generations[i] + 1U);
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    for (uint32_t i = 0; i < PROCESS_HANDLE_CAPACITY; ++i)
        if (releases[i]) releases[i](objects[i]);
}

int process_handle_get_retain(process_handle_table_t *table, uint32_t handle,
                              uint32_t required_rights, process_handle_ref_t *ref) {
    uint32_t slot; uint16_t generation;
    if (!table || !ref || !decode_handle(handle, &slot, &generation) ||
        (required_rights & ~7U) != 0) return 0;
    ref->table = 0; ref->entry = 0; ref->object = 0;
    ref->release = 0; ref->active = 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    process_handle_t *entry = &table->entries[slot];
    if (table->generations[slot] != generation || entry->closing ||
        !entry->object || (entry->rights & required_rights) != required_rights ||
        entry->retained_references == UINT32_MAX ||
        table->retained_references == UINT32_MAX) {
        spinlock_unlock_irqrestore(&table->lock, flags);
        return 0;
    }
    ++entry->retained_references;
    ++table->retained_references;
    ref->table = table; ref->entry = entry; ref->object = entry->object;
            ref->release = entry->release; ref->active = 1;
    spinlock_unlock_irqrestore(&table->lock, flags);
    return 1;
}

void process_handle_release_ref(process_handle_ref_t *ref) {
    if (!ref || !ref->active || !ref->table || !ref->entry) return;
    process_handle_table_t *table = ref->table;
    void *object = 0;
    process_handle_release_fn release = 0;
    uint64_t flags = spinlock_lock_irqsave(&table->lock);
    if (ref->entry->retained_references != 0 && table->retained_references != 0) {
        --ref->entry->retained_references;
        --table->retained_references;
        if (ref->entry->closing && ref->entry->retained_references == 0) {
            object = ref->entry->object;
            release = ref->entry->release;
            ref->entry->object = 0;
            ref->entry->release = 0;
        }
    }
    spinlock_unlock_irqrestore(&table->lock, flags);
    ref->active = 0; ref->table = 0; ref->entry = 0;
    ref->object = 0; ref->release = 0;
    if (release) release(object);
}

int process_handle_table_has_retained(const process_handle_table_t *table) {
    if (!table) return 0;
    process_handle_table_t *mutable_table = (process_handle_table_t *)table;
    uint64_t flags = spinlock_lock_irqsave(&mutable_table->lock);
    int result = table->retained_references != 0;
    spinlock_unlock_irqrestore(&mutable_table->lock, flags);
    return result;
}
