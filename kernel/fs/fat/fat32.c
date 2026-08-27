#include "fat32.h"
#include "../../drivers/storage/storage.h"

static uint16_t load16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t load32(const uint8_t *data) {
    return (uint32_t)load16(data) | ((uint32_t)load16(data + 2) << 16);
}

static int read_sector(uint32_t device, uint32_t sector, void *buffer) {
    return storage_read(device, sector, 1, buffer);
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
    fs->fat_sector_number = 0;
    fs->fat_sector_valid = 0;
    spinlock_init(&fs->fat_lock);
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
