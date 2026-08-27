#include "fat32.h"
#include "../../drivers/storage/storage.h"

static uint16_t load16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t load32(const uint8_t *data) {
    return (uint32_t)load16(data) | ((uint32_t)load16(data + 2) << 16);
}

static void store32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value; data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16); data[3] = (uint8_t)(value >> 24);
}

static int read_sector(uint32_t device, uint32_t sector, void *buffer) {
    return storage_read(device, sector, 1, buffer);
}

static void fat32_fsinfo_adjust(fat32_fs_t *fs, int32_t delta, uint32_t hint) {
    if (!fs || !fs->fsinfo_valid) return;
    uint8_t data[FAT32_SECTOR_SIZE];
    if (!read_sector(fs->device, fs->fsinfo_sector, data) ||
        load32(data) != 0x41615252U || load32(&data[484]) != 0x61417272U ||
        load32(&data[508]) != 0xaa550000U) return;
    if (delta < 0) {
        uint32_t amount = (uint32_t)(-delta);
        if (fs->fsinfo_free_count != 0xffffffffU && fs->fsinfo_free_count >= amount)
            fs->fsinfo_free_count -= amount;
        else if (fs->fsinfo_free_count != 0xffffffffU)
            fs->fsinfo_free_count = 0xffffffffU;
    } else if (fs->fsinfo_free_count != 0xffffffffU &&
               fs->fsinfo_free_count <= fs->data_clusters - (uint32_t)delta) {
        fs->fsinfo_free_count += (uint32_t)delta;
    } else if (fs->fsinfo_free_count != 0xffffffffU) {
        fs->fsinfo_free_count = 0xffffffffU;
    }
    if (hint >= 2 && hint < fs->data_clusters + 2U)
        fs->fsinfo_next_free = hint;
    store32(&data[488], fs->fsinfo_free_count);
    store32(&data[492], fs->fsinfo_next_free);
    (void)storage_write(fs->device, fs->fsinfo_sector, 1, data);
}

static int cluster_valid(const fat32_fs_t *fs, uint32_t cluster) {
    return fs && cluster >= 2 && cluster < fs->data_clusters + 2;
}

static int utf8_next(const char **text, uint32_t *codepoint) {
    const uint8_t *p = (const uint8_t *)*text; uint32_t value, length;
    if (!p[0]) return 0;
    if (p[0] < 0x80) { value = p[0]; length = 1; }
    else if (p[0] >= 0xc2 && p[0] <= 0xdf) { value = p[0] & 0x1fU; length = 2; }
    else if (p[0] >= 0xe0 && p[0] <= 0xef) { value = p[0] & 0x0fU; length = 3; }
    else if (p[0] >= 0xf0 && p[0] <= 0xf4) { value = p[0] & 7U; length = 4; }
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

static int lfn_equal(const uint16_t *units, uint32_t count, const char *name) {
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
            uint16_t high = (uint16_t)(0xd800U + ((value - 0x10000U) >> 10));
            uint16_t low = (uint16_t)(0xdc00U + ((value - 0x10000U) & 0x3ffU));
            if (i + 1U >= count || units[i] != high || units[++i] != low) return 0;
        }
    }
    return *cursor == 0;
}

static uint8_t lfn_checksum(const uint8_t short_name[11]) {
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < 11; ++i)
        checksum = (uint8_t)(((checksum & 1U) ? 0x80U : 0U) + (checksum >> 1) + short_name[i]);
    return checksum;
}

static void lfn_units(const uint8_t *entry, uint16_t *units, uint32_t base) {
    for (uint32_t i = 0; i < 5; ++i) units[base + i] = load16(&entry[1 + i * 2U]);
    for (uint32_t i = 0; i < 6; ++i) units[base + 5U + i] = load16(&entry[14 + i * 2U]);
    for (uint32_t i = 0; i < 2; ++i) units[base + 11U + i] = load16(&entry[28 + i * 2U]);
}

static int fat_load_sector(fat32_fs_t *fs, uint32_t sector) {
    if (!fs || sector >= fs->sectors_per_fat) return 0;
    if (fs->fat_sector_valid && fs->fat_sector_number == sector) return 1;
    if (!read_sector(fs->device, fs->fat_start + sector, fs->fat_sector)) return 0;
    fs->fat_sector_number = sector;
    fs->fat_sector_valid = 1;
    return 1;
}

static int fat_next_locked(fat32_fs_t *fs, uint32_t cluster, uint32_t *next) {
    uint32_t byte_offset = cluster * 4U;
    uint32_t sector = byte_offset / FAT32_SECTOR_SIZE;
    uint32_t offset = byte_offset % FAT32_SECTOR_SIZE;
    uint8_t bytes[4];
    if (!next || !fat_load_sector(fs, sector)) return 0;
    for (uint32_t i = 0; i < 4; ++i) {
        if (offset + i < FAT32_SECTOR_SIZE) bytes[i] = fs->fat_sector[offset + i];
        else {
            if (!fat_load_sector(fs, sector + 1)) return 0;
            bytes[i] = fs->fat_sector[offset + i - FAT32_SECTOR_SIZE];
        }
    }
    *next = load32(bytes) & 0x0fffffffU;
    return 1;
}

static int fat_next(fat32_fs_t *fs, uint32_t cluster, uint32_t *next) {
    if (!fs || !next) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->fat_lock);
    int result = fat_next_locked(fs, cluster, next);
    spinlock_unlock_irqrestore(&fs->fat_lock, flags);
    return result;
}

static int fat_set(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (!fs || !cluster_valid(fs, cluster)) return 0;
    uint32_t byte_offset = cluster * 4U;
    uint32_t sector = byte_offset / FAT32_SECTOR_SIZE;
    uint32_t offset = byte_offset % FAT32_SECTOR_SIZE;
    if (sector >= fs->sectors_per_fat ||
        (offset > FAT32_SECTOR_SIZE - 4U &&
         sector + 1U >= fs->sectors_per_fat))
        return 0;
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    uint64_t flags = spinlock_lock_irqsave(&fs->fat_lock);
    for (uint32_t copy = 0; copy < fs->fat_count; ++copy) {
        uint32_t base = fs->fat_start + copy * fs->sectors_per_fat;
        uint8_t first[FAT32_SECTOR_SIZE], second[FAT32_SECTOR_SIZE];
        if (!read_sector(fs->device, base + sector, first)) {
            spinlock_unlock_irqrestore(&fs->fat_lock, flags); return 0;
        }
        if (offset > FAT32_SECTOR_SIZE - 4U) {
            if (!read_sector(fs->device, base + sector + 1U, second)) {
                spinlock_unlock_irqrestore(&fs->fat_lock, flags); return 0;
            }
            for (uint32_t i = 0; i < 4; ++i) {
                if (i < FAT32_SECTOR_SIZE - offset)
                    first[offset + i] = bytes[i];
                else
                    second[offset + i - FAT32_SECTOR_SIZE] = bytes[i];
            }
            if (!storage_write(fs->device, base + sector, 1, first) ||
                !storage_write(fs->device, base + sector + 1U, 1, second)) {
                spinlock_unlock_irqrestore(&fs->fat_lock, flags); return 0;
            }
        } else {
            for (uint32_t i = 0; i < 4; ++i) first[offset + i] = bytes[i];
            if (!storage_write(fs->device, base + sector, 1, first)) {
                spinlock_unlock_irqrestore(&fs->fat_lock, flags); return 0;
            }
        }
    }
    fs->fat_sector_valid = 0;
    spinlock_unlock_irqrestore(&fs->fat_lock, flags);
    return 1;
}

int fat32_mount(fat32_fs_t *fs, uint32_t device) {
    if (!fs || !storage_device_at(device) ||
        storage_device_at(device)->block_size != FAT32_SECTOR_SIZE) return 0;
    uint8_t boot[FAT32_SECTOR_SIZE];
    if (!read_sector(device, 0, boot) || boot[510] != 0x55 || boot[511] != 0xaa ||
        load16(&boot[11]) != FAT32_SECTOR_SIZE || boot[13] == 0 ||
        boot[16] == 0 || load16(&boot[14]) == 0 || load32(&boot[36]) == 0 ||
        load32(&boot[44]) < 2 || load16(&boot[42]) != 0) return 0;
    uint32_t total = load16(&boot[19]);
    if (total == 0) total = load32(&boot[32]);
    uint32_t fat_start = load16(&boot[14]);
    uint32_t fat_size = load32(&boot[36]);
    uint64_t data_start = fat_start + (uint64_t)boot[16] * fat_size;
    uint64_t data_clusters = data_start < total && boot[13] != 0 ?
                             (total - data_start) / boot[13] : 0;
    uint64_t device_sectors = storage_device_at(device)->block_count;
    if (total <= data_start || total > device_sectors ||
        data_start > UINT32_MAX || boot[13] > FAT32_MAX_SECTORS_PER_CLUSTER ||
        fat_size > total - fat_start || data_clusters < 65525U ||
        data_clusters > 0x0ffffff5ULL ||
        (uint64_t)fat_size * FAT32_SECTOR_SIZE / 4U < data_clusters + 2U)
        return 0;
    fs->device = device;
    fs->bytes_per_sector = FAT32_SECTOR_SIZE;
    fs->sectors_per_cluster = boot[13];
    fs->reserved_sectors = load16(&boot[14]);
    fs->fat_count = boot[16];
    fs->sectors_per_fat = fat_size;
    fs->fat_start = fat_start;
    fs->data_start = (uint32_t)data_start;
    fs->root_cluster = load32(&boot[44]) & 0x0fffffffU;
    fs->total_sectors = total;
    fs->data_clusters = (uint32_t)data_clusters;
    fs->fsinfo_sector = load16(&boot[48]);
    fs->fsinfo_free_count = 0xffffffffU;
    fs->fsinfo_next_free = 0xffffffffU;
    fs->fsinfo_valid = 0;
    if (fs->fsinfo_sector != 0 && fs->fsinfo_sector < fs->reserved_sectors) {
        uint8_t fsinfo[FAT32_SECTOR_SIZE];
        if (read_sector(device, fs->fsinfo_sector, fsinfo) &&
            load32(fsinfo) == 0x41615252U && load32(&fsinfo[484]) == 0x61417272U &&
            load32(&fsinfo[508]) == 0xaa550000U) {
            fs->fsinfo_free_count = load32(&fsinfo[488]);
            fs->fsinfo_next_free = load32(&fsinfo[492]);
            if (fs->fsinfo_free_count == 0xffffffffU ||
                fs->fsinfo_free_count <= fs->data_clusters) {
                fs->fsinfo_valid = 1;
            }
        }
    }
    fs->fat_sector_number = 0;
    fs->fat_sector_valid = 0;
    spinlock_init(&fs->fat_lock);
    spinlock_init(&fs->write_lock);
    fs->mounted = cluster_valid(fs, fs->root_cluster);
    return fs->mounted;
}

int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, void *buffer) {
    if (!fs || !fs->mounted || !buffer || !cluster_valid(fs, cluster)) return 0;
    uint32_t sector = fs->data_start + (cluster - 2U) * fs->sectors_per_cluster;
    return storage_read(fs->device, sector, fs->sectors_per_cluster, buffer);
}

int fat32_lookup_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                              const char short_name[11], uint32_t *first_cluster,
                              uint32_t *size, uint8_t *is_directory) {
    if (!fs || !fs->mounted || !cluster_valid(fs, directory_cluster) || !short_name ||
        !first_cluster || !size || !is_directory) return 0;
    uint8_t directory[FAT32_MAX_SECTORS_PER_CLUSTER * FAT32_SECTOR_SIZE];
    uint32_t cluster = directory_cluster;
    for (uint32_t hops = 0; hops < fs->data_clusters; ++hops) {
        if (!fat32_read_cluster(fs, cluster, directory)) return 0;
        for (uint32_t offset = 0;
             offset < fs->sectors_per_cluster * FAT32_SECTOR_SIZE; offset += 32) {
            uint8_t first = directory[offset];
            uint8_t attributes = directory[offset + 11];
            if (first == 0x00) return 0;
            if (first == 0xe5 || attributes == 0x0f || (attributes & 0x08) != 0) continue;
            uint8_t match = 1;
            for (uint32_t byte = 0; byte < 11; ++byte)
                if (directory[offset + byte] != (uint8_t)short_name[byte]) match = 0;
            if (match) {
                *first_cluster = ((load32(&directory[offset + 20]) & 0x0fffU) << 16) |
                                  load16(&directory[offset + 26]);
                *size = load32(&directory[offset + 28]);
                *is_directory = (uint8_t)((attributes & 0x10) != 0);
                return cluster_valid(fs, *first_cluster) || (!*is_directory && *size == 0);
            }
        }
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U) return 0;
        if (next == 0x0ffffff7U || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

int fat32_lookup(fat32_fs_t *fs, const char short_name[11],
                 uint32_t *first_cluster, uint32_t *size) {
    uint8_t is_directory = 0;
    return fat32_lookup_in_directory(fs, fs ? fs->root_cluster : 0, short_name,
                                     first_cluster, size, &is_directory) && !is_directory;
}

int fat32_lookup_name_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                   const char *name, uint32_t *first_cluster,
                                   uint32_t *size, uint8_t *is_directory) {
    uint16_t units[260] = {0}; uint32_t slots = 0, expected = 0; uint8_t checksum = 0, valid = 0;
    uint8_t directory[FAT32_MAX_SECTORS_PER_CLUSTER * FAT32_SECTOR_SIZE];
    if (!fs || !fs->mounted || !cluster_valid(fs, directory_cluster) || !name || !name[0] ||
        !first_cluster || !size || !is_directory) return 0;
    uint32_t cluster = directory_cluster;
    for (uint32_t hops = 0; hops < fs->data_clusters; ++hops) {
        if (!fat32_read_cluster(fs, cluster, directory)) return 0;
        for (uint32_t offset = 0; offset < fs->sectors_per_cluster * FAT32_SECTOR_SIZE; offset += 32U) {
            const uint8_t *entry = &directory[offset]; uint8_t first = entry[0];
            uint8_t attributes = entry[11];
            if (first == 0x00) return 0;
            if (first == 0xe5) { valid = 0; continue; }
            if (attributes == 0x0f) {
                uint8_t order = first & 0x1fU;
                if (order == 0 || order > 20U || (first & 0x40U)) {
                    valid = (uint8_t)((first & 0x40U) != 0 && order != 0 && order <= 20U);
                    expected = order; slots = order; checksum = entry[13];
                } else if (!valid || order != expected || entry[13] != checksum) valid = 0;
                if (valid) { lfn_units(entry, units, (order - 1U) * 13U); expected = order - 1U; }
                continue;
            }
            if ((attributes & 0x08) != 0) { valid = 0; continue; }
            uint8_t short_name[11]; for (uint32_t i = 0; i < 11; ++i) short_name[i] = entry[i];
            if (valid && expected == 0 && lfn_checksum(short_name) == checksum) {
                uint32_t count = slots * 13U;
                while (count && (units[count - 1U] == 0x0000U || units[count - 1U] == 0xffffU)) --count;
                if (count && lfn_equal(units, count, name)) {
                    *first_cluster = ((load32(&entry[20]) & 0x0fffU) << 16) | load16(&entry[26]);
                    *size = load32(&entry[28]); *is_directory = (uint8_t)((attributes & 0x10) != 0);
                    return cluster_valid(fs, *first_cluster) || (!*is_directory && *size == 0);
                }
            }
            valid = 0;
        }
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U) return 0;
        if (next == 0x0ffffff7U || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

static int fat32_read_file_cluster(fat32_fs_t *fs, uint32_t cluster, uint32_t file_size,
                                   uint32_t offset, void *buffer, uint32_t size) {
    if (!fs || !fs->mounted || !buffer || !cluster_valid(fs, cluster) ||
        offset > file_size || size > file_size - offset) return 0;
    if (size == 0) return 1;
    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t skip = offset / cluster_size;
    uint32_t inside = offset % cluster_size;
    for (uint32_t i = 0; i < skip; ++i) {
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
            !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    uint8_t cluster_data[FAT32_MAX_SECTORS_PER_CLUSTER * FAT32_SECTOR_SIZE];
    uint8_t *output = (uint8_t *)buffer;
    uint32_t remaining = size;
    for (uint32_t hops = 0; remaining != 0 && hops < fs->data_clusters; ++hops) {
        if (!fat32_read_cluster(fs, cluster, cluster_data)) return 0;
        uint32_t available = cluster_size - inside;
        uint32_t copy = remaining < available ? remaining : available;
        for (uint32_t i = 0; i < copy; ++i) output[i] = cluster_data[inside + i];
        output += copy;
        remaining -= copy;
        inside = 0;
        if (remaining != 0) {
            uint32_t next;
            if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
                !cluster_valid(fs, next)) return 0;
            cluster = next;
        }
    }
    return remaining == 0;
}

int fat32_read_file(fat32_fs_t *fs, const char short_name[11],
                    uint32_t offset, void *buffer, uint32_t size) {
    uint32_t cluster, file_size;
    if (!fat32_lookup(fs, short_name, &cluster, &file_size)) return 0;
    return fat32_read_file_cluster(fs, cluster, file_size, offset, buffer, size);
}

static int fat32_write_file_unlocked(fat32_fs_t *fs, uint32_t directory_cluster,
                                     const char short_name[11], uint32_t offset,
                                     const void *buffer, uint32_t size) {
    uint32_t cluster, file_size; uint8_t is_directory = 0;
    if (!fs || !fs->mounted || !buffer || size == 0 ||
        !fat32_lookup_in_directory(fs, directory_cluster, short_name, &cluster,
                                   &file_size, &is_directory) || is_directory ||
        offset > file_size || size > file_size - offset) return 0;
    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t skip = offset / cluster_size, inside = offset % cluster_size;
    for (uint32_t i = 0; i < skip; ++i) {
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
            !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    const uint8_t *input = (const uint8_t *)buffer;
    uint8_t sector_data[FAT32_SECTOR_SIZE];
    uint32_t remaining = size;
    for (uint32_t hops = 0; remaining != 0 && hops < fs->data_clusters; ++hops) {
        uint64_t sector = (uint64_t)fs->data_start +
                          (uint64_t)(cluster - 2U) * fs->sectors_per_cluster +
                          inside / FAT32_SECTOR_SIZE;
        uint32_t sector_offset = inside % FAT32_SECTOR_SIZE;
        uint32_t copy = FAT32_SECTOR_SIZE - sector_offset;
        if (copy > remaining) copy = remaining;
        if (sector > UINT32_MAX || !read_sector(fs->device, (uint32_t)sector,
                                                sector_data)) return 0;
        for (uint32_t i = 0; i < copy; ++i)
            sector_data[sector_offset + i] = input[i];
        if (!storage_write(fs->device, (uint32_t)sector, 1, sector_data)) return 0;
        input += copy; remaining -= copy; inside += copy;
        if (remaining != 0 && inside == cluster_size) {
            uint32_t next;
            if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
                !cluster_valid(fs, next)) return 0;
            cluster = next; inside = 0;
        }
    }
    return remaining == 0;
}

int fat32_write_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                  const char short_name[11], uint32_t offset,
                                  const void *buffer, uint32_t size) {
    uint32_t cluster = 0, file_size = 0; uint8_t is_directory = 0;
    if (!fs || !fs->mounted || !buffer || size == 0 ||
        !fat32_lookup_in_directory(fs, directory_cluster, short_name, &cluster,
                                   &file_size, &is_directory) || is_directory ||
        offset > file_size || size > file_size - offset) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->write_lock);
    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t skip = offset / cluster_size, inside = offset % cluster_size;
    for (uint32_t i = 0; i < skip; ++i) {
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
            !cluster_valid(fs, next)) {
            spinlock_unlock_irqrestore(&fs->write_lock, flags);
            return 0;
        }
        cluster = next;
    }
    const uint8_t *input = (const uint8_t *)buffer;
    uint8_t sector_data[FAT32_SECTOR_SIZE];
    uint32_t remaining = size;
    for (uint32_t hops = 0; remaining != 0 && hops < fs->data_clusters; ++hops) {
        uint64_t sector = (uint64_t)fs->data_start +
                          (uint64_t)(cluster - 2U) * fs->sectors_per_cluster +
                          inside / FAT32_SECTOR_SIZE;
        uint32_t sector_offset = inside % FAT32_SECTOR_SIZE;
        uint32_t copy = FAT32_SECTOR_SIZE - sector_offset;
        if (copy > remaining) copy = remaining;
        if (sector > UINT32_MAX || !read_sector(fs->device, (uint32_t)sector,
                                                sector_data)) break;
        for (uint32_t i = 0; i < copy; ++i)
            sector_data[sector_offset + i] = input[i];
        if (!storage_write(fs->device, (uint32_t)sector, 1, sector_data)) break;
        input += copy; remaining -= copy; inside += copy;
        if (remaining != 0 && inside == cluster_size) {
            uint32_t next;
            if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
                !cluster_valid(fs, next)) break;
            cluster = next; inside = 0;
        }
    }
    int result = remaining == 0;
    spinlock_unlock_irqrestore(&fs->write_lock, flags);
    return result;
}

int fat32_write_file(fat32_fs_t *fs, const char short_name[11],
                     uint32_t offset, const void *buffer, uint32_t size) {
    if (!fs) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->write_lock);
    int result = fat32_write_file_unlocked(fs, fs->root_cluster, short_name,
                                           offset, buffer, size);
    spinlock_unlock_irqrestore(&fs->write_lock, flags);
    return result;
}

static int fat32_find_entry(fat32_fs_t *fs, uint32_t directory_cluster,
                            const char short_name[11], uint32_t *sector,
                            uint32_t *entry_offset, uint32_t *first_cluster,
                            uint32_t *size) {
    if (!fs || !short_name || !sector || !entry_offset || !first_cluster || !size)
        return 0;
    uint8_t directory[FAT32_MAX_SECTORS_PER_CLUSTER * FAT32_SECTOR_SIZE];
    if (!cluster_valid(fs, directory_cluster)) return 0;
    uint32_t cluster = directory_cluster;
    for (uint32_t hops = 0; hops < fs->data_clusters; ++hops) {
        if (!fat32_read_cluster(fs, cluster, directory)) return 0;
        uint32_t cluster_bytes = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
        for (uint32_t offset = 0; offset < cluster_bytes; offset += 32U) {
            if (directory[offset] == 0x00) return 0;
            if (directory[offset] == 0xe5 || directory[offset + 11] == 0x0f ||
                (directory[offset + 11] & 0x08U) != 0) continue;
            uint8_t match = 1;
            for (uint32_t i = 0; i < 11; ++i)
                if (directory[offset + i] != (uint8_t)short_name[i]) match = 0;
            if (match) {
                *sector = fs->data_start + (cluster - 2U) * fs->sectors_per_cluster +
                          offset / FAT32_SECTOR_SIZE;
                *entry_offset = offset % FAT32_SECTOR_SIZE;
                *first_cluster = ((load32(&directory[offset + 20]) & 0x0fffU) << 16) |
                                  load16(&directory[offset + 26]);
                *size = load32(&directory[offset + 28]);
                return (directory[offset + 11] & 0x10U) == 0;
            }
        }
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U ||
            !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

static int fat32_update_entry(fat32_fs_t *fs, uint32_t sector,
                              uint32_t offset, uint32_t first_cluster,
                              uint32_t size) {
    uint8_t data[FAT32_SECTOR_SIZE];
    if (!fs || offset > FAT32_SECTOR_SIZE - 32U ||
        !read_sector(fs->device, sector, data)) return 0;
    data[offset + 20] = (uint8_t)(first_cluster >> 16);
    data[offset + 21] = (uint8_t)(first_cluster >> 24);
    data[offset + 26] = (uint8_t)first_cluster;
    data[offset + 27] = (uint8_t)(first_cluster >> 8);
    data[offset + 28] = (uint8_t)size;
    data[offset + 29] = (uint8_t)(size >> 8);
    data[offset + 30] = (uint8_t)(size >> 16);
    data[offset + 31] = (uint8_t)(size >> 24);
    return storage_write(fs->device, sector, 1, data);
}

static int fat32_find_free_cluster(fat32_fs_t *fs, uint32_t *cluster) {
    if (!fs || !cluster) return 0;
    for (uint32_t candidate = 2; candidate < fs->data_clusters + 2U; ++candidate) {
        uint32_t value;
        if (!fat_next(fs, candidate, &value)) return 0;
        if (value == 0) { *cluster = candidate; return 1; }
    }
    return 0;
}

static void fat32_free_chain(fat32_fs_t *fs, uint32_t first) {
    uint32_t cluster = first;
    for (uint32_t hops = 0; fs && cluster_valid(fs, cluster) &&
                              hops < fs->data_clusters; ++hops) {
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || !fat_set(fs, cluster, 0)) return;
        fat32_fsinfo_adjust(fs, 1, cluster + 1U);
        if (next >= 0x0ffffff8U || !cluster_valid(fs, next)) return;
        cluster = next;
    }
}

int fat32_truncate_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                     const char short_name[11], uint32_t size) {
    if (!fs || !short_name) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->write_lock);
    uint32_t entry_sector, entry_offset, first, old_size;
    int result = 0;
    if (!fat32_find_entry(fs, directory_cluster, short_name, &entry_sector,
                          &entry_offset, &first, &old_size) || size > old_size)
        goto done;
    if (size == old_size) { result = 1; goto done; }

    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t required = (uint32_t)(((uint64_t)size + cluster_size - 1U) /
                                   cluster_size);
    uint32_t retained = 0, detached = first, current = first;
    for (uint32_t index = 0; index < required; ++index) {
        if (!cluster_valid(fs, current)) goto done;
        retained = current;
        uint32_t next;
        if (!fat_next(fs, current, &next)) goto done;
        if (index + 1U < required) {
            if (next >= 0x0ffffff8U || !cluster_valid(fs, next)) goto done;
            current = next;
        } else {
            detached = next;
        }
    }
    if (required == 0) detached = first;
    if (!fat32_update_entry(fs, entry_sector, entry_offset,
                            required == 0 ? 0 : first, size))
        goto done;
    if (retained && !fat_set(fs, retained, 0x0fffffffU)) goto done;
    if (detached < 0x0ffffff8U && cluster_valid(fs, detached))
        fat32_free_chain(fs, detached);
    result = 1;
done:
    spinlock_unlock_irqrestore(&fs->write_lock, flags);
    return result;
}

int fat32_append_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                   const char short_name[11],
                                   const void *buffer, uint32_t size) {
    if (!fs || !buffer || size == 0) return 0;
    uint64_t flags = spinlock_lock_irqsave(&fs->write_lock);
    uint32_t entry_sector, entry_offset, first, old_size;
    int result = 0;
    if (!fat32_find_entry(fs, directory_cluster, short_name, &entry_sector,
                          &entry_offset, &first, &old_size) ||
        size > UINT32_MAX - old_size) goto done;
    uint32_t cluster_size = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t old_count = 0, tail = 0, current = first;
    while (cluster_valid(fs, current) && old_count < fs->data_clusters) {
        tail = current; ++old_count;
        uint32_t next;
        if (!fat_next(fs, current, &next)) goto done;
        if (next >= 0x0ffffff8U) break;
        if (!cluster_valid(fs, next)) goto done;
        current = next;
    }
    if (old_size != 0 && old_count == 0) goto done;
    uint64_t required64 = ((uint64_t)old_size + size + cluster_size - 1U) /
                          cluster_size;
    if (required64 > fs->data_clusters) goto done;
    uint32_t required = (uint32_t)required64;
    uint32_t added_first = 0, added_last = 0;
    while (old_count < required) {
        uint32_t fresh;
        if (!fat32_find_free_cluster(fs, &fresh) || !fat_set(fs, fresh, 0x0fffffffU)) {
            if (added_first) fat32_free_chain(fs, added_first);
            goto done;
        }
        fat32_fsinfo_adjust(fs, -1, fresh + 1U);
        if (!added_first) added_first = fresh;
        if (added_last && !fat_set(fs, added_last, fresh)) {
            (void)fat_set(fs, fresh, 0);
            fat32_free_chain(fs, added_first); goto done;
        }
        added_last = fresh; ++old_count;
    }
    if (added_first && tail && !fat_set(fs, tail, added_first)) {
        fat32_free_chain(fs, added_first); goto done;
    }
    if (added_first && !first) first = added_first;
    if (!fat32_update_entry(fs, entry_sector, entry_offset, first,
                            old_size + size)) {
        if (tail) (void)fat_set(fs, tail, 0x0fffffffU);
        if (added_first) fat32_free_chain(fs, added_first);
        goto done;
    }
    result = fat32_write_file_unlocked(fs, directory_cluster, short_name,
                                       old_size, buffer, size);
    if (!result) {
        (void)fat32_update_entry(fs, entry_sector, entry_offset, first, old_size);
        if (tail) (void)fat_set(fs, tail, 0x0fffffffU);
        if (added_first) fat32_free_chain(fs, added_first);
    }
done:
    spinlock_unlock_irqrestore(&fs->write_lock, flags);
    return result;
}

int fat32_truncate_file(fat32_fs_t *fs, const char short_name[11],
                        uint32_t size) {
    return fat32_truncate_file_in_directory(fs, fs ? fs->root_cluster : 0,
                                            short_name, size);
}

int fat32_append_file(fat32_fs_t *fs, const char short_name[11],
                      const void *buffer, uint32_t size) {
    return fat32_append_file_in_directory(fs, fs ? fs->root_cluster : 0,
                                          short_name, buffer, size);
}

int fat32_read_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                 const char short_name[11], uint32_t offset,
                                 void *buffer, uint32_t size) {
    uint32_t cluster, file_size; uint8_t is_directory = 0;
    if (!fat32_lookup_in_directory(fs, directory_cluster, short_name, &cluster,
                                   &file_size, &is_directory) || is_directory) return 0;
    return fat32_read_file_cluster(fs, cluster, file_size, offset, buffer, size);
}

int fat32_read_named_file_in_directory(fat32_fs_t *fs, uint32_t directory_cluster,
                                       const char *name, uint32_t offset,
                                       void *buffer, uint32_t size) {
    uint32_t cluster, file_size; uint8_t is_directory = 0;
    if (!fat32_lookup_name_in_directory(fs, directory_cluster, name, &cluster,
                                        &file_size, &is_directory) || is_directory) return 0;
    return fat32_read_file_cluster(fs, cluster, file_size, offset, buffer, size);
}
