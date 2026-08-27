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
BTRFS_TEST := $(TEST_DIR)/btrfs_contract
DEFLATE_TEST := $(TEST_DIR)/deflate_contract
LZO_TEST := $(TEST_DIR)/lzo_contract
ZSTD_TEST := $(TEST_DIR)/zstd_contract
ZSTD_FIXTURE := $(TEST_DIR)/zstd_real.zst
FSE_TEST := $(TEST_DIR)/fse_contract
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
KERNEL_NVME_IRQ_OBJ := $(BUILD_DIR)/kernel/nvme_irq.asm.o
KERNEL_AHCI_IRQ_OBJ := $(BUILD_DIR)/kernel/ahci_irq.asm.o
KERNEL_ELF := $(BUILD_DIR)/kernel/kernel.elf
IMAGE := $(DIST_DIR)/os.img
OVMF_CODE ?= /usr/share/edk2/x64/OVMF_CODE.4m.fd
OVMF_VARS ?= /usr/share/edk2/x64/OVMF_VARS.4m.fd
QEMU_LOG := $(BUILD_DIR)/qemu-serial.log

.PHONY: all test image qemu-test fat32-test exfat-test ext4-test xfs-test btrfs-test deflate-test lzo-test zstd-test fse-test run clean distclean

all: $(CONTRACT_ELF) $(UEFI_EFI) $(KERNEL_ELF)

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

$(KERNEL_OBJ): kernel/arch/x86_64/entry/kernel_entry.c kernel/arch/x86_64/cpu/tables.h kernel/drivers/network/e1000.h kernel/drivers/network/ethernet.h kernel/drivers/network/arp.h kernel/drivers/network/arp_cache.h kernel/drivers/network/ipv4.h kernel/drivers/network/udp.h kernel/fs/vfs/vfs.h | $(BUILD_DIR)/kernel
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
KERNEL_SECURITY_OBJ := $(BUILD_DIR)/kernel/security_credentials.o
KERNEL_VFS_OBJ := $(BUILD_DIR)/kernel/vfs.o
KERNEL_VFS_MOUNT_OBJ := $(BUILD_DIR)/kernel/vfs_mount.o
KERNEL_BLOCK_OBJ := $(BUILD_DIR)/kernel/block.o
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
KERNEL_FRAMEBUFFER_OBJ := $(BUILD_DIR)/kernel/framebuffer.o
KERNEL_USB_OBJ := $(BUILD_DIR)/kernel/usb.o
KERNEL_HID_OBJ := $(BUILD_DIR)/kernel/hid.o
KERNEL_UHCI_OBJ := $(BUILD_DIR)/kernel/uhci.o
KERNEL_AHCI_OBJ := $(BUILD_DIR)/kernel/ahci.o
KERNEL_NVME_OBJ := $(BUILD_DIR)/kernel/nvme.o
KERNEL_E1000_OBJ := $(BUILD_DIR)/kernel/e1000.o
KERNEL_NVME_OBJ := $(KERNEL_NVME_OBJ) $(KERNEL_E1000_OBJ) $(KERNEL_ETHERNET_OBJ) $(KERNEL_ARP_OBJ) $(KERNEL_ARP_CACHE_OBJ) $(KERNEL_IPV4_OBJ) $(KERNEL_UDP_OBJ) $(KERNEL_E1000_IRQ_OBJ) $(KERNEL_NVME_IRQ_OBJ) $(KERNEL_AHCI_IRQ_OBJ) $(KERNEL_HID_OBJ) $(KERNEL_EXFAT_VFS_OBJ) $(KERNEL_EXT4_OBJ) $(KERNEL_EXT4_VFS_OBJ) $(KERNEL_XFS_OBJ) $(KERNEL_XFS_VFS_OBJ) $(KERNEL_BTRFS_OBJ) $(KERNEL_BTRFS_DEFLATE_OBJ) $(KERNEL_BTRFS_LZO_OBJ) $(KERNEL_BTRFS_ZSTD_OBJ) $(KERNEL_BTRFS_FSE_OBJ) $(KERNEL_BTRFS_VFS_OBJ)
KERNEL_DEBUG_OBJ := $(BUILD_DIR)/kernel/debug_assert.o
KERNEL_CLOCK_OBJ := $(BUILD_DIR)/kernel/clock.o

$(KERNEL_MEMORY_OBJ): kernel/lib/memory.c kernel/lib/memory.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_IPC_OBJ): kernel/ipc/channel.c kernel/ipc/channel.h kernel/core/sync/spinlock.h kernel/core/task/wait_queue.h kernel/sched/core/scheduler.h kernel/sched/core/scheduler.c | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SECURITY_OBJ): kernel/security/credentials.c kernel/security/credentials.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VFS_OBJ): kernel/fs/vfs/vfs.c kernel/fs/vfs/vfs.h kernel/core/sync/spinlock.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_VFS_MOUNT_OBJ): kernel/fs/vfs/mount.c kernel/fs/vfs/mount.h kernel/fs/vfs/vfs.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_BLOCK_OBJ): kernel/fs/block/block.c kernel/fs/block/block.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
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

$(KERNEL_INPUT_OBJ): kernel/drivers/input/input.c kernel/drivers/input/input.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PS2_OBJ): kernel/drivers/input/ps2.c kernel/drivers/input/ps2.h kernel/drivers/input/input.h kernel/arch/x86_64/time/timer.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_FRAMEBUFFER_OBJ): kernel/drivers/display/framebuffer.c kernel/drivers/display/framebuffer.h | $(BUILD_DIR)/kernel
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

$(KERNEL_TASK_DESC_OBJ): kernel/core/task/task.c kernel/core/task/task.h kernel/core/task/context.h kernel/core/task/wait_queue.h | $(BUILD_DIR)/kernel
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

$(KERNEL_PROCESS_LIFECYCLE_OBJ): kernel/core/process/process.c kernel/core/process/process.h kernel/core/process/thread.h kernel/core/process/handle.c kernel/core/process/handle.h kernel/core/process/user_image.h kernel/security/credentials.h kernel/mm/heap/heap.h kernel/mm/physical/frame.h kernel/core/sync/spinlock.h kernel/core/task/wait_queue.h kernel/sched/core/scheduler.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PROCESS_HANDLE_OBJ): kernel/core/process/handle.c kernel/core/process/handle.h kernel/core/sync/spinlock.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_PROCESS_THREAD_OBJ): kernel/core/process/thread.c kernel/core/process/thread.h kernel/core/process/process.h kernel/core/task/task.h kernel/core/task/wait_queue.h kernel/mm/heap/heap.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SYSCALL_OBJ): kernel/core/syscall/syscall.c kernel/core/syscall/syscall.h kernel/syscall/abi.h kernel/time/clock.h kernel/core/process/process.h kernel/arch/x86_64/cpu/tables.h kernel/arch/x86_64/time/timer.h kernel/mm/virtual/address_space.h | $(BUILD_DIR)/kernel
	$(CC) -target x86_64-pc-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fPIE -fno-plt \
		-mno-red-zone -Wall -Wextra -Werror -O2 -c $< -o $@

$(KERNEL_SYSCALL_ASM_OBJ): kernel/arch/x86_64/syscall/entry.asm | $(BUILD_DIR)/kernel
	$(NASM) -f elf64 $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJ) $(KERNEL_ENTRY_ASM_OBJ) $(KERNEL_TABLES_OBJ) $(KERNEL_TABLES_ASM_OBJ) $(KERNEL_SERIAL_OBJ) $(KERNEL_CPU_OBJ) $(KERNEL_EXCEPTIONS_OBJ) $(KERNEL_PANIC_OBJ) $(KERNEL_PHYSICAL_OBJ) $(KERNEL_VIRTUAL_OBJ) $(KERNEL_HEAP_OBJ) $(KERNEL_IRQ_OBJ) $(KERNEL_APIC_OBJ) $(KERNEL_ACPI_OBJ) $(KERNEL_PERCPU_OBJ) $(KERNEL_TRAMPOLINE_OBJ) $(KERNEL_TIMER_OBJ) $(KERNEL_TIMER_ASM_OBJ) $(KERNEL_KEYBOARD_ASM_OBJ) $(KERNEL_SYNC_OBJ) $(KERNEL_TASK_OBJ) $(KERNEL_TASK_ASM_OBJ) $(KERNEL_WAIT_OBJ) $(KERNEL_TASK_DESC_OBJ) $(KERNEL_SCHED_OBJ) $(KERNEL_SCHED_POLICY_OBJ) $(KERNEL_PROCESS_OBJ) $(KERNEL_PROCESS_LIFECYCLE_OBJ) $(KERNEL_PROCESS_HANDLE_OBJ) $(KERNEL_PROCESS_THREAD_OBJ) $(KERNEL_EXEC_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_SYSCALL_ASM_OBJ) $(KERNEL_DEVICE_OBJ) $(KERNEL_PCI_OBJ) $(KERNEL_MEMORY_OBJ) $(KERNEL_STORAGE_OBJ) $(KERNEL_ATA_OBJ) $(KERNEL_IPC_OBJ) $(KERNEL_SECURITY_OBJ) $(KERNEL_VFS_OBJ) $(KERNEL_DEBUG_OBJ) $(KERNEL_VFS_MOUNT_OBJ) $(KERNEL_BLOCK_OBJ) $(KERNEL_CACHE_OBJ) $(KERNEL_CLOCK_OBJ) $(KERNEL_DEVFS_OBJ) $(KERNEL_PROCFS_OBJ) $(KERNEL_SLAB_OBJ) $(KERNEL_FAT12_OBJ) $(KERNEL_FAT12_VFS_OBJ) $(KERNEL_FAT32_OBJ) $(KERNEL_EXFAT_OBJ) $(KERNEL_EXFAT_VFS_OBJ) $(KERNEL_INPUT_OBJ) $(KERNEL_PS2_OBJ) $(KERNEL_FRAMEBUFFER_OBJ) $(KERNEL_USB_OBJ) $(KERNEL_AHCI_OBJ) $(KERNEL_UHCI_OBJ) $(KERNEL_NVME_OBJ)
	$(LD) -m elf_x86_64 -pie -T kernel/arch/x86_64/entry/kernel.ld --build-id=none -o $@ $(KERNEL_ENTRY_ASM_OBJ) $(KERNEL_OBJ) $(KERNEL_TABLES_OBJ) $(KERNEL_TABLES_ASM_OBJ) $(KERNEL_SERIAL_OBJ) $(KERNEL_CPU_OBJ) $(KERNEL_EXCEPTIONS_OBJ) $(KERNEL_PANIC_OBJ) $(KERNEL_PHYSICAL_OBJ) $(KERNEL_VIRTUAL_OBJ) $(KERNEL_HEAP_OBJ) $(KERNEL_IRQ_OBJ) $(KERNEL_APIC_OBJ) $(KERNEL_ACPI_OBJ) $(KERNEL_PERCPU_OBJ) $(KERNEL_TRAMPOLINE_OBJ) $(KERNEL_TIMER_OBJ) $(KERNEL_TIMER_ASM_OBJ) $(KERNEL_KEYBOARD_ASM_OBJ) $(KERNEL_SYNC_OBJ) $(KERNEL_TASK_OBJ) $(KERNEL_TASK_ASM_OBJ) $(KERNEL_WAIT_OBJ) $(KERNEL_TASK_DESC_OBJ) $(KERNEL_SCHED_OBJ) $(KERNEL_SCHED_POLICY_OBJ) $(KERNEL_PROCESS_OBJ) $(KERNEL_PROCESS_LIFECYCLE_OBJ) $(KERNEL_PROCESS_HANDLE_OBJ) $(KERNEL_PROCESS_THREAD_OBJ) $(KERNEL_EXEC_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_SYSCALL_ASM_OBJ) $(KERNEL_DEVICE_OBJ) $(KERNEL_PCI_OBJ) $(KERNEL_MEMORY_OBJ) $(KERNEL_STORAGE_OBJ) $(KERNEL_ATA_OBJ) $(KERNEL_IPC_OBJ) $(KERNEL_SECURITY_OBJ) $(KERNEL_VFS_OBJ) $(KERNEL_DEBUG_OBJ) $(KERNEL_VFS_MOUNT_OBJ) $(KERNEL_BLOCK_OBJ) $(KERNEL_CACHE_OBJ) $(KERNEL_CLOCK_OBJ) $(KERNEL_DEVFS_OBJ) $(KERNEL_PROCFS_OBJ) $(KERNEL_SLAB_OBJ) $(KERNEL_FAT12_OBJ) $(KERNEL_FAT12_VFS_OBJ) $(KERNEL_FAT32_OBJ) $(KERNEL_EXFAT_OBJ) $(KERNEL_INPUT_OBJ) $(KERNEL_PS2_OBJ) $(KERNEL_FRAMEBUFFER_OBJ) $(KERNEL_USB_OBJ) $(KERNEL_AHCI_OBJ) $(KERNEL_UHCI_OBJ) $(KERNEL_NVME_OBJ)

test: all image
	$(MAKE) fat32-test
	$(MAKE) exfat-test
	$(MAKE) ext4-test
	$(MAKE) xfs-test
	$(MAKE) btrfs-test
	$(MAKE) deflate-test
	$(MAKE) lzo-test
	$(MAKE) zstd-test
	$(MAKE) fse-test
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

$(EXFAT_TEST): scripts/tests/c/exfat_contract.c kernel/fs/exfat/exfat.c kernel/fs/exfat/exfat.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/exfat_contract.c kernel/fs/exfat/exfat.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(EXT4_TEST): scripts/tests/c/ext4_contract.c kernel/fs/ext4/ext4.c kernel/fs/ext4/ext4.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/ext4_contract.c kernel/fs/ext4/ext4.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

$(XFS_TEST): scripts/tests/c/xfs_contract.c kernel/fs/xfs/xfs.c kernel/fs/xfs/xfs.h kernel/drivers/storage/storage.c kernel/drivers/storage/storage.h kernel/core/sync/spinlock.c kernel/core/sync/spinlock.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. -o $@ scripts/tests/c/xfs_contract.c kernel/fs/xfs/xfs.c kernel/drivers/storage/storage.c kernel/core/sync/spinlock.c

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

$(FAT32_TEST): scripts/tests/c/fat32_contract.c kernel/fs/fat/fat32.c kernel/fs/fat/fat32.h kernel/drivers/storage/storage.h | $(TEST_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -I. scripts/tests/c/fat32_contract.c kernel/fs/fat/fat32.c -o $@

image: $(IMAGE)

$(IMAGE): $(UEFI_EFI) $(KERNEL_ELF) scripts/image/create_fat_image.py
	python3 scripts/image/create_fat_image.py $(UEFI_EFI) $(KERNEL_ELF) $@
	sh scripts/tests/sh/validate_image.sh $@

qemu-test: $(IMAGE)
	@test -f "$(OVMF_CODE)" || (echo "OVMF_CODE not found: $(OVMF_CODE)" >&2; exit 2)
	@test -f "$(OVMF_VARS)" || (echo "OVMF_VARS not found: $(OVMF_VARS)" >&2; exit 2)
	cp "$(OVMF_VARS)" $(BUILD_DIR)/OVMF_VARS.4m.fd
	cp "$(IMAGE)" $(BUILD_DIR)/ahci-test.img
	cp "$(IMAGE)" $(BUILD_DIR)/nvme-test.img
	: > $(QEMU_LOG)
	timeout 20s qemu-system-x86_64 -machine pc -smp 2 -m 128M \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/OVMF_VARS.4m.fd \
		-drive format=raw,file=$(IMAGE) -serial file:$(QEMU_LOG) \
		-netdev user,id=osnet -device e1000,netdev=osnet \
		-device piix3-usb-uhci \
		-device usb-kbd \
		-device ich9-ahci,id=ahci \
		-drive if=none,id=ahcidisk,format=raw,file=$(BUILD_DIR)/ahci-test.img \
		-device ide-hd,drive=ahcidisk,bus=ahci.1 \
		-device nvme,drive=nvmedisk,serial=OSNVME01 \
		-drive if=none,id=nvmedisk,format=raw,file=$(BUILD_DIR)/nvme-test.img \
	-display none -no-reboot -no-shutdown || test $$? -eq 124
	sh scripts/tests/sh/validate_qemu.sh $(QEMU_LOG)

run: $(IMAGE)
	@test -f "$(OVMF_CODE)" || (echo "OVMF_CODE not found: $(OVMF_CODE)" >&2; exit 2)
	@test -f "$(OVMF_VARS)" || (echo "OVMF_VARS not found: $(OVMF_VARS)" >&2; exit 2)
	cp "$(OVMF_VARS)" $(BUILD_DIR)/OVMF_VARS.4m.fd
	cp "$(IMAGE)" $(BUILD_DIR)/ahci-test.img
	cp "$(IMAGE)" $(BUILD_DIR)/nvme-test.img
	exec qemu-system-x86_64 -machine pc -smp 2 -m 128M \
		-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,file=$(BUILD_DIR)/OVMF_VARS.4m.fd \
		-drive format=raw,file=$(IMAGE) -serial stdio \
		-netdev user,id=osnet -device e1000,netdev=osnet \
		-device piix3-usb-uhci \
		-device usb-kbd \
		-device ich9-ahci,id=ahci \
		-drive if=none,id=ahcidisk,format=raw,file=$(BUILD_DIR)/ahci-test.img \
		-device ide-hd,drive=ahcidisk,bus=ahci.1 \
		-device nvme,drive=nvmedisk,serial=OSNVME01 \
		-drive if=none,id=nvmedisk,format=raw,file=$(BUILD_DIR)/nvme-test.img \
		-no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(DIST_DIR)
