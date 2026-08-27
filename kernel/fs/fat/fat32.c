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

static int fat_load_sector(fat32_fs_t *fs, uint32_t sector) {
    if (!fs || sector >= fs->sectors_per_fat) return 0;
    if (fs->fat_sector_valid && fs->fat_sector_number == sector) return 1;
    if (!read_sector(fs->device, fs->fat_start + sector, fs->fat_sector)) return 0;
    fs->fat_sector_number = sector;
    fs->fat_sector_valid = 1;
    return 1;
}

static int fat_next(fat32_fs_t *fs, uint32_t cluster, uint32_t *next) {
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
    uint32_t data_start = fat_start + (uint32_t)boot[16] * fat_size;
    if (total <= data_start || boot[13] > FAT32_MAX_SECTORS_PER_CLUSTER ||
        fat_size > total - fat_start || data_start > total ||
        (total - data_start) / boot[13] < 65525U) return 0;
    fs->device = device;
    fs->bytes_per_sector = FAT32_SECTOR_SIZE;
    fs->sectors_per_cluster = boot[13];
    fs->reserved_sectors = load16(&boot[14]);
    fs->fat_count = boot[16];
    fs->sectors_per_fat = fat_size;
    fs->fat_start = fat_start;
    fs->data_start = data_start;
    fs->root_cluster = load32(&boot[44]) & 0x0fffffffU;
    fs->total_sectors = total;
    fs->data_clusters = (total - data_start) / fs->sectors_per_cluster;
    fs->fat_sector_number = 0;
    fs->fat_sector_valid = 0;
    fs->mounted = cluster_valid(fs, fs->root_cluster);
    return fs->mounted;
}

int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, void *buffer) {
    if (!fs || !fs->mounted || !buffer || !cluster_valid(fs, cluster)) return 0;
    uint32_t sector = fs->data_start + (cluster - 2U) * fs->sectors_per_cluster;
    return storage_read(fs->device, sector, fs->sectors_per_cluster, buffer);
}

int fat32_lookup(fat32_fs_t *fs, const char short_name[11],
                 uint32_t *first_cluster, uint32_t *size) {
    if (!fs || !fs->mounted || !short_name || !first_cluster || !size) return 0;
    uint8_t directory[FAT32_MAX_SECTORS_PER_CLUSTER * FAT32_SECTOR_SIZE];
    uint32_t cluster = fs->root_cluster;
    for (uint32_t hops = 0; hops < fs->data_clusters; ++hops) {
        if (!fat32_read_cluster(fs, cluster, directory)) return 0;
        for (uint32_t offset = 0;
             offset < fs->sectors_per_cluster * FAT32_SECTOR_SIZE; offset += 32) {
            uint8_t first = directory[offset];
            uint8_t attributes = directory[offset + 11];
            if (first == 0x00) return 0;
            if (first == 0xe5 || attributes == 0x0f || (attributes & 0x08) != 0 ||
                (attributes & 0x10) != 0) continue;
            uint8_t match = 1;
            for (uint32_t byte = 0; byte < 11; ++byte)
                if (directory[offset + byte] != (uint8_t)short_name[byte]) match = 0;
            if (match) {
                *first_cluster = ((load32(&directory[offset + 20]) & 0x0fffU) << 16) |
                                  load16(&directory[offset + 26]);
                *size = load32(&directory[offset + 28]);
                return cluster_valid(fs, *first_cluster) || *size == 0;
            }
        }
        uint32_t next;
        if (!fat_next(fs, cluster, &next) || next >= 0x0ffffff8U) return 0;
        if (next == 0x0ffffff7U || !cluster_valid(fs, next)) return 0;
        cluster = next;
    }
    return 0;
}

int fat32_read_file(fat32_fs_t *fs, const char short_name[11],
                    uint32_t offset, void *buffer, uint32_t size) {
    if (!fs || !buffer || !short_name) return 0;
    uint32_t cluster, file_size;
    if (!fat32_lookup(fs, short_name, &cluster, &file_size) ||
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
