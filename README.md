# os

`os` is a from-scratch x86_64 operating-system project built around a modular-monolithic kernel, a custom bootloader, and eventually a native userland.

The implementation language is C with unrestricted use of NASM assembly where architecture-specific control, ABI boundaries, CPU initialization, interrupt/syscall entry, context switching, or low-level performance justify it. The project is intended to grow into a detailed systems codebase rather than a minimal tutorial kernel.

## Repository areas

- `boot/UEFI/` — primary modern boot path, implemented from scratch around UEFI interfaces.
- `boot/BIOS/` — reserved for a later legacy BIOS boot path.
- `kernel/` — deeply separated modular-monolithic kernel subsystems.
- `userland/` — intentionally empty until kernel foundations and the userspace ABI are ready.
- `scripts/` — build, run, and test entry points plus test-source areas.
- `docs/` — architecture, design, and project-roadmap documentation.
- `build/` — **all generated/intermediate artifacts**. No generated file belongs in a source directory.
- `dist/` — **final distributable output only**, primarily `os.img`.

## Initial platform

- Architecture: x86_64
- Kernel: modular monolithic
- Primary boot environment: UEFI
- Later boot environment: legacy BIOS
- Languages: C and NASM assembly
- Recommended compiler: Clang/LLVM with freestanding cross-target configuration
- Build orchestration: GNU Make
- Primary emulator: QEMU

See `docs/DESIGN.md` and `docs/ROADMAP.md` for the intended architecture and development order.


## Build-output contract

The repository enforces a strict separation between source, intermediate output, and distributable output.

- `build/` contains every generated build artifact: object files, ELF files, binaries, EFI executables, dependency files, generated headers, symbol/map files, temporary filesystem trees, ESP staging trees, test binaries, logs, and other intermediate data.
- `dist/` contains only final distributable images. The primary output is `dist/os.img`.
- Source directories such as `boot/`, `kernel/`, `userland/`, `scripts/`, and `docs/` must never receive compiler/linker/generated artifacts.
- UEFI image staging should eventually use `build/image/esp/`, with the loader staged at `build/image/esp/EFI/BOOT/BOOTX64.EFI`.
- Generated test artifacts belong under `build/tests/`.
- Future BIOS build products follow the same rule: intermediate output in `build/`, final bootable image in `dist/`.
- `make clean` removes `build/`.
- `make distclean` removes both `build/` and `dist/`.
- `make` / `make all` builds into `build/`.
- `make image` produces `dist/os.img`.
- QEMU and automated boot tests should boot `dist/os.img`, never an intermediate image.
