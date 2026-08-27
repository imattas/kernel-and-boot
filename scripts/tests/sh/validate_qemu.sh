#!/bin/sh
set -eu

log=${1:?usage: validate_qemu.sh <serial-log>}
test -f "$log"
grep -F 'os UEFI loader' "$log" >/dev/null
grep -F 'serial driver ready' "$log" >/dev/null
grep -F 'firmware boot contract ready' "$log" >/dev/null
grep -F 'local APIC ready' "$log" >/dev/null
grep -F 'address space foundation ready' "$log" >/dev/null
grep -F 'user image loader ready' "$log" >/dev/null
grep -E 'acpi RSDP=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'ACPI MADT CPUs=' "$log" >/dev/null
grep -E 'ACPI reset service (ready|unavailable)' "$log" >/dev/null
grep -F 'per-CPU BSP ready' "$log" >/dev/null
grep -F 'SMP online=' "$log" >/dev/null
grep -E 'pci devices=0x[0-9a-fA-F]*[1-9a-fA-F] resources=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'device model ready' "$log" >/dev/null
grep -E 'UHCI driver ready controllers=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -E 'UHCI root hub ready ports=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'UHCI control transfer ready' "$log" >/dev/null
grep -F 'UHCI device enumeration ready' "$log" >/dev/null
grep -F 'UHCI interrupt path configured' "$log" >/dev/null
grep -F 'NVMe driver ready' "$log" >/dev/null
grep -F 'NVMe admin I/O ready' "$log" >/dev/null
grep -F 'NVMe sector I/O ready' "$log" >/dev/null
grep -F 'NVMe sector write I/O ready' "$log" >/dev/null
grep -F 'NVMe interrupt delivery ready' "$log" >/dev/null
grep -E 'e1000 driver ready controllers=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'e1000 network I/O ready' "$log" >/dev/null
grep -F 'e1000 link ready' "$log" >/dev/null
grep -F 'e1000 completion service ready' "$log" >/dev/null
grep -F 'e1000 interrupt path ready' "$log" >/dev/null
grep -F 'e1000 interrupt delivery ready' "$log" >/dev/null
grep -E 'AHCI driver ready controllers=0x[0-9a-fA-F]*[1-9a-fA-F].*ready=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'AHCI identify ready' "$log" >/dev/null
grep -F 'AHCI sector read ready' "$log" >/dev/null
grep -F 'AHCI interrupt delivery ready' "$log" >/dev/null
grep -E 'storage devices=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'storage ready' "$log" >/dev/null
grep -F 'storage read-write ready' "$log" >/dev/null
grep -F 'FAT32 filesystem ready' "$log" >/dev/null
grep -F 'filesystem probe dispatch ready' "$log" >/dev/null
grep -F 'FAT32 VFS adapter ready' "$log" >/dev/null
grep -F 'driver resource ownership ready' "$log" >/dev/null
grep -F 'synchronization primitives ready' "$log" >/dev/null
grep -F 'ipc channels ready' "$log" >/dev/null
grep -F 'ipc blocking ready' "$log" >/dev/null
grep -F 'security policy ready' "$log" >/dev/null
grep -F 'VFS core ready' "$log" >/dev/null
grep -F 'devfs ready' "$log" >/dev/null
grep -F 'procfs ready' "$log" >/dev/null
grep -F 'block interface ready' "$log" >/dev/null
grep -F 'block cache ready' "$log" >/dev/null
grep -F 'block cache lifecycle ready' "$log" >/dev/null
grep -F 'slab cache ready' "$log" >/dev/null
grep -F 'input event queue ready' "$log" >/dev/null
grep -F 'PS2 keyboard ready' "$log" >/dev/null
grep -F 'framebuffer surface ready' "$log" >/dev/null
grep -F 'firmware framebuffer ready' "$log" >/dev/null
grep -F 'USB descriptor layer ready' "$log" >/dev/null
grep -F 'USB HID keyboard ready' "$log" >/dev/null
grep -F 'kernel debug ready' "$log" >/dev/null
grep -F 'process thread lifecycle ready' "$log" >/dev/null
grep -F 'kernel debug ready' "$log" >/dev/null
grep -F 'task wait queues ready' "$log" >/dev/null
grep -F 'scheduler core ready' "$log" >/dev/null
grep -F 'scheduler policy ready' "$log" >/dev/null
grep -F 'scheduler preemption ready' "$log" >/dev/null
grep -F 'kernel task entered' "$log" >/dev/null
grep -F 'task context entered' "$log" >/dev/null
grep -F 'task context returned' "$log" >/dev/null
grep -E 'timer ticks=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -E 'time ns=0x[0-9a-fA-F]*[1-9a-fA-F]' "$log" >/dev/null
grep -F 'generic clock ready' "$log" >/dev/null
grep -F 'os kernel entry ok' "$log" >/dev/null
grep -F 'syscall ABI ready' "$log" >/dev/null
grep -F 'signal syscalls ready' "$log" >/dev/null
grep -F 'process lifecycle ready' "$log" >/dev/null
grep -F 'process signals ready' "$log" >/dev/null
grep -F 'signal blocking ready' "$log" >/dev/null
grep -F 'process handles ready' "$log" >/dev/null
grep -F 'user mode deferred until kernel completion' "$log" >/dev/null
if grep -E 'X64 Exception|kernel contract invalid|KERNEL (PANIC|EXCEPTION)' "$log" >/dev/null; then
    echo "QEMU boot reported a failure" >&2
    exit 1
fi
echo "QEMU UEFI handoff: PASS"
