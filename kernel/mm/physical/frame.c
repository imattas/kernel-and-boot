#include <stdint.h>
#include "frame.h"
#include "../../core/sync/spinlock.h"

#define FRAME_SIZE 4096ULL
#define MAX_PHYSICAL_ADDRESS (1ULL << 32)
#define MAX_FRAMES (MAX_PHYSICAL_ADDRESS / FRAME_SIZE)
#define BITMAP_BYTES (MAX_FRAMES / 8)

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attributes;
} efi_memory_descriptor_t;

static uint8_t frame_bitmap[BITMAP_BYTES];
static uint8_t allocated_bitmap[BITMAP_BYTES];
static physical_memory_stats_t stats;
static spinlock_t frame_lock;

static void mark_frame(uint64_t frame, int free) {
    if (frame >= MAX_FRAMES) return;
    uint8_t mask = (uint8_t)(1U << (frame & 7));
    if (free) frame_bitmap[frame >> 3] &= (uint8_t)~mask;
    else frame_bitmap[frame >> 3] |= mask;
}

static int frame_is_free(uint64_t frame) {
    return (frame_bitmap[frame >> 3] & (uint8_t)(1U << (frame & 7))) == 0;
}

static int frame_is_allocated(uint64_t frame) {
    return (allocated_bitmap[frame >> 3] & (uint8_t)(1U << (frame & 7))) != 0;
}

static void mark_allocated(uint64_t frame, int allocated) {
    uint8_t mask = (uint8_t)(1U << (frame & 7));
    if (allocated) allocated_bitmap[frame >> 3] |= mask;
    else allocated_bitmap[frame >> 3] &= (uint8_t)~mask;
}

int physical_init(const os_boot_info_t *boot_info) {
    if (!boot_info || !boot_info->memory_map || !boot_info->memory_descriptor_size ||
        boot_info->memory_descriptor_size < sizeof(efi_memory_descriptor_t) ||
        boot_info->memory_map_size < boot_info->memory_descriptor_size ||
        boot_info->kernel_base >= MAX_PHYSICAL_ADDRESS ||
        boot_info->kernel_size == 0 ||
        boot_info->kernel_size > MAX_PHYSICAL_ADDRESS - boot_info->kernel_base)
        return 0;
    spinlock_init(&frame_lock);
    for (uint64_t i = 0; i < BITMAP_BYTES; ++i) {
        frame_bitmap[i] = 0xff;
        allocated_bitmap[i] = 0;
    }
    stats.total_frames = MAX_FRAMES;
    const uint8_t *cursor = (const uint8_t *)(uintptr_t)boot_info->memory_map;
    uint64_t count = boot_info->memory_map_size / boot_info->memory_descriptor_size;
    for (uint64_t i = 0; i < count; ++i) {
        const efi_memory_descriptor_t *descriptor = (const efi_memory_descriptor_t *)cursor;
        if (descriptor->type == 7 && descriptor->physical_start < MAX_PHYSICAL_ADDRESS) {
            uint64_t first = descriptor->physical_start / FRAME_SIZE;
            uint64_t pages = descriptor->number_of_pages;
            if (pages > MAX_FRAMES - first) pages = MAX_FRAMES - first;
            for (uint64_t page = 0; page < pages; ++page) mark_frame(first + page, 1);
        }
        cursor += boot_info->memory_descriptor_size;
    }
    uint64_t kernel_first = boot_info->kernel_base / FRAME_SIZE;
    uint64_t kernel_pages = (boot_info->kernel_size - 1) / FRAME_SIZE + 1;
    for (uint64_t page = 0; page < kernel_pages && kernel_first + page < MAX_FRAMES; ++page) mark_frame(kernel_first + page, 0);
    stats.free_frames = 0;
    for (uint64_t frame = 0; frame < MAX_FRAMES; ++frame) if (frame_is_free(frame)) ++stats.free_frames;
    stats.reserved_frames = stats.total_frames - stats.free_frames;
    return stats.free_frames != 0;
}

const physical_memory_stats_t *physical_stats(void) { return &stats; }

uint64_t physical_alloc_frame(void) {
    return physical_alloc_frames(1);
}

uint64_t physical_alloc_frames(uint32_t count) {
    if (count == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&frame_lock);
    for (uint64_t frame = 256; frame <= MAX_FRAMES - count; ++frame) {
        uint32_t available = 0;
        while (available < count && frame_is_free(frame + available)) ++available;
        if (available == count) {
            for (uint32_t page = 0; page < count; ++page) {
                mark_frame(frame + page, 0);
                mark_allocated(frame + page, 1);
                volatile uint64_t *memory = (volatile uint64_t *)(uintptr_t)
                    ((frame + page) * FRAME_SIZE);
                for (uint32_t word = 0; word < FRAME_SIZE / sizeof(uint64_t); ++word)
                    memory[word] = 0;
            }
            stats.free_frames -= count;
            stats.reserved_frames += count;
            spinlock_unlock_irqrestore(&frame_lock, flags);
            return frame * FRAME_SIZE;
        }
    }
    spinlock_unlock_irqrestore(&frame_lock, flags);
    return 0;
}

void physical_free_frame(uint64_t address) {
    physical_free_frames(address, 1);
}

void physical_free_frames(uint64_t address, uint32_t count) {
    if (count == 0 || (address & (FRAME_SIZE - 1)) != 0 ||
        address >= MAX_PHYSICAL_ADDRESS ||
        count > (MAX_PHYSICAL_ADDRESS - address) / FRAME_SIZE) return;
    uint64_t first = address / FRAME_SIZE;
    uint64_t flags = spinlock_lock_irqsave(&frame_lock);
    for (uint32_t page = 0; page < count; ++page) {
        uint64_t frame = first + page;
        if (frame_is_allocated(frame)) {
            mark_allocated(frame, 0);
            mark_frame(frame, 1);
            ++stats.free_frames;
            --stats.reserved_frames;
        }
    }
    spinlock_unlock_irqrestore(&frame_lock, flags);
}
