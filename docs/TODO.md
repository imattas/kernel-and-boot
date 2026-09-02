# OS TODO and Completion Ledger

This file is the short execution checklist. `docs/ROADMAP.md` remains the
authoritative design and phase document. Userland development is now active in
parallel with final kernel hardening; the kernel gate still controls when the
kernel may be declared complete.

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
- [x] Add GPU-backed desktop policy for bounded background/panel lifecycle,
  color updates, reflow, and teardown.
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

## Userland — active development

Current focus: continue userland contracts and shell/application behavior;
XFS feature expansion is paused. Kernel hardening continues separately, and
the init-owned shell supervisor remains a deferred kernel/userland boundary
because the first live spawn attempt stalled after init's initial write.
Keep the stable kernel-created shell handoff until the spawn/wait path has a
focused lifecycle fix.

The current userland slice adds the `version` shell built-in. XFS expansion
and init-owned supervision remain paused while the stable boot path is kept.

- [x] Echo printable shell input and coalesce CRLF console submissions.

The identity utility slice now includes bounded `id -u` and `id -g` output.

The shell directory-navigation slice now supports `cd` with no operand.

The first shell predicate utility is now packaged as `test.elf`, supporting
bounded existence, non-empty-string, and equality predicates.
It also distinguishes regular files and directories with `-f` and `-d`.
Numeric comparison predicates are also available through `-eq`, `-ne`,
`-lt`, `-le`, `-gt`, and `-ge`.
Negation, empty-string (`-z`), and string inequality (`!=`) are also
available.
The leading `!` form now negates every supported predicate, including file,
type, string, and numeric tests.

- [x] Add a small userland runtime layer for shared argument parsing, status
  handling, and diagnostic output across standalone applications.
- [x] Migrate the remaining standalone applications to the shared runtime
  helpers without changing their syscall contracts.
- [x] Add host contract coverage for shared runtime parsing and string helpers.
- [x] Add a complete built-userland ELF-set contract before image packaging.
- [x] Add standalone `HELP.ELF` and expose it through the live FAT32 VFS.
- [x] Run the full userland/kernel/image/QEMU regression gate after the
  standalone application expansion.
- [x] Give active address-space and CR3 ownership per logical CPU for SMP
  userland transitions.
- [x] Add shell/application integration coverage for filesystem mutation,
  pipelines, redirection, background jobs, and inherited environment state.
- [x] Make native shell `ls <path>` honor its directory operand consistently
  with standalone `ls.elf`.
- [x] Implement and verify init-owned shell supervision through spawn/wait/reap;
  the live QEMU shell-run gate proves the complete child lifecycle.
- [x] Add direct contract coverage for quote-aware command sequencing.
- [x] Add shell `clear` terminal control and parser coverage.
- [x] Add bounded shell aliases with explicit `alias` and `unalias` commands.
- [x] Keep standalone `HELP.ELF` command inventory synchronized with shell
  built-ins.
- [x] Add the first bounded file-reading utility, `head.elf`, to the packaged
  userland set and FAT32 VFS namespace.
- [x] Add streaming `wc.elf` line, word, and byte counting to the packaged
  userland set and FAT32 VFS namespace.
- [x] Add bounded literal line matching through standalone `grep.elf`.
- [x] Dispatch the packaged text utilities directly through shell
  spawn/wait/reap integration.
- [x] Add PATH-based fallback execution for packaged external applications.
- [x] Add shell `export NAME=VALUE` environment assignment syntax.
- [x] Add shell `read NAME` line input and environment assignment.
- [x] Add shell `uname` and `uname -a` system-identification output.
- [x] Add shell `cd -` previous-directory navigation with bounded state.
- [x] Extend standalone `env` with bounded assignment-and-command execution.
- [x] Centralize bounded `NAME=VALUE` parsing in the freestanding runtime.
- [x] Add standalone `unsetenv.elf` and expose it through the FAT32 VFS.
- [x] Dispatch `env NAME=VALUE COMMAND` through the shell's external PATH path.
- [x] Allow shell `unsetenv` to remove multiple variable names per command.
- [x] Add standalone `env -i` and repeated `env -u NAME` filtering.
- [x] Preserve empty values in shell `export NAME=` assignments.
- [x] Add `echo -n` no-newline behavior to shell and standalone echo.
- [x] Support bounded four-stage foreground pipelines in the shell.
- [x] Add shell append redirection (`>>`) while preserving truncating `>`.
- [x] Reject malformed shell pipeline, redirection, and background operators
  before process launch.
- [x] Preserve direct external utility commands through pipeline and
  redirection dispatch.
- [x] Preserve arbitrary external commands through pipeline and redirection
  parsing.
- [x] Add packaged `uptime.elf` using the monotonic clock syscall and expose
  it through PATH and the live FAT32 VFS namespace.
- [x] Implement shell `uptime` directly through the clock syscall while
  retaining external/pipeline dispatch.
- [x] Implement shell `date` directly through the realtime clock syscall while
  retaining external/pipeline dispatch.
- [x] Add packaged `date.elf` using the validated RTC syscall and expose it
  through PATH and the live FAT32 VFS namespace.
- [x] Package `clear.elf` with the shell's terminal-clear sequence for PATH
  execution and the live FAT32 VFS namespace.
- [x] Route redirected `echo` through the external descriptor path.
- [x] Route redirected `cat`, `pwd`, and `ls` through packaged utilities.
- [x] Add quote-aware bounded `;` command sequencing.
- [x] Add quote-aware conditional `&&`/`||` command sequencing driven by the
  preceding command's exit status.
- [x] Make `grep.elf` consume standard input for pipeline execution.
- [x] Make `wc.elf` consume standard input for pipeline execution.
- [x] Make `head.elf` consume standard input for pipeline execution.
- [x] Add stdin-streaming `tee.elf` with bounded multi-file persistence and package it in the FAT32 image.
- [x] Add stdin/file `tail.elf` with bounded final-line buffering and package it in the FAT32 image.
- [x] Add stdin/file `sort.elf` with bounded lexical line sorting and package it in the FAT32 image.
- [x] Add stdin/file `uniq.elf` with streaming adjacent-duplicate filtering and package it in the FAT32 image.
- [x] Add standalone `printf.elf` with bounded `%s`, `%d`, `%%`, and basic
  escaped-control output through PATH and the FAT32 image.
- [x] Extend standalone `printf.elf` with bounded `%u`, `%x`, `%X`, `%o`, and
  `%c` conversions.
- [x] Add standalone `basename.elf` path-component extraction through PATH and
  the FAT32 image.
- [x] Add standalone `dirname.elf` parent-path extraction through PATH and the
  FAT32 image.
- [x] Add standalone `cut.elf` bounded delimiter/field selection from stdin or
  a file through PATH and the FAT32 image.
- [x] Add standalone `tr.elf` bounded character translation from stdin through
  PATH and the FAT32 image.
- [x] Add standalone `cmp.elf` status-based file comparison for shell
  conditional sequencing through PATH and the FAT32 image.
- [x] Add standalone `seq.elf` bounded signed ascending/descending sequence generation through
  PATH and the FAT32 image.
- [x] Add standalone `find.elf` bounded recursive directory traversal and
  `-type f`/`-type d` filtering through the existing readdir/stat ABI; package
  it in the FAT32 image.
- [x] Add standalone `expr.elf` with checked signed integer arithmetic for
  `+`, `-`, `*`, `/`, and `%`, packaged in the FAT32 image and VFS namespace.
- [x] Expose `expr.elf` through bare shell PATH resolution and synchronized
  help/parser coverage.
- [x] Expose the shell executable under conventional `sh.elf`/`sh` aliases
  without duplicating its image payload.
- [x] Support bounded `exit [status]` values from the interactive shell.
- [x] Add native shell `basename` and `dirname` path inspection commands while
  preserving their external pipeline/redirection forms.
- [x] Add standalone `which.elf` PATH executable discovery through the shared
  runtime and FAT32 image namespace.
- [x] Add background execution and job-table tracking for PATH-resolved
  external commands.
- [x] Poll, reap, and remove completed background jobs without blocking the
  shell prompt.
- [x] Make init supervise `shell.elf` through the normal spawn/wait ABI after
  the namespace-retain and shared-scheduler startup deadlocks were fixed.

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
- [x] Fix and verify shell `run <path>` process execution through spawn/wait;
  the live QEMU gate proves spawn, wait, and reap for `/true.elf`.
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
- [x] Add standalone `ENV.ELF` utility consuming the inherited environment.
- [x] Add standalone `CAT.ELF` utility using userland file descriptors.
- [x] Add standalone `PWD.ELF` utility using the working-directory ABI.
- [x] Add standalone `MKDIR.ELF` utility using the directory-creation ABI.
- [x] Add standalone `RM.ELF` utility using the unlink ABI.
- [x] Add standalone `RMDIR.ELF` utility using the directory-removal ABI.
- [x] Add standalone `TOUCH.ELF` utility using the create/close ABI.
- [x] Add standalone `WRITE.ELF` utility using the create/write ABI.
- [x] Add standalone `LS.ELF` utility using directory enumeration.
- [x] Add standalone `CHMOD.ELF` utility using the permission-update ABI.
- [x] Add shell background `run ... &` and explicit `wait <pid>` handling.
- [x] Add bounded `jobs` tracking for background processes.
- [x] Expand the FAT32 boot-image root directory beyond one cluster.
- [x] Add standalone `ECHO.ELF` in the expanded root directory.
- [x] Add standalone `STAT.ELF` using descriptor metadata.
- [x] Add native shell `stat <path>` metadata reporting.
- [x] Add native shell `chmod <mode> <path>` permission updates.
- [x] Add native shell `kill <pid> <signal>` process signaling.
- [x] Add native shell `sleep <milliseconds>` timing utility.
- [x] Add native shell `mv <old> <new>` rename operation.
- [x] Add standalone `MV.ELF` utility using the rename ABI.
- [x] Add standalone `KILL.ELF` utility using the targeted signal ABI.
- [x] Add standalone `SLEEP.ELF` utility using the clock and yield ABI.
- [x] Add standalone `SETENV.ELF` utility using the environment-update ABI.
- [x] Add userland IPC channel wrappers, including blocking variants, and a
  standalone round-trip probe.
- [x] Add standalone `DUP.ELF` descriptor-duplication probe.
- [x] Add shell `setenv <key> <value>` and multi-entry environment inheritance.
- [x] Add shell `unsetenv <key>` environment removal.
- [x] Add shell `status` reporting for the most recent waited process.
- [x] Add shell `true` and `false` built-ins for explicit success/failure
  status control.
- [x] Parse quoted and escaped external-program arguments while constructing
  the ring-3 argv stack.
- [x] Unquote and unescape shell built-in arguments with bounded validation,
  while preserving raw quoting for external `run` arguments.
- [x] Make pipeline and redirection operators quote-aware and unquote quoted
  redirection paths before filesystem access.
- [x] Add standalone `TRUE.ELF` status utility to the FAT32 boot image.
- [x] Add standalone `FALSE.ELF` nonzero-status utility to the FAT32 boot
  image.
- [x] Add standalone `ID.ELF` and `PS.ELF` process-inspection utilities to
  the FAT32 boot image.
- [x] Add standalone `WAIT.ELF` wait/reap process-control utility to the
  FAT32 boot image.
- [x] Expose seek/truncate syscalls through userland and add standalone
  `TRUNCATE.ELF` file-size utility.
- [x] Add standalone `SEEK.ELF` seek/read file-position utility.
- [x] Add standalone `CHDIR.ELF` directory-change/cwd utility.
- [x] Add standalone `CP.ELF` file-copy utility using read/create/write.
- [x] Add native shell `cp <source> <destination>` file-copy behavior.
- [x] Add native shell `history` listing for the bounded command ring.
- [x] Add bounded shell `history -c` clearing for the command ring.
- [x] Preserve source mode and reject same-path copies in `cp`.
- [x] Add native shell `mkdir -p` recursive directory creation.
- [x] Add native shell `rmdir -p` empty-parent cleanup.
- [x] Add bounded native shell `rm -r` recursive tree removal.
- [x] Add bounded shell command history with ANSI up/down navigation.
- [x] Add bounded shell expansion for environment variables, `$?`, and `$$`
  with quote/escape-aware behavior.
- [x] Add typed pipe descriptors and blocking userland read/write wrappers.
- [x] Add optional per-process standard-input and standard-output bindings for
  redirected child processes.
- [x] Add the first shell pipeline form: `run <producer> | <consumer>`.
- [x] Add shell output redirection: `run <command> > <path>`.
- [x] Add shell input redirection: `run <command> < <path>`.
- [x] Add shell `fg <pid>` foregrounding for tracked background jobs.
- [x] Track both processes in background two-stage pipelines and reap them
  together through `wait`/`fg`.
- [x] Add a bounded serial-input bridge into the standard-input queue for
  terminal-backed userland sessions.
- [x] Start input and network runtime services before launching the persistent
  shell, so an immediately-blocking shell cannot starve kernel services.
- [x] Expose `args.elf` through the live FAT32-backed VFS namespace and grant
  boot-image ELF nodes execute permissions.
- [x] Expose `env.elf` through the live FAT32-backed VFS namespace.
- [x] Expose `cat.elf`, `pwd.elf`, and `mkdir.elf` through the live
  FAT32-backed VFS namespace.
- [x] Expose `rm.elf`, `rmdir.elf`, `touch.elf`, and `write.elf` through the
  live FAT32-backed VFS namespace.
- [x] Expose `ls.elf`, `chmod.elf`, `echo.elf`, and `stat.elf` through the live
  FAT32-backed VFS namespace.
- [x] Expose `mv.elf`, `kill.elf`, `sleep.elf`, and `setenv.elf` through the
  live FAT32-backed VFS namespace.
- [x] Expose `ipc.elf`, `dup.elf`, `true.elf`, and `false.elf` through the live
  FAT32-backed VFS namespace.
- [x] Expose `id.elf`, `ps.elf`, and `wait.elf` through the live FAT32-backed
  VFS namespace.
- [x] Expose `truncate.elf`, `seek.elf`, `chdir.elf`, and `cp.elf` through the
  live FAT32-backed VFS namespace.
- [x] Expose `shell.elf` through the live FAT32-backed VFS namespace and allow
  partial final reads for unaligned ELF loading.
- [x] Add reliable QEMU serial-input verification for the interactive shell;
  `make qemu-input-test` waits for boot, submits `echo input-test`, and checks
  that the command reaches the live shell.
- [x] Prefer the active USB HID keyboard over the legacy PS/2 stream when both
  are exposed by QEMU, preventing duplicate or cross-mapped Windows input.
- [x] Keep PS/2 enabled as a fallback until the first valid USB HID report is
  received, so an exposed-but-inactive Windows USB endpoint cannot disable the
  working legacy keyboard path.
- [x] Emit a runtime source-selection marker so QEMU logs prove USB HID
  arbitration was enabled.
- [x] Emit a first-event USB HID runtime marker to separate host-device
  delivery failures from shell processing failures.
- [x] Accept valid padded USB HID boot-keyboard packets without changing the
  canonical eight-byte report decoding.
- [ ] Add reliable QEMU live USB/PS2 keyboard verification for `run INIT.ELF`.
- [x] Expose the complete multi-cluster FAT32 boot-image application set in the
  live VFS namespace for reliable external `run` execution.
- [x] Make userland init spawn the shell through the normal process ABI after
  scheduler/runtime service startup can support an immediately-blocking child.
- [x] Add the command-line shell and terminal/console I/O.
- [x] Add process tools and initial utilities (`init`, `echo`, `ls`, `cat`,
  `pwd`, `mkdir`, `rm`, and `sh`).
- [x] Add executable loading, environment/arguments, file descriptors,
  pipes, job control, and shell error/status handling.
- [x] Add applications incrementally with contracts and QEMU integration tests;
  the packaged userland set now includes the initial filesystem, process,
  environment, and text utilities.

## Working rules

- Implement one subsystem at a time in dependency order.
- Batch meaningful implementation, then debug at the milestone gate rather than
  after every small edit.
- Keep Btrfs disk allocation deferred unless that scope is explicitly resumed.
