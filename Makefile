SHELL := /bin/sh

BUILD_DIR := build
TEST_DIR := $(BUILD_DIR)/tests
DIST_DIR := dist

CC := clang
NASM ?= nasm

ifeq ($(shell command -v $(NASM) 2>/dev/null),)
NASM := /mnt/c/Users/ian/AppData/Local/bin/NASM/nasm.exe
endif

ifeq ($(shell command -v ld.lld 2>/dev/null),)
LD := "/mnt/c/Program Files/LLVM/bin/ld.lld.exe"
else
LD := ld.lld
endif

CFLAGS := -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
	-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2
LDFLAGS := -m elf_x86_64 -T scripts/tests/contract.ld --gc-sections

CONTRACT_C := scripts/tests/c/toolchain_contract.c
CONTRACT_ASM := scripts/tests/asm/toolchain_contract.asm
CONTRACT_OBJ := $(TEST_DIR)/toolchain_contract.o
CONTRACT_ASM_OBJ := $(TEST_DIR)/toolchain_contract.asm.o
CONTRACT_ELF := $(TEST_DIR)/toolchain_contract.elf
FAT32_TEST := $(TEST_DIR)/fat32_contract
EXFAT_TEST := $(TEST_DIR)/exfat_contract
EXT4_TEST := $(TEST_DIR)/ext4_contract
XFS_TEST := $(TEST_DIR)/xfs_contract
XFS_RENAME_TEST := $(TEST_DIR)/xfs_rename_contract
XFS_ALLOC_TEST := $(TEST_DIR)/xfs_alloc_contract
XFS_UNWRITTEN_TEST := $(TEST_DIR)/xfs_unwritten_contract
XFS_AUTH_TEST := $(TEST_DIR)/xfs_auth_contract
BTRFS_TEST := $(TEST_DIR)/btrfs_contract
DEFLATE_TEST := $(TEST_DIR)/deflate_contract
LZO_TEST := $(TEST_DIR)/lzo_contract
ZSTD_TEST := $(TEST_DIR)/zstd_contract
ZSTD_FIXTURE := $(TEST_DIR)/zstd_real.zst
FSE_TEST := $(TEST_DIR)/fse_contract
CACHE_TEST := $(TEST_DIR)/cache_contract
DEVICE_TEST := $(TEST_DIR)/device_contract
SHELL_TEST := $(TEST_DIR)/shell_contract
SHELL_INTEGRATION_TEST := $(TEST_DIR)/shell_integration_contract
USERLAND_RUNTIME_TEST := $(TEST_DIR)/userland_runtime_contract
TEST_PREDICATE_TEST := $(TEST_DIR)/test_predicate_contract
SEQ_TEST := $(TEST_DIR)/seq_contract
USERLAND_INIT_ELF := $(BUILD_DIR)/userland/init.elf
USERLAND_SHELL_ELF := $(BUILD_DIR)/userland/shell.elf
USERLAND_SH_ELF := $(BUILD_DIR)/userland/sh.elf
USERLAND_ARGS_ELF := $(BUILD_DIR)/userland/args.elf
USERLAND_ENV_ELF := $(BUILD_DIR)/userland/env.elf
USERLAND_CAT_ELF := $(BUILD_DIR)/userland/cat.elf
USERLAND_PWD_ELF := $(BUILD_DIR)/userland/pwd.elf
USERLAND_MKDIR_ELF := $(BUILD_DIR)/userland/mkdir.elf
USERLAND_RM_ELF := $(BUILD_DIR)/userland/rm.elf
USERLAND_RMDIR_ELF := $(BUILD_DIR)/userland/rmdir.elf
USERLAND_TOUCH_ELF := $(BUILD_DIR)/userland/touch.elf
USERLAND_WRITE_ELF := $(BUILD_DIR)/userland/write.elf
USERLAND_LS_ELF := $(BUILD_DIR)/userland/ls.elf
USERLAND_CHMOD_ELF := $(BUILD_DIR)/userland/chmod.elf
USERLAND_ECHO_ELF := $(BUILD_DIR)/userland/echo.elf
USERLAND_HELP_ELF := $(BUILD_DIR)/userland/help.elf
USERLAND_STAT_ELF := $(BUILD_DIR)/userland/stat.elf
USERLAND_MV_ELF := $(BUILD_DIR)/userland/mv.elf
USERLAND_KILL_ELF := $(BUILD_DIR)/userland/kill.elf
USERLAND_SLEEP_ELF := $(BUILD_DIR)/userland/sleep.elf
USERLAND_SETENV_ELF := $(BUILD_DIR)/userland/setenv.elf
USERLAND_UNSETENV_ELF := $(BUILD_DIR)/userland/unsetenv.elf
USERLAND_UPTIME_ELF := $(BUILD_DIR)/userland/uptime.elf
USERLAND_DATE_ELF := $(BUILD_DIR)/userland/date.elf
USERLAND_CLEAR_ELF := $(BUILD_DIR)/userland/clear.elf
USERLAND_IPC_ELF := $(BUILD_DIR)/userland/ipc.elf
USERLAND_DUP_ELF := $(BUILD_DIR)/userland/dup.elf
USERLAND_TRUE_ELF := $(BUILD_DIR)/userland/true.elf
USERLAND_SEQ_ELF := $(BUILD_DIR)/userland/seq.elf
USERLAND_FALSE_ELF := $(BUILD_DIR)/userland/false.elf
USERLAND_ID_ELF := $(BUILD_DIR)/userland/id.elf
USERLAND_PS_ELF := $(BUILD_DIR)/userland/ps.elf
USERLAND_WAIT_ELF := $(BUILD_DIR)/userland/wait.elf
USERLAND_TRUNCATE_ELF := $(BUILD_DIR)/userland/truncate.elf
USERLAND_SEEK_ELF := $(BUILD_DIR)/userland/seek.elf
USERLAND_CHDIR_ELF := $(BUILD_DIR)/userland/chdir.elf
USERLAND_CP_ELF := $(BUILD_DIR)/userland/cp.elf
USERLAND_HEAD_ELF := $(BUILD_DIR)/userland/head.elf
USERLAND_WC_ELF := $(BUILD_DIR)/userland/wc.elf
USERLAND_GREP_ELF := $(BUILD_DIR)/userland/grep.elf
USERLAND_TEE_ELF := $(BUILD_DIR)/userland/tee.elf
USERLAND_TAIL_ELF := $(BUILD_DIR)/userland/tail.elf
USERLAND_SORT_ELF := $(BUILD_DIR)/userland/sort.elf
USERLAND_UNIQ_ELF := $(BUILD_DIR)/userland/uniq.elf
USERLAND_PRINTF_ELF := $(BUILD_DIR)/userland/printf.elf
USERLAND_BASENAME_ELF := $(BUILD_DIR)/userland/basename.elf
USERLAND_DIRNAME_ELF := $(BUILD_DIR)/userland/dirname.elf
USERLAND_CUT_ELF := $(BUILD_DIR)/userland/cut.elf
USERLAND_TR_ELF := $(BUILD_DIR)/userland/tr.elf
USERLAND_CMP_ELF := $(BUILD_DIR)/userland/cmp.elf
USERLAND_WHICH_ELF := $(BUILD_DIR)/userland/which.elf
USERLAND_FIND_ELF := $(BUILD_DIR)/userland/find.elf
USERLAND_EXPR_ELF := $(BUILD_DIR)/userland/expr.elf
USERLAND_TEST_ELF := $(BUILD_DIR)/userland/test.elf
USERLAND_INIT_OBJ := $(BUILD_DIR)/userland/init_start.o
USERLAND_INIT_MAIN_OBJ := $(BUILD_DIR)/userland/init_main.o
USERLAND_SYSCALL_OBJ := $(BUILD_DIR)/userland/syscall.o
USERLAND_SHELL_START_OBJ := $(BUILD_DIR)/userland/shell_start.o
USERLAND_SHELL_MAIN_OBJ := $(BUILD_DIR)/userland/shell_main.o
USERLAND_LD := userland/init/init.ld
USERLAND_ARGS_START_OBJ := $(BUILD_DIR)/userland/args_start.o
USERLAND_ARGS_MAIN_OBJ := $(BUILD_DIR)/userland/args_main.o
USERLAND_ARGS_LD := userland/apps/args/args.ld
USERLAND_ENV_START_OBJ := $(BUILD_DIR)/userland/env_start.o
USERLAND_ENV_MAIN_OBJ := $(BUILD_DIR)/userland/env_main.o
USERLAND_ENV_LD := userland/apps/env/env.ld
USERLAND_CAT_START_OBJ := $(BUILD_DIR)/userland/cat_start.o
USERLAND_CAT_MAIN_OBJ := $(BUILD_DIR)/userland/cat_main.o
USERLAND_CAT_LD := userland/apps/cat/cat.ld
USERLAND_PWD_START_OBJ := $(BUILD_DIR)/userland/pwd_start.o
USERLAND_PWD_MAIN_OBJ := $(BUILD_DIR)/userland/pwd_main.o
USERLAND_PWD_LD := userland/apps/pwd/pwd.ld
USERLAND_MKDIR_START_OBJ := $(BUILD_DIR)/userland/mkdir_start.o
USERLAND_MKDIR_MAIN_OBJ := $(BUILD_DIR)/userland/mkdir_main.o
USERLAND_MKDIR_LD := userland/apps/mkdir/mkdir.ld
USERLAND_RM_START_OBJ := $(BUILD_DIR)/userland/rm_start.o
USERLAND_RM_MAIN_OBJ := $(BUILD_DIR)/userland/rm_main.o
USERLAND_RM_LD := userland/apps/rm/rm.ld
USERLAND_RMDIR_START_OBJ := $(BUILD_DIR)/userland/rmdir_start.o
USERLAND_RMDIR_MAIN_OBJ := $(BUILD_DIR)/userland/rmdir_main.o
USERLAND_RMDIR_LD := userland/apps/rmdir/rmdir.ld
USERLAND_TOUCH_START_OBJ := $(BUILD_DIR)/userland/touch_start.o
USERLAND_TOUCH_MAIN_OBJ := $(BUILD_DIR)/userland/touch_main.o
USERLAND_TOUCH_LD := userland/apps/touch/touch.ld
USERLAND_WRITE_START_OBJ := $(BUILD_DIR)/userland/write_start.o
USERLAND_WRITE_MAIN_OBJ := $(BUILD_DIR)/userland/write_main.o
USERLAND_WRITE_LD := userland/apps/write/write.ld
USERLAND_LS_START_OBJ := $(BUILD_DIR)/userland/ls_start.o
USERLAND_LS_MAIN_OBJ := $(BUILD_DIR)/userland/ls_main.o
USERLAND_LS_LD := userland/apps/ls/ls.ld
USERLAND_CHMOD_START_OBJ := $(BUILD_DIR)/userland/chmod_start.o
USERLAND_CHMOD_MAIN_OBJ := $(BUILD_DIR)/userland/chmod_main.o
USERLAND_CHMOD_LD := userland/apps/chmod/chmod.ld
USERLAND_ECHO_START_OBJ := $(BUILD_DIR)/userland/echo_start.o
USERLAND_ECHO_MAIN_OBJ := $(BUILD_DIR)/userland/echo_main.o
USERLAND_ECHO_LD := userland/apps/echo/echo.ld
USERLAND_HELP_START_OBJ := $(BUILD_DIR)/userland/help_start.o
USERLAND_HELP_MAIN_OBJ := $(BUILD_DIR)/userland/help_main.o
USERLAND_HELP_LD := userland/apps/help/help.ld
USERLAND_STAT_START_OBJ := $(BUILD_DIR)/userland/stat_start.o
USERLAND_STAT_MAIN_OBJ := $(BUILD_DIR)/userland/stat_main.o
USERLAND_STAT_LD := userland/apps/stat/stat.ld
USERLAND_MV_START_OBJ := $(BUILD_DIR)/userland/mv_start.o
USERLAND_MV_MAIN_OBJ := $(BUILD_DIR)/userland/mv_main.o
USERLAND_MV_LD := userland/apps/mv/mv.ld
USERLAND_KILL_START_OBJ := $(BUILD_DIR)/userland/kill_start.o
USERLAND_KILL_MAIN_OBJ := $(BUILD_DIR)/userland/kill_main.o
USERLAND_KILL_LD := userland/apps/kill/kill.ld
USERLAND_SLEEP_START_OBJ := $(BUILD_DIR)/userland/sleep_start.o
USERLAND_SLEEP_MAIN_OBJ := $(BUILD_DIR)/userland/sleep_main.o
USERLAND_SLEEP_LD := userland/apps/sleep/sleep.ld
USERLAND_SETENV_START_OBJ := $(BUILD_DIR)/userland/setenv_start.o
USERLAND_SETENV_MAIN_OBJ := $(BUILD_DIR)/userland/setenv_main.o
USERLAND_SETENV_LD := userland/apps/setenv/setenv.ld
USERLAND_UNSETENV_START_OBJ := $(BUILD_DIR)/userland/unsetenv_start.o
USERLAND_UNSETENV_MAIN_OBJ := $(BUILD_DIR)/userland/unsetenv_main.o
USERLAND_UNSETENV_LD := userland/apps/unsetenv/unsetenv.ld
USERLAND_UPTIME_START_OBJ := $(BUILD_DIR)/userland/uptime_start.o
USERLAND_UPTIME_MAIN_OBJ := $(BUILD_DIR)/userland/uptime_main.o
USERLAND_UPTIME_LD := userland/apps/uptime/uptime.ld
USERLAND_DATE_START_OBJ := $(BUILD_DIR)/userland/date_start.o
USERLAND_DATE_MAIN_OBJ := $(BUILD_DIR)/userland/date_main.o
USERLAND_DATE_LD := userland/apps/date/date.ld
USERLAND_CLEAR_START_OBJ := $(BUILD_DIR)/userland/clear_start.o
USERLAND_CLEAR_MAIN_OBJ := $(BUILD_DIR)/userland/clear_main.o
USERLAND_CLEAR_LD := userland/apps/clear/clear.ld
USERLAND_IPC_START_OBJ := $(BUILD_DIR)/userland/ipc_start.o
USERLAND_IPC_MAIN_OBJ := $(BUILD_DIR)/userland/ipc_main.o
USERLAND_IPC_LD := userland/apps/ipc/ipc.ld
USERLAND_DUP_START_OBJ := $(BUILD_DIR)/userland/dup_start.o
USERLAND_DUP_MAIN_OBJ := $(BUILD_DIR)/userland/dup_main.o
USERLAND_DUP_LD := userland/apps/dup/dup.ld
USERLAND_TRUE_START_OBJ := $(BUILD_DIR)/userland/true_start.o
USERLAND_TRUE_MAIN_OBJ := $(BUILD_DIR)/userland/true_main.o
USERLAND_TRUE_LD := userland/apps/true/true.ld
USERLAND_SEQ_START_OBJ := $(BUILD_DIR)/userland/seq_start.o
USERLAND_SEQ_MAIN_OBJ := $(BUILD_DIR)/userland/seq_main.o
USERLAND_SEQ_LD := userland/apps/seq/seq.ld
USERLAND_FALSE_START_OBJ := $(BUILD_DIR)/userland/false_start.o
USERLAND_FALSE_MAIN_OBJ := $(BUILD_DIR)/userland/false_main.o
USERLAND_FALSE_LD := userland/apps/false/false.ld
USERLAND_ID_START_OBJ := $(BUILD_DIR)/userland/id_start.o
USERLAND_ID_MAIN_OBJ := $(BUILD_DIR)/userland/id_main.o
USERLAND_ID_LD := userland/apps/id/id.ld
USERLAND_PS_START_OBJ := $(BUILD_DIR)/userland/ps_start.o
USERLAND_PS_MAIN_OBJ := $(BUILD_DIR)/userland/ps_main.o
USERLAND_PS_LD := userland/apps/ps/ps.ld
USERLAND_WAIT_START_OBJ := $(BUILD_DIR)/userland/wait_start.o
USERLAND_WAIT_MAIN_OBJ := $(BUILD_DIR)/userland/wait_main.o
USERLAND_WAIT_LD := userland/apps/wait/wait.ld
USERLAND_TRUNCATE_START_OBJ := $(BUILD_DIR)/userland/truncate_start.o
USERLAND_TRUNCATE_MAIN_OBJ := $(BUILD_DIR)/userland/truncate_main.o
USERLAND_TRUNCATE_LD := userland/apps/truncate/truncate.ld
USERLAND_SEEK_START_OBJ := $(BUILD_DIR)/userland/seek_start.o
USERLAND_SEEK_MAIN_OBJ := $(BUILD_DIR)/userland/seek_main.o
USERLAND_SEEK_LD := userland/apps/seek/seek.ld
USERLAND_CHDIR_START_OBJ := $(BUILD_DIR)/userland/chdir_start.o
USERLAND_CHDIR_MAIN_OBJ := $(BUILD_DIR)/userland/chdir_main.o
USERLAND_CHDIR_LD := userland/apps/chdir/chdir.ld
USERLAND_CP_START_OBJ := $(BUILD_DIR)/userland/cp_start.o
USERLAND_CP_MAIN_OBJ := $(BUILD_DIR)/userland/cp_main.o
USERLAND_CP_LD := userland/apps/cp/cp.ld
USERLAND_HEAD_START_OBJ := $(BUILD_DIR)/userland/head_start.o
USERLAND_HEAD_MAIN_OBJ := $(BUILD_DIR)/userland/head_main.o
USERLAND_HEAD_LD := userland/apps/head/head.ld
USERLAND_WC_START_OBJ := $(BUILD_DIR)/userland/wc_start.o
USERLAND_WC_MAIN_OBJ := $(BUILD_DIR)/userland/wc_main.o
USERLAND_WC_LD := userland/apps/wc/wc.ld
USERLAND_GREP_START_OBJ := $(BUILD_DIR)/userland/grep_start.o
USERLAND_GREP_MAIN_OBJ := $(BUILD_DIR)/userland/grep_main.o
USERLAND_GREP_LD := userland/apps/grep/grep.ld
USERLAND_TEE_START_OBJ := $(BUILD_DIR)/userland/tee_start.o
USERLAND_TEE_MAIN_OBJ := $(BUILD_DIR)/userland/tee_main.o
USERLAND_TEE_LD := userland/apps/tee/tee.ld
USERLAND_TAIL_START_OBJ := $(BUILD_DIR)/userland/tail_start.o
USERLAND_TAIL_MAIN_OBJ := $(BUILD_DIR)/userland/tail_main.o
USERLAND_TAIL_LD := userland/apps/tail/tail.ld
USERLAND_SORT_START_OBJ := $(BUILD_DIR)/userland/sort_start.o
USERLAND_SORT_MAIN_OBJ := $(BUILD_DIR)/userland/sort_main.o
USERLAND_SORT_LD := userland/apps/sort/sort.ld
USERLAND_UNIQ_START_OBJ := $(BUILD_DIR)/userland/uniq_start.o
USERLAND_UNIQ_MAIN_OBJ := $(BUILD_DIR)/userland/uniq_main.o
USERLAND_UNIQ_LD := userland/apps/uniq/uniq.ld
USERLAND_PRINTF_START_OBJ := $(BUILD_DIR)/userland/printf_start.o
USERLAND_PRINTF_MAIN_OBJ := $(BUILD_DIR)/userland/printf_main.o
USERLAND_PRINTF_LD := userland/apps/printf/printf.ld
USERLAND_BASENAME_START_OBJ := $(BUILD_DIR)/userland/basename_start.o
USERLAND_BASENAME_MAIN_OBJ := $(BUILD_DIR)/userland/basename_main.o
USERLAND_BASENAME_LD := userland/apps/basename/basename.ld
USERLAND_DIRNAME_START_OBJ := $(BUILD_DIR)/userland/dirname_start.o
USERLAND_DIRNAME_MAIN_OBJ := $(BUILD_DIR)/userland/dirname_main.o
USERLAND_DIRNAME_LD := userland/apps/dirname/dirname.ld
USERLAND_CUT_START_OBJ := $(BUILD_DIR)/userland/cut_start.o
USERLAND_CUT_MAIN_OBJ := $(BUILD_DIR)/userland/cut_main.o
USERLAND_CUT_LD := userland/apps/cut/cut.ld
USERLAND_TR_START_OBJ := $(BUILD_DIR)/userland/tr_start.o
USERLAND_TR_MAIN_OBJ := $(BUILD_DIR)/userland/tr_main.o
USERLAND_TR_LD := userland/apps/tr/tr.ld
USERLAND_CMP_START_OBJ := $(BUILD_DIR)/userland/cmp_start.o
USERLAND_CMP_MAIN_OBJ := $(BUILD_DIR)/userland/cmp_main.o
USERLAND_CMP_LD := userland/apps/cmp/cmp.ld
USERLAND_WHICH_START_OBJ := $(BUILD_DIR)/userland/which_start.o
USERLAND_WHICH_MAIN_OBJ := $(BUILD_DIR)/userland/which_main.o
USERLAND_WHICH_LD := userland/apps/which/which.ld
USERLAND_FIND_START_OBJ := $(BUILD_DIR)/userland/find_start.o
USERLAND_FIND_MAIN_OBJ := $(BUILD_DIR)/userland/find_main.o
USERLAND_FIND_LD := userland/apps/find/find.ld
USERLAND_EXPR_START_OBJ := $(BUILD_DIR)/userland/expr_start.o
USERLAND_EXPR_MAIN_OBJ := $(BUILD_DIR)/userland/expr_main.o
USERLAND_EXPR_LD := userland/apps/expr/expr.ld
USERLAND_TEST_START_OBJ := $(BUILD_DIR)/userland/test_start.o
USERLAND_TEST_MAIN_OBJ := $(BUILD_DIR)/userland/test_main.o
USERLAND_TEST_LD := userland/apps/test/test.ld
UEFI_OBJ := $(BUILD_DIR)/uefi/efi_main.obj
UEFI_ENTRY_OBJ := $(BUILD_DIR)/uefi/entry.obj
UEFI_CONSOLE_OBJ := $(BUILD_DIR)/uefi/console.obj
UEFI_FIRMWARE_OBJ := $(BUILD_DIR)/uefi/firmware.obj
UEFI_FILE_OBJ := $(BUILD_DIR)/uefi/file.obj
UEFI_ELF_OBJ := $(BUILD_DIR)/uefi/elf.obj
UEFI_MEMORY_MAP_OBJ := $(BUILD_DIR)/uefi/memory_map.obj
UEFI_EFI := $(BUILD_DIR)/image/esp/EFI/BOOT/BOOTX64.EFI
KERNEL_OBJ := $(BUILD_DIR)/kernel/kernel_entry.o
KERNEL_ENTRY_ASM_OBJ := $(BUILD_DIR)/kernel/entry.asm.o
KERNEL_TABLES_OBJ := $(BUILD_DIR)/kernel/tables.o
KERNEL_TABLES_ASM_OBJ := $(BUILD_DIR)/kernel/tables.asm.o
KERNEL_SERIAL_OBJ := $(BUILD_DIR)/kernel/serial.o
KERNEL_CPU_OBJ := $(BUILD_DIR)/kernel/cpu.o
KERNEL_EXCEPTIONS_OBJ := $(BUILD_DIR)/kernel/exceptions.o
KERNEL_PANIC_OBJ := $(BUILD_DIR)/kernel/panic.o
KERNEL_PHYSICAL_OBJ := $(BUILD_DIR)/kernel/physical.o
KERNEL_VIRTUAL_OBJ := $(BUILD_DIR)/kernel/virtual.o
KERNEL_PAGING_OBJ := $(BUILD_DIR)/kernel/paging.o
KERNEL_HEAP_OBJ := $(BUILD_DIR)/kernel/heap.o
KERNEL_IRQ_OBJ := $(BUILD_DIR)/kernel/irq.o
KERNEL_APIC_OBJ := $(BUILD_DIR)/kernel/apic.o
KERNEL_ACPI_OBJ := $(BUILD_DIR)/kernel/acpi.o
KERNEL_PERCPU_OBJ := $(BUILD_DIR)/kernel/percpu.o
KERNEL_TRAMPOLINE_OBJ := $(BUILD_DIR)/kernel/trampoline.o
KERNEL_TIMER_OBJ := $(BUILD_DIR)/kernel/timer.o
KERNEL_TIMER_ASM_OBJ := $(BUILD_DIR)/kernel/timer.asm.o
KERNEL_KEYBOARD_ASM_OBJ := $(BUILD_DIR)/kernel/keyboard.asm.o
KERNEL_E1000_IRQ_OBJ := $(BUILD_DIR)/kernel/e1000_irq.asm.o
KERNEL_ETHERNET_OBJ := $(BUILD_DIR)/kernel/ethernet.o
KERNEL_ARP_OBJ := $(BUILD_DIR)/kernel/arp.o
KERNEL_ARP_CACHE_OBJ := $(BUILD_DIR)/kernel/arp_cache.o
KERNEL_IPV4_OBJ := $(BUILD_DIR)/kernel/ipv4.o
KERNEL_UDP_OBJ := $(BUILD_DIR)/kernel/udp.o
KERNEL_TCP_OBJ := $(BUILD_DIR)/kernel/tcp.o
KERNEL_TCP_ENDPOINT_OBJ := $(BUILD_DIR)/kernel/tcp_endpoint.o
KERNEL_ICMP_OBJ := $(BUILD_DIR)/kernel/icmp.o
KERNEL_ROUTE_OBJ := $(BUILD_DIR)/kernel/route.o
KERNEL_PACKET_QUEUE_OBJ := $(BUILD_DIR)/kernel/packet_queue.o
KERNEL_NETWORK_OBJ := $(BUILD_DIR)/kernel/network.o
KERNEL_REASSEMBLY_OBJ := $(BUILD_DIR)/kernel/reassembly.o
KERNEL_UDP_ENDPOINT_OBJ := $(BUILD_DIR)/kernel/udp_endpoint.o
KERNEL_NVME_IRQ_OBJ := $(BUILD_DIR)/kernel/nvme_irq.asm.o
KERNEL_AHCI_IRQ_OBJ := $(BUILD_DIR)/kernel/ahci_irq.asm.o
KERNEL_ELF := $(BUILD_DIR)/kernel/kernel.elf
IMAGE := $(DIST_DIR)/os.img
OVMF_CODE ?= /usr/share/edk2/x64/OVMF_CODE.4m.fd
OVMF_VARS ?= /usr/share/edk2/x64/OVMF_VARS.4m.fd
QEMU_LOG := $(BUILD_DIR)/qemu-serial.log

.PHONY: all test userland-test userland-set-test userland-runtime-test test-predicate-test shell-test shell-integration-test args-test env-test cat-test pwd-test mkdir-test rm-test rmdir-test touch-test write-test ls-test chmod-test echo-test help-test stat-test mv-test kill-test sleep-test setenv-test unsetenv-test uptime-test date-test clear-test ipc-test dup-test true-test seq-test false-test id-test ps-test wait-test truncate-test seek-test chdir-test cp-test head-test wc-test grep-test tee-test tail-test sort-test uniq-test printf-test basename-test dirname-test cut-test tr-test cmp-test which-test find-test expr-test test-utility-test image qemu-test qemu-input-test fat32-test exfat-test ext4-test xfs-test xfs-rename-test xfs-alloc-test xfs-unwritten-test xfs-auth-test btrfs-test deflate-test lzo-test zstd-test fse-test cache-test device-test run clean distclean

all: $(CONTRACT_ELF) $(UEFI_EFI) $(KERNEL_ELF) $(USERLAND_INIT_ELF) $(USERLAND_SHELL_ELF) $(USERLAND_ARGS_ELF) $(USERLAND_ENV_ELF) $(USERLAND_CAT_ELF) $(USERLAND_PWD_ELF) $(USERLAND_MKDIR_ELF) $(USERLAND_RM_ELF) $(USERLAND_RMDIR_ELF) $(USERLAND_TOUCH_ELF) $(USERLAND_WRITE_ELF) $(USERLAND_LS_ELF) $(USERLAND_CHMOD_ELF) $(USERLAND_ECHO_ELF) $(USERLAND_HELP_ELF) $(USERLAND_STAT_ELF) $(USERLAND_MV_ELF) $(USERLAND_KILL_ELF) $(USERLAND_SLEEP_ELF) $(USERLAND_SETENV_ELF) $(USERLAND_IPC_ELF) $(USERLAND_DUP_ELF) $(USERLAND_TRUE_ELF) $(USERLAND_FALSE_ELF) $(USERLAND_ID_ELF) $(USERLAND_PS_ELF) $(USERLAND_WAIT_ELF) $(USERLAND_TRUNCATE_ELF) $(USERLAND_SEEK_ELF) $(USERLAND_CHDIR_ELF) $(USERLAND_CP_ELF) $(USERLAND_HEAD_ELF) $(USERLAND_WC_ELF) $(USERLAND_GREP_ELF) $(USERLAND_TEE_ELF) $(USERLAND_TAIL_ELF) $(USERLAND_SORT_ELF) $(USERLAND_UNIQ_ELF) $(USERLAND_PRINTF_ELF) $(USERLAND_BASENAME_ELF) $(USERLAND_DIRNAME_ELF) $(USERLAND_CUT_ELF)
all: $(USERLAND_TR_ELF)
all: $(USERLAND_SEQ_ELF)
all: $(USERLAND_CMP_ELF)
all: $(USERLAND_WHICH_ELF)
all: $(USERLAND_FIND_ELF)
all: $(USERLAND_EXPR_ELF)
all: $(USERLAND_SH_ELF)
all: $(USERLAND_TEST_ELF)
all: $(USERLAND_UNSETENV_ELF)
all: $(USERLAND_UPTIME_ELF)
all: $(USERLAND_DATE_ELF)
all: $(USERLAND_CLEAR_ELF)

userland-test: $(USERLAND_INIT_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_INIT_ELF)

userland-set-test: all
	sh scripts/tests/sh/validate_userland_set.sh $(BUILD_DIR)/userland

shell-test: $(SHELL_TEST)
	$(SHELL_TEST)

shell-integration-test: $(SHELL_INTEGRATION_TEST)
	$(SHELL_INTEGRATION_TEST)

userland-runtime-test: $(USERLAND_RUNTIME_TEST)
	$(USERLAND_RUNTIME_TEST)

test-predicate-test: $(TEST_PREDICATE_TEST)
	$(TEST_PREDICATE_TEST)

args-test: $(USERLAND_ARGS_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_ARGS_ELF)

env-test: $(USERLAND_ENV_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_ENV_ELF)

cat-test: $(USERLAND_CAT_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CAT_ELF)

pwd-test: $(USERLAND_PWD_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_PWD_ELF)

mkdir-test: $(USERLAND_MKDIR_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_MKDIR_ELF)

rm-test: $(USERLAND_RM_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_RM_ELF)

rmdir-test: $(USERLAND_RMDIR_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_RMDIR_ELF)

touch-test: $(USERLAND_TOUCH_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TOUCH_ELF)

write-test: $(USERLAND_WRITE_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_WRITE_ELF)

ls-test: $(USERLAND_LS_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_LS_ELF)

chmod-test: $(USERLAND_CHMOD_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CHMOD_ELF)

echo-test: $(USERLAND_ECHO_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_ECHO_ELF)

help-test: $(USERLAND_HELP_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_HELP_ELF)

stat-test: $(USERLAND_STAT_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_STAT_ELF)

mv-test: $(USERLAND_MV_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_MV_ELF)

kill-test: $(USERLAND_KILL_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_KILL_ELF)

sleep-test: $(USERLAND_SLEEP_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_SLEEP_ELF)

setenv-test: $(USERLAND_SETENV_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_SETENV_ELF)

unsetenv-test: $(USERLAND_UNSETENV_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_UNSETENV_ELF)

uptime-test: $(USERLAND_UPTIME_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_UPTIME_ELF)

date-test: $(USERLAND_DATE_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_DATE_ELF)

clear-test: $(USERLAND_CLEAR_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CLEAR_ELF)

ipc-test: $(USERLAND_IPC_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_IPC_ELF)

dup-test: $(USERLAND_DUP_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_DUP_ELF)

true-test: $(USERLAND_TRUE_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TRUE_ELF)

seq-test: $(USERLAND_SEQ_ELF) $(SEQ_TEST)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_SEQ_ELF)
	$(SEQ_TEST)

false-test: $(USERLAND_FALSE_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_FALSE_ELF)

id-test: $(USERLAND_ID_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_ID_ELF)

ps-test: $(USERLAND_PS_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_PS_ELF)

wait-test: $(USERLAND_WAIT_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_WAIT_ELF)

truncate-test: $(USERLAND_TRUNCATE_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TRUNCATE_ELF)

seek-test: $(USERLAND_SEEK_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_SEEK_ELF)

chdir-test: $(USERLAND_CHDIR_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CHDIR_ELF)

cp-test: $(USERLAND_CP_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CP_ELF)

$(SHELL_TEST): scripts/tests/c/shell_contract.c userland/shell/shell.c userland/shell/shell.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/shell_contract.c userland/shell/shell.c

$(SHELL_INTEGRATION_TEST): scripts/tests/c/shell_integration_contract.c userland/shell/shell.c userland/shell/shell.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/shell_integration_contract.c userland/shell/shell.c

$(USERLAND_RUNTIME_TEST): scripts/tests/c/userland_runtime_contract.c userland/lib/runtime.h userland/lib/os.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/userland_runtime_contract.c

$(TEST_PREDICATE_TEST): scripts/tests/c/test_predicate_contract.c userland/apps/test/main.c userland/lib/runtime.h userland/lib/os.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/test_predicate_contract.c

$(SEQ_TEST): scripts/tests/c/seq_contract.c userland/apps/seq/seq_logic.h
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/seq_contract.c

$(BUILD_DIR)/userland:
	mkdir -p $@

$(USERLAND_INIT_OBJ): userland/init/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_INIT_MAIN_OBJ): userland/init/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SYSCALL_OBJ): userland/lib/syscall.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_INIT_ELF): $(USERLAND_INIT_OBJ) $(USERLAND_INIT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_LD) --build-id=none -o $@ $(USERLAND_INIT_OBJ) $(USERLAND_INIT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_SHELL_START_OBJ): userland/shell/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_SHELL_MAIN_OBJ): userland/shell/main.c userland/shell/shell.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SHELL_ELF): $(USERLAND_SHELL_START_OBJ) $(USERLAND_SHELL_MAIN_OBJ) userland/shell/shell.c userland/shell/shell.h $(USERLAND_SYSCALL_OBJ) userland/shell/shell.ld | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -c userland/shell/shell.c -o build/userland/shell_parser.o
	$(LD) -m elf_x86_64 -T userland/shell/shell.ld --build-id=none -o $@ $(USERLAND_SHELL_START_OBJ) $(USERLAND_SHELL_MAIN_OBJ) build/userland/shell_parser.o $(USERLAND_SYSCALL_OBJ)

$(USERLAND_ARGS_START_OBJ): userland/apps/args/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_ARGS_MAIN_OBJ): userland/apps/args/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_ARGS_ELF): $(USERLAND_ARGS_START_OBJ) $(USERLAND_ARGS_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_ARGS_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_ARGS_LD) --build-id=none -o $@ $(USERLAND_ARGS_START_OBJ) $(USERLAND_ARGS_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_ENV_START_OBJ): userland/apps/env/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_ENV_MAIN_OBJ): userland/apps/env/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_ENV_ELF): $(USERLAND_ENV_START_OBJ) $(USERLAND_ENV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_ENV_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_ENV_LD) --build-id=none -o $@ $(USERLAND_ENV_START_OBJ) $(USERLAND_ENV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_CAT_START_OBJ): userland/apps/cat/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CAT_MAIN_OBJ): userland/apps/cat/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CAT_ELF): $(USERLAND_CAT_START_OBJ) $(USERLAND_CAT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CAT_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CAT_LD) --build-id=none -o $@ $(USERLAND_CAT_START_OBJ) $(USERLAND_CAT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_PWD_START_OBJ): userland/apps/pwd/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_PWD_MAIN_OBJ): userland/apps/pwd/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_PWD_ELF): $(USERLAND_PWD_START_OBJ) $(USERLAND_PWD_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_PWD_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_PWD_LD) --build-id=none -o $@ $(USERLAND_PWD_START_OBJ) $(USERLAND_PWD_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_MKDIR_START_OBJ): userland/apps/mkdir/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_MKDIR_MAIN_OBJ): userland/apps/mkdir/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_MKDIR_ELF): $(USERLAND_MKDIR_START_OBJ) $(USERLAND_MKDIR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_MKDIR_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_MKDIR_LD) --build-id=none -o $@ $(USERLAND_MKDIR_START_OBJ) $(USERLAND_MKDIR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_RM_START_OBJ): userland/apps/rm/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_RM_MAIN_OBJ): userland/apps/rm/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_RM_ELF): $(USERLAND_RM_START_OBJ) $(USERLAND_RM_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_RM_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_RM_LD) --build-id=none -o $@ $(USERLAND_RM_START_OBJ) $(USERLAND_RM_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_RMDIR_START_OBJ): userland/apps/rmdir/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_RMDIR_MAIN_OBJ): userland/apps/rmdir/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_RMDIR_ELF): $(USERLAND_RMDIR_START_OBJ) $(USERLAND_RMDIR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_RMDIR_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_RMDIR_LD) --build-id=none -o $@ $(USERLAND_RMDIR_START_OBJ) $(USERLAND_RMDIR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_TOUCH_START_OBJ): userland/apps/touch/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TOUCH_MAIN_OBJ): userland/apps/touch/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TOUCH_ELF): $(USERLAND_TOUCH_START_OBJ) $(USERLAND_TOUCH_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TOUCH_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TOUCH_LD) --build-id=none -o $@ $(USERLAND_TOUCH_START_OBJ) $(USERLAND_TOUCH_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_WRITE_START_OBJ): userland/apps/write/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_WRITE_MAIN_OBJ): userland/apps/write/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_WRITE_ELF): $(USERLAND_WRITE_START_OBJ) $(USERLAND_WRITE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_WRITE_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_WRITE_LD) --build-id=none -o $@ $(USERLAND_WRITE_START_OBJ) $(USERLAND_WRITE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_LS_START_OBJ): userland/apps/ls/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_LS_MAIN_OBJ): userland/apps/ls/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_LS_ELF): $(USERLAND_LS_START_OBJ) $(USERLAND_LS_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_LS_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_LS_LD) --build-id=none -o $@ $(USERLAND_LS_START_OBJ) $(USERLAND_LS_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_CHMOD_START_OBJ): userland/apps/chmod/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CHMOD_MAIN_OBJ): userland/apps/chmod/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CHMOD_ELF): $(USERLAND_CHMOD_START_OBJ) $(USERLAND_CHMOD_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CHMOD_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CHMOD_LD) --build-id=none -o $@ $(USERLAND_CHMOD_START_OBJ) $(USERLAND_CHMOD_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_ECHO_START_OBJ): userland/apps/echo/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_ECHO_MAIN_OBJ): userland/apps/echo/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_ECHO_ELF): $(USERLAND_ECHO_START_OBJ) $(USERLAND_ECHO_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_ECHO_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_ECHO_LD) --build-id=none -o $@ $(USERLAND_ECHO_START_OBJ) $(USERLAND_ECHO_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_HELP_START_OBJ): userland/apps/help/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_HELP_MAIN_OBJ): userland/apps/help/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_HELP_ELF): $(USERLAND_HELP_START_OBJ) $(USERLAND_HELP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_HELP_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_HELP_LD) --build-id=none -o $@ $(USERLAND_HELP_START_OBJ) $(USERLAND_HELP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_STAT_START_OBJ): userland/apps/stat/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_STAT_MAIN_OBJ): userland/apps/stat/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_STAT_ELF): $(USERLAND_STAT_START_OBJ) $(USERLAND_STAT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_STAT_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_STAT_LD) --build-id=none -o $@ $(USERLAND_STAT_START_OBJ) $(USERLAND_STAT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_MV_START_OBJ): userland/apps/mv/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_MV_MAIN_OBJ): userland/apps/mv/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_MV_ELF): $(USERLAND_MV_START_OBJ) $(USERLAND_MV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_MV_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_MV_LD) --build-id=none -o $@ $(USERLAND_MV_START_OBJ) $(USERLAND_MV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_KILL_START_OBJ): userland/apps/kill/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_KILL_MAIN_OBJ): userland/apps/kill/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_KILL_ELF): $(USERLAND_KILL_START_OBJ) $(USERLAND_KILL_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_KILL_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_KILL_LD) --build-id=none -o $@ $(USERLAND_KILL_START_OBJ) $(USERLAND_KILL_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_SLEEP_START_OBJ): userland/apps/sleep/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_SLEEP_MAIN_OBJ): userland/apps/sleep/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SLEEP_ELF): $(USERLAND_SLEEP_START_OBJ) $(USERLAND_SLEEP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_SLEEP_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_SLEEP_LD) --build-id=none -o $@ $(USERLAND_SLEEP_START_OBJ) $(USERLAND_SLEEP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_SETENV_START_OBJ): userland/apps/setenv/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_SETENV_MAIN_OBJ): userland/apps/setenv/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SETENV_ELF): $(USERLAND_SETENV_START_OBJ) $(USERLAND_SETENV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_SETENV_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_SETENV_LD) --build-id=none -o $@ $(USERLAND_SETENV_START_OBJ) $(USERLAND_SETENV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_UNSETENV_START_OBJ): userland/apps/unsetenv/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_UNSETENV_MAIN_OBJ): userland/apps/unsetenv/main.c userland/lib/runtime.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_UNSETENV_ELF): $(USERLAND_UNSETENV_START_OBJ) $(USERLAND_UNSETENV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_UNSETENV_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_UNSETENV_LD) --build-id=none -o $@ $(USERLAND_UNSETENV_START_OBJ) $(USERLAND_UNSETENV_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_UPTIME_START_OBJ): userland/apps/uptime/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_UPTIME_MAIN_OBJ): userland/apps/uptime/main.c userland/lib/runtime.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_UPTIME_ELF): $(USERLAND_UPTIME_START_OBJ) $(USERLAND_UPTIME_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_UPTIME_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_UPTIME_LD) --build-id=none -o $@ $(USERLAND_UPTIME_START_OBJ) $(USERLAND_UPTIME_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_DATE_START_OBJ): userland/apps/date/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_DATE_MAIN_OBJ): userland/apps/date/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_DATE_ELF): $(USERLAND_DATE_START_OBJ) $(USERLAND_DATE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_DATE_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_DATE_LD) --build-id=none -o $@ $(USERLAND_DATE_START_OBJ) $(USERLAND_DATE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_CLEAR_START_OBJ): userland/apps/clear/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CLEAR_MAIN_OBJ): userland/apps/clear/main.c userland/lib/runtime.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CLEAR_ELF): $(USERLAND_CLEAR_START_OBJ) $(USERLAND_CLEAR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CLEAR_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CLEAR_LD) --build-id=none -o $@ $(USERLAND_CLEAR_START_OBJ) $(USERLAND_CLEAR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_IPC_START_OBJ): userland/apps/ipc/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_IPC_MAIN_OBJ): userland/apps/ipc/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_IPC_ELF): $(USERLAND_IPC_START_OBJ) $(USERLAND_IPC_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_IPC_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_IPC_LD) --build-id=none -o $@ $(USERLAND_IPC_START_OBJ) $(USERLAND_IPC_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_DUP_START_OBJ): userland/apps/dup/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_DUP_MAIN_OBJ): userland/apps/dup/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_DUP_ELF): $(USERLAND_DUP_START_OBJ) $(USERLAND_DUP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_DUP_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_DUP_LD) --build-id=none -o $@ $(USERLAND_DUP_START_OBJ) $(USERLAND_DUP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_TRUE_START_OBJ): userland/apps/true/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TRUE_MAIN_OBJ): userland/apps/true/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TRUE_ELF): $(USERLAND_TRUE_START_OBJ) $(USERLAND_TRUE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TRUE_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TRUE_LD) --build-id=none -o $@ $(USERLAND_TRUE_START_OBJ) $(USERLAND_TRUE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_SEQ_START_OBJ): userland/apps/seq/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_SEQ_MAIN_OBJ): userland/apps/seq/main.c userland/apps/seq/seq_logic.h userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SEQ_ELF): $(USERLAND_SEQ_START_OBJ) $(USERLAND_SEQ_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_SEQ_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_SEQ_LD) --build-id=none -o $@ $(USERLAND_SEQ_START_OBJ) $(USERLAND_SEQ_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_FALSE_START_OBJ): userland/apps/false/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_FALSE_MAIN_OBJ): userland/apps/false/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_FALSE_ELF): $(USERLAND_FALSE_START_OBJ) $(USERLAND_FALSE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_FALSE_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_FALSE_LD) --build-id=none -o $@ $(USERLAND_FALSE_START_OBJ) $(USERLAND_FALSE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_ID_START_OBJ): userland/apps/id/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_ID_MAIN_OBJ): userland/apps/id/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_ID_ELF): $(USERLAND_ID_START_OBJ) $(USERLAND_ID_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_ID_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_ID_LD) --build-id=none -o $@ $(USERLAND_ID_START_OBJ) $(USERLAND_ID_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_PS_START_OBJ): userland/apps/ps/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_PS_MAIN_OBJ): userland/apps/ps/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_PS_ELF): $(USERLAND_PS_START_OBJ) $(USERLAND_PS_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_PS_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_PS_LD) --build-id=none -o $@ $(USERLAND_PS_START_OBJ) $(USERLAND_PS_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_WAIT_START_OBJ): userland/apps/wait/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_WAIT_MAIN_OBJ): userland/apps/wait/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_WAIT_ELF): $(USERLAND_WAIT_START_OBJ) $(USERLAND_WAIT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_WAIT_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_WAIT_LD) --build-id=none -o $@ $(USERLAND_WAIT_START_OBJ) $(USERLAND_WAIT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_TRUNCATE_START_OBJ): userland/apps/truncate/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TRUNCATE_MAIN_OBJ): userland/apps/truncate/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TRUNCATE_ELF): $(USERLAND_TRUNCATE_START_OBJ) $(USERLAND_TRUNCATE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TRUNCATE_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TRUNCATE_LD) --build-id=none -o $@ $(USERLAND_TRUNCATE_START_OBJ) $(USERLAND_TRUNCATE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_SEEK_START_OBJ): userland/apps/seek/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_SEEK_MAIN_OBJ): userland/apps/seek/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SEEK_ELF): $(USERLAND_SEEK_START_OBJ) $(USERLAND_SEEK_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_SEEK_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_SEEK_LD) --build-id=none -o $@ $(USERLAND_SEEK_START_OBJ) $(USERLAND_SEEK_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_CHDIR_START_OBJ): userland/apps/chdir/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CHDIR_MAIN_OBJ): userland/apps/chdir/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CHDIR_ELF): $(USERLAND_CHDIR_START_OBJ) $(USERLAND_CHDIR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CHDIR_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CHDIR_LD) --build-id=none -o $@ $(USERLAND_CHDIR_START_OBJ) $(USERLAND_CHDIR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_CP_START_OBJ): userland/apps/cp/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CP_MAIN_OBJ): userland/apps/cp/main.c userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CP_ELF): $(USERLAND_CP_START_OBJ) $(USERLAND_CP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CP_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CP_LD) --build-id=none -o $@ $(USERLAND_CP_START_OBJ) $(USERLAND_CP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_HEAD_START_OBJ): userland/apps/head/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_HEAD_MAIN_OBJ): userland/apps/head/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_HEAD_ELF): $(USERLAND_HEAD_START_OBJ) $(USERLAND_HEAD_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_HEAD_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_HEAD_LD) --build-id=none -o $@ $(USERLAND_HEAD_START_OBJ) $(USERLAND_HEAD_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_WC_START_OBJ): userland/apps/wc/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_WC_MAIN_OBJ): userland/apps/wc/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_WC_ELF): $(USERLAND_WC_START_OBJ) $(USERLAND_WC_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_WC_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_WC_LD) --build-id=none -o $@ $(USERLAND_WC_START_OBJ) $(USERLAND_WC_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_GREP_START_OBJ): userland/apps/grep/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_GREP_MAIN_OBJ): userland/apps/grep/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_GREP_ELF): $(USERLAND_GREP_START_OBJ) $(USERLAND_GREP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_GREP_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_GREP_LD) --build-id=none -o $@ $(USERLAND_GREP_START_OBJ) $(USERLAND_GREP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_TEE_START_OBJ): userland/apps/tee/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TEE_MAIN_OBJ): userland/apps/tee/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TEE_ELF): $(USERLAND_TEE_START_OBJ) $(USERLAND_TEE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TEE_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TEE_LD) --build-id=none -o $@ $(USERLAND_TEE_START_OBJ) $(USERLAND_TEE_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_TAIL_START_OBJ): userland/apps/tail/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TAIL_MAIN_OBJ): userland/apps/tail/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TAIL_ELF): $(USERLAND_TAIL_START_OBJ) $(USERLAND_TAIL_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TAIL_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TAIL_LD) --build-id=none -o $@ $(USERLAND_TAIL_START_OBJ) $(USERLAND_TAIL_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_SORT_START_OBJ): userland/apps/sort/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_SORT_MAIN_OBJ): userland/apps/sort/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_SORT_ELF): $(USERLAND_SORT_START_OBJ) $(USERLAND_SORT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_SORT_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_SORT_LD) --build-id=none -o $@ $(USERLAND_SORT_START_OBJ) $(USERLAND_SORT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_UNIQ_START_OBJ): userland/apps/uniq/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_UNIQ_MAIN_OBJ): userland/apps/uniq/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_UNIQ_ELF): $(USERLAND_UNIQ_START_OBJ) $(USERLAND_UNIQ_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_UNIQ_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_UNIQ_LD) --build-id=none -o $@ $(USERLAND_UNIQ_START_OBJ) $(USERLAND_UNIQ_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_PRINTF_START_OBJ): userland/apps/printf/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_PRINTF_MAIN_OBJ): userland/apps/printf/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_PRINTF_ELF): $(USERLAND_PRINTF_START_OBJ) $(USERLAND_PRINTF_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_PRINTF_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_PRINTF_LD) --build-id=none -o $@ $(USERLAND_PRINTF_START_OBJ) $(USERLAND_PRINTF_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_BASENAME_START_OBJ): userland/apps/basename/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_BASENAME_MAIN_OBJ): userland/apps/basename/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_BASENAME_ELF): $(USERLAND_BASENAME_START_OBJ) $(USERLAND_BASENAME_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_BASENAME_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_BASENAME_LD) --build-id=none -o $@ $(USERLAND_BASENAME_START_OBJ) $(USERLAND_BASENAME_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(TEST_DIR):
	mkdir -p $@

$(CONTRACT_OBJ): $(CONTRACT_C) | $(TEST_DIR)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

$(CONTRACT_ASM_OBJ): $(CONTRACT_ASM) | $(TEST_DIR)
	$(NASM) -f elf64 $< -o $@

$(CONTRACT_ELF): $(CONTRACT_OBJ) $(CONTRACT_ASM_OBJ) scripts/tests/contract.ld
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_OBJ) $(CONTRACT_ASM_OBJ)

$(BUILD_DIR)/uefi:
	mkdir -p $@

$(dir $(UEFI_EFI)):
	mkdir -p $@

$(UEFI_OBJ): boot/UEFI/core/efi_main.c boot/UEFI/core/efi_context.h boot/UEFI/core/efi_types.h boot/UEFI/core/boot_info.h boot/UEFI/core/console.h boot/UEFI/core/firmware.h boot/UEFI/core/file.h boot/UEFI/core/elf.h boot/UEFI/core/memory_map.h | $(BUILD_DIR)/uefi
	$(CC) -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core \
		-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(UEFI_CONSOLE_OBJ): boot/UEFI/core/console.c boot/UEFI/core/console.h boot/UEFI/core/efi_types.h | $(BUILD_DIR)/uefi
	$(CC) -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core \
		-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(UEFI_FIRMWARE_OBJ): boot/UEFI/core/firmware.c boot/UEFI/core/firmware.h boot/UEFI/core/efi_types.h boot/UEFI/core/boot_info.h | $(BUILD_DIR)/uefi
	$(CC) -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core \
		-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(UEFI_FILE_OBJ): boot/UEFI/core/file.c boot/UEFI/core/file.h boot/UEFI/core/efi_types.h | $(BUILD_DIR)/uefi
	$(CC) -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core \
		-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(UEFI_ELF_OBJ): boot/UEFI/core/elf.c boot/UEFI/core/elf.h boot/UEFI/core/efi_types.h | $(BUILD_DIR)/uefi
	$(CC) -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core \
		-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(UEFI_MEMORY_MAP_OBJ): boot/UEFI/core/memory_map.c boot/UEFI/core/memory_map.h boot/UEFI/core/efi_types.h boot/UEFI/core/boot_info.h | $(BUILD_DIR)/uefi
	$(CC) -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core \
		-fno-stack-protector -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(UEFI_ENTRY_OBJ): boot/UEFI/core/entry.asm | $(BUILD_DIR)/uefi
	$(NASM) -f win64 $< -o $@

$(UEFI_EFI): $(UEFI_OBJ) $(UEFI_ENTRY_OBJ) $(UEFI_CONSOLE_OBJ) $(UEFI_FIRMWARE_OBJ) $(UEFI_FILE_OBJ) $(UEFI_ELF_OBJ) $(UEFI_MEMORY_MAP_OBJ) | $(dir $(UEFI_EFI))
	$(LD) -flavor link /subsystem:efi_application /entry:efi_entry \
		/nodefaultlib /machine:x64 /out:$@ $(UEFI_ENTRY_OBJ) $(UEFI_OBJ) $(UEFI_CONSOLE_OBJ) $(UEFI_FIRMWARE_OBJ) $(UEFI_FILE_OBJ) $(UEFI_ELF_OBJ) $(UEFI_MEMORY_MAP_OBJ)

$(BUILD_DIR)/kernel:
	mkdir -p $@

$(KERNEL_OBJ): kernel/arch/x86_64/entry/kernel_entry.c kernel/arch/x86_64/cpu/tables.h kernel/drivers/storage/ata.h kernel/drivers/usb/uhci.h kernel/drivers/network/e1000.h kernel/drivers/network/ethernet.h kernel/drivers/network/arp.h kernel/drivers/network/arp_cache.h kernel/drivers/network/ipv4.h kernel/drivers/network/udp.h kernel/drivers/network/icmp.h kernel/drivers/network/route.h kernel/drivers/network/packet_queue.h kernel/drivers/network/network.h kernel/drivers/network/reassembly.h kernel/drivers/network/udp_endpoint.h kernel/fs/vfs/vfs.h kernel/fs/vfs/file.h kernel/ipc/endpoint.h kernel/syscall/abi.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core -fno-stack-protector \
		-fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ENTRY_ASM_OBJ): kernel/arch/x86_64/entry/entry.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_TABLES_OBJ): kernel/arch/x86_64/cpu/tables.c kernel/arch/x86_64/cpu/tables.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -Iboot/UEFI/core -Ikernel/arch/x86_64/cpu \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TABLES_ASM_OBJ): kernel/arch/x86_64/cpu/tables.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_SERIAL_OBJ): kernel/drivers/serial/serial.c kernel/drivers/serial/serial.h kernel/core/printk/serial.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_CPU_OBJ): kernel/arch/x86_64/cpu/cpu.c kernel/arch/x86_64/cpu/cpu.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_EXCEPTIONS_OBJ): kernel/arch/x86_64/interrupts/exceptions.c kernel/arch/x86_64/interrupts/exceptions.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PANIC_OBJ): kernel/core/panic/panic.c kernel/core/panic/panic.h kernel/core/printk/serial.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PHYSICAL_OBJ): kernel/mm/physical/frame.c kernel/mm/physical/frame.h boot/UEFI/core/boot_info.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VIRTUAL_OBJ): kernel/mm/virtual/address_space.c kernel/mm/virtual/address_space.h kernel/arch/x86_64/memory/paging.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PAGING_OBJ): kernel/arch/x86_64/memory/paging.c kernel/arch/x86_64/memory/paging.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

# Keep the source rule above tied to address_space.c while adding the
# architecture paging object to the final kernel dependency/link expansion.
KERNEL_VIRTUAL_OBJ := $(KERNEL_VIRTUAL_OBJ) $(KERNEL_PAGING_OBJ)

$(KERNEL_HEAP_OBJ): kernel/mm/heap/heap.c kernel/mm/heap/heap.h kernel/mm/physical/frame.h kernel/mm/virtual/address_space.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_IRQ_OBJ): kernel/arch/x86_64/interrupts/irq.c kernel/arch/x86_64/interrupts/irq.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_APIC_OBJ): kernel/arch/x86_64/interrupts/apic.c kernel/arch/x86_64/interrupts/apic.h kernel/arch/x86_64/cpu/cpu.h kernel/drivers/acpi/acpi.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ACPI_OBJ): kernel/drivers/acpi/acpi.c kernel/drivers/acpi/acpi.h kernel/arch/x86_64/platform/acpi.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PERCPU_OBJ): kernel/arch/x86_64/smp/percpu.c kernel/arch/x86_64/smp/percpu.h kernel/arch/x86_64/platform/acpi.h kernel/arch/x86_64/interrupts/apic.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TRAMPOLINE_OBJ): kernel/arch/x86_64/smp/trampoline.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_TIMER_OBJ): kernel/arch/x86_64/time/timer.c kernel/arch/x86_64/time/timer.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TIMER_ASM_OBJ): kernel/arch/x86_64/time/timer.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_KEYBOARD_ASM_OBJ): kernel/arch/x86_64/interrupts/keyboard.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

KERNEL_SYNC_OBJ := $(BUILD_DIR)/kernel/spinlock.o
KERNEL_TASK_OBJ := $(BUILD_DIR)/kernel/task_context.o
KERNEL_TASK_ASM_OBJ := $(BUILD_DIR)/kernel/task_context.asm.o
KERNEL_WAIT_OBJ := $(BUILD_DIR)/kernel/task_wait_queue.o
KERNEL_SCHED_OBJ := $(BUILD_DIR)/kernel/task_scheduler.o
KERNEL_SCHED_POLICY_OBJ := $(BUILD_DIR)/kernel/round_robin.o
KERNEL_TASK_DESC_OBJ := $(BUILD_DIR)/kernel/task.o
KERNEL_PROCESS_OBJ := $(BUILD_DIR)/kernel/user_image.o
KERNEL_PROCESS_LIFECYCLE_OBJ := $(BUILD_DIR)/kernel/process.o
KERNEL_PROCESS_HANDLE_OBJ := $(BUILD_DIR)/kernel/handle.o
KERNEL_PROCESS_THREAD_OBJ := $(BUILD_DIR)/kernel/process_thread.o
KERNEL_EXEC_OBJ := $(BUILD_DIR)/kernel/exec.o
KERNEL_SYSCALL_OBJ := $(BUILD_DIR)/kernel/syscall.o
KERNEL_SYSCALL_ASM_OBJ := $(BUILD_DIR)/kernel/syscall.asm.o
KERNEL_DEVICE_OBJ := $(BUILD_DIR)/kernel/device.o
KERNEL_PCI_OBJ := $(BUILD_DIR)/kernel/pci.o
KERNEL_MEMORY_OBJ := $(BUILD_DIR)/kernel/memory.o
KERNEL_STORAGE_OBJ := $(BUILD_DIR)/kernel/storage.o
KERNEL_ATA_OBJ := $(BUILD_DIR)/kernel/ata.o
KERNEL_IPC_OBJ := $(BUILD_DIR)/kernel/ipc_channel.o
KERNEL_IPC_ENDPOINT_OBJ := $(BUILD_DIR)/kernel/ipc_endpoint.o
KERNEL_PIPE_OBJ := $(BUILD_DIR)/kernel/pipe.o
KERNEL_SECURITY_OBJ := $(BUILD_DIR)/kernel/security_credentials.o
KERNEL_VFS_OBJ := $(BUILD_DIR)/kernel/vfs.o
KERNEL_VFS_FILE_OBJ := $(BUILD_DIR)/kernel/vfs_file.o
KERNEL_VFS_MOUNT_OBJ := $(BUILD_DIR)/kernel/vfs_mount.o
KERNEL_VFS_PROBE_OBJ := $(BUILD_DIR)/kernel/vfs_probe.o
KERNEL_BLOCK_OBJ := $(BUILD_DIR)/kernel/block.o
KERNEL_STORAGE_BLOCK_OBJ := $(BUILD_DIR)/kernel/storage_block.o
KERNEL_CACHE_OBJ := $(BUILD_DIR)/kernel/block_cache.o
KERNEL_DEVFS_OBJ := $(BUILD_DIR)/kernel/devfs.o
KERNEL_PROCFS_OBJ := $(BUILD_DIR)/kernel/procfs.o
KERNEL_SLAB_OBJ := $(BUILD_DIR)/kernel/slab.o
KERNEL_FAT12_OBJ := $(BUILD_DIR)/kernel/fat12.o
KERNEL_FAT12_VFS_OBJ := $(BUILD_DIR)/kernel/fat12_vfs.o
KERNEL_FAT32_VFS_OBJ := $(BUILD_DIR)/kernel/fat32_vfs.o
KERNEL_FAT32_OBJ := $(BUILD_DIR)/kernel/fat32.o $(KERNEL_FAT32_VFS_OBJ)
KERNEL_EXFAT_OBJ := $(BUILD_DIR)/kernel/exfat.o
KERNEL_EXFAT_VFS_OBJ := $(BUILD_DIR)/kernel/exfat_vfs.o
KERNEL_EXT4_OBJ := $(BUILD_DIR)/kernel/ext4.o
KERNEL_EXT4_VFS_OBJ := $(BUILD_DIR)/kernel/ext4_vfs.o
KERNEL_XFS_OBJ := $(BUILD_DIR)/kernel/xfs.o
KERNEL_XFS_VFS_OBJ := $(BUILD_DIR)/kernel/xfs_vfs.o
KERNEL_BTRFS_OBJ := $(BUILD_DIR)/kernel/btrfs.o
KERNEL_BTRFS_DEFLATE_OBJ := $(BUILD_DIR)/kernel/btrfs_deflate.o
KERNEL_BTRFS_LZO_OBJ := $(BUILD_DIR)/kernel/btrfs_lzo.o
KERNEL_BTRFS_ZSTD_OBJ := $(BUILD_DIR)/kernel/btrfs_zstd.o
KERNEL_BTRFS_FSE_OBJ := $(BUILD_DIR)/kernel/btrfs_fse.o
KERNEL_BTRFS_VFS_OBJ := $(BUILD_DIR)/kernel/btrfs_vfs.o
KERNEL_INPUT_OBJ := $(BUILD_DIR)/kernel/input.o
KERNEL_PS2_OBJ := $(BUILD_DIR)/kernel/ps2.o
KERNEL_CONSOLE_OBJ := $(BUILD_DIR)/kernel/console.o
KERNEL_FRAMEBUFFER_OBJ := $(BUILD_DIR)/kernel/framebuffer.o $(KERNEL_CONSOLE_OBJ)
KERNEL_BOCHS_VGA_OBJ := $(BUILD_DIR)/kernel/bochs_vga.o
KERNEL_DISPLAY_SURFACE_OBJ := $(BUILD_DIR)/kernel/surface.o
KERNEL_COMPOSITOR_OBJ := $(BUILD_DIR)/kernel/compositor.o
KERNEL_WINDOW_MANAGER_OBJ := $(BUILD_DIR)/kernel/window_manager.o
KERNEL_DISPLAY_SERVICE_OBJ := $(BUILD_DIR)/kernel/display_service.o
KERNEL_USB_OBJ := $(BUILD_DIR)/kernel/usb.o
KERNEL_HID_OBJ := $(BUILD_DIR)/kernel/hid.o
KERNEL_UHCI_OBJ := $(BUILD_DIR)/kernel/uhci.o
KERNEL_AHCI_OBJ := $(BUILD_DIR)/kernel/ahci.o
KERNEL_NVME_OBJ := $(BUILD_DIR)/kernel/nvme.o
KERNEL_E1000_OBJ := $(BUILD_DIR)/kernel/e1000.o
KERNEL_RTC_OBJ := $(BUILD_DIR)/kernel/rtc.o
KERNEL_NVME_OBJ := $(KERNEL_NVME_OBJ) $(KERNEL_E1000_OBJ) $(KERNEL_ETHERNET_OBJ) $(KERNEL_ARP_OBJ) $(KERNEL_ARP_CACHE_OBJ) $(KERNEL_IPV4_OBJ) $(KERNEL_UDP_OBJ) $(KERNEL_TCP_OBJ) $(KERNEL_ICMP_OBJ) $(KERNEL_ROUTE_OBJ) $(KERNEL_PACKET_QUEUE_OBJ) $(KERNEL_NETWORK_OBJ) $(KERNEL_REASSEMBLY_OBJ) $(KERNEL_UDP_ENDPOINT_OBJ) $(KERNEL_TCP_ENDPOINT_OBJ) $(KERNEL_E1000_IRQ_OBJ) $(KERNEL_NVME_IRQ_OBJ) $(KERNEL_AHCI_IRQ_OBJ) $(KERNEL_HID_OBJ) $(KERNEL_STORAGE_BLOCK_OBJ) $(KERNEL_EXFAT_VFS_OBJ) $(KERNEL_EXT4_OBJ) $(KERNEL_EXT4_VFS_OBJ) $(KERNEL_XFS_OBJ) $(KERNEL_XFS_VFS_OBJ) $(KERNEL_BTRFS_OBJ) $(KERNEL_BTRFS_DEFLATE_OBJ) $(KERNEL_BTRFS_LZO_OBJ) $(KERNEL_BTRFS_ZSTD_OBJ) $(KERNEL_BTRFS_FSE_OBJ) $(KERNEL_BTRFS_VFS_OBJ) $(KERNEL_RTC_OBJ)
KERNEL_NVME_OBJ := $(KERNEL_NVME_OBJ) $(KERNEL_VFS_PROBE_OBJ)
KERNEL_NVME_OBJ := $(KERNEL_NVME_OBJ) $(KERNEL_VFS_FILE_OBJ)
KERNEL_NVME_OBJ := $(KERNEL_NVME_OBJ) $(KERNEL_IPC_ENDPOINT_OBJ)
KERNEL_NVME_OBJ := $(KERNEL_NVME_OBJ) $(KERNEL_PIPE_OBJ)
KERNEL_DEBUG_OBJ := $(BUILD_DIR)/kernel/debug_assert.o
KERNEL_CLOCK_OBJ := $(BUILD_DIR)/kernel/clock.o

$(KERNEL_MEMORY_OBJ): kernel/lib/memory.c kernel/lib/memory.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_IPC_OBJ): kernel/ipc/channel.c kernel/ipc/channel.h kernel/core/sync/spinlock.h kernel/core/task/wait_queue.h kernel/sched/core/scheduler.h kernel/sched/core/scheduler.c | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_IPC_ENDPOINT_OBJ): kernel/ipc/endpoint.c kernel/ipc/endpoint.h kernel/ipc/channel.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PIPE_OBJ): kernel/ipc/pipe.c kernel/ipc/pipe.h kernel/ipc/channel.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SECURITY_OBJ): kernel/security/credentials.c kernel/security/credentials.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VFS_OBJ): kernel/fs/vfs/vfs.c kernel/fs/vfs/vfs.h kernel/core/sync/spinlock.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VFS_FILE_OBJ): kernel/fs/vfs/file.c kernel/fs/vfs/file.h kernel/fs/vfs/vfs.h kernel/core/process/handle.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VFS_MOUNT_OBJ): kernel/fs/vfs/mount.c kernel/fs/vfs/mount.h kernel/fs/vfs/vfs.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VFS_PROBE_OBJ): kernel/fs/vfs/probe.c kernel/fs/vfs/probe.h kernel/fs/fat/fat32.h kernel/fs/exfat/exfat.h kernel/fs/ext4/ext4.h kernel/fs/xfs/xfs.h kernel/fs/btrfs/btrfs.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BLOCK_OBJ): kernel/fs/block/block.c kernel/fs/block/block.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_STORAGE_BLOCK_OBJ): kernel/fs/block/storage_block.c kernel/fs/block/storage_block.h kernel/fs/block/block.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_CACHE_OBJ): kernel/fs/cache/cache.c kernel/fs/cache/cache.h kernel/fs/block/block.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_DEVFS_OBJ): kernel/fs/devfs/devfs.c kernel/fs/devfs/devfs.h kernel/fs/vfs/vfs.h kernel/device/device.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PROCFS_OBJ): kernel/fs/procfs/procfs.c kernel/fs/procfs/procfs.h kernel/fs/vfs/vfs.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SLAB_OBJ): kernel/mm/slab/slab.c kernel/mm/slab/slab.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_FAT12_OBJ): kernel/fs/fat/fat12.c kernel/fs/fat/fat12.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_FAT12_VFS_OBJ): kernel/fs/fat/fat12_vfs.c kernel/fs/fat/fat12_vfs.h kernel/fs/fat/fat12.h kernel/fs/vfs/vfs.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(BUILD_DIR)/kernel/fat32.o: kernel/fs/fat/fat32.c kernel/fs/fat/fat32.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_FAT32_VFS_OBJ): kernel/fs/fat/fat32_vfs.c kernel/fs/fat/fat32_vfs.h kernel/fs/fat/fat32.h kernel/fs/vfs/vfs.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_EXFAT_OBJ): kernel/fs/exfat/exfat.c kernel/fs/exfat/exfat.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_EXFAT_VFS_OBJ): kernel/fs/exfat/exfat_vfs.c kernel/fs/exfat/exfat_vfs.h kernel/fs/exfat/exfat.h kernel/fs/vfs/vfs.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_EXT4_OBJ): kernel/fs/ext4/ext4.c kernel/fs/ext4/ext4.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_EXT4_VFS_OBJ): kernel/fs/ext4/ext4_vfs.c kernel/fs/ext4/ext4_vfs.h kernel/fs/ext4/ext4.h kernel/fs/vfs/vfs.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_XFS_OBJ): kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_XFS_VFS_OBJ): kernel/fs/xfs/xfs_vfs.c kernel/fs/xfs/xfs_vfs.h kernel/fs/xfs/xfs.h kernel/fs/vfs/vfs.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BTRFS_OBJ): kernel/fs/btrfs/btrfs.c kernel/fs/btrfs/btrfs.h kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BTRFS_DEFLATE_OBJ): kernel/fs/btrfs/deflate.c kernel/fs/btrfs/deflate.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BTRFS_LZO_OBJ): kernel/fs/btrfs/lzo.c kernel/fs/btrfs/lzo.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BTRFS_ZSTD_OBJ): kernel/fs/btrfs/zstd.c kernel/fs/btrfs/zstd.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BTRFS_FSE_OBJ): kernel/fs/btrfs/fse.c kernel/fs/btrfs/fse.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BTRFS_VFS_OBJ): kernel/fs/btrfs/btrfs_vfs.c kernel/fs/btrfs/btrfs_vfs.h kernel/fs/btrfs/btrfs.h kernel/fs/vfs/vfs.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_INPUT_OBJ): kernel/drivers/input/input.c kernel/drivers/input/input.h kernel/drivers/input/ps2.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PS2_OBJ): kernel/drivers/input/ps2.c kernel/drivers/input/ps2.h kernel/drivers/input/input.h kernel/arch/x86_64/time/timer.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(BUILD_DIR)/kernel/framebuffer.o: kernel/drivers/display/framebuffer.c kernel/drivers/display/framebuffer.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_CONSOLE_OBJ): kernel/drivers/display/console.c kernel/drivers/display/console.h kernel/drivers/display/framebuffer.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BOCHS_VGA_OBJ): kernel/drivers/display/bochs_vga.c kernel/drivers/display/bochs_vga.h kernel/device/device.h kernel/drivers/pci/pci.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_DISPLAY_SURFACE_OBJ): kernel/drivers/display/surface.c kernel/drivers/display/surface.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_COMPOSITOR_OBJ): kernel/drivers/display/compositor.c kernel/drivers/display/compositor.h kernel/drivers/display/surface.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_WINDOW_MANAGER_OBJ): kernel/drivers/display/window_manager.c kernel/drivers/display/window_manager.h kernel/drivers/display/compositor.h kernel/drivers/display/surface.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_DISPLAY_SERVICE_OBJ): kernel/drivers/display/display_service.c kernel/drivers/display/display_service.h kernel/drivers/display/window_manager.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_USB_OBJ): kernel/drivers/usb/usb.c kernel/drivers/usb/usb.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_HID_OBJ): kernel/drivers/usb/hid.c kernel/drivers/usb/hid.h kernel/drivers/input/input.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_UHCI_OBJ): kernel/drivers/usb/uhci.c kernel/drivers/usb/uhci.h kernel/device/device.h kernel/drivers/pci/pci.h kernel/mm/physical/frame.h kernel/arch/x86_64/cpu/tables.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_AHCI_OBJ): kernel/drivers/ahci/ahci.c kernel/drivers/ahci/ahci.h kernel/device/device.h kernel/arch/x86_64/cpu/tables.h kernel/drivers/pci/pci.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(BUILD_DIR)/kernel/nvme.o: kernel/drivers/nvme/nvme.c kernel/drivers/nvme/nvme.h kernel/device/device.h kernel/drivers/pci/pci.h kernel/mm/physical/frame.h kernel/arch/x86_64/cpu/tables.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_E1000_OBJ): kernel/drivers/network/e1000.c kernel/drivers/network/e1000.h kernel/device/device.h kernel/drivers/pci/pci.h kernel/mm/physical/frame.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ETHERNET_OBJ): kernel/drivers/network/ethernet.c kernel/drivers/network/ethernet.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ARP_OBJ): kernel/drivers/network/arp.c kernel/drivers/network/arp.h kernel/drivers/network/ethernet.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ARP_CACHE_OBJ): kernel/drivers/network/arp_cache.c kernel/drivers/network/arp_cache.h kernel/drivers/network/ethernet.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_IPV4_OBJ): kernel/drivers/network/ipv4.c kernel/drivers/network/ipv4.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_UDP_OBJ): kernel/drivers/network/udp.c kernel/drivers/network/udp.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TCP_OBJ): kernel/drivers/network/tcp.c kernel/drivers/network/tcp.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TCP_ENDPOINT_OBJ): kernel/drivers/network/tcp_endpoint.c kernel/drivers/network/tcp_endpoint.h kernel/drivers/network/tcp.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ICMP_OBJ): kernel/drivers/network/icmp.c kernel/drivers/network/icmp.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ROUTE_OBJ): kernel/drivers/network/route.c kernel/drivers/network/route.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PACKET_QUEUE_OBJ): kernel/drivers/network/packet_queue.c kernel/drivers/network/packet_queue.h kernel/drivers/network/ethernet.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_NETWORK_OBJ): kernel/drivers/network/network.c kernel/drivers/network/network.h kernel/drivers/network/e1000.h kernel/drivers/network/packet_queue.h kernel/drivers/network/ethernet.h kernel/drivers/network/arp.h kernel/drivers/network/arp_cache.h kernel/drivers/network/ipv4.h kernel/drivers/network/udp.h kernel/drivers/network/tcp.h kernel/drivers/network/icmp.h kernel/drivers/network/udp_endpoint.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_REASSEMBLY_OBJ): kernel/drivers/network/reassembly.c kernel/drivers/network/reassembly.h kernel/drivers/network/ipv4.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_UDP_ENDPOINT_OBJ): kernel/drivers/network/udp_endpoint.c kernel/drivers/network/udp_endpoint.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_E1000_IRQ_OBJ): kernel/arch/x86_64/interrupts/e1000.asm kernel/drivers/network/e1000.h | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_NVME_IRQ_OBJ): kernel/arch/x86_64/interrupts/nvme.asm kernel/drivers/nvme/nvme.h | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_AHCI_IRQ_OBJ): kernel/arch/x86_64/interrupts/ahci.asm kernel/drivers/ahci/ahci.h | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@




$(KERNEL_DEBUG_OBJ): kernel/debug/assert.c kernel/debug/assert.h kernel/core/panic/panic.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_EXEC_OBJ): kernel/exec/exec.c kernel/exec/exec.h kernel/core/process/user_image.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_CLOCK_OBJ): kernel/time/clock.c kernel/time/clock.h kernel/arch/x86_64/time/timer.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_RTC_OBJ): kernel/drivers/time/rtc.c kernel/drivers/time/rtc.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_STORAGE_OBJ): kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_ATA_OBJ): kernel/drivers/storage/ata.c kernel/drivers/storage/ata.h kernel/drivers/storage/storage.h kernel/device/device.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_DEVICE_OBJ): kernel/device/device.c kernel/device/device.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PCI_OBJ): kernel/drivers/pci/pci.c kernel/drivers/pci/pci.h kernel/device/device.h kernel/core/sync/spinlock.h kernel/arch/x86_64/interrupts/apic.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SYNC_OBJ): kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TASK_OBJ): kernel/core/task/context.c kernel/core/task/context.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TASK_ASM_OBJ): kernel/arch/x86_64/task/context.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_WAIT_OBJ): kernel/core/task/wait_queue.c kernel/core/task/wait_queue.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_TASK_DESC_OBJ): kernel/core/task/task.c kernel/core/task/task.h kernel/core/task/context.h kernel/core/task/wait_queue.h kernel/mm/virtual/address_space.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SCHED_OBJ): kernel/sched/core/scheduler.c kernel/sched/core/scheduler.h kernel/sched/policy/round_robin.c kernel/sched/policy/round_robin.h kernel/core/task/task.h kernel/core/task/context.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SCHED_POLICY_OBJ): kernel/sched/policy/round_robin.c kernel/sched/policy/round_robin.h kernel/core/task/task.h kernel/core/task/wait_queue.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@


$(KERNEL_PROCESS_OBJ): kernel/core/process/user_image.c kernel/core/process/user_image.h kernel/mm/virtual/address_space.h kernel/mm/physical/frame.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PROCESS_LIFECYCLE_OBJ): kernel/core/process/process.c kernel/core/process/process.h kernel/fs/vfs/vfs.h kernel/core/process/thread.h kernel/core/process/handle.c kernel/core/process/handle.h kernel/core/process/user_image.h kernel/mm/virtual/address_space.h kernel/security/credentials.h kernel/mm/heap/heap.h kernel/mm/physical/frame.h kernel/core/sync/spinlock.h kernel/core/task/wait_queue.h kernel/sched/core/scheduler.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PROCESS_HANDLE_OBJ): kernel/core/process/handle.c kernel/core/process/handle.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PROCESS_THREAD_OBJ): kernel/core/process/thread.c kernel/core/process/thread.h kernel/core/process/process.h kernel/core/process/user_image.h kernel/mm/virtual/address_space.h kernel/core/task/task.h kernel/core/task/wait_queue.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SYSCALL_OBJ): kernel/core/syscall/syscall.c kernel/core/syscall/syscall.h kernel/syscall/abi.h kernel/time/clock.h kernel/core/process/process.h kernel/fs/vfs/file.h kernel/ipc/endpoint.h kernel/arch/x86_64/cpu/tables.h kernel/arch/x86_64/time/timer.h kernel/mm/virtual/address_space.h kernel/drivers/input/input.h kernel/drivers/display/console.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SYSCALL_ASM_OBJ): kernel/arch/x86_64/syscall/entry.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJ) $(KERNEL_ENTRY_ASM_OBJ) $(KERNEL_TABLES_OBJ) $(KERNEL_TABLES_ASM_OBJ) $(KERNEL_SERIAL_OBJ) $(KERNEL_CPU_OBJ) $(KERNEL_EXCEPTIONS_OBJ) $(KERNEL_PANIC_OBJ) $(KERNEL_PHYSICAL_OBJ) $(KERNEL_VIRTUAL_OBJ) $(KERNEL_HEAP_OBJ) $(KERNEL_IRQ_OBJ) $(KERNEL_APIC_OBJ) $(KERNEL_ACPI_OBJ) $(KERNEL_PERCPU_OBJ) $(KERNEL_TRAMPOLINE_OBJ) $(KERNEL_TIMER_OBJ) $(KERNEL_TIMER_ASM_OBJ) $(KERNEL_KEYBOARD_ASM_OBJ) $(KERNEL_SYNC_OBJ) $(KERNEL_TASK_OBJ) $(KERNEL_TASK_ASM_OBJ) $(KERNEL_WAIT_OBJ) $(KERNEL_TASK_DESC_OBJ) $(KERNEL_SCHED_OBJ) $(KERNEL_SCHED_POLICY_OBJ) $(KERNEL_PROCESS_OBJ) $(KERNEL_PROCESS_LIFECYCLE_OBJ) $(KERNEL_PROCESS_HANDLE_OBJ) $(KERNEL_PROCESS_THREAD_OBJ) $(KERNEL_EXEC_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_SYSCALL_ASM_OBJ) $(KERNEL_DEVICE_OBJ) $(KERNEL_PCI_OBJ) $(KERNEL_MEMORY_OBJ) $(KERNEL_STORAGE_OBJ) $(KERNEL_ATA_OBJ) $(KERNEL_IPC_OBJ) $(KERNEL_SECURITY_OBJ) $(KERNEL_VFS_OBJ) $(KERNEL_DEBUG_OBJ) $(KERNEL_VFS_MOUNT_OBJ) $(KERNEL_BLOCK_OBJ) $(KERNEL_CACHE_OBJ) $(KERNEL_CLOCK_OBJ) $(KERNEL_DEVFS_OBJ) $(KERNEL_PROCFS_OBJ) $(KERNEL_SLAB_OBJ) $(KERNEL_FAT12_OBJ) $(KERNEL_FAT12_VFS_OBJ) $(KERNEL_FAT32_OBJ) $(KERNEL_EXFAT_OBJ) $(KERNEL_INPUT_OBJ) $(KERNEL_PS2_OBJ) $(KERNEL_FRAMEBUFFER_OBJ) $(KERNEL_BOCHS_VGA_OBJ) $(KERNEL_DISPLAY_SURFACE_OBJ) $(KERNEL_COMPOSITOR_OBJ) $(KERNEL_WINDOW_MANAGER_OBJ) $(KERNEL_DISPLAY_SERVICE_OBJ) $(KERNEL_USB_OBJ) $(KERNEL_AHCI_OBJ) $(KERNEL_UHCI_OBJ) $(KERNEL_NVME_OBJ)
$(KERNEL_ELF): $(KERNEL_PIPE_OBJ)
	$(LD) -m elf_x86_64 -pie -T kernel/arch/x86_64/entry/kernel.ld --build-id=none -o $@ $(KERNEL_ENTRY_ASM_OBJ) $(KERNEL_OBJ) $(KERNEL_TABLES_OBJ) $(KERNEL_TABLES_ASM_OBJ) $(KERNEL_SERIAL_OBJ) $(KERNEL_CPU_OBJ) $(KERNEL_EXCEPTIONS_OBJ) $(KERNEL_PANIC_OBJ) $(KERNEL_PHYSICAL_OBJ) $(KERNEL_VIRTUAL_OBJ) $(KERNEL_HEAP_OBJ) $(KERNEL_IRQ_OBJ) $(KERNEL_APIC_OBJ) $(KERNEL_ACPI_OBJ) $(KERNEL_PERCPU_OBJ) $(KERNEL_TRAMPOLINE_OBJ) $(KERNEL_TIMER_OBJ) $(KERNEL_TIMER_ASM_OBJ) $(KERNEL_KEYBOARD_ASM_OBJ) $(KERNEL_SYNC_OBJ) $(KERNEL_TASK_OBJ) $(KERNEL_TASK_ASM_OBJ) $(KERNEL_WAIT_OBJ) $(KERNEL_TASK_DESC_OBJ) $(KERNEL_SCHED_OBJ) $(KERNEL_SCHED_POLICY_OBJ) $(KERNEL_PROCESS_OBJ) $(KERNEL_PROCESS_LIFECYCLE_OBJ) $(KERNEL_PROCESS_HANDLE_OBJ) $(KERNEL_PROCESS_THREAD_OBJ) $(KERNEL_EXEC_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_SYSCALL_ASM_OBJ) $(KERNEL_DEVICE_OBJ) $(KERNEL_PCI_OBJ) $(KERNEL_MEMORY_OBJ) $(KERNEL_STORAGE_OBJ) $(KERNEL_ATA_OBJ) $(KERNEL_IPC_OBJ) $(KERNEL_SECURITY_OBJ) $(KERNEL_VFS_OBJ) $(KERNEL_DEBUG_OBJ) $(KERNEL_VFS_MOUNT_OBJ) $(KERNEL_BLOCK_OBJ) $(KERNEL_CACHE_OBJ) $(KERNEL_CLOCK_OBJ) $(KERNEL_DEVFS_OBJ) $(KERNEL_PROCFS_OBJ) $(KERNEL_SLAB_OBJ) $(KERNEL_FAT12_OBJ) $(KERNEL_FAT12_VFS_OBJ) $(KERNEL_FAT32_OBJ) $(KERNEL_EXFAT_OBJ) $(KERNEL_INPUT_OBJ) $(KERNEL_PS2_OBJ) $(KERNEL_FRAMEBUFFER_OBJ) $(KERNEL_BOCHS_VGA_OBJ) $(KERNEL_DISPLAY_SURFACE_OBJ) $(KERNEL_COMPOSITOR_OBJ) $(KERNEL_WINDOW_MANAGER_OBJ) $(KERNEL_DISPLAY_SERVICE_OBJ) $(KERNEL_USB_OBJ) $(KERNEL_AHCI_OBJ) $(KERNEL_UHCI_OBJ) $(KERNEL_NVME_OBJ)

test: all image
	$(MAKE) userland-test
	$(MAKE) userland-set-test
	$(MAKE) args-test
	$(MAKE) env-test
	$(MAKE) cat-test
	$(MAKE) pwd-test
	$(MAKE) mkdir-test
	$(MAKE) rm-test
	$(MAKE) rmdir-test
	$(MAKE) touch-test
	$(MAKE) write-test
	$(MAKE) ls-test
	$(MAKE) chmod-test
	$(MAKE) echo-test
	$(MAKE) help-test
	$(MAKE) stat-test
	$(MAKE) mv-test
	$(MAKE) kill-test
	$(MAKE) sleep-test
	$(MAKE) setenv-test
	$(MAKE) unsetenv-test
	$(MAKE) uptime-test
	$(MAKE) date-test
	$(MAKE) clear-test
	$(MAKE) ipc-test
	$(MAKE) dup-test
	$(MAKE) true-test
	$(MAKE) seq-test
	$(MAKE) false-test
	$(MAKE) id-test
	$(MAKE) ps-test
	$(MAKE) wait-test
	$(MAKE) truncate-test
	$(MAKE) seek-test
	$(MAKE) chdir-test
	$(MAKE) cp-test
	$(MAKE) head-test
	$(MAKE) wc-test
	$(MAKE) grep-test
	$(MAKE) tee-test
	$(MAKE) tail-test
	$(MAKE) sort-test
	$(MAKE) uniq-test
	$(MAKE) printf-test
	$(MAKE) basename-test
	$(MAKE) dirname-test
	$(MAKE) cut-test
	$(MAKE) tr-test
	$(MAKE) cmp-test
	$(MAKE) which-test
	$(MAKE) find-test
	$(MAKE) expr-test
	$(MAKE) shell-test
	$(MAKE) shell-integration-test
	$(MAKE) userland-runtime-test
	$(MAKE) test-predicate-test
	$(MAKE) fat32-test
	$(MAKE) exfat-test
	$(MAKE) ext4-test
	$(MAKE) xfs-test
	$(MAKE) xfs-rename-test
	$(MAKE) xfs-alloc-test
	$(MAKE) xfs-unwritten-test
	$(MAKE) xfs-auth-test
	$(MAKE) btrfs-test
	$(MAKE) deflate-test
	$(MAKE) lzo-test
	$(MAKE) zstd-test
	$(MAKE) fse-test
	$(MAKE) cache-test
	$(MAKE) device-test
	sh scripts/tests/sh/validate_build.sh $(CONTRACT_ELF)
	sh scripts/tests/sh/validate_uefi.sh $(UEFI_EFI)
	sh scripts/tests/sh/validate_kernel.sh $(KERNEL_ELF)
	$(MAKE) qemu-test

fat32-test: $(FAT32_TEST)
	$(FAT32_TEST)

exfat-test: $(EXFAT_TEST)
	$(EXFAT_TEST)

ext4-test: $(EXT4_TEST)
	$(EXT4_TEST)

xfs-test: $(XFS_TEST)
	$(XFS_TEST)

xfs-rename-test: $(XFS_RENAME_TEST)
	$(XFS_RENAME_TEST)

xfs-alloc-test: $(XFS_ALLOC_TEST)
	$(XFS_ALLOC_TEST)

xfs-unwritten-test: $(XFS_UNWRITTEN_TEST)
	$(XFS_UNWRITTEN_TEST)

xfs-auth-test: $(XFS_AUTH_TEST)
	$(XFS_AUTH_TEST)

btrfs-test: $(BTRFS_TEST)
	$(BTRFS_TEST)

deflate-test: $(DEFLATE_TEST)
	$(DEFLATE_TEST)

lzo-test: $(LZO_TEST)
	$(LZO_TEST)

zstd-test: $(ZSTD_TEST)
	$(ZSTD_TEST)

fse-test: $(FSE_TEST)
	$(FSE_TEST)

cache-test: $(CACHE_TEST)
	$(CACHE_TEST)

device-test: $(DEVICE_TEST)
	$(DEVICE_TEST)

$(EXFAT_TEST): scripts/tests/c/exfat_contract.c kernel/fs/exfat/exfat.c kernel/fs/exfat/exfat.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/exfat_contract.c kernel/fs/exfat/exfat.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(EXT4_TEST): scripts/tests/c/ext4_contract.c kernel/fs/ext4/ext4.c kernel/fs/ext4/ext4.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/ext4_contract.c kernel/fs/ext4/ext4.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(XFS_TEST): scripts/tests/c/xfs_contract.c kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/xfs_contract.c kernel/fs/xfs/xfs.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(XFS_RENAME_TEST): scripts/tests/c/xfs_rename_contract.c kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/xfs_rename_contract.c kernel/fs/xfs/xfs.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(XFS_ALLOC_TEST): scripts/tests/c/xfs_alloc_contract.c kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/xfs_alloc_contract.c kernel/fs/xfs/xfs.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(XFS_UNWRITTEN_TEST): scripts/tests/c/xfs_unwritten_contract.c kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/xfs_unwritten_contract.c kernel/fs/xfs/xfs.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(XFS_AUTH_TEST): scripts/tests/c/xfs_auth_contract.c kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/xfs_auth_contract.c kernel/fs/xfs/xfs.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(BTRFS_TEST): scripts/tests/c/btrfs_contract.c kernel/fs/btrfs/btrfs.c kernel/fs/btrfs/btrfs.h kernel/fs/btrfs/deflate.c kernel/fs/btrfs/deflate.h kernel/fs/btrfs/lzo.c kernel/fs/btrfs/lzo.h kernel/fs/btrfs/zstd.c kernel/fs/btrfs/zstd.h kernel/fs/btrfs/fse.c kernel/fs/btrfs/fse.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/btrfs_contract.c kernel/fs/btrfs/btrfs.c kernel/fs/btrfs/deflate.c kernel/fs/btrfs/lzo.c kernel/fs/btrfs/zstd.c kernel/fs/btrfs/fse.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(DEFLATE_TEST): scripts/tests/c/deflate_contract.c kernel/fs/btrfs/deflate.c kernel/fs/btrfs/deflate.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/deflate_contract.c kernel/fs/btrfs/deflate.c

$(LZO_TEST): scripts/tests/c/lzo_contract.c kernel/fs/btrfs/lzo.c kernel/fs/btrfs/lzo.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/lzo_contract.c kernel/fs/btrfs/lzo.c

$(ZSTD_FIXTURE): scripts/tests/python/create_zstd_fixture.py | $(TEST_DIR)
	python3 $< $@

$(ZSTD_TEST): scripts/tests/c/zstd_contract.c kernel/fs/btrfs/zstd.c kernel/fs/btrfs/zstd.h kernel/fs/btrfs/fse.c kernel/fs/btrfs/fse.h $(ZSTD_FIXTURE) | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/zstd_contract.c kernel/fs/btrfs/zstd.c kernel/fs/btrfs/fse.c

$(FSE_TEST): scripts/tests/c/fse_contract.c kernel/fs/btrfs/fse.c kernel/fs/btrfs/fse.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/fse_contract.c kernel/fs/btrfs/fse.c

$(CACHE_TEST): scripts/tests/c/cache_contract.c kernel/fs/cache/cache.c kernel/fs/cache/cache.h kernel/fs/block/block.c kernel/fs/block/block.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/cache_contract.c kernel/fs/cache/cache.c kernel/fs/block/block.c kernel/core/sync/spinlock.c

$(DEVICE_TEST): scripts/tests/c/device_contract.c kernel/device/device.c kernel/device/device.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/device_contract.c kernel/device/device.c kernel/core/sync/spinlock.c

$(FAT32_TEST): scripts/tests/c/fat32_contract.c kernel/fs/fat/fat32.c kernel/fs/fat/fat32_vfs.c kernel/fs/vfs/vfs.c kernel/fs/fat/fat32.h kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/security/credentials.c kernel/security/credentials.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -I. scripts/tests/c/fat32_contract.c kernel/fs/fat/fat32.c kernel/fs/fat/fat32_vfs.c kernel/fs/vfs/vfs.c kernel/core/sync/spinlock.c kernel/security/credentials.c -o $@

image: $(IMAGE)

head-test: $(USERLAND_HEAD_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_HEAD_ELF)

wc-test: $(USERLAND_WC_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_WC_ELF)

grep-test: $(USERLAND_GREP_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_GREP_ELF)

tee-test: $(USERLAND_TEE_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TEE_ELF)

tail-test: $(USERLAND_TAIL_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TAIL_ELF)

sort-test: $(USERLAND_SORT_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_SORT_ELF)

uniq-test: $(USERLAND_UNIQ_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_UNIQ_ELF)

printf-test: $(USERLAND_PRINTF_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_PRINTF_ELF)

basename-test: $(USERLAND_BASENAME_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_BASENAME_ELF)

$(USERLAND_DIRNAME_START_OBJ): userland/apps/dirname/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_DIRNAME_MAIN_OBJ): userland/apps/dirname/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_DIRNAME_ELF): $(USERLAND_DIRNAME_START_OBJ) $(USERLAND_DIRNAME_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_DIRNAME_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_DIRNAME_LD) --build-id=none -o $@ $(USERLAND_DIRNAME_START_OBJ) $(USERLAND_DIRNAME_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

dirname-test: $(USERLAND_DIRNAME_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_DIRNAME_ELF)

$(USERLAND_CUT_START_OBJ): userland/apps/cut/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CUT_MAIN_OBJ): userland/apps/cut/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CUT_ELF): $(USERLAND_CUT_START_OBJ) $(USERLAND_CUT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CUT_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CUT_LD) --build-id=none -o $@ $(USERLAND_CUT_START_OBJ) $(USERLAND_CUT_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

cut-test: $(USERLAND_CUT_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CUT_ELF)

$(USERLAND_TR_START_OBJ): userland/apps/tr/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TR_MAIN_OBJ): userland/apps/tr/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TR_ELF): $(USERLAND_TR_START_OBJ) $(USERLAND_TR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TR_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TR_LD) --build-id=none -o $@ $(USERLAND_TR_START_OBJ) $(USERLAND_TR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

tr-test: $(USERLAND_TR_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TR_ELF)

$(USERLAND_CMP_START_OBJ): userland/apps/cmp/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_CMP_MAIN_OBJ): userland/apps/cmp/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_CMP_ELF): $(USERLAND_CMP_START_OBJ) $(USERLAND_CMP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_CMP_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_CMP_LD) --build-id=none -o $@ $(USERLAND_CMP_START_OBJ) $(USERLAND_CMP_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

cmp-test: $(USERLAND_CMP_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_CMP_ELF)

$(USERLAND_WHICH_START_OBJ): userland/apps/which/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_WHICH_MAIN_OBJ): userland/apps/which/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_WHICH_ELF): $(USERLAND_WHICH_START_OBJ) $(USERLAND_WHICH_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_WHICH_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_WHICH_LD) --build-id=none -o $@ $(USERLAND_WHICH_START_OBJ) $(USERLAND_WHICH_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

which-test: $(USERLAND_WHICH_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_WHICH_ELF)

find-test: $(USERLAND_FIND_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_FIND_ELF)

expr-test: $(USERLAND_EXPR_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_EXPR_ELF)

$(USERLAND_SH_ELF): $(USERLAND_SHELL_ELF) | $(BUILD_DIR)/userland
	cp $< $@

$(USERLAND_FIND_START_OBJ): userland/apps/find/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_FIND_MAIN_OBJ): userland/apps/find/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_FIND_ELF): $(USERLAND_FIND_START_OBJ) $(USERLAND_FIND_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_FIND_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_FIND_LD) --build-id=none -o $@ $(USERLAND_FIND_START_OBJ) $(USERLAND_FIND_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_EXPR_START_OBJ): userland/apps/expr/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_EXPR_MAIN_OBJ): userland/apps/expr/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_EXPR_ELF): $(USERLAND_EXPR_START_OBJ) $(USERLAND_EXPR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_EXPR_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_EXPR_LD) --build-id=none -o $@ $(USERLAND_EXPR_START_OBJ) $(USERLAND_EXPR_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

$(USERLAND_TEST_START_OBJ): userland/apps/test/start.asm | $(BUILD_DIR)/userland
	$(NASM) -f elf64 $< -o $@

$(USERLAND_TEST_MAIN_OBJ): userland/apps/test/main.c userland/lib/runtime.h userland/lib/os.h | $(BUILD_DIR)/userland
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIE -fno-plt -mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(USERLAND_TEST_ELF): $(USERLAND_TEST_START_OBJ) $(USERLAND_TEST_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ) $(USERLAND_TEST_LD) | $(BUILD_DIR)/userland
	$(LD) -m elf_x86_64 -T $(USERLAND_TEST_LD) --build-id=none -o $@ $(USERLAND_TEST_START_OBJ) $(USERLAND_TEST_MAIN_OBJ) $(USERLAND_SYSCALL_OBJ)

test-utility-test: $(USERLAND_TEST_ELF)
	sh scripts/tests/sh/validate_userland.sh $(USERLAND_TEST_ELF)


$(IMAGE): $(UEFI_EFI) $(KERNEL_ELF) $(USERLAND_INIT_ELF) $(USERLAND_SHELL_ELF) $(USERLAND_SH_ELF) $(USERLAND_ARGS_ELF) $(USERLAND_ENV_ELF) $(USERLAND_CAT_ELF) $(USERLAND_PWD_ELF) $(USERLAND_MKDIR_ELF) $(USERLAND_RM_ELF) $(USERLAND_RMDIR_ELF) $(USERLAND_TOUCH_ELF) $(USERLAND_WRITE_ELF) $(USERLAND_LS_ELF) $(USERLAND_CHMOD_ELF) $(USERLAND_ECHO_ELF) $(USERLAND_HELP_ELF) $(USERLAND_STAT_ELF) $(USERLAND_MV_ELF) $(USERLAND_KILL_ELF) $(USERLAND_SLEEP_ELF) $(USERLAND_SETENV_ELF) $(USERLAND_UNSETENV_ELF) $(USERLAND_UPTIME_ELF) $(USERLAND_DATE_ELF) $(USERLAND_CLEAR_ELF) $(USERLAND_IPC_ELF) $(USERLAND_DUP_ELF) $(USERLAND_TRUE_ELF) $(USERLAND_FALSE_ELF) $(USERLAND_ID_ELF) $(USERLAND_PS_ELF) $(USERLAND_WAIT_ELF) $(USERLAND_TRUNCATE_ELF) $(USERLAND_SEEK_ELF) $(USERLAND_CHDIR_ELF) $(USERLAND_CP_ELF) $(USERLAND_HEAD_ELF) $(USERLAND_WC_ELF) $(USERLAND_GREP_ELF) $(USERLAND_TEE_ELF) $(USERLAND_TAIL_ELF) $(USERLAND_SORT_ELF) $(USERLAND_UNIQ_ELF) $(USERLAND_PRINTF_ELF) $(USERLAND_BASENAME_ELF) $(USERLAND_DIRNAME_ELF) $(USERLAND_CUT_ELF) $(USERLAND_TR_ELF) $(USERLAND_CMP_ELF) $(USERLAND_WHICH_ELF) $(USERLAND_TEST_ELF) $(USERLAND_UNSETENV_ELF) $(USERLAND_UPTIME_ELF) $(USERLAND_DATE_ELF) $(USERLAND_CLEAR_ELF) $(USERLAND_SEQ_ELF) $(USERLAND_FIND_ELF) $(USERLAND_EXPR_ELF) $(USERLAND_SH_ELF) scripts/image/create_fat_image.py
	python3 scripts/image/create_fat_image.py $(UEFI_EFI) $(KERNEL_ELF) $(USERLAND_INIT_ELF) $(USERLAND_SHELL_ELF) $(USERLAND_ARGS_ELF) $(USERLAND_ENV_ELF) $(USERLAND_CAT_ELF) $(USERLAND_PWD_ELF) $(USERLAND_MKDIR_ELF) $(USERLAND_RM_ELF) $(USERLAND_RMDIR_ELF) $(USERLAND_TOUCH_ELF) $(USERLAND_WRITE_ELF) $(USERLAND_LS_ELF) $(USERLAND_CHMOD_ELF) $(USERLAND_ECHO_ELF) $(USERLAND_HELP_ELF) $(USERLAND_STAT_ELF) $(USERLAND_MV_ELF) $(USERLAND_KILL_ELF) $(USERLAND_SLEEP_ELF) $(USERLAND_SETENV_ELF) $(USERLAND_IPC_ELF) $(USERLAND_DUP_ELF) $(USERLAND_TRUE_ELF) $(USERLAND_FALSE_ELF) $(USERLAND_ID_ELF) $(USERLAND_PS_ELF) $(USERLAND_WAIT_ELF) $(USERLAND_TRUNCATE_ELF) $(USERLAND_SEEK_ELF) $(USERLAND_CHDIR_ELF) $(USERLAND_CP_ELF) $(USERLAND_HEAD_ELF) $(USERLAND_WC_ELF) $(USERLAND_GREP_ELF) $(USERLAND_TEE_ELF) $(USERLAND_TAIL_ELF) $(USERLAND_SORT_ELF) $(USERLAND_UNIQ_ELF) $(USERLAND_PRINTF_ELF) $(USERLAND_BASENAME_ELF) $(USERLAND_DIRNAME_ELF) $(USERLAND_CUT_ELF) $(USERLAND_TR_ELF) $(USERLAND_CMP_ELF) $(USERLAND_WHICH_ELF) $(USERLAND_TEST_ELF) $(USERLAND_UNSETENV_ELF) $(USERLAND_UPTIME_ELF) $(USERLAND_DATE_ELF) $(USERLAND_CLEAR_ELF) $(USERLAND_SEQ_ELF) $(USERLAND_FIND_ELF) $(USERLAND_EXPR_ELF) $(USERLAND_SH_ELF) $@
	sh scripts/tests/sh/validate_image.sh $@

$(IMAGE): $(USERLAND_SEQ_ELF)

qemu-test: $(IMAGE)
	@test -f "$(OVMF_CODE)" || (echo "OVMF_CODE not found: $(OVMF_CODE)" >&2; exit 2)
	@test -f "$(OVMF_VARS)" || (echo "OVMF_VARS not found: $(OVMF_VARS)" >&2; exit 2)
	cp "$(OVMF_VARS)" $(BUILD_DIR)/OVMF_VARS.4m.fd
	cp "$(IMAGE)" $(BUILD_DIR)/ahci-test.img
	cp "$(IMAGE)" $(BUILD_DIR)/ahci-test-secondary.img
	cp "$(IMAGE)" $(BUILD_DIR)/nvme-test.img
	: > $(QEMU_LOG)
	timeout 60s qemu-system-x86_64 -machine pc -smp 2 -m 128M \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/OVMF_VARS.4m.fd \
		-drive format=raw,file=$(IMAGE) -serial file:$(QEMU_LOG) \
		-netdev user,id=osnet -device e1000,netdev=osnet \
		-device piix3-usb-uhci \
		-device usb-kbd \
		-device ich9-ahci,id=ahci \
		-drive if=none,id=ahcidisk,format=raw,file=$(BUILD_DIR)/ahci-test.img \
		-device ide-hd,drive=ahcidisk,bus=ahci.1 \
		-drive if=none,id=ahcidisk2,format=raw,file=$(BUILD_DIR)/ahci-test-secondary.img \
		-device ide-hd,drive=ahcidisk2,bus=ahci.2 \
		-device nvme,drive=nvmedisk,serial=OSNVME01 \
		-drive if=none,id=nvmedisk,format=raw,file=$(BUILD_DIR)/nvme-test.img \
	-display none -no-reboot -no-shutdown || test $$? -eq 124
	sh scripts/tests/sh/validate_qemu.sh $(QEMU_LOG)

qemu-input-test: $(IMAGE)
	@test -f "$(OVMF_CODE)" || (echo "OVMF_CODE not found: $(OVMF_CODE)" >&2; exit 2)
	@test -f "$(OVMF_VARS)" || (echo "OVMF_VARS not found: $(OVMF_VARS)" >&2; exit 2)
	cp "$(OVMF_VARS)" $(BUILD_DIR)/OVMF_INPUT_VARS.4m.fd
	: > $(BUILD_DIR)/qemu-input.log
	((sleep 10; printf 'echo input-test\r') | timeout 25s qemu-system-x86_64 -machine pc -smp 2 -m 128M \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/OVMF_INPUT_VARS.4m.fd \
		-drive format=raw,file=$(IMAGE) -serial stdio -display none -no-reboot -no-shutdown \
		> $(BUILD_DIR)/qemu-input.log 2>&1) || test $$? -eq 124
	sh scripts/tests/sh/validate_qemu_input.sh $(BUILD_DIR)/qemu-input.log

run: $(IMAGE)
	@test -f "$(OVMF_CODE)" || (echo "OVMF_CODE not found: $(OVMF_CODE)" >&2; exit 2)
	@test -f "$(OVMF_VARS)" || (echo "OVMF_VARS not found: $(OVMF_VARS)" >&2; exit 2)
	cp "$(OVMF_VARS)" $(BUILD_DIR)/OVMF_VARS.4m.fd
	cp "$(IMAGE)" $(BUILD_DIR)/ahci-test.img
	cp "$(IMAGE)" $(BUILD_DIR)/ahci-test-secondary.img
	cp "$(IMAGE)" $(BUILD_DIR)/nvme-test.img
	exec qemu-system-x86_64 -machine pc -smp 2 -m 128M \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/OVMF_VARS.4m.fd \
		-drive format=raw,file=$(IMAGE) -serial file:$(BUILD_DIR)/qemu-run.log \
		-netdev user,id=osnet -device e1000,netdev=osnet \
		-device piix3-usb-uhci \
		-device usb-kbd \
		-device ich9-ahci,id=ahci \
		-drive if=none,id=ahcidisk,format=raw,file=$(BUILD_DIR)/ahci-test.img \
		-device ide-hd,drive=ahcidisk,bus=ahci.1 \
		-drive if=none,id=ahcidisk2,format=raw,file=$(BUILD_DIR)/ahci-test-secondary.img \
		-device ide-hd,drive=ahcidisk2,bus=ahci.2 \
		-device nvme,drive=nvmedisk,serial=OSNVME01 \
		-drive if=none,id=nvmedisk,format=raw,file=$(BUILD_DIR)/nvme-test.img \
		-no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(DIST_DIR)
