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
- [x] Filesystem VFS append-growth boundaries for FAT32, exFAT, ext4, and XFS
  now update cached sizes after successful writes.
- [x] Serialize exFAT data-write and resize mutations across file handles.
- [x] Serialize exFAT attribute, create, directory-create, and unlink mutations.
- [x] Serialize ext4 mode, data-write, growth, and truncation mutations.
- [x] Serialize Btrfs in-place data writes and supported truncation mutations.
- [x] Harden Btrfs mirrored-write rollback against absent mirror mappings.
- [x] Roll back both copies when mirrored Btrfs metadata-node publication fails.

## Current kernel milestone

- [x] Finish the current XFS batch: VFS append-growth boundary, 256/512-byte
  inode-core handling, and the corresponding contract coverage.
- [x] Run one grouped filesystem, kernel, image, and QEMU verification gate;
  update `ROADMAP.md` and commit the milestone.
- [x] Push the milestone to `main` after configuring the authenticated GitHub
  CLI credential helper.

## Remaining kernel completion work

- [x] Filesystem hardening: complete the bounded metadata mutation and
  persistence/rollback coverage for FAT32, exFAT, ext4, and XFS.
- [x] Decide and document the intentionally deferred Btrfs disk allocation
  scope; it remains explicitly deferred and is not claimed complete.
- [x] Driver hardening: grouped error paths, reset/timeout behavior, resource
  ownership, and QEMU coverage for all enabled drivers.
- [x] AHCI failed-probe and failed-IDENTIFY command-list/FIS cleanup is bounded
  by port-idle confirmation.
- [x] Bound storage-device name validation and duplicate comparisons.
- [x] Bound device-driver name validation and duplicate comparisons.
- [x] Serialize every XFS journal-backed publication path across journal
  prepare, target writes, flush, and clear.
- [x] Final kernel audit: no required kernel directory is empty or only a
  placeholder; every retained component has build integration and evidence.
- [x] Final completion gate: clean build, image validation/mountability checks,
  focused contracts, full `make test`, and QEMU boot with `os kernel entry ok`.
- [ ] Notify the user that the kernel is complete and mark the goal complete.

## Userland — starts only after the kernel gate

- [x] Tell the user before creating ring-3/userland implementation.
- [x] Build the first freestanding ring-3 init ELF against the existing syscall
  ABI boundary.
- [x] Load `INIT.ELF` from the UEFI boot contract and launch it as the first
  external user process.
- [x] Add the first C-facing userland syscall runtime and use it from init.
- [x] Add nonblocking standard-input reads backed by the kernel keyboard queue.
- [x] Add and contract-test the first shell command parser (`help`, `echo`, and
  `pwd`, with bounded unknown-command handling).
- [x] Build and launch the persistent ring-3 shell process with standard I/O.
- [x] Add shell `cd` and `exit` command dispatch through the syscall runtime.
- [x] Add the first core utility: shell `ls` backed by open/readdir/close.
- [x] Add shell `cat` backed by open/read/close.
- [x] Add shell `mkdir` backed by the mkdir syscall.
- [x] Add shell `rm` backed by the unlink syscall.
- [x] Add shell `rmdir` backed by the rmdir syscall.
- [x] Add shell `touch` and userland file create/write wrappers.
- [x] Add shell `write <path> <text>` file-writing behavior.
- [x] Add shell `run <path>` process execution through spawn/wait.
- [x] Add shell `id` process and credential identity utility.
- [x] Add shell `ps` live process-ID listing.
- [x] Add process status details to shell `ps`.
- [x] Add bounded inherited `PATH` environment support and shell `env`.
- [x] Add shell descriptor inheritance policy control.
- [x] Reap completed spawned processes and report `run` exit status.
- [x] Pass initial arguments to spawned user programs.
- [x] Resolve bare `run` commands through the inherited `PATH` environment.
- [x] Add shell `which` executable discovery through inherited `PATH`.
- [x] Add shell line editing for backspace/delete and cancel-line controls.
- [x] Add the first standalone ring-3 argument-printing utility to the image.
- [x] Pass inherited environment strings through spawned-process `envp`.
- [ ] Add reliable QEMU live keyboard verification for `run INIT.ELF`.
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
