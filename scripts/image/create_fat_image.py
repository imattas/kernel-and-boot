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
    if len(sys.argv) != 40:
        raise SystemExit("usage: create_fat_image.py ... <CP.ELF> <HEAD.ELF> <WC.ELF> <GREP.ELF> <TEE.ELF> <TAIL.ELF> <os.img>")
    efi_source = Path(sys.argv[1]).read_bytes()
    kernel_source = Path(sys.argv[2]).read_bytes()
    init_source = Path(sys.argv[3]).read_bytes()
    shell_source = Path(sys.argv[4]).read_bytes()
    args_source = Path(sys.argv[5]).read_bytes()
    env_source = Path(sys.argv[6]).read_bytes()
    cat_source = Path(sys.argv[7]).read_bytes()
    pwd_source = Path(sys.argv[8]).read_bytes()
    mkdir_source = Path(sys.argv[9]).read_bytes()
    rm_source = Path(sys.argv[10]).read_bytes()
    rmdir_source = Path(sys.argv[11]).read_bytes()
    touch_source = Path(sys.argv[12]).read_bytes()
    write_source = Path(sys.argv[13]).read_bytes()
    ls_source = Path(sys.argv[14]).read_bytes()
    chmod_source = Path(sys.argv[15]).read_bytes()
    echo_source = Path(sys.argv[16]).read_bytes()
    help_source = Path(sys.argv[17]).read_bytes()
    stat_source = Path(sys.argv[18]).read_bytes()
    mv_source = Path(sys.argv[19]).read_bytes()
    kill_source = Path(sys.argv[20]).read_bytes()
    sleep_source = Path(sys.argv[21]).read_bytes()
    setenv_source = Path(sys.argv[22]).read_bytes()
    ipc_source = Path(sys.argv[23]).read_bytes()
    dup_source = Path(sys.argv[24]).read_bytes()
    true_source = Path(sys.argv[25]).read_bytes()
    false_source = Path(sys.argv[26]).read_bytes()
    id_source = Path(sys.argv[27]).read_bytes()
    ps_source = Path(sys.argv[28]).read_bytes()
    wait_source = Path(sys.argv[29]).read_bytes()
    truncate_source = Path(sys.argv[30]).read_bytes()
    seek_source = Path(sys.argv[31]).read_bytes()
    chdir_source = Path(sys.argv[32]).read_bytes()
    cp_source = Path(sys.argv[33]).read_bytes()
    head_source = Path(sys.argv[34]).read_bytes()
    wc_source = Path(sys.argv[35]).read_bytes()
    grep_source = Path(sys.argv[36]).read_bytes()
    tee_source = Path(sys.argv[37]).read_bytes()
    tail_source = Path(sys.argv[38]).read_bytes()
    output = Path(sys.argv[39])
    efi_clusters = max(1, math.ceil(len(efi_source) / SECTOR))
    kernel_clusters = max(1, math.ceil(len(kernel_source) / SECTOR))
    init_clusters = max(1, math.ceil(len(init_source) / SECTOR))
    shell_clusters = max(1, math.ceil(len(shell_source) / SECTOR))
    args_clusters = max(1, math.ceil(len(args_source) / SECTOR))
    env_clusters = max(1, math.ceil(len(env_source) / SECTOR))
    cat_clusters = max(1, math.ceil(len(cat_source) / SECTOR))
    pwd_clusters = max(1, math.ceil(len(pwd_source) / SECTOR))
    mkdir_clusters = max(1, math.ceil(len(mkdir_source) / SECTOR))
    rm_clusters = max(1, math.ceil(len(rm_source) / SECTOR))
    rmdir_clusters = max(1, math.ceil(len(rmdir_source) / SECTOR))
    touch_clusters = max(1, math.ceil(len(touch_source) / SECTOR))
    write_clusters = max(1, math.ceil(len(write_source) / SECTOR))
    ls_clusters = max(1, math.ceil(len(ls_source) / SECTOR))
    chmod_clusters = max(1, math.ceil(len(chmod_source) / SECTOR))
    echo_clusters = max(1, math.ceil(len(echo_source) / SECTOR))
    help_clusters = max(1, math.ceil(len(help_source) / SECTOR))
    stat_clusters = max(1, math.ceil(len(stat_source) / SECTOR))
    mv_clusters = max(1, math.ceil(len(mv_source) / SECTOR))
    kill_clusters = max(1, math.ceil(len(kill_source) / SECTOR))
    sleep_clusters = max(1, math.ceil(len(sleep_source) / SECTOR))
    setenv_clusters = max(1, math.ceil(len(setenv_source) / SECTOR))
    ipc_clusters = max(1, math.ceil(len(ipc_source) / SECTOR))
    dup_clusters = max(1, math.ceil(len(dup_source) / SECTOR))
    true_clusters = max(1, math.ceil(len(true_source) / SECTOR))
    false_clusters = max(1, math.ceil(len(false_source) / SECTOR))
    id_clusters = max(1, math.ceil(len(id_source) / SECTOR))
    ps_clusters = max(1, math.ceil(len(ps_source) / SECTOR))
    wait_clusters = max(1, math.ceil(len(wait_source) / SECTOR))
    truncate_clusters = max(1, math.ceil(len(truncate_source) / SECTOR))
    seek_clusters = max(1, math.ceil(len(seek_source) / SECTOR))
    chdir_clusters = max(1, math.ceil(len(chdir_source) / SECTOR))
    cp_clusters = max(1, math.ceil(len(cp_source) / SECTOR))
    head_clusters = max(1, math.ceil(len(head_source) / SECTOR))
    wc_clusters = max(1, math.ceil(len(wc_source) / SECTOR))
    grep_clusters = max(1, math.ceil(len(grep_source) / SECTOR))
    tee_clusters = max(1, math.ceil(len(tee_source) / SECTOR))
    tail_clusters = max(1, math.ceil(len(tail_source) / SECTOR))
    efi_chain = list(range(7, 7 + efi_clusters))
    kernel_chain = list(range(7 + efi_clusters, 7 + efi_clusters + kernel_clusters))
    init_chain = list(range(7 + efi_clusters + kernel_clusters,
                            7 + efi_clusters + kernel_clusters + init_clusters))
    shell_chain = list(range(7 + efi_clusters + kernel_clusters + init_clusters,
                             7 + efi_clusters + kernel_clusters + init_clusters + shell_clusters))
    args_chain = list(range(shell_chain[-1] + 1, shell_chain[-1] + 1 + args_clusters))
    env_chain = list(range(args_chain[-1] + 1, args_chain[-1] + 1 + env_clusters))
    cat_chain = list(range(env_chain[-1] + 1, env_chain[-1] + 1 + cat_clusters))
    pwd_chain = list(range(cat_chain[-1] + 1, cat_chain[-1] + 1 + pwd_clusters))
    mkdir_chain = list(range(pwd_chain[-1] + 1, pwd_chain[-1] + 1 + mkdir_clusters))
    rm_chain = list(range(mkdir_chain[-1] + 1, mkdir_chain[-1] + 1 + rm_clusters))
    rmdir_chain = list(range(rm_chain[-1] + 1, rm_chain[-1] + 1 + rmdir_clusters))
    touch_chain = list(range(rmdir_chain[-1] + 1, rmdir_chain[-1] + 1 + touch_clusters))
    write_chain = list(range(touch_chain[-1] + 1, touch_chain[-1] + 1 + write_clusters))
    ls_chain = list(range(write_chain[-1] + 1, write_chain[-1] + 1 + ls_clusters))
    chmod_chain = list(range(ls_chain[-1] + 1, ls_chain[-1] + 1 + chmod_clusters))
    echo_chain = list(range(chmod_chain[-1] + 1, chmod_chain[-1] + 1 + echo_clusters))
    help_chain = list(range(echo_chain[-1] + 1, echo_chain[-1] + 1 + help_clusters))
    stat_chain = list(range(help_chain[-1] + 1, help_chain[-1] + 1 + stat_clusters))
    mv_chain = list(range(stat_chain[-1] + 1, stat_chain[-1] + 1 + mv_clusters))
    kill_chain = list(range(mv_chain[-1] + 1, mv_chain[-1] + 1 + kill_clusters))
    sleep_chain = list(range(kill_chain[-1] + 1, kill_chain[-1] + 1 + sleep_clusters))
    setenv_chain = list(range(sleep_chain[-1] + 1, sleep_chain[-1] + 1 + setenv_clusters))
    ipc_chain = list(range(setenv_chain[-1] + 1, setenv_chain[-1] + 1 + ipc_clusters))
    dup_chain = list(range(ipc_chain[-1] + 1, ipc_chain[-1] + 1 + dup_clusters))
    true_chain = list(range(dup_chain[-1] + 1, dup_chain[-1] + 1 + true_clusters))
    false_chain = list(range(true_chain[-1] + 1, true_chain[-1] + 1 + false_clusters))
    id_chain = list(range(false_chain[-1] + 1, false_chain[-1] + 1 + id_clusters))
    ps_chain = list(range(id_chain[-1] + 1, id_chain[-1] + 1 + ps_clusters))
    wait_chain = list(range(ps_chain[-1] + 1, ps_chain[-1] + 1 + wait_clusters))
    truncate_chain = list(range(wait_chain[-1] + 1, wait_chain[-1] + 1 + truncate_clusters))
    seek_chain = list(range(truncate_chain[-1] + 1, truncate_chain[-1] + 1 + seek_clusters))
    chdir_chain = list(range(seek_chain[-1] + 1, seek_chain[-1] + 1 + chdir_clusters))
    cp_chain = list(range(chdir_chain[-1] + 1, chdir_chain[-1] + 1 + cp_clusters))
    head_chain = list(range(cp_chain[-1] + 1, cp_chain[-1] + 1 + head_clusters))
    wc_chain = list(range(head_chain[-1] + 1, head_chain[-1] + 1 + wc_clusters))
    grep_chain = list(range(wc_chain[-1] + 1, wc_chain[-1] + 1 + grep_clusters))
    tee_chain = list(range(grep_chain[-1] + 1, grep_chain[-1] + 1 + tee_clusters))
    tail_chain = list(range(tee_chain[-1] + 1, tee_chain[-1] + 1 + tail_clusters))
    if tail_chain[-1] >= TOTAL_SECTORS - DATA_START + 2:
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
    struct.pack_into("<I", fsinfo, 488, TOTAL_SECTORS - DATA_START - len(efi_chain) - len(kernel_chain) - len(init_chain) - len(shell_chain) - len(args_chain) - len(env_chain) - len(cat_chain) - len(pwd_chain) - len(mkdir_chain) - len(rm_chain) - len(rmdir_chain) - len(touch_chain) - len(write_chain) - len(ls_chain) - len(chmod_chain) - len(echo_chain) - len(help_chain) - len(stat_chain) - len(mv_chain) - len(kill_chain) - len(sleep_chain) - len(setenv_chain) - len(ipc_chain) - len(dup_chain) - len(true_chain) - len(false_chain) - len(id_chain) - len(ps_chain) - len(wait_chain) - len(truncate_chain) - len(seek_chain) - len(chdir_chain) - len(cp_chain) - len(head_chain) - len(wc_chain) - len(grep_chain) - len(tee_chain) - len(tail_chain) - 5)
    struct.pack_into("<I", fsinfo, 492, 7 + efi_clusters + kernel_clusters + init_clusters + shell_clusters)
    struct.pack_into("<I", fsinfo, 508, 0xaa550000)
    image[SECTOR:2 * SECTOR] = fsinfo
    image[6 * SECTOR:7 * SECTOR] = boot
    image[7 * SECTOR:8 * SECTOR] = fsinfo
    fat = bytearray(FAT_SECTORS * SECTOR)
    def set_fat(cluster, next_cluster):
        struct.pack_into("<I", fat, cluster * 4, next_cluster)
    for cluster, value in ((0, 0x0ffffff8), (1, 0x0fffffff),
                           (2, 5), (3, 0x0fffffff), (4, 0x0fffffff),
                           (5, 6), (6, 0x0fffffff)):
        set_fat(cluster, value)
    for chain in (efi_chain, kernel_chain, init_chain, shell_chain, args_chain, env_chain, cat_chain, pwd_chain, mkdir_chain, rm_chain, rmdir_chain, touch_chain, write_chain, ls_chain, chmod_chain, echo_chain, help_chain, stat_chain, mv_chain, kill_chain, sleep_chain, setenv_chain, ipc_chain, dup_chain, true_chain, false_chain, id_chain, ps_chain, wait_chain, truncate_chain, seek_chain, chdir_chain, cp_chain, head_chain, wc_chain, grep_chain, tee_chain, tail_chain):
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
    root[64:96] = short_entry("INIT    ELF", 0x20, init_chain[0], len(init_source))
    root[96:128] = short_entry("SHELL   ELF", 0x20, shell_chain[0], len(shell_source))
    root[128:160] = short_entry("ARGS    ELF", 0x20, args_chain[0], len(args_source))
    root[160:192] = short_entry("ENV     ELF", 0x20, env_chain[0], len(env_source))
    root[192:224] = short_entry("CAT     ELF", 0x20, cat_chain[0], len(cat_source))
    root[224:256] = short_entry("PWD     ELF", 0x20, pwd_chain[0], len(pwd_source))
    root[256:288] = short_entry("MKDIR   ELF", 0x20, mkdir_chain[0], len(mkdir_source))
    root[288:320] = short_entry("RM      ELF", 0x20, rm_chain[0], len(rm_source))
    root[320:352] = short_entry("RMDIR   ELF", 0x20, rmdir_chain[0], len(rmdir_source))
    root[352:384] = short_entry("TOUCH   ELF", 0x20, touch_chain[0], len(touch_source))
    root[384:416] = short_entry("WRITE   ELF", 0x20, write_chain[0], len(write_source))
    root[416:448] = short_entry("LS      ELF", 0x20, ls_chain[0], len(ls_source))
    root[448:480] = short_entry("CHMOD   ELF", 0x20, chmod_chain[0], len(chmod_source))
    root[480:512] = short_entry("OS FAT32   ", 0x08, 0, 0)
    image[cluster_offset(2):cluster_offset(2) + SECTOR] = root
    root_extension = bytearray(SECTOR)
    root_extension[0:32] = short_entry("ECHO    ELF", 0x20, echo_chain[0], len(echo_source))
    root_extension[32:64] = short_entry("STAT    ELF", 0x20, stat_chain[0], len(stat_source))
    root_extension[64:96] = short_entry("MV      ELF", 0x20, mv_chain[0], len(mv_source))
    root_extension[96:128] = short_entry("KILL    ELF", 0x20, kill_chain[0], len(kill_source))
    root_extension[128:160] = short_entry("SLEEP   ELF", 0x20, sleep_chain[0], len(sleep_source))
    root_extension[160:192] = short_entry("SETENV  ELF", 0x20, setenv_chain[0], len(setenv_source))
    root_extension[192:224] = short_entry("IPC     ELF", 0x20, ipc_chain[0], len(ipc_source))
    root_extension[224:256] = short_entry("DUP     ELF", 0x20, dup_chain[0], len(dup_source))
    root_extension[256:288] = short_entry("TRUE    ELF", 0x20, true_chain[0], len(true_source))
    root_extension[288:320] = short_entry("FALSE   ELF", 0x20, false_chain[0], len(false_source))
    root_extension[320:352] = short_entry("ID      ELF", 0x20, id_chain[0], len(id_source))
    root_extension[352:384] = short_entry("PS      ELF", 0x20, ps_chain[0], len(ps_source))
    root_extension[384:416] = short_entry("WAIT    ELF", 0x20, wait_chain[0], len(wait_source))
    root_extension[416:448] = short_entry("TRUNCATEELF", 0x20, truncate_chain[0], len(truncate_source))
    root_extension[448:480] = short_entry("SEEK    ELF", 0x20, seek_chain[0], len(seek_source))
    root_extension[480:512] = short_entry("CHDIR   ELF", 0x20, chdir_chain[0], len(chdir_source))
    image[cluster_offset(5):cluster_offset(5) + SECTOR] = root_extension
    root_extension_second = bytearray(SECTOR)
    root_extension_second[0:32] = short_entry("HELP    ELF", 0x20, help_chain[0], len(help_source))
    root_extension_second[32:64] = short_entry("CP      ELF", 0x20, cp_chain[0], len(cp_source))
    root_extension_second[64:96] = short_entry("HEAD    ELF", 0x20, head_chain[0], len(head_source))
    root_extension_second[96:128] = short_entry("WC      ELF", 0x20, wc_chain[0], len(wc_source))
    root_extension_second[128:160] = short_entry("GREP    ELF", 0x20, grep_chain[0], len(grep_source))
    root_extension_second[160:192] = short_entry("TEE     ELF", 0x20, tee_chain[0], len(tee_source))
    root_extension_second[192:224] = short_entry("TAIL    ELF", 0x20, tail_chain[0], len(tail_source))
    root_extension_second[160:192] = short_entry("TEE     ELF", 0x20, tee_chain[0], len(tee_source))
    image[cluster_offset(6):cluster_offset(6) + SECTOR] = root_extension_second
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
    for index, cluster in enumerate(init_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(init_source[index * SECTOR:(index + 1) * SECTOR])] = init_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(shell_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(shell_source[index * SECTOR:(index + 1) * SECTOR])] = shell_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(args_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(args_source[index * SECTOR:(index + 1) * SECTOR])] = args_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(env_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(env_source[index * SECTOR:(index + 1) * SECTOR])] = env_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(cat_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(cat_source[index * SECTOR:(index + 1) * SECTOR])] = cat_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(pwd_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(pwd_source[index * SECTOR:(index + 1) * SECTOR])] = pwd_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(mkdir_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(mkdir_source[index * SECTOR:(index + 1) * SECTOR])] = mkdir_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(rm_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(rm_source[index * SECTOR:(index + 1) * SECTOR])] = rm_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(rmdir_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(rmdir_source[index * SECTOR:(index + 1) * SECTOR])] = rmdir_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(touch_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(touch_source[index * SECTOR:(index + 1) * SECTOR])] = touch_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(write_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(write_source[index * SECTOR:(index + 1) * SECTOR])] = write_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(ls_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(ls_source[index * SECTOR:(index + 1) * SECTOR])] = ls_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(chmod_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(chmod_source[index * SECTOR:(index + 1) * SECTOR])] = chmod_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(echo_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(echo_source[index * SECTOR:(index + 1) * SECTOR])] = echo_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(help_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(help_source[index * SECTOR:(index + 1) * SECTOR])] = help_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(stat_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(stat_source[index * SECTOR:(index + 1) * SECTOR])] = stat_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(mv_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(mv_source[index * SECTOR:(index + 1) * SECTOR])] = mv_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(kill_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(kill_source[index * SECTOR:(index + 1) * SECTOR])] = kill_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(sleep_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(sleep_source[index * SECTOR:(index + 1) * SECTOR])] = sleep_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(setenv_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(setenv_source[index * SECTOR:(index + 1) * SECTOR])] = setenv_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(ipc_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(ipc_source[index * SECTOR:(index + 1) * SECTOR])] = ipc_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(dup_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(dup_source[index * SECTOR:(index + 1) * SECTOR])] = dup_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(true_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(true_source[index * SECTOR:(index + 1) * SECTOR])] = true_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(false_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(false_source[index * SECTOR:(index + 1) * SECTOR])] = false_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(id_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(id_source[index * SECTOR:(index + 1) * SECTOR])] = id_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(ps_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(ps_source[index * SECTOR:(index + 1) * SECTOR])] = ps_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(wait_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(wait_source[index * SECTOR:(index + 1) * SECTOR])] = wait_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(truncate_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(truncate_source[index * SECTOR:(index + 1) * SECTOR])] = truncate_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(seek_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(seek_source[index * SECTOR:(index + 1) * SECTOR])] = seek_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(chdir_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(chdir_source[index * SECTOR:(index + 1) * SECTOR])] = chdir_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(cp_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(cp_source[index * SECTOR:(index + 1) * SECTOR])] = cp_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(head_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(head_source[index * SECTOR:(index + 1) * SECTOR])] = head_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(wc_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(wc_source[index * SECTOR:(index + 1) * SECTOR])] = wc_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(grep_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(grep_source[index * SECTOR:(index + 1) * SECTOR])] = grep_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(tee_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(tee_source[index * SECTOR:(index + 1) * SECTOR])] = tee_source[index * SECTOR:(index + 1) * SECTOR]
    for index, cluster in enumerate(tail_chain):
        image[cluster_offset(cluster):cluster_offset(cluster) + len(tail_source[index * SECTOR:(index + 1) * SECTOR])] = tail_source[index * SECTOR:(index + 1) * SECTOR]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)

if __name__ == "__main__":
    main()
