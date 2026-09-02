#include <stdint.h>
#include "heap.h"
#include "../physical/frame.h"
#include "../virtual/address_space.h"
#include "../../core/sync/spinlock.h"

#define HEAP_BASE 0x40000000ULL
#define HEAP_PAGE_SIZE 0x1000ULL
#define HEAP_PAGE_COUNT 4096ULL
#define HEAP_LIMIT (HEAP_BASE + HEAP_PAGE_COUNT * HEAP_PAGE_SIZE)
#define HEAP_ALIGNMENT 16ULL
#define HEAP_BLOCK_MAGIC 0x48454150424c4f43ULL

typedef struct heap_block {
    uint64_t magic;
    uint64_t size;
    uint8_t free;
    uint8_t reserved[7];
    struct heap_block *next;
} heap_block_t;

static heap_block_t *first_block;
static uint64_t committed_end;
static spinlock_t heap_lock;

static uint64_t align_up(uint64_t value) { return (value + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1); }

static int commit_page(void) {
    if (committed_end >= HEAP_LIMIT) return 0;
    uint64_t frame = physical_alloc_frame();
    if (!frame) return 0;
    if (!virtual_memory_map_page(committed_end, frame, 2)) {
        physical_free_frame(frame);
        return 0;
    }
    heap_block_t *block = (heap_block_t *)(uintptr_t)committed_end;
    block->magic = HEAP_BLOCK_MAGIC;
    block->size = HEAP_PAGE_SIZE - sizeof(*block);
    block->free = 1;
    block->next = 0;
    if (!first_block) first_block = block;
    else {
        heap_block_t *tail = first_block;
        while (tail->next) tail = tail->next;
        uint8_t *tail_end = (uint8_t *)(tail + 1) + tail->size;
        if (tail->free && tail_end == (uint8_t *)block) {
            tail->size += sizeof(*block) + block->size;
        } else {
            tail->next = block;
        }
    }
    committed_end += HEAP_PAGE_SIZE;
    return 1;
}

int heap_initialize(void) {
    spinlock_init(&heap_lock);
    first_block = 0; committed_end = HEAP_BASE;
    return commit_page();
}

void *kmalloc(uint64_t size) {
    if (size == 0 || size > UINT64_MAX - (HEAP_ALIGNMENT - 1)) return 0;
    size = align_up(size);
    uint64_t flags = spinlock_lock_irqsave(&heap_lock);
    for (;;) {
        for (heap_block_t *block = first_block; block; block = block->next) {
            if (!block->free || block->size < size) continue;
            uint64_t remainder = block->size - size;
            if (remainder > sizeof(heap_block_t) + HEAP_ALIGNMENT) {
                heap_block_t *split = (heap_block_t *)((uint8_t *)(block + 1) + size);
                split->magic = HEAP_BLOCK_MAGIC;
                split->size = remainder - sizeof(*split); split->free = 1; split->next = block->next;
                block->next = split; block->size = size;
            }
            block->free = 0;
            spinlock_unlock_irqrestore(&heap_lock, flags);
            return block + 1;
        }
        if (!commit_page()) {
            spinlock_unlock_irqrestore(&heap_lock, flags);
            return 0;
        }
    }
}

void kfree(void *pointer) {
    if (!pointer) return;
    uint64_t flags = spinlock_lock_irqsave(&heap_lock);
    uintptr_t address = (uintptr_t)pointer;
    if (address < HEAP_BASE + sizeof(heap_block_t) || address >= committed_end ||
        (address & (HEAP_ALIGNMENT - 1)) != 0) {
        spinlock_unlock_irqrestore(&heap_lock, flags);
        return;
    }
    heap_block_t *block = ((heap_block_t *)pointer) - 1;
    if (block->magic != HEAP_BLOCK_MAGIC || block->free ||
        block->size == 0 || block->size > HEAP_LIMIT - address) {
        spinlock_unlock_irqrestore(&heap_lock, flags);
        return;
    }
    block->free = 1;
    for (heap_block_t *current = first_block; current && current->next; current = current->next) {
        if (!current->free || !current->next->free) continue;
        current->size += sizeof(heap_block_t) + current->next->size;
        current->next = current->next->next;
    }
    spinlock_unlock_irqrestore(&heap_lock, flags);
}
