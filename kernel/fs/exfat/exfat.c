#include "exfat.h"
#include "../../drivers/storage/storage.h"

#define EXFAT_SECTOR_SIZE 512U
#define EXFAT_END 0xfffffff8U
#define EXFAT_BAD 0xfffffff7U
#define EXFAT_MAX_CLUSTER_BYTES 4096U

static uint16_t load16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t load32(const uint8_t *p) { return (uint32_t)load16(p) | ((uint32_t)load16(p + 2) << 16); }
static uint64_t load64(const uint8_t *p) { return (uint64_t)load32(p) | ((uint64_t)load32(p + 4) << 32); }
static void store16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void store32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void store64(uint8_t *p, uint64_t value) {
    store32(p, (uint32_t)value); store32(p + 4, (uint32_t)(value >> 32));
}

static int utf8_next(const char **text, uint32_t *codepoint);

static int exfat_encode_name(const char *name, uint16_t *units, uint32_t *count) {
    const char *cursor = name; uint32_t written = 0;
    if (!name || !units || !count || !name[0]) return 0;
    while (*cursor) {
        uint32_t value;
        if (!utf8_next(&cursor, &value)) return 0;
        if (value <= 0xffffU) {
            if (written >= 255U) return 0;
            units[written++] = (uint16_t)value;
        } else {
            if (written > 253U) return 0;
            units[written++] = (uint16_t)(0xd800U + ((value - 0x10000U) >> 10));
            units[written++] = (uint16_t)(0xdc00U + ((value - 0x10000U) & 0x3ffU));
        }
    }
    *count = written; return written != 0;
}

static int read_sector(const exfat_fs_t *fs, uint64_t sector, void *buffer) {
    return fs && buffer && storage_read(fs->device, sector, 1, buffer);
}

static int cluster_valid(const exfat_fs_t *fs, uint32_t cluster) {
    return fs && cluster >= 2 && fs->cluster_count <= UINT32_MAX - 2U &&
           cluster < fs->cluster_count + 2U;
}

static uint32_t boot_checksum(const exfat_fs_t *fs, uint32_t start) {
    uint8_t sector[EXFAT_SECTOR_SIZE];
    uint32_t checksum = 0;
    for (uint32_t number = 0; number < 11U; ++number) {
        if (!read_sector(fs, start + number, sector)) return 0;
        for (uint32_t i = 0; i < EXFAT_SECTOR_SIZE; ++i)
            checksum = ((checksum >> 1) | (checksum << 31)) + sector[i];
    }
    if (!read_sector(fs, start + 11U, sector)) return 0;
    for (uint32_t i = 0; i < EXFAT_SECTOR_SIZE; i += 4U)
        if (load32(&sector[i]) != checksum) return 0;
    return checksum;
}

static int validate_boot_regions(uint32_t device) {
    exfat_fs_t probe = {.device = device};
    return boot_checksum(&probe, 0) != 0 && boot_checksum(&probe, 12) != 0;
}

static int fat_next(const exfat_fs_t *fs, uint32_t cluster, uint32_t *next) {
    uint8_t sector[EXFAT_SECTOR_SIZE * 2U];
    uint64_t byte_offset = (uint64_t)cluster * 4U;
    uint64_t sector_number = fs->fat_start + byte_offset / EXFAT_SECTOR_SIZE;
    if (!cluster_valid(fs, cluster) || sector_number >= (uint64_t)fs->fat_start + fs->fat_sectors ||
        !read_sector(fs, sector_number, sector)) return 0;
    if (byte_offset % EXFAT_SECTOR_SIZE > EXFAT_SECTOR_SIZE - 4U) {
        if (sector_number + 1U >= (uint64_t)fs->fat_start + fs->fat_sectors ||
            !read_sector(fs, sector_number + 1U, sector + EXFAT_SECTOR_SIZE)) return 0;
    }
    *next = load32(&sector[byte_offset % EXFAT_SECTOR_SIZE]);
    return 1;
}

int exfat_mount(exfat_fs_t *fs, uint32_t device) {
    if (!fs || !storage_device_at(device) ||
        storage_device_at(device)->block_size != EXFAT_SECTOR_SIZE) return 0;
    uint8_t boot[EXFAT_SECTOR_SIZE];
    if (!read_sector(&(exfat_fs_t){.device = device}, 0, boot) ||
        boot[510] != 0x55 || boot[511] != 0xaa ||
        boot[0] != 0xeb || boot[2] != 0x90 ||
        boot[3] != 'E' || boot[4] != 'X' || boot[5] != 'F' || boot[6] != 'A' ||
        boot[7] != 'T' || boot[8] != ' ' || boot[9] != ' ' || boot[10] != ' ') return 0;
    if (!validate_boot_regions(device)) return 0;
    uint8_t bytes_shift = boot[108], sectors_shift = boot[109];
    if (bytes_shift != 9 || sectors_shift > 3) return 0;
    uint32_t bytes_per_sector = 1U << bytes_shift;
    uint32_t sectors_per_cluster = 1U << sectors_shift;
    uint64_t volume_length = load64(&boot[72]);
    uint32_t fat_start = load32(&boot[80]);
    uint32_t fat_length = load32(&boot[84]);
    uint32_t heap_start = load32(&boot[88]);
    uint32_t cluster_count = load32(&boot[92]);
    uint32_t root_cluster = load32(&boot[96]);
    if (bytes_per_sector != EXFAT_SECTOR_SIZE || sectors_per_cluster == 0 ||
        sectors_per_cluster * EXFAT_SECTOR_SIZE > EXFAT_MAX_CLUSTER_BYTES ||
        volume_length == 0 || volume_length > storage_device_at(device)->block_count ||
        fat_start < 24 || fat_length == 0 || heap_start <= fat_start ||
        (uint64_t)fat_start + fat_length > volume_length ||
        heap_start >= volume_length || cluster_count == 0 ||
        (uint64_t)fat_length * EXFAT_SECTOR_SIZE / 4U <
            (uint64_t)cluster_count + 2U ||
        (uint64_t)heap_start + (uint64_t)cluster_count * sectors_per_cluster > volume_length ||
        !cluster_valid(&(exfat_fs_t){.cluster_count = cluster_count}, root_cluster)) return 0;
    fs->device = device; fs->fat_start = fat_start; fs->fat_sectors = fat_length;
    fs->heap_start = heap_start; fs->cluster_count = cluster_count;
    fs->root_cluster = root_cluster; fs->sectors_per_cluster = sectors_per_cluster;
    fs->volume_sectors = volume_length; fs->mounted = 1;
    return 1;
}

int exfat_read_cluster(exfat_fs_t *fs, uint32_t cluster, void *buffer) {
    if (!fs || !fs->mounted || !buffer || !cluster_valid(fs, cluster)) return 0;
    uint64_t sector = fs->heap_start + (uint64_t)(cluster - 2U) * fs->sectors_per_cluster;
    return storage_read(fs->device, sector, fs->sectors_per_cluster, buffer);
}

static int utf8_next(const char **text, uint32_t *codepoint) {
    const uint8_t *p = (const uint8_t *)*text;
    uint32_t value, length;
    if (!p[0]) return 0;
    if (p[0] < 0x80) { value = p[0]; length = 1; }
    else if (p[0] >= 0xc2 && p[0] <= 0xdf) { value = p[0] & 0x1fU; length = 2; }
    else if (p[0] >= 0xe0 && p[0] <= 0xef) { value = p[0] & 0x0fU; length = 3; }
    else if (p[0] >= 0xf0 && p[0] <= 0xf4) { value = p[0] & 0x07U; length = 4; }
    else return 0;
    for (uint32_t i = 1; i < length; ++i) {
        if ((p[i] & 0xc0U) != 0x80U) return 0;
        value = (value << 6) | (p[i] & 0x3fU);
    }
    if ((length == 2 && value < 0x80U) || (length == 3 && value < 0x800U) ||
        (length == 4 && value < 0x10000U) || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) return 0;
    *text += length; *codepoint = value; return 1;
}

static int utf16_name_equal(const uint16_t *units, uint32_t count, const char *name) {
    const char *cursor = name;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value;
        if (!utf8_next(&cursor, &value)) return 0;
        if (value <= 0xffffU) {
            uint16_t unit = units[i], wanted = (uint16_t)value;
            if (unit >= 'a' && unit <= 'z') unit = (uint16_t)(unit - 'a' + 'A');
            if (wanted >= 'a' && wanted <= 'z') wanted = (uint16_t)(wanted - 'a' + 'A');
            if (unit != wanted) return 0;
        } else {
            uint32_t high = 0xd800U + ((value - 0x10000U) >> 10);
            uint32_t low = 0xdc00U + ((value - 0x10000U) & 0x3ffU);
            if (i + 1U >= count || units[i] != high || units[++i] != low) return 0;
        }
    }
    return *cursor == 0;
}

static uint16_t entry_set_checksum(const uint8_t *directory, uint32_t index,
                                   uint32_t secondary_count) {
    uint32_t checksum = 0;
    for (uint32_t entry = 0; entry <= secondary_count; ++entry) {
        const uint8_t *data = &directory[(index + entry) * 32U];
        for (uint32_t byte = 0; byte < 32U; ++byte) {
            if (entry == 0U && (byte == 2U || byte == 3U)) continue;
            checksum = ((checksum >> 1) | (checksum << 15)) + data[byte];
        }
    }
    return (uint16_t)checksum;
}

static int exfat_write_fat(exfat_fs_t *fs, uint32_t cluster, uint32_t value) {
    uint8_t sector[EXFAT_SECTOR_SIZE];
    uint64_t offset = (uint64_t)cluster * 4U;
    uint64_t lba = fs->fat_start + offset / EXFAT_SECTOR_SIZE;
    uint32_t in_sector = (uint32_t)(offset % EXFAT_SECTOR_SIZE);
    if (!fs || !cluster_valid(fs, cluster) || in_sector > 508U ||
        lba >= (uint64_t)fs->fat_start + fs->fat_sectors ||
        !read_sector(fs, lba, sector)) return 0;
    store32(&sector[in_sector], value);
    return storage_write(fs->device, lba, 1, sector);
}

static int exfat_find_free(exfat_fs_t *fs, uint32_t *cluster) {
    uint32_t value;
    if (!fs || !cluster) return 0;
    for (uint32_t candidate = 2; candidate < fs->cluster_count + 2U; ++candidate)
        if (fat_next(fs, candidate, &value) && value == 0) {
            *cluster = candidate;
            return 1;
        }
    return 0;
}

static int exfat_zero_cluster(exfat_fs_t *fs, uint32_t cluster) {
    uint8_t zero[EXFAT_MAX_CLUSTER_BYTES] = {0};
    uint64_t lba;
    if (!fs || !cluster_valid(fs, cluster)) return 0;
    lba = fs->heap_start + (uint64_t)(cluster - 2U) * fs->sectors_per_cluster;
    return storage_write(fs->device, lba, fs->sectors_per_cluster, zero);
}

static int exfat_locate_entry(exfat_fs_t *fs, uint32_t directory_cluster,
                              const char *name, uint8_t *directory,
                              uint32_t *index, uint32_t *secondary_count,
                              uint32_t *entry_cluster) {
    uint32_t cluster = directory_cluster;
    if (!fs || !directory || !index || !secondary_count || !entry_cluster ||
        !name || !name[0]) return 0;
    for (uint32_t visited = 0; visited < fs->cluster_count; ++visited) {
        if (!exfat_read_cluster(fs, cluster, directory)) return 0;
        uint32_t entries = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE / 32U;
        for (uint32_t i = 0; i < entries; ++i) {
            uint8_t *entry = &directory[i * 32U];
            if (entry[0] == 0) return 0;
            if (entry[0] != 0x85) continue;
            uint32_t count = entry[1], length = 0, written = 0;
            if (count < 2 || i + count >= entries ||
                load16(&entry[2]) != entry_set_checksum(directory, i, count)) continue;
            uint8_t *stream = &directory[(i + 1U) * 32U];
            if (stream[0] != 0xc0 || (length = stream[3]) == 0 || length >= 256U) continue;
            uint16_t units[256] = {0};
            for (uint32_t s = 2; s <= count && written < length; ++s) {
                uint8_t *name_entry = &directory[(i + s) * 32U];
                if (name_entry[0] != 0xc1) { written = 0; break; }
                for (uint32_t c = 0; c < 15 && written < length; ++c)
                    units[written++] = load16(&name_entry[2 + c * 2U]);
            }
            if (written == length && utf16_name_equal(units, written, name)) {
                *index = i; *secondary_count = count; *entry_cluster = cluster;
                return 1;
            }
        }
        uint32_t next = 0;
        if (!fat_next(fs, cluster, &next) || next >= EXFAT_END ||
            next == EXFAT_BAD || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

static int exfat_directory_empty(exfat_fs_t *fs, uint32_t directory_cluster) {
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES];
    uint32_t cluster = directory_cluster;
    if (!fs || !cluster_valid(fs, cluster)) return 0;
    for (uint32_t visited = 0; visited < fs->cluster_count; ++visited) {
        if (!exfat_read_cluster(fs, cluster, directory)) return 0;
        uint32_t entries = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE / 32U;
        for (uint32_t i = 0; i < entries; ++i) {
            uint8_t type = directory[i * 32U];
            if (type == 0x00) return 1;
            if (type == 0x05) {
                uint32_t secondary = directory[i * 32U + 1U];
                if (secondary == 0 || secondary >= entries - i) return 0;
                i += secondary;
                continue;
            }
            /* Any active entry, including malformed metadata, makes the
               directory unsafe to remove. */
            return 0;
        }
        uint32_t next = 0;
        if (!fat_next(fs, cluster, &next) || next >= EXFAT_END ||
            next == EXFAT_BAD || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

int exfat_lookup_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                              const char *name, uint32_t *first_cluster,
                              uint64_t *size, uint8_t *no_fat_chain) {
    if (!fs || !fs->mounted || !cluster_valid(fs, directory_cluster) || !name ||
        !first_cluster || !size || !no_fat_chain || name[0] == 0) return 0;
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES];
    uint32_t cluster = directory_cluster;
    for (uint32_t visited = 0; visited < fs->cluster_count; ++visited) {
        if (!exfat_read_cluster(fs, cluster, directory)) return 0;
        uint32_t entries = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE / 32U;
        for (uint32_t index = 0; index < entries; ++index) {
            const uint8_t *entry = &directory[index * 32U];
            if (entry[0] == 0x00) return 0;
            if (entry[0] != 0x85) continue;
            uint32_t secondary_count = entry[1];
            if (secondary_count < 2 || index + secondary_count >= entries) continue;
            if (load16(&entry[2]) != entry_set_checksum(directory, index, secondary_count)) continue;
            const uint8_t *stream = &directory[(index + 1U) * 32U];
            if (stream[0] != 0xc0) continue;
            uint16_t candidate[256] = {0};
            uint32_t name_length = stream[3];
            if (name_length == 0 || name_length >= sizeof(candidate)) continue;
            uint32_t written = 0;
            for (uint32_t secondary = 2; secondary <= secondary_count && written < name_length; ++secondary) {
                const uint8_t *name_entry = &directory[(index + secondary) * 32U];
                if (name_entry[0] != 0xc1) { written = 0; break; }
                for (uint32_t character = 0; character < 15 && written < name_length; ++character) {
                    uint16_t value = load16(&name_entry[2 + character * 2U]);
                    candidate[written++] = value;
                }
            }
            if (written != name_length || !utf16_name_equal(candidate, written, name)) continue;
            *first_cluster = load32(&stream[20]); *size = load64(&stream[24]);
            *no_fat_chain = (stream[1] & 2U) != 0;
            /* Empty regular files may legally have no allocated cluster. */
            return cluster_valid(fs, *first_cluster) || *size == 0;
        }
        uint32_t next = 0;
        if (!fat_next(fs, cluster, &next) || next >= EXFAT_END || next == EXFAT_BAD || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

int exfat_lookup(exfat_fs_t *fs, const char *name, uint32_t *first_cluster,
                 uint64_t *size, uint8_t *no_fat_chain) {
    return exfat_lookup_in_directory(fs, fs ? fs->root_cluster : 0, name,
                                     first_cluster, size, no_fat_chain);
}

static int exfat_read_file_cluster(exfat_fs_t *fs, uint32_t cluster,
                                   uint64_t file_size, uint8_t no_fat_chain,
                                   uint64_t offset, void *buffer, uint32_t size) {
    if (!fs || !buffer || size == 0 || !cluster_valid(fs, cluster) ||
        offset > file_size || size > file_size - offset) return 0;
    uint32_t cluster_bytes = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE;
    uint64_t cluster_index = offset / cluster_bytes;
    uint32_t in_cluster = (uint32_t)(offset % cluster_bytes);
    uint8_t data[EXFAT_MAX_CLUSTER_BYTES];
    for (uint64_t i = 0; i < cluster_index; ++i) {
        uint32_t next = 0;
        if (no_fat_chain) next = cluster + 1U;
        else if (!fat_next(fs, cluster, &next)) return 0;
        if (!cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    uint8_t *destination = buffer; uint32_t remaining = size;
    while (remaining) {
        if (!exfat_read_cluster(fs, cluster, data)) return 0;
        uint32_t chunk = cluster_bytes - in_cluster;
        if (chunk > remaining) chunk = remaining;
        for (uint32_t i = 0; i < chunk; ++i) destination[i] = data[in_cluster + i];
        destination += chunk; remaining -= chunk; in_cluster = 0;
        if (remaining) {
            uint32_t next = 0;
            if (no_fat_chain) next = cluster + 1U;
            else if (!fat_next(fs, cluster, &next)) return 0;
            if (!cluster_valid(fs, next)) return 0;
            cluster = next;
        }
    }
    return 1;
}

int exfat_read_file(exfat_fs_t *fs, const char *name, uint64_t offset,
                    void *buffer, uint32_t size) {
    uint32_t cluster = 0; uint64_t file_size = 0; uint8_t no_fat_chain = 0;
    if (!exfat_lookup(fs, name, &cluster, &file_size, &no_fat_chain)) return 0;
    return exfat_read_file_cluster(fs, cluster, file_size, no_fat_chain,
                                   offset, buffer, size);
}

static int exfat_resize_file(exfat_fs_t *fs, uint32_t directory_cluster,
                             const char *name, uint64_t new_size) {
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES];
    uint32_t index, secondary_count, entry_cluster;
    if (!fs || !fs->mounted || !exfat_locate_entry(fs, directory_cluster, name,
                                                   directory, &index,
                                                   &secondary_count,
                                                   &entry_cluster)) return 0;
    uint8_t *stream = &directory[(index + 1U) * 32U];
    uint64_t old_size = load64(&stream[24]);
    uint32_t cluster_bytes = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE;
    uint64_t old_count = (old_size + cluster_bytes - 1U) / cluster_bytes;
    uint64_t new_count = (new_size + cluster_bytes - 1U) / cluster_bytes;
    if (new_count > fs->cluster_count || new_size > UINT64_MAX - cluster_bytes)
        return 0;
    uint32_t first = load32(&stream[20]), tail = 0, next = 0;
    if (old_count != 0 && !cluster_valid(fs, first)) return 0;
    if (old_count != 0) {
        tail = first;
        for (uint64_t n = 1; n < old_count; ++n) {
            if ((stream[1] & 2U) != 0) next = tail + 1U;
            else if (!fat_next(fs, tail, &next)) return 0;
            if (!cluster_valid(fs, next)) return 0;
            tail = next;
        }
    }
    if (new_count > old_count) {
        uint32_t previous = tail;
        for (uint64_t n = old_count; n < new_count; ++n) {
            uint32_t allocated;
            if (!exfat_find_free(fs, &allocated) || !exfat_zero_cluster(fs, allocated)) return 0;
            if (previous != 0 && !exfat_write_fat(fs, previous, allocated)) return 0;
            if (!exfat_write_fat(fs, allocated, EXFAT_END)) return 0;
            if (first == 0) first = allocated;
            previous = allocated;
        }
        stream[1] &= (uint8_t)~2U;
    } else if (new_count < old_count) {
        if (new_count == 0) {
            uint32_t current = first;
            for (uint64_t n = 0; n < old_count; ++n) {
                if (stream[1] & 2U) next = current + 1U;
                else if (!fat_next(fs, current, &next)) return 0;
                if (!exfat_write_fat(fs, current, 0)) return 0;
                if (!cluster_valid(fs, next)) break;
                current = next;
            }
            first = 0;
        } else {
            uint32_t retained = first;
            for (uint64_t n = 1; n < new_count; ++n) {
                if (stream[1] & 2U) next = retained + 1U;
                else if (!fat_next(fs, retained, &next)) return 0;
                retained = next;
            }
            if (stream[1] & 2U) next = retained + 1U;
            else if (!fat_next(fs, retained, &next)) return 0;
            if (!exfat_write_fat(fs, retained, EXFAT_END)) return 0;
            for (uint64_t n = new_count; n < old_count; ++n) {
                uint32_t current = next;
                if (!exfat_write_fat(fs, current, 0)) return 0;
                if (stream[1] & 2U) next = current + 1U;
                else if (!fat_next(fs, current, &next)) break;
                if (!cluster_valid(fs, next)) break;
            }
            stream[1] &= (uint8_t)~2U;
        }
    }
    store32(&stream[20], first);
    store64(&stream[8], new_size);
    store64(&stream[24], new_size);
    uint16_t checksum = entry_set_checksum(directory, index, secondary_count);
    directory[index * 32U + 2] = (uint8_t)checksum;
    directory[index * 32U + 3] = (uint8_t)(checksum >> 8);
    uint64_t lba = fs->heap_start + (uint64_t)(entry_cluster - 2U) * fs->sectors_per_cluster;
    return storage_write(fs->device, lba, fs->sectors_per_cluster, directory);
}

int exfat_read_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                 const char *name, uint64_t offset,
                                 void *buffer, uint32_t size) {
    uint32_t cluster = 0; uint64_t file_size = 0; uint8_t no_fat_chain = 0;
    if (!exfat_lookup_in_directory(fs, directory_cluster, name, &cluster,
                                   &file_size, &no_fat_chain)) return 0;
    return exfat_read_file_cluster(fs, cluster, file_size, no_fat_chain,
                                   offset, buffer, size);
}

int exfat_write_file(exfat_fs_t *fs, const char *name, uint64_t offset,
                     const void *buffer, uint32_t size) {
    return exfat_write_file_in_directory(fs, fs ? fs->root_cluster : 0,
                                         name, offset, buffer, size);
}

int exfat_write_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                  const char *name, uint64_t offset,
                                  const void *buffer, uint32_t size) {
    uint32_t cluster = 0;
    uint64_t file_size = 0;
    uint8_t no_fat_chain = 0;
    if (!exfat_lookup_in_directory(fs, directory_cluster, name, &cluster,
                                   &file_size, &no_fat_chain) ||
        !buffer || size == 0 || offset > UINT64_MAX - size ||
        !exfat_resize_file(fs, directory_cluster, name,
                           file_size > offset + size ? file_size : offset + size) ||
        !exfat_lookup_in_directory(fs, directory_cluster, name, &cluster,
                                   &file_size, &no_fat_chain) ||
        offset > file_size || (uint64_t)size > file_size - offset ||
        !cluster_valid(fs, cluster))
        return 0;
    uint32_t cluster_bytes = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE;
    uint64_t cluster_index = offset / cluster_bytes;
    uint32_t in_cluster = (uint32_t)(offset % cluster_bytes);
    for (uint64_t i = 0; i < cluster_index; ++i) {
        uint32_t next = 0;
        if (no_fat_chain) {
            if (cluster == UINT32_MAX) return 0;
            next = cluster + 1U;
        } else if (!fat_next(fs, cluster, &next)) return 0;
        if (!cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    const uint8_t *source = (const uint8_t *)buffer;
    uint8_t sector_data[EXFAT_SECTOR_SIZE];
    uint32_t remaining = size;
    while (remaining != 0) {
        uint64_t sector = fs->heap_start +
            (uint64_t)(cluster - 2U) * fs->sectors_per_cluster +
            in_cluster / EXFAT_SECTOR_SIZE;
        uint32_t in_sector = in_cluster % EXFAT_SECTOR_SIZE;
        uint32_t chunk = EXFAT_SECTOR_SIZE - in_sector;
        if (chunk > remaining) chunk = remaining;
        if (!read_sector(fs, sector, sector_data)) return 0;
        for (uint32_t i = 0; i < chunk; ++i)
            sector_data[in_sector + i] = source[i];
        if (!storage_write(fs->device, sector, 1, sector_data)) return 0;
        source += chunk;
        remaining -= chunk;
        in_cluster += chunk;
        if (remaining != 0 && in_cluster == cluster_bytes) {
            uint32_t next = 0;
            if (no_fat_chain) {
                if (cluster == UINT32_MAX) return 0;
                next = cluster + 1U;
            } else if (!fat_next(fs, cluster, &next)) return 0;
            if (!cluster_valid(fs, next)) return 0;
            cluster = next;
            in_cluster = 0;
        }
    }
    return 1;
}

int exfat_truncate_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                     const char *name, uint64_t size) {
    return exfat_resize_file(fs, directory_cluster, name, size);
}

int exfat_set_attributes_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                      const char *name, uint16_t attributes) {
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES];
    uint32_t index, secondary_count, entry_cluster;
    if (!fs || !fs->mounted || !exfat_locate_entry(fs, directory_cluster, name,
                                                   directory, &index,
                                                   &secondary_count,
                                                   &entry_cluster)) return 0;
    store16(&directory[index * 32U + 4], attributes);
    uint16_t checksum = entry_set_checksum(directory, index, secondary_count);
    store16(&directory[index * 32U + 2], checksum);
    uint64_t lba = fs->heap_start + (uint64_t)(entry_cluster - 2U) *
                   fs->sectors_per_cluster;
    return storage_write(fs->device, lba, fs->sectors_per_cluster, directory);
}

static int exfat_find_free_entries(exfat_fs_t *fs, uint32_t directory_cluster,
                                   uint32_t needed, uint8_t *directory,
                                   uint32_t *index, uint32_t *entry_cluster) {
    uint32_t cluster = directory_cluster;
    if (!fs || !directory || !index || !entry_cluster || needed == 0) return 0;
    for (uint32_t visited = 0; visited < fs->cluster_count; ++visited) {
        if (!exfat_read_cluster(fs, cluster, directory)) return 0;
        uint32_t entries = fs->sectors_per_cluster * EXFAT_SECTOR_SIZE / 32U;
        for (uint32_t i = 0; i + needed <= entries; ++i) {
            uint8_t free_run = 1;
            for (uint32_t n = 0; n < needed; ++n)
                if (directory[(i + n) * 32U] != 0 &&
                    directory[(i + n) * 32U] != 0x05) free_run = 0;
            if (free_run) { *index = i; *entry_cluster = cluster; return 1; }
        }
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= EXFAT_END ||
            next == EXFAT_BAD || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

static void exfat_free_chain(exfat_fs_t *fs, uint32_t first, uint8_t no_fat_chain) {
    uint32_t cluster = first;
    for (uint32_t count = 0; fs && cluster_valid(fs, cluster) &&
                              count < fs->cluster_count; ++count) {
        uint32_t next = cluster + 1U;
        if (!no_fat_chain && !fat_next(fs, cluster, &next)) return;
        if (!exfat_write_fat(fs, cluster, 0)) return;
        if (no_fat_chain || next >= EXFAT_END || !cluster_valid(fs, next)) return;
        cluster = next;
    }
}

int exfat_create_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                   const char *name, uint16_t attributes) {
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES]; uint16_t units[255];
    uint32_t name_length = 0, index = 0, entry_cluster = 0;
    if (!fs || !fs->mounted || !cluster_valid(fs, directory_cluster) ||
        !exfat_encode_name(name, units, &name_length)) return 0;
    uint32_t secondary = 1U + (name_length + 14U) / 15U;
    uint32_t total = secondary + 1U;
    if (!exfat_find_free_entries(fs, directory_cluster, total, directory,
                                 &index, &entry_cluster)) return 0;
    for (uint32_t n = 0; n < total * 32U; ++n) directory[index * 32U + n] = 0;
    uint8_t *primary = &directory[index * 32U];
    primary[0] = 0x85; primary[1] = (uint8_t)secondary;
    store16(&primary[4], attributes);
    uint8_t *stream = &directory[(index + 1U) * 32U];
    stream[0] = 0xc0; stream[1] = 0; stream[3] = (uint8_t)name_length;
    for (uint32_t n = 0; n < name_length; ++n)
        store16(&directory[(index + 2U + n / 15U) * 32U + 2U + (n % 15U) * 2U], units[n]);
    for (uint32_t n = 0; n < (secondary - 1U) * 15U; ++n)
        if (n >= name_length) store16(&directory[(index + 2U + n / 15U) * 32U + 2U + (n % 15U) * 2U], 0xffffU);
    for (uint32_t n = 0; n < secondary - 1U; ++n)
        directory[(index + 2U + n) * 32U] = 0xc1;
    store16(&primary[2], entry_set_checksum(directory, index, secondary));
    uint64_t lba = fs->heap_start + (uint64_t)(entry_cluster - 2U) * fs->sectors_per_cluster;
    return storage_write(fs->device, lba, fs->sectors_per_cluster, directory);
}

int exfat_create_directory_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                         const char *name) {
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES];
    uint16_t units[255];
    uint32_t name_length = 0, index = 0, entry_cluster = 0, child = 0;
    if (!fs || !fs->mounted || !cluster_valid(fs, directory_cluster) ||
        !exfat_encode_name(name, units, &name_length)) return 0;
    uint32_t secondary = 1U + (name_length + 14U) / 15U;
    uint32_t total = secondary + 1U;
    if (!exfat_find_free_entries(fs, directory_cluster, total, directory,
                                 &index, &entry_cluster) ||
        !exfat_find_free(fs, &child) || !exfat_write_fat(fs, child, EXFAT_END) ||
        !exfat_zero_cluster(fs, child)) {
        if (child) exfat_free_chain(fs, child, 0);
        return 0;
    }
    for (uint32_t n = 0; n < total * 32U; ++n) directory[index * 32U + n] = 0;
    uint8_t *primary = &directory[index * 32U];
    primary[0] = 0x85; primary[1] = (uint8_t)secondary;
    store16(&primary[4], 0x0010U);
    uint8_t *stream = &directory[(index + 1U) * 32U];
    stream[0] = 0xc0; stream[1] = 0; stream[3] = (uint8_t)name_length;
    store32(&stream[20], child);
    store64(&stream[8], 0); store64(&stream[24], 0);
    for (uint32_t n = 0; n < name_length; ++n)
        store16(&directory[(index + 2U + n / 15U) * 32U + 2U +
                           (n % 15U) * 2U], units[n]);
    for (uint32_t n = 0; n < secondary - 1U; ++n)
        directory[(index + 2U + n) * 32U] = 0xc1;
    uint16_t checksum = entry_set_checksum(directory, index, secondary);
    store16(&primary[2], checksum);
    uint64_t lba = fs->heap_start + (uint64_t)(entry_cluster - 2U) *
                   fs->sectors_per_cluster;
    if (!storage_write(fs->device, lba, fs->sectors_per_cluster, directory)) {
        exfat_free_chain(fs, child, 0);
        return 0;
    }
    return 1;
}

int exfat_unlink_file_in_directory(exfat_fs_t *fs, uint32_t directory_cluster,
                                   const char *name) {
    uint8_t directory[EXFAT_MAX_CLUSTER_BYTES]; uint32_t index, secondary, cluster;
    if (!fs || !fs->mounted || !exfat_locate_entry(fs, directory_cluster, name,
                                                   directory, &index, &secondary,
                                                   &cluster)) return 0;
    uint8_t *stream = &directory[(index + 1U) * 32U];
    uint32_t first = load32(&stream[20]); uint8_t no_fat = (stream[1] & 2U) != 0;
    if ((load16(&directory[index * 32U + 4]) & 0x0010U) != 0 &&
        (!first || !exfat_directory_empty(fs, first))) return 0;
    for (uint32_t n = 0; n <= secondary; ++n) directory[(index + n) * 32U] &= 0x7fU;
    uint64_t lba = fs->heap_start + (uint64_t)(cluster - 2U) * fs->sectors_per_cluster;
    if (!storage_write(fs->device, lba, fs->sectors_per_cluster, directory)) return 0;
    if (first) exfat_free_chain(fs, first, no_fat);
    return 1;
}
