#include "exfat.h"
#include "../../drivers/storage/storage.h"

#define EXFAT_SECTOR_SIZE 512U
#define EXFAT_END 0xfffffff8U
#define EXFAT_BAD 0xfffffff7U
#define EXFAT_MAX_CLUSTER_BYTES 4096U

static uint16_t load16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t load32(const uint8_t *p) { return (uint32_t)load16(p) | ((uint32_t)load16(p + 2) << 16); }
static uint64_t load64(const uint8_t *p) { return (uint64_t)load32(p) | ((uint64_t)load32(p + 4) << 32); }

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
    uint32_t bytes_per_sector = 1U << bytes_shift;
    uint32_t sectors_per_cluster = 1U << sectors_shift;
    uint64_t volume_length = load64(&boot[72]);
    uint32_t fat_start = load32(&boot[80]);
    uint32_t fat_length = load32(&boot[84]);
    uint32_t heap_start = load32(&boot[88]);
    uint32_t cluster_count = load32(&boot[92]);
    uint32_t root_cluster = load32(&boot[96]);
    if (bytes_shift != 9 || bytes_per_sector != EXFAT_SECTOR_SIZE ||
        sectors_shift > 3 || sectors_per_cluster == 0 ||
        sectors_per_cluster * EXFAT_SECTOR_SIZE > EXFAT_MAX_CLUSTER_BYTES ||
        volume_length == 0 || volume_length > storage_device_at(device)->block_count ||
        fat_start < 24 || fat_length == 0 || heap_start <= fat_start ||
        (uint64_t)fat_start + fat_length > volume_length ||
        heap_start >= volume_length || cluster_count == 0 ||
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

static int name_equal(const char *left, const char *right) {
    uint32_t i = 0;
    while (left[i] && right[i]) {
        char a = left[i], b = right[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
        ++i;
    }
    return left[i] == 0 && right[i] == 0;
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
            const uint8_t *stream = &directory[(index + 1U) * 32U];
            if (stream[0] != 0xc0) continue;
            char candidate[256] = {0};
            uint32_t name_length = stream[3];
            if (name_length == 0 || name_length >= sizeof(candidate)) continue;
            uint32_t written = 0;
            for (uint32_t secondary = 2; secondary <= secondary_count && written < name_length; ++secondary) {
                const uint8_t *name_entry = &directory[(index + secondary) * 32U];
                if (name_entry[0] != 0xc1) { written = 0; break; }
                for (uint32_t character = 0; character < 15 && written < name_length; ++character) {
                    uint16_t value = load16(&name_entry[2 + character * 2U]);
                    if (value > 0x7f) { written = 0; break; }
                    candidate[written++] = (char)value;
                }
            }
            if (written != name_length || !name_equal(candidate, name)) continue;
            *first_cluster = load32(&stream[20]); *size = load64(&stream[24]);
            *no_fat_chain = (stream[1] & 2U) != 0;
            return cluster_valid(fs, *first_cluster) && *size != 0;
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

int exfat_read_file(exfat_fs_t *fs, const char *name, uint64_t offset,
                    void *buffer, uint32_t size) {
    if (!fs || !buffer || size == 0) return 0;
    uint32_t cluster = 0; uint64_t file_size = 0; uint8_t no_fat_chain = 0;
    if (!exfat_lookup(fs, name, &cluster, &file_size, &no_fat_chain) ||
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
