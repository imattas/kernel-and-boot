# UEFI Loader Completion Implementation Plan

**Goal:** Modularize and harden the first-party x86_64 UEFI loader while preserving the existing kernel boot contract.

**Architecture:** Split UEFI declarations, console, file I/O, ELF loading, firmware discovery, memory-map capture, and handoff into focused C modules. Add one NASM ABI entry shim and keep firmware filesystem access through Simple File System.

**Tech Stack:** Freestanding C11, NASM x86_64, PE32+ UEFI application, LLVM/LLD, GNU Make, OVMF/QEMU.

**Spec:** `docs/superpowers/specs/2026-08-26-uefi-loader-design.md`

## Global Constraints

- Target x86_64 only.
- UEFI is the primary boot path and the loader is first-party software.
- Generated artifacts remain under `build/`; final image remains `dist/os.img`.
- The kernel receives `os_boot_info_t` and must not call UEFI services.
- No userland/ring3 work begins before the kernel completion gate.
- No subagents or branches; execute inline on the existing workspace.

---

### Task 1: Extract shared UEFI declarations and context

**Files:**
- Create: `boot/UEFI/core/efi_types.h`
- Create: `boot/UEFI/core/efi_context.h`
- Modify: `boot/UEFI/core/efi_main.c`
- Modify: `Makefile`

**Interfaces:**
- Produces `efi_context_t`, protocol typedefs, GUID declarations, and status helpers for all later loader modules.

- [ ] Move the currently local UEFI typedefs and protocol layouts into `efi_types.h` without changing field order.
- [ ] Define `efi_context_t` with image handle, system table, boot services, loaded image, root file, and boot-info pointers.
- [ ] Update the Makefile to compile each new C file into `build/uefi/` and link all UEFI objects.
- [ ] Build `make all` and run `scripts/tests/sh/validate_uefi.sh`.

### Task 2: Extract console and firmware discovery

**Files:**
- Create: `boot/UEFI/core/console.c`
- Create: `boot/UEFI/core/console.h`
- Create: `boot/UEFI/core/firmware.c`
- Create: `boot/UEFI/core/firmware.h`
- Modify: `boot/UEFI/core/efi_main.c`

**Interfaces:**
- `void uefi_console_write(efi_system_table_t *, const efi_char16_t *)`
- `void uefi_console_hex(efi_system_table_t *, const efi_char16_t *, uint64_t)`
- `uint64_t uefi_find_acpi_rsdp(const efi_system_table_t *)`
- `int uefi_find_framebuffer(efi_boot_services_t *, os_boot_info_t *)`

- [ ] Move GUID comparison, ACPI table selection, GOP validation, and console diagnostics behind these functions.
- [ ] Make all optional firmware discoveries fail closed without invalidating the boot when unavailable.
- [ ] Add watchdog disable through `SetWatchdogTimer` when present.
- [ ] Rebuild and verify the existing OVMF handoff markers.

### Task 3: Extract bounded file loading

**Files:**
- Create: `boot/UEFI/core/file.c`
- Create: `boot/UEFI/core/file.h`
- Modify: `boot/UEFI/core/efi_main.c`

**Interfaces:**
- `efi_status_t uefi_open_kernel(efi_context_t *, efi_file_protocol_t **)`
- `efi_status_t uefi_read_file(efi_context_t *, efi_file_protocol_t *, void **, efi_uintn_t *)`

- [ ] Validate file protocol methods and obtain file size through `GetInfo` before allocation.
- [ ] Reject zero-length, over-limit, and short reads; close every opened protocol on failure.
- [ ] Replace the fixed unconditional 16 MiB read allocation with the validated file size plus a defined maximum.
- [ ] Build and run the UEFI validator.

### Task 4: Harden ELF loading

**Files:**
- Create: `boot/UEFI/core/elf.c`
- Create: `boot/UEFI/core/elf.h`
- Modify: `boot/UEFI/core/efi_main.c`

**Interfaces:**
- `int uefi_elf_validate(const void *, uint64_t, uint64_t *, uint64_t *)`
- `efi_status_t uefi_elf_load(efi_boot_services_t *, const void *, uint64_t, efi_physical_address_t *, uint64_t *, uint64_t *)`

- [ ] Validate ELF class/data/version/type/machine, program-header arithmetic, page alignment, segment file ranges, memory ranges, and pairwise load-segment overlap.
- [ ] Allocate page-rounded memory, zero each load segment, copy only file-backed bytes, and return the relocated entry address.
- [ ] Reject entry points outside executable load segments.
- [ ] Add a host contract test for malformed headers, overflow, overlap, and valid BSS.
- [ ] Build and run the focused contract test.

### Task 5: Make memory-map capture and handoff retry-safe

**Files:**
- Create: `boot/UEFI/core/memory_map.c`
- Create: `boot/UEFI/core/memory_map.h`
- Create: `boot/UEFI/core/handoff.asm`
- Modify: `boot/UEFI/core/efi_main.c`
- Modify: `Makefile`

**Interfaces:**
- `efi_status_t uefi_capture_memory_map(efi_context_t *, void *, efi_uintn_t, efi_uintn_t *)`
- `efi_status_t uefi_exit_and_handoff(efi_context_t *, efi_uintn_t, kernel_entry_t)`

- [ ] Capture returned map size, descriptor size/version, and map key rather than assuming fixed descriptor layout.
- [ ] Retry map capture and `ExitBootServices` after `EFI_INVALID_PARAMETER` with no allocations between the final capture and exit.
- [ ] Add the NASM shim for stack alignment and the SysV kernel call boundary; preserve the boot-info pointer in the agreed register.
- [ ] Verify the complete UEFI-to-kernel QEMU path.

### Task 6: Integrate, document, and gate the loader milestone

**Files:**
- Modify: `boot/UEFI/core/efi_main.c`
- Modify: `boot/UEFI/core/boot_info.h`
- Modify: `docs/DESIGN.md`
- Modify: `docs/ROADMAP.md`
- Test: `scripts/tests/sh/validate_uefi.sh`

- [ ] Reduce `efi_main.c` to orchestration and ensure all failure paths return a diagnostic status without use-after-close or service calls after exit.
- [ ] Run one consolidated `make test` gate, including image validation, `fsck.fat`, and QEMU/OVMF.
- [ ] Record the completed UEFI milestone and explicitly keep exFAT/ext4/XFS/Btrfs and kernel drivers as later phases.
- [ ] Stop at this milestone for review before starting the first filesystem implementation.

