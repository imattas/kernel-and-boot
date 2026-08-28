# OS TODO and Completion Ledger

This file is the short execution checklist. `docs/ROADMAP.md` remains the
authoritative design and phase document. Userland must not begin until the
kernel gate below is explicitly marked complete.

## Completed kernel areas

- [x] Deterministic Clang/NASM build and generated-artifact layout.
- [x] UEFI loader, ELF validation, memory map handoff, and kernel entry.
- [x] Interrupts, exceptions, APIC/timer paths, SMP bring-up, and scheduler.
- [x] Physical/virtual memory, address spaces, process/thread ownership, and
  deferred ring-3 transition scaffolding.
- [x] VFS core, permissions, path handling, descriptors, IPC, signals, and
  filesystem syscalls.
- [x] FAT32, exFAT, ext4, XFS, and bounded Btrfs read/write integration.
- [x] Core storage, AHCI/ATA, NVMe, PCI, UHCI/USB HID, PS/2, framebuffer,
  serial, and e1000 networking paths.
- [x] Root build/image contracts and QEMU UEFI handoff gate.

## Current kernel milestone

- [x] Finish the current XFS batch: VFS append-growth boundary, 256/512-byte
  inode-core handling, and the corresponding contract coverage.
- [x] Run one grouped filesystem, kernel, image, and QEMU verification gate;
  update `ROADMAP.md` and commit the milestone.
- [ ] Push the milestone to `main` when the remote connection responds.

## Remaining kernel completion work

- [ ] Filesystem hardening: complete bounded metadata mutation behavior and
  persistence/rollback coverage for FAT32, exFAT, ext4, and XFS.
- [ ] Decide and document the intentionally deferred Btrfs disk allocation
  scope; do not claim it is complete while it remains deferred.
- [ ] Driver hardening: grouped error paths, reset/timeout behavior, resource
  ownership, and QEMU coverage for all enabled drivers.
- [ ] Final kernel audit: no required kernel directory is empty or only a
  placeholder; every retained component has build integration and evidence.
- [ ] Final completion gate: clean build, image validation/mountability checks,
  focused contracts, full `make test`, and QEMU boot with `os kernel entry ok`.
- [ ] Notify the user that the kernel is complete and mark the goal complete.

## Userland — starts only after the kernel gate

- [ ] Tell the user before creating ring-3/userland implementation.
- [ ] Launch the first verified ring-3 process with safe user-memory copying.
- [ ] Add the command-line shell and terminal/console I/O.
- [ ] Add process tools and initial utilities (`init`, `echo`, `ls`, `cat`,
  `pwd`, `mkdir`, `rm`, and `sh`).
- [ ] Add executable loading, environment/arguments, file descriptors,
  pipes, job control, and shell error/status handling.
- [ ] Add applications incrementally with contracts and QEMU integration tests.

## Working rules

- Implement one subsystem at a time in dependency order.
- Batch meaningful implementation, then debug at the milestone gate rather than
  after every small edit.
- Keep Btrfs disk allocation deferred unless that scope is explicitly resumed.
