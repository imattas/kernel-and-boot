# AGENTS.md

## Mission

Develop `os` as a serious from-scratch x86_64 operating system. Do not optimize the architecture for tutorial simplicity. Prefer explicit, testable, maintainable low-level mechanisms that can evolve into a substantial kernel, bootloader, and userland.

## Core constraints

- Target x86_64 only unless the repository is deliberately expanded later.
- Kernel architecture is modular monolithic: major services remain independently structured but execute in kernel address space.
- C is the principal systems language and NASM is the assembly language.
- Assembly is encouraged when it provides direct architectural control or a cleaner implementation than forcing low-level operations through C.
- The UEFI bootloader is first-class software, not a disposable shim. BIOS support comes later.
- Do not depend on GRUB, Limine, systemd-boot, or another third-party OS loader for the normal boot path.
- Userland remains untouched until the project reaches the corresponding roadmap phase.

## Engineering expectations

Implement complete mechanisms rather than demonstration-only placeholders. Keep architecture-specific code beneath `arch/x86_64` where practical. Define interfaces between subsystems instead of allowing unrelated components to reach through each other's internals. Treat error paths, invariants, integer overflow, memory ownership, synchronization, ABI rules, and hardware state transitions as part of the design.

Prefer deterministic builds and reproducible tests. QEMU should be the primary automated execution target, while the design must remain suitable for eventual bare-metal testing.

## Build direction

Use a root Makefile as the public build interface. The intended toolchain is Clang/LLVM for C, NASM for assembly, GNU Make for orchestration, and QEMU for emulation. Build scripts under `scripts/` should eventually wrap repeatable developer workflows rather than replace the Make dependency graph.

## Documentation discipline

Architectural decisions that change boot contracts, kernel virtual layout, ABI behavior, memory ownership, subsystem boundaries, or roadmap assumptions must be reflected in `docs/DESIGN.md` or `docs/ROADMAP.md` as appropriate.


## Mandatory artifact layout

Treat output placement as a repository invariant. **Every generated artifact goes under `build/` unless it is the final distributable disk image, which goes under `dist/`.** Never place `.o`, `.elf`, `.bin`, `.efi`, generated headers, maps, dependency files, test binaries, logs, staging filesystems, or other generated data inside source directories.

Use `build/image/esp/` for UEFI ESP staging and `build/tests/` for generated test output. The final bootable image is `dist/os.img`. Future BIOS support must obey the same separation. The public Make interface should provide `all`, `image`, `clean`, and `distclean`; `clean` removes `build/`, while `distclean` removes both `build/` and `dist/`. Run/emulation workflows must consume `dist/os.img`.
