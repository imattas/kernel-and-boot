#include "fat12.h"
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

static uint16_t fat_next(const fat12_fs_t *fs, uint16_t cluster) {
    uint32_t offset = cluster + cluster / 2;
    uint16_t value = load16(&fs->fat[offset]);
    return (cluster & 1) ? (value >> 4) : (value & 0x0fff);
}

int fat12_mount(fat12_fs_t *fs, uint32_t device) {
    if (!fs || !storage_device_at(device) ||
        storage_device_at(device)->block_size != FAT12_SECTOR_SIZE)
        return 0;
    uint8_t boot[FAT12_SECTOR_SIZE];
    if (!read_sector(device, 0, boot) || boot[510] != 0x55 || boot[511] != 0xaa ||
        load16(&boot[11]) != FAT12_SECTOR_SIZE || boot[13] == 0 ||
        load16(&boot[14]) == 0 || boot[16] == 0 || load16(&boot[17]) == 0 ||
        load16(&boot[22]) == 0 || boot[54] != 'F' || boot[55] != 'A' ||
        boot[56] != 'T' || boot[57] != '1' || boot[58] != '2') return 0;
    fs->device = device;
    fs->bytes_per_sector = load16(&boot[11]);
    fs->sectors_per_cluster = boot[13];
    fs->reserved_sectors = load16(&boot[14]);
    fs->fat_count = boot[16];
    fs->root_entries = load16(&boot[17]);
    fs->sectors_per_fat = load16(&boot[22]);
    if (fs->sectors_per_fat > FAT12_MAX_FAT_SECTORS) return 0;
    uint32_t root_sectors = ((uint32_t)fs->root_entries * 32U + 511U) / 512U;
    fs->root_start = fs->reserved_sectors + fs->fat_count * fs->sectors_per_fat;
    fs->data_start = fs->root_start + root_sectors;
    uint32_t total_sectors = load16(&boot[19]);
    if (total_sectors == 0) total_sectors = load32(&boot[32]);
    if (total_sectors <= fs->data_start ||
        fs->sectors_per_cluster > 8 ||
        (total_sectors - fs->data_start) / fs->sectors_per_cluster >= 4085)
        return 0;
    fs->data_clusters = (total_sectors - fs->data_start) /
                        fs->sectors_per_cluster;
    for (uint16_t sector = 0; sector < fs->sectors_per_fat; ++sector)
        if (!read_sector(device, fs->reserved_sectors + sector,
                         &fs->fat[sector * FAT12_SECTOR_SIZE])) return 0;
    fs->mounted = 1;
    return 1;
}

int fat12_lookup(fat12_fs_t *fs, const char short_name[11],
                 uint16_t *first_cluster, uint32_t *size) {
    if (!fs || !fs->mounted || !short_name || !first_cluster || !size) return 0;
    uint32_t root_sectors = ((uint32_t)fs->root_entries * 32U + 511U) / 512U;
    uint8_t sector[FAT12_SECTOR_SIZE];
    for (uint32_t sector_index = 0; sector_index < root_sectors; ++sector_index) {
        if (!read_sector(fs->device, fs->root_start + sector_index, sector)) return 0;
        for (uint32_t offset = 0; offset < FAT12_SECTOR_SIZE; offset += 32) {
            if (sector[offset] == 0x00) return 0;
            if (sector[offset] == 0xe5 || (sector[offset + 11] & 0x08) != 0)
                continue;
            int match = 1;
            for (uint32_t byte = 0; byte < 11; ++byte)
                if (sector[offset + byte] != (uint8_t)short_name[byte]) match = 0;
            if (match) {
                *first_cluster = load16(&sector[offset + 26]);
                *size = load32(&sector[offset + 28]);
                return 1;
            }
        }
    }
    return 0;
}

int fat12_read_cluster(fat12_fs_t *fs, uint16_t cluster, void *buffer) {
    if (!fs || !fs->mounted || !buffer || cluster < 2 ||
        cluster >= fs->data_clusters + 2 || fat_next(fs, cluster) == 0xff7)
        return 0;
    uint32_t sector = fs->data_start + (uint32_t)(cluster - 2) *
                      fs->sectors_per_cluster;
    return storage_read(fs->device, sector, fs->sectors_per_cluster, buffer);
}

int fat12_read_file(fat12_fs_t *fs, const char short_name[11],
                    uint32_t offset, void *buffer, uint32_t size) {
    if (!fs || !buffer || !short_name) return 0;
    uint16_t cluster;
    uint32_t file_size;
    if (!fat12_lookup(fs, short_name, &cluster, &file_size) ||
        offset > file_size || size > file_size - offset) return 0;
    if (size == 0) return 1;
    uint32_t cluster_size = (uint32_t)fs->sectors_per_cluster *
                            FAT12_SECTOR_SIZE;
    uint32_t skip = offset / cluster_size;
    uint32_t inside = offset % cluster_size;
    for (uint32_t i = 0; i < skip; ++i) {
        uint16_t next = fat_next(fs, cluster);
        if (next < 2 || next >= 0xff8) return 0;
        cluster = next;
    }
    uint8_t cluster_data[8 * FAT12_SECTOR_SIZE];
    uint8_t *output = (uint8_t *)buffer;
    uint32_t remaining = size;
    while (remaining != 0) {
        if (!fat12_read_cluster(fs, cluster, cluster_data)) return 0;
        uint32_t available = cluster_size - inside;
        uint32_t copy = remaining < available ? remaining : available;
        for (uint32_t i = 0; i < copy; ++i)
            output[i] = cluster_data[inside + i];
        output += copy;
        remaining -= copy;
        inside = 0;
        if (remaining != 0) {
            uint16_t next = fat_next(fs, cluster);
            if (next < 2 || next >= 0xff8) return 0;
            cluster = next;
        }
    }
    return 1;
}
