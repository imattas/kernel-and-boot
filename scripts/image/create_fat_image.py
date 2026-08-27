#!/usr/bin/env python3
import math
import struct
import sys
from pathlib import Path

SECTOR = 512
TOTAL_SECTORS = 67500
RESERVED = 32
FAT_COUNT = 2
FAT_SECTORS = 520
DATA_START = RESERVED + FAT_COUNT * FAT_SECTORS

def short_entry(name, attributes, cluster, size):
    raw = name.encode("ascii")
    if len(raw) != 11:
        raise ValueError(name)
    return raw + bytes([attributes]) + bytes(8) + struct.pack(
        "<H", (cluster >> 16) & 0x0fff) + bytes(4) + struct.pack(
        "<HI", cluster & 0xffff, size)

def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: create_fat_image.py <BOOTX64.EFI> <KERNEL.ELF> <os.img>")
    efi_source = Path(sys.argv[1]).read_bytes()
    kernel_source = Path(sys.argv[2]).read_bytes()
    output = Path(sys.argv[3])
    efi_clusters = max(1, math.ceil(len(efi_source) / SECTOR))
    kernel_clusters = max(1, math.ceil(len(kernel_source) / SECTOR))
    efi_chain = list(range(5, 5 + efi_clusters))
    kernel_chain = list(range(5 + efi_clusters, 5 + efi_clusters + kernel_clusters))
    if kernel_chain[-1] >= TOTAL_SECTORS - DATA_START + 2:
        raise SystemExit("boot files are too large for the FAT32 image")
    image = bytearray(TOTAL_SECTORS * SECTOR)
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xeb\x58\x90"
    boot[3:11] = b"OSUEFI  "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, 1, RESERVED, FAT_COUNT, 0,
                     0, 0xF8, 0, 63, 255, 0, TOTAL_SECTORS)
    struct.pack_into("<IIIHH", boot, 36, FAT_SECTORS, 0, 0, 0, 0)
    struct.pack_into("<I", boot, 44, 2)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    boot[67:71] = b"OS32"
    boot[71:82] = b"OS FAT32   "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xaa"
    image[:SECTOR] = boot
    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, TOTAL_SECTORS - DATA_START - len(efi_chain) - len(kernel_chain) - 3)
    struct.pack_into("<I", fsinfo, 492, 5 + efi_clusters + kernel_clusters)
    struct.pack_into("<I", fsinfo, 508, 0xaa550000)
    image[SECTOR:2 * SECTOR] = fsinfo
    image[6 * SECTOR:7 * SECTOR] = boot
    image[7 * SECTOR:8 * SECTOR] = fsinfo
    fat = bytearray(FAT_SECTORS * SECTOR)
    def set_fat(cluster, next_cluster):
        struct.pack_into("<I", fat, cluster * 4, next_cluster)
    for cluster, value in ((0, 0x0ffffff8), (1, 0x0fffffff),
                           (2, 0x0fffffff), (3, 0x0fffffff), (4, 0x0fffffff)):
        set_fat(cluster, value)
    for chain in (efi_chain, kernel_chain):
        for index, cluster in enumerate(chain):
            set_fat(cluster, chain[index + 1] if index + 1 < len(chain) else 0x0fffffff)
    for fat_index in range(FAT_COUNT):
        start = (RESERVED + fat_index * FAT_SECTORS) * SECTOR
        image[start:start + len(fat)] = fat
    def cluster_offset(cluster):
        return (DATA_START + cluster - 2) * SECTOR
    root = bytearray(SECTOR)
    root[0:32] = short_entry("EFI        ", 0x10, 3, 0)
    root[32:64] = short_entry("KERNEL  ELF", 0x20, kernel_chain[0], len(kernel_source))
    root[64:96] = short_entry("OS FAT32   ", 0x08, 0, 0)
    image[cluster_offset(2):cluster_offset(2) + SECTOR] = root
    efi_dir = bytearray(SECTOR)
    efi_dir[0:32] = short_entry(".          ", 0x10, 3, 0)
    efi_dir[32:64] = short_entry("..         ", 0x10, 0, 0)
    efi_dir[64:96] = short_entry("BOOT       ", 0x10, 4, 0)
    image[cluster_offset(3):cluster_offset(3) + SECTOR] = efi_dir
    boot_dir = bytearray(SECTOR)
    boot_dir[0:32] = short_entry(".          ", 0x10, 4, 0)
    boot_dir[32:64] = short_entry("..         ", 0x10, 3, 0)
    boot_dir[64:96] = short_entry("BOOTX64 EFI", 0x20, efi_chain[0], len(efi_source))
    image[cluster_offset(4):cluster_offset(4) + SECTOR] = boot_dir
    for index, cluster in enumerate(efi_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(efi_source[index * SECTOR:(index + 1) * SECTOR])] = efi_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(kernel_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(kernel_source[index * SECTOR:(index + 1) * SECTOR])] = kernel_source[index * SECTOR:(index + 1) * SECTOR]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)

if __name__ == "__main__":
    main()
