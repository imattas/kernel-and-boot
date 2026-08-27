#include "spinlock.h"

static uint64_t read_flags(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpop %0" : "=r"(flags) :: "memory");
    return flags;
}

void spinlock_init(spinlock_t *lock) {
    __atomic_store_n(&lock->state, 0, __ATOMIC_RELAXED);
}

void spinlock_lock(spinlock_t *lock) {
    while (__atomic_exchange_n(&lock->state, 1, __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile ("pause" ::: "memory");
    }
}

void spinlock_unlock(spinlock_t *lock) {
    __atomic_store_n(&lock->state, 0, __ATOMIC_RELEASE);
}

uint64_t spinlock_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = read_flags();
    __asm__ volatile ("cli" ::: "memory");
    spinlock_lock(lock);
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spinlock_unlock(lock);
    __asm__ volatile ("push %0\n\tpopfq" :: "r"(flags) : "memory", "cc");
}
