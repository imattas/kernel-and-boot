# UEFI Loader Completion Design

## Goal

Turn the existing first-party x86_64 UEFI loader into a modular, robust loader
that can discover its boot volume, validate and load the kernel ELF, collect
firmware state, and hand off safely after `ExitBootServices`.

## Scope

The loader remains a PE32+ UEFI application loaded from `EFI/BOOT/BOOTX64.EFI`.
It will support the UEFI protocols required for this boot path: loaded-image,
Simple File System, file I/O, Boot Services memory allocation/map, Graphics
Output, ACPI configuration tables, console output, watchdog control, and
`ExitBootServices`. It will not attempt to implement a replacement UEFI
firmware or every optional UEFI protocol.

## Architecture

Split `efi_main.c` into focused loader modules: ABI/protocol declarations,
console diagnostics, file loading, ELF validation/loading, firmware discovery,
memory-map capture, and handoff. The modules share a small `efi_context_t` and
the versioned `os_boot_info_t` contract. UEFI protocol interaction and image
parsing remain C; a small NASM entry shim will enforce the Microsoft x64 entry
ABI and establish a known stack/alignment boundary before calling the C entry.

The loader will use firmware Simple File System rather than duplicate FAT,
exFAT, ext4, XFS, or Btrfs parsing. Those filesystems belong in the kernel’s
block/VFS layer and are separate follow-up components.

## Safety and acceptance

- Validate all protocol pointers, status returns, integer ranges, ELF segment
  alignment/overlap, and file-size bounds before dereferencing or allocating.
- Allocate the kernel with page alignment and preserve zero-filled BSS.
- Disable the watchdog before lengthy loading.
- Capture the memory map with the returned descriptor size/version and retry
  after `ExitBootServices` key invalidation.
- Populate the boot contract before the final successful exit and never call
  Boot Services from the kernel.
- Prove the loader with PE/ELF static checks, QEMU/OVMF serial handoff, and a
  deliberately forced memory-map retry path where practical.

