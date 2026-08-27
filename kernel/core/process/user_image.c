#include "user_image.h"
#include "../../mm/physical/frame.h"

#define ELF_MAGIC 0x464c457fU
#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_EXEC 2
#define ELF_MACHINE_X86_64 62
#define ELF_PT_LOAD 1
#define ELF_PF_X 1
#define ELF_PF_W 2
#define PAGE_SIZE 0x1000ULL
#define USER_BASE (1ULL << 39)

typedef struct {
    uint8_t magic[4];
    uint8_t class;
    uint8_t data;
    uint8_t version;
    uint8_t abi;
    uint8_t abi_version;
    uint8_t padding[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf64_header_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} elf64_program_header_t;

static int range_inside(uint64_t offset, uint64_t length, uint64_t size) {
    return offset <= size && length <= size - offset;
}

static uint64_t page_down(uint64_t value) { return value & ~(PAGE_SIZE - 1); }
static uint64_t page_up(uint64_t value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}
static int valid_alignment(uint64_t alignment, uint64_t offset,
                           uint64_t virtual_address) {
    if (alignment <= 1) return 1;
    return (alignment & (alignment - 1)) == 0 &&
           (offset & (alignment - 1)) ==
           (virtual_address & (alignment - 1));
}

static int loaded_page_index(const user_image_t *loaded, uint64_t virtual_page) {
    for (uint32_t index = 0; index < loaded->page_count; ++index)
        if (loaded->virtual_pages[index] == virtual_page) return (int)index;
    return -1;
}

static void release_pages(address_space_t *space, user_image_t *loaded) {
    for (uint32_t i = 0; i < loaded->page_count; ++i) {
        if (space) (void)address_space_unmap_page(space, loaded->virtual_pages[i]);
        physical_free_frame(loaded->pages[i]);
    }
    loaded->page_count = 0;
}

int user_image_load(address_space_t *space, const void *image, uint64_t image_size,
                    user_image_t *loaded) {
    if (!space || !image || !loaded || image_size < sizeof(elf64_header_t)) return 0;
    uint8_t *loaded_bytes = (uint8_t *)loaded;
    for (uint64_t byte = 0; byte < sizeof(*loaded); ++byte) loaded_bytes[byte] = 0;
    const uint8_t *bytes = (const uint8_t *)image;
    const elf64_header_t *header = (const elf64_header_t *)image;
    if (*(const uint32_t *)header->magic != ELF_MAGIC || header->class != ELF_CLASS_64 ||
        header->data != ELF_DATA_LSB || header->type != ELF_EXEC ||
        header->machine != ELF_MACHINE_X86_64 || header->ehsize != sizeof(*header) ||
        header->phentsize != sizeof(elf64_program_header_t) || header->phnum == 0 ||
        (uint64_t)header->phnum > (UINT64_MAX / sizeof(elf64_program_header_t)) ||
        !range_inside(header->phoff, (uint64_t)header->phnum * sizeof(elf64_program_header_t), image_size))
        return 0;

    const elf64_program_header_t *programs =
        (const elf64_program_header_t *)(bytes + header->phoff);
    int entry_loaded = 0;
    for (uint16_t index = 0; index < header->phnum; ++index) {
        const elf64_program_header_t *program = &programs[index];
        if (program->type != ELF_PT_LOAD) continue;
        if ((program->flags & ~(ELF_PF_X | ELF_PF_W | 4U)) != 0) {
            release_pages(space, loaded);
            return 0;
        }
        if (program->memory_size < program->file_size ||
            program->virtual_address < USER_BASE ||
            program->virtual_address + program->memory_size < program->virtual_address ||
            program->virtual_address + program->memory_size >= (1ULL << 48) ||
            !valid_alignment(program->alignment, program->offset,
                             program->virtual_address) ||
            !range_inside(program->offset, program->file_size, image_size)) {
            release_pages(space, loaded);
            return 0;
        }
        uint64_t first = page_down(program->virtual_address);
        uint64_t end = page_up(program->virtual_address + program->memory_size);
        for (uint64_t virtual_page = first; virtual_page < end; virtual_page += PAGE_SIZE) {
            int page_index = loaded_page_index(loaded, virtual_page);
            uint64_t physical_page;
            uint64_t page_flags = ADDRESS_SPACE_USER;
            if ((program->flags & ELF_PF_W) != 0) page_flags |= ADDRESS_SPACE_WRITABLE;
            if ((program->flags & ELF_PF_X) != 0) page_flags |= ADDRESS_SPACE_EXECUTABLE;
            if (page_index >= 0) {
                physical_page = loaded->pages[page_index];
                if ((program->flags & (ELF_PF_W | ELF_PF_X)) != 0 &&
                    !address_space_update_page_flags(space, virtual_page, page_flags)) {
            release_pages(space, loaded);
                    return 0;
                }
            } else {
                if (loaded->page_count >= USER_IMAGE_MAX_PAGES) {
            release_pages(space, loaded);
                    return 0;
                }
                physical_page = physical_alloc_frame();
                if (!physical_page || !address_space_map_page(space, virtual_page, physical_page,
                                                              page_flags)) {
                    if (physical_page) physical_free_frame(physical_page);
            release_pages(space, loaded);
                    return 0;
                }
                loaded->pages[loaded->page_count] = physical_page;
                loaded->virtual_pages[loaded->page_count] = virtual_page;
                ++loaded->page_count;
                uint8_t *new_page = (uint8_t *)(uintptr_t)physical_page;
                for (uint64_t byte = 0; byte < PAGE_SIZE; ++byte) new_page[byte] = 0;
            }
            uint8_t *destination = (uint8_t *)(uintptr_t)physical_page;
            uint64_t file_start = program->virtual_address > virtual_page ?
                                  program->virtual_address : virtual_page;
            uint64_t file_end = program->virtual_address + program->file_size;
            uint64_t page_end = virtual_page + PAGE_SIZE;
            if (file_end > page_end) file_end = page_end;
            if (file_end > file_start) {
                uint64_t source_offset = program->offset + (file_start - program->virtual_address);
                uint64_t destination_offset = file_start - virtual_page;
                for (uint64_t byte = 0; byte < file_end - file_start; ++byte)
                    destination[destination_offset + byte] = bytes[source_offset + byte];
            }
        }
        if ((header->entry >= program->virtual_address) &&
            (header->entry - program->virtual_address < program->memory_size) &&
            (program->flags & ELF_PF_X) != 0) entry_loaded = 1;
    }
    if (!entry_loaded) {
        release_pages(space, loaded);
        return 0;
    }
    loaded->entry = header->entry;
    return loaded->page_count != 0;
}

void user_image_destroy(address_space_t *space, user_image_t *loaded) {
    if (!loaded) return;
    release_pages(space, loaded);
    loaded->entry = 0;
}
