#ifndef OS_CORE_SYNC_SPINLOCK_H
#define OS_CORE_SYNC_SPINLOCK_H

#include <stdint.h>

typedef struct {
    volatile uint32_t state;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
uint64_t spinlock_lock_irqsave(spinlock_t *lock);
void spinlock_unlock_irqrestore(spinlock_t *lock, uint64_t flags);

#endif
