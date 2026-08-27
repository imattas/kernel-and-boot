#include <stdint.h>
#include "address_space.h"
#include "../physical/frame.h"
#include "../../arch/x86_64/memory/paging.h"
#include "../../arch/x86_64/memory/paging.c"

#define PAGE_SIZE 0x1000ULL
#define HUGE_PAGE_SIZE 0x200000ULL
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITABLE 0x002ULL
#define PAGE_HUGE 0x080ULL
#define PAGE_WRITE_THROUGH 0x008ULL
#define PAGE_USER 0x004ULL
#define PAGE_NX (1ULL << 63)

static uint64_t pml4[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t pdpt[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t page_directories[4][512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t heap_page_table[512] __attribute__((aligned(PAGE_SIZE)));
static int heap_page_table_active;
static uint64_t root;
static uint64_t active_root;
static const address_space_t *active_space;

static void clear_table(uint64_t *table) {
    for (uint64_t i = 0; i < 512; ++i) table[i] = 0;
}

int virtual_memory_initialize(void) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
    low |= 1U << 11;
    __asm__ volatile ("wrmsr" :: "a"(low), "d"(high), "c"(0xc0000080));
    clear_table(pml4); clear_table(pdpt);
    for (uint64_t directory = 0; directory < 4; ++directory) {
        clear_table(page_directories[directory]);
        pdpt[directory] = (uint64_t)(uintptr_t)page_directories[directory] | PAGE_PRESENT | PAGE_WRITABLE;
        for (uint64_t entry = 0; entry < 512; ++entry) {
            uint64_t physical = (directory * 512 + entry) * HUGE_PAGE_SIZE;
            page_directories[directory][entry] = physical | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
        }
    }
    pml4[0] = (uint64_t)(uintptr_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE;
    root = (uint64_t)(uintptr_t)pml4;
    x86_64_load_page_root(root);
    active_root = root;
    active_space = 0;
    return 1;
}

int virtual_memory_map_identity(uint64_t address, uint64_t size, uint64_t flags) {
    if ((address & (HUGE_PAGE_SIZE - 1)) != 0 || (size & (HUGE_PAGE_SIZE - 1)) != 0 ||
        size == 0 || address >= 0x100000000ULL || size > 0x100000000ULL - address) return 0;
    uint64_t first = address / HUGE_PAGE_SIZE;
    uint64_t count = size / HUGE_PAGE_SIZE;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t directory = (first + i) / 512;
        uint64_t entry = (first + i) % 512;
        page_directories[directory][entry] = (address + i * HUGE_PAGE_SIZE) | flags | PAGE_PRESENT | PAGE_HUGE;
    }
    x86_64_load_page_root(root);
    return 1;
}

int virtual_memory_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || (physical_address & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < 0x40000000ULL || virtual_address >= 0x40200000ULL || physical_address >= 0x100000000ULL) return 0;
    if (!heap_page_table_active) {
        for (uint64_t i = 0; i < 512; ++i) heap_page_table[i] = 0;
        page_directories[1][0] = (uint64_t)(uintptr_t)heap_page_table | PAGE_PRESENT | PAGE_WRITABLE;
        heap_page_table_active = 1;
    }
    heap_page_table[(virtual_address - 0x40000000ULL) / PAGE_SIZE] = physical_address | flags | PAGE_PRESENT;
    x86_64_load_page_root(root);
    return 1;
}

uint64_t virtual_memory_root(void) { return root; }

static uint64_t address_space_allocate_table(address_space_t *space) {
    if (space->owned_count >= 32) return 0;
    uint64_t frame = physical_alloc_frame();
    if (!frame) return 0;
    uint64_t *table = (uint64_t *)(uintptr_t)frame;
    for (uint64_t i = 0; i < 512; ++i) table[i] = 0;
    space->owned_frames[space->owned_count++] = frame;
    return frame;
}

int address_space_create(address_space_t *space) {
    if (!space || space->root != 0) return 0;
    space->owned_count = 0;
    uint64_t root_frame = address_space_allocate_table(space);
    uint64_t pdpt_frame = address_space_allocate_table(space);
    if (!root_frame || !pdpt_frame) {
        for (uint32_t i = 0; i < space->owned_count; ++i) physical_free_frame(space->owned_frames[i]);
        space->owned_count = 0;
        return 0;
    }
    uint64_t *new_root = (uint64_t *)(uintptr_t)root_frame;
    uint64_t *new_pdpt = (uint64_t *)(uintptr_t)pdpt_frame;
    for (uint64_t i = 0; i < 512; ++i) new_pdpt[i] = pdpt[i];
    new_root[0] = pdpt_frame | PAGE_PRESENT | PAGE_WRITABLE;
    space->root = root_frame;
    return 1;
}

int address_space_activate(const address_space_t *space) {
    if (!space || (space->root & (PAGE_SIZE - 1)) != 0 || space->root == 0) return 0;
    x86_64_load_page_root(space->root);
    active_root = space->root;
    active_space = space;
    return 1;
}

int address_space_destroy(address_space_t *space) {
    if (!space || space->root == 0 || space->root == root || space->root == active_root)
        return 0;
    for (uint32_t i = space->owned_count; i != 0; --i)
        physical_free_frame(space->owned_frames[i - 1]);
    space->owned_count = 0;
    space->root = 0;
    return 1;
}

int address_space_map_page(address_space_t *space, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags) {
    if (!space || space->root == 0 || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        (physical_address & (PAGE_SIZE - 1)) != 0 || virtual_address < (1ULL << 39) ||
        virtual_address >= (1ULL << 48) || physical_address >= 0x100000000ULL)
        return 0;
    uint64_t *new_root = (uint64_t *)(uintptr_t)space->root;
    uint64_t table_flags = PAGE_PRESENT | PAGE_WRITABLE;
    if ((flags & PAGE_USER) != 0) table_flags |= PAGE_USER;
    uint64_t pml4_index = (virtual_address >> 39) & 0x1ff;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1ff;
    uint64_t pd_index = (virtual_address >> 21) & 0x1ff;
    uint64_t pt_index = (virtual_address >> 12) & 0x1ff;
    if ((new_root[pml4_index] & PAGE_PRESENT) != 0) {
        uint64_t *existing_pdpt =
            (uint64_t *)(uintptr_t)(new_root[pml4_index] & ~(PAGE_SIZE - 1));
        if ((existing_pdpt[pdpt_index] & PAGE_PRESENT) != 0) {
            uint64_t *existing_pd =
                (uint64_t *)(uintptr_t)(existing_pdpt[pdpt_index] & ~(PAGE_SIZE - 1));
            if ((existing_pd[pd_index] & PAGE_PRESENT) != 0) {
                uint64_t *existing_pt =
                    (uint64_t *)(uintptr_t)(existing_pd[pd_index] & ~(PAGE_SIZE - 1));
                if ((existing_pt[pt_index] & PAGE_PRESENT) != 0) return 0;
            }
        }
    }
    if ((new_root[pml4_index] & PAGE_PRESENT) == 0) {
        uint64_t frame = address_space_allocate_table(space);
        if (!frame) return 0;
        new_root[pml4_index] = frame | table_flags;
    }
    uint64_t *new_pdpt = (uint64_t *)(uintptr_t)(new_root[pml4_index] & ~(PAGE_SIZE - 1));
    if ((new_pdpt[pdpt_index] & PAGE_PRESENT) == 0) {
        uint64_t frame = address_space_allocate_table(space);
        if (!frame) return 0;
        new_pdpt[pdpt_index] = frame | table_flags;
    }
    uint64_t *pd = (uint64_t *)(uintptr_t)(new_pdpt[pdpt_index] & ~(PAGE_SIZE - 1));
    if ((pd[pd_index] & PAGE_PRESENT) == 0) {
        uint64_t frame = address_space_allocate_table(space);
        if (!frame) return 0;
        pd[pd_index] = frame | table_flags;
    }
    uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_index] & ~(PAGE_SIZE - 1));
    pt[pt_index] = physical_address | flags | PAGE_PRESENT;
    if ((flags & ADDRESS_SPACE_EXECUTABLE) == 0) pt[pt_index] |= PAGE_NX;
    return 1;
}

int address_space_update_page_flags(address_space_t *space, uint64_t virtual_address,
                                    uint64_t flags) {
    if (!space || space->root == 0 || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < (1ULL << 39) || virtual_address >= (1ULL << 48) ||
        (flags & ~(ADDRESS_SPACE_WRITABLE | ADDRESS_SPACE_USER | ADDRESS_SPACE_EXECUTABLE)) != 0)
        return 0;
    uint64_t *pml4_table = (uint64_t *)(uintptr_t)space->root;
    uint64_t pml4e = pml4_table[(virtual_address >> 39) & 0x1ff];
    if ((pml4e & PAGE_PRESENT) == 0) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & ~(PAGE_SIZE - 1));
    uint64_t pdpte = pdpt[(virtual_address >> 30) & 0x1ff];
    if ((pdpte & PAGE_PRESENT) == 0) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & ~(PAGE_SIZE - 1));
    uint64_t pde = pd[(virtual_address >> 21) & 0x1ff];
    if ((pde & PAGE_PRESENT) == 0) return 0;
    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & ~(PAGE_SIZE - 1));
    uint64_t *pte = &pt[(virtual_address >> 12) & 0x1ff];
    if ((*pte & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return 0;
    *pte |= flags | PAGE_PRESENT;
    if ((flags & ADDRESS_SPACE_EXECUTABLE) != 0) *pte &= ~PAGE_NX;
    return 1;
}

int address_space_unmap_page(address_space_t *space, uint64_t virtual_address) {
    if (!space || space->root == 0 || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < (1ULL << 39) || virtual_address >= (1ULL << 48)) return 0;
    uint64_t *pml4_table = (uint64_t *)(uintptr_t)space->root;
    uint64_t pml4e = pml4_table[(virtual_address >> 39) & 0x1ff];
    if ((pml4e & PAGE_PRESENT) == 0) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & ~(PAGE_SIZE - 1));
    uint64_t pdpte = pdpt[(virtual_address >> 30) & 0x1ff];
    if ((pdpte & PAGE_PRESENT) == 0) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & ~(PAGE_SIZE - 1));
    uint64_t pde = pd[(virtual_address >> 21) & 0x1ff];
    if ((pde & PAGE_PRESENT) == 0) return 0;
    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & ~(PAGE_SIZE - 1));
    uint64_t *pte = &pt[(virtual_address >> 12) & 0x1ff];
    if ((*pte & PAGE_PRESENT) == 0) return 0;
    *pte = 0;
    return 1;
}

int address_space_page_executable(const address_space_t *space, uint64_t virtual_address) {
    if (!space || space->root == 0 || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        virtual_address < (1ULL << 39) || virtual_address >= (1ULL << 48)) return 0;
    uint64_t *pml4_table = (uint64_t *)(uintptr_t)space->root;
    uint64_t pml4e = pml4_table[(virtual_address >> 39) & 0x1ff];
    if ((pml4e & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & ~(PAGE_SIZE - 1));
    uint64_t pdpte = pdpt[(virtual_address >> 30) & 0x1ff];
    if ((pdpte & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & ~(PAGE_SIZE - 1));
    uint64_t pde = pd[(virtual_address >> 21) & 0x1ff];
    if ((pde & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return 0;
    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & ~(PAGE_SIZE - 1));
    uint64_t pte = pt[(virtual_address >> 12) & 0x1ff];
    return (pte & (PAGE_PRESENT | PAGE_USER)) == (PAGE_PRESENT | PAGE_USER) &&
           (pte & PAGE_NX) == 0;
}

const address_space_t *address_space_active(void) { return active_space; }

int address_space_user_range_valid(const address_space_t *space, uint64_t address,
                                   uint64_t size, int writable) {
    if (!space || space->root == 0 || size == 0 || address < (1ULL << 39) ||
        address >= (1ULL << 48) || size > (1ULL << 48) - address) return 0;
    uint64_t end = address + size - 1;
    uint64_t last_page = end & ~(PAGE_SIZE - 1);
    for (uint64_t page = address & ~(PAGE_SIZE - 1); ; page += PAGE_SIZE) {
        uint64_t *pml4 = (uint64_t *)(uintptr_t)space->root;
        uint64_t pml4e = pml4[(page >> 39) & 0x1ff];
        if ((pml4e & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return 0;
        uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & ~(PAGE_SIZE - 1));
        uint64_t pdpte = pdpt[(page >> 30) & 0x1ff];
        if ((pdpte & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return 0;
        uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & ~(PAGE_SIZE - 1));
        uint64_t pde = pd[(page >> 21) & 0x1ff];
        if ((pde & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return 0;
        uint64_t *pt = (uint64_t *)(uintptr_t)(pde & ~(PAGE_SIZE - 1));
        uint64_t pte = pt[(page >> 12) & 0x1ff];
        uint64_t required = PAGE_PRESENT | PAGE_USER;
        if (writable) required |= PAGE_WRITABLE;
        if ((pte & required) != required) return 0;
        if (page == last_page) break;
    }
    return 1;
}
