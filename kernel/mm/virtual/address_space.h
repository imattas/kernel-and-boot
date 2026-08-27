#ifndef OS_VIRTUAL_ADDRESS_SPACE_H
#define OS_VIRTUAL_ADDRESS_SPACE_H

#include <stdint.h>

typedef struct address_space {
    uint64_t root;
    uint64_t owned_frames[32];
    uint32_t owned_count;
    struct {
        uint64_t virtual_address;
        uint64_t physical_address;
    } anonymous_frames[128];
    uint32_t anonymous_count;
} address_space_t;

#define ADDRESS_SPACE_WRITABLE 0x002ULL
#define ADDRESS_SPACE_USER 0x004ULL
#define ADDRESS_SPACE_EXECUTABLE 0x008ULL
#define ADDRESS_SPACE_MAX_ANONYMOUS_PAGES 128U

int virtual_memory_initialize(void);
int virtual_memory_map_identity(uint64_t address, uint64_t size, uint64_t flags);
int virtual_memory_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
uint64_t virtual_memory_root(void);
int address_space_create(address_space_t *space);
int address_space_activate(const address_space_t *space);
int address_space_activate_kernel(void);
int address_space_destroy(address_space_t *space);
int address_space_map_page(address_space_t *space, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags);
int address_space_unmap_page(address_space_t *space, uint64_t virtual_address);
int address_space_map_anonymous(address_space_t *space, uint64_t virtual_address,
                                uint32_t pages, uint64_t flags);
int address_space_unmap_anonymous(address_space_t *space, uint64_t virtual_address,
                                  uint32_t pages);
int address_space_protect_range(address_space_t *space, uint64_t virtual_address,
                                uint32_t pages, uint64_t flags);
int address_space_update_page_flags(address_space_t *space, uint64_t virtual_address,
                                    uint64_t flags);
int address_space_page_executable(const address_space_t *space, uint64_t virtual_address);
const address_space_t *address_space_active(void);
int address_space_user_range_valid(const address_space_t *space, uint64_t address,
                                   uint64_t size, int writable);
int address_space_copy_from_user(void *destination, uint64_t source, uint64_t size);
int address_space_copy_to_user(uint64_t destination, const void *source, uint64_t size);

#endif
