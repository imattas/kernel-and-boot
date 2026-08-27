#include "elf.h"

static int valid_elf(const elf64_header_t *e, uint64_t size) {
    if (size < sizeof(*e) || e->ident[0] != 0x7f || e->ident[1] != 'E' ||
        e->ident[2] != 'L' || e->ident[3] != 'F' || e->ident[4] != 2 ||
        e->ident[5] != 1 || e->ident[6] != 1 || e->type != 3 ||
        e->machine != 0x3e || e->version != 1 || e->phnum == 0 ||
        e->phentsize != sizeof(elf64_program_t) || e->phoff > size ||
        (uint64_t)e->phnum * e->phentsize > size - e->phoff) return 0;
    int entry_executable = 0;
    for (uint16_t i = 0; i < e->phnum; ++i) {
        const elf64_program_t *first = (const elf64_program_t *)
            ((const uint8_t *)e + e->phoff + (uint64_t)i * e->phentsize);
        if (first->type != 1) continue;
        if (first->filesz > first->memsz || first->offset > size ||
            first->filesz > size - first->offset ||
            first->vaddr + first->memsz < first->vaddr ||
            (first->align > 1 && ((first->align & (first->align - 1)) != 0 ||
                                  (first->vaddr & (first->align - 1)) !=
                                  (first->offset & (first->align - 1))))) return 0;
        uint64_t first_end = first->vaddr + first->memsz;
        if ((first->flags & 1U) != 0 && e->entry >= first->vaddr &&
            e->entry < first_end) entry_executable = 1;
        for (uint16_t j = 0; j < i; ++j) {
            const elf64_program_t *second = (const elf64_program_t *)
                ((const uint8_t *)e + e->phoff + (uint64_t)j * e->phentsize);
            if (second->type != 1 || second->memsz == 0 || first->memsz == 0) continue;
            uint64_t second_end = second->vaddr + second->memsz;
            if (first->vaddr < second_end && second->vaddr < first_end) return 0;
        }
    }
    return entry_executable;
}

static void copy_bytes(void *dst, const void *src, uint64_t size) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint64_t i = 0; i < size; ++i) d[i] = s[i];
}

efi_status_t uefi_elf_load(efi_boot_services_t *bs, const void *file,
                           uint64_t size, efi_physical_address_t *base,
                           uint64_t *image_size, kernel_entry_t *entry) {
    if (!bs || !bs->allocate_pages || !file || !base || !image_size || !entry)
        return 1;
    const elf64_header_t *elf = file;
    if (!valid_elf(elf, size)) return 1;
    uint64_t min_vaddr = UINT64_MAX, max_vaddr = 0;
    for (uint16_t i = 0; i < elf->phnum; ++i) {
        const elf64_program_t *ph = (const elf64_program_t *)
            ((const uint8_t *)file + elf->phoff + (uint64_t)i * elf->phentsize);
        if (ph->type != 1) continue;
        if (ph->vaddr < min_vaddr) min_vaddr = ph->vaddr;
        if (ph->vaddr + ph->memsz > max_vaddr) max_vaddr = ph->vaddr + ph->memsz;
    }
    if (min_vaddr == UINT64_MAX || max_vaddr <= min_vaddr ||
        max_vaddr - min_vaddr > UINT64_MAX - 0xfffULL) return 1;
    uint64_t rounded_size = (max_vaddr - min_vaddr + 0xfffULL) & ~0xfffULL;
    uint64_t pages = rounded_size / 0x1000ULL;
    if (pages == 0 || pages > UINT64_MAX) return 1;
    efi_physical_address_t load_address = 0x02000000ULL;
    efi_allocate_pages_t allocate_pages = (efi_allocate_pages_t)bs->allocate_pages;
    efi_status_t status = allocate_pages(2, 4, pages, &load_address);
    if (status != 0) {
        load_address = 0xffffffffULL;
        status = allocate_pages(1, 4, pages, &load_address);
    }
    if (status != 0 || load_address > 0xffffffffULL ||
        rounded_size > 0x100000000ULL - load_address) return 1;
    for (uint16_t i = 0; i < elf->phnum; ++i) {
        const elf64_program_t *ph = (const elf64_program_t *)
            ((const uint8_t *)file + elf->phoff + (uint64_t)i * elf->phentsize);
        if (ph->type != 1) continue;
        uint8_t *destination = (uint8_t *)(load_address + ph->vaddr - min_vaddr);
        for (uint64_t j = 0; j < ph->memsz; ++j) destination[j] = 0;
        copy_bytes(destination, (const uint8_t *)file + ph->offset, ph->filesz);
    }
    *base = load_address;
    *image_size = rounded_size;
    *entry = (kernel_entry_t)(load_address + elf->entry - min_vaddr);
    return 0;
}
