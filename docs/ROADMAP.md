# Roadmap

The roadmap is dependency-oriented rather than version-oriented. Each phase should leave behind testable infrastructure rather than merely reaching the next visible demo.

## Phase 0 — Toolchain and repository foundation

The FAT12 image generator emits valid subdirectory dot entries and a matching
root volume label; the resulting image passes `fsck.fat` without repair
warnings and is mountable as a FAT12 superfloppy image.

Establish deterministic Make-driven builds, freestanding compiler/linker configuration, NASM integration, image generation, QEMU execution, serial logging, and automated test entry points. Enforce the repository output contract from the beginning: all generated/intermediate artifacts under `build/`, UEFI staging under `build/image/esp/`, generated tests under `build/tests/`, and only the completed bootable `dist/os.img` under `dist/`.

### Current milestone: UEFI entry artifact

The loader-to-kernel path is runtime-proven under OVMF. The kernel's first
architectural slice now prepares a flat GDT and installs a 256-entry IDT with
a default halt handler; activation of the new GDT/CS transition is the next
architecture-entry slice.

Complete. The root Makefile compiles and links a freestanding x86_64 C/NASM contract test entirely under `build/tests/`, builds a first-party PE32+ UEFI loader, builds a relocatable kernel ELF, and packages both into a validated FAT32 `dist/os.img`. The loader uses shared UEFI protocol/context declarations, independently linked console, firmware, file, ELF, and memory-map modules, a controlled NASM entry shim, firmware-sized file reads, watchdog disable, strong ELF validation, and growable memory-map capture before `ExitBootServices`. `make test` verifies the complete loader-to-kernel serial path under OVMF.
UEFI console diagnostics are now independently built and linked under `build/uefi/`, while the loader retains a minimal orchestration entry point. The OVMF gate continues to pass after this extraction.
ACPI table selection and GOP framebuffer discovery are now independently
implemented in the UEFI firmware module and linked through the same loader
context; a NASM entry shim, firmware-sized file loading, watchdog control, and
growable memory-map capture remain covered by the OVMF gate.

## Phase 1 — UEFI bootloader foundation

Create the UEFI executable environment and loader architecture. Establish firmware interaction, filesystem access, kernel image discovery/loading, memory-map acquisition, graphics discovery, boot-information structures, safe `ExitBootServices` handling, and a controlled x86_64 kernel handoff.

## Phase 2 — Architectural kernel entry

Establish early stack/state assumptions, descriptor tables, exception handling, interrupt infrastructure, CPU feature discovery, panic/diagnostic paths, and a documented boot-to-kernel contract.

### Current status

The kernel now enters through a dedicated x86_64 assembly stub, disables
interrupts, loads a kernel GDT, reloads CS/data segments, installs a 256-entry
IDT, wires all 32 architectural exception vectors to panic diagnostics,
initializes COM1, and records CPUID vendor/features. `make test` and
`make qemu-test` verify the resulting boot path; memory management is the next
major subsystem.

The first memory-management component is now present: a 4 GiB physical-frame
bitmap consumes the UEFI memory map, reserves the loaded kernel, and exposes
frame allocation/free operations. QEMU reports the resulting free-frame count
after boot; virtual address spaces and page-table ownership are the next
memory-management component.

## Phase 3 — Memory management

Build physical frame management, kernel virtual-memory management, page-table ownership, mapping APIs, higher-half/kernel layout policy, heap allocation, slab/object allocation, guard strategies, and memory diagnostics.

### Current status

Physical frame ownership, the initial virtual-memory layer, and the first
page-backed kernel heap are implemented and QEMU-verified. The kernel owns
page-aligned PML4/PDPT/PD tables, maps the initial 4 GiB with 2 MiB identity
leaves, installs CR3, and maps a dedicated 4 KiB heap window with splitting
and coalescing allocation.

## Phase 4 — Interrupts, timers, and SMP

The legacy PIC is retained masked while the BSP uses the local APIC periodic
timer for the IRQ0-equivalent tick source, delivered through the kernel IDT and
acknowledged with APIC EOI.
Tick-based waits and interrupt-safe spinlocks are now available. QEMU verifies
that interrupts advance the tick counter before startup completes. APIC,
capability/base discovery and local APIC enablement are now also verified.
The loader now carries the firmware ACPI RSDP into the boot contract and QEMU
verifies it. The kernel validates the RSDP/XSDT, parses the MADT, and reports
the enabled CPU count and LAPIC address under QEMU. A bounded processor table
now records discovered APIC IDs and marks the BSP online. The AP trampoline
and LAPIC INIT/SIPI path now bring the second QEMU CPU online. Each QEMU CPU
now owns and loads a separate early IDT, enables its local APIC timer, and
enters an interruptible idle loop. Per-CPU lookup is APIC-identified and global
tick accounting is BSP-owned. Tick counts are exposed as a monotonic
nanosecond clock with overflow-safe waits and BSP-owned global tick accounting;
the BSP and AP now calibrate their periodic APIC timer against the PIT with a
bounded fallback;
richer per-CPU execution stacks, and scheduler policy remain in progress. The
kernel now has intrusive, spinlock-protected FIFO wait queues with duplicate
enqueue rejection, removal, dequeue, and task-state definitions. The scheduler
core now owns a ready queue and transitions task descriptors between ready and
running states while remaining independent of scheduling policy.
Current-task ownership and a cooperative yield/dispatch path now sit above the
queue; timer preemption remains next. Kernel task objects can
now own heap-backed stacks and initialized architecture contexts, with safe
rejection of queued or running destruction; address-space-backed threads remain
later work.
The kernel-thread path is exercised by an actual context entry and return in
QEMU, not only by descriptor construction.
Process-owned kernel threads now have explicit scheduler start semantics, and
ready-but-not-running threads can be removed from the scheduler before their
owned stack is reclaimed.
Timer-driven scheduler preemption now saves and rotates live kernel-task
contexts from the timer interrupt, supports task exit back to the scheduler
host, and reclaims completed task objects under QEMU.
The virtual-memory layer now creates independent roots with private paging
tables for user-range pages, activates them through CR3, and reclaims owned
tables safely. A strict ELF64 executable loader now validates PT_LOAD ranges,
maps private user pages, zero-fills memory, records the entry point, and
reclaims image pages; privilege transition and isolation policy remain later.
BSP DPL3 descriptors and TSS/RSP0 groundwork are present; AP privilege-entry
state remains later.
The BSP now enters a validated user image through `iretq`; a DPL3 `int 0x80`
gate dispatches a first syscall and returns to ring 3. Argument validation,
copy-to/from-user, and a minimal defined ABI now validate mapped user pages and
support debug, stdout write, and monotonic-time calls. Process lifetime,
descriptor policy, signals/events, and a complete production ABI remain next.
The current user-image probe now runs through an owned process object containing
its address space, image, and user stack. Signal mask/send/receive operations
are now exposed through the validated syscall ABI.
Blocking and wake-one transitions are now implemented over the wait-queue
interface. The scheduler now also provides a block-current operation that
queues the running task and dispatches the next ready or idle task, rolling
back safely when no replacement exists. Task exit likewise falls back to the
idle task when no runnable peer remains. Timer preemption is implemented and
covered by the QEMU scheduler probe.

Develop APIC-era interrupt handling, timer sources, timekeeping foundations, multiprocessor discovery and startup, per-CPU state, inter-processor interrupts, and synchronization primitives suitable for SMP. AP startup now has bounded retries for transient delivery loss.
APIC IPI startup now polls the hardware delivery-status bit with a bounded
timeout instead of treating a fixed delay as delivery proof; the complete
QEMU SMP boot gate passes with this path.

## Phase 5 — Tasks and scheduling

Define scheduler core/policy separation, blocking/wakeup primitives, and the
foundation required for processes and threads. Initial execution contexts,
architecture-specific switching, task states, and synchronized FIFO wait
queues are now in place.

## Phase 6 — Kernel core services

The freestanding kernel memory library and centralized panic/exception halt
path are now implemented. The kernel initialization path, complete
process/thread lifecycle with process-owned thread teardown, scheduler
preemption, bounded IPC channels, and the initial security policy are now
implemented. The kernel-facing syscall ABI and executable-loader boundary
are implemented. The first filesystem-independent VFS hierarchy,
path-lookup layer, block interface, write-through cache, and device/process
pseudo-filesystem population and the FAT12-to-VFS read adapter are now
implemented. Hardware drivers are
deliberately excluded from this phase.
Process destruction now owns teardown of all non-running process threads,
including removal of scheduler-queued tasks, and is covered by the QEMU
lifecycle probe.
The kernel now maintains atomic pending/blocked signal transitions and a bounded process registry with duplicate-ID
rejection, lookup, removal on reap, and a targeted signal-send syscall ABI.
The QEMU syscall probe exercises the target lookup and signal delivery path.
Process termination now reclaims all non-running owned threads while refusing
to terminate a process with a running task; the QEMU lifecycle probe covers
both paths.
Address-space mapping now rejects duplicate present virtual pages before
allocating replacement state, preventing silent remap and frame leaks; the
QEMU memory probe covers the rejection path.
The ELF loader now reuses pages shared by adjacent load segments while
preserving zero-fill and ownership accounting; the QEMU image probe exercises
two load segments sharing one page.
User-copy helpers now define zero-length operations as safe no-ops and reject
copies crossing into an unmapped or non-writable user page; the QEMU syscall
probe covers both boundary classes.
ELF page mappings now carry the segment write policy, with a controlled flag
update for shared pages; the QEMU image probe verifies a non-writable code
page while the process stack remains writable.
The x86-64 memory layer now enables NXE and maps non-executable user pages with
NX set while preserving executable pages. The QEMU probe validates both page
classes; actual ring-3 launch is intentionally deferred until the kernel
completion gate is closed.
ELF image teardown now unmaps each image page before returning its physical
frame, allowing an address space to be reused without stale mappings; the
kernel image probe exercises this unload path.
Active address-space page flag updates and unmaps now issue `invlpg`, so
permission and lifetime changes cannot remain hidden behind stale TLB entries.
Page-flag replacement now clears stale writable, user, and NX state before
applying the requested user-page policy, and failed page-table allocation
rolls back newly installed table links and owned frames.

## Phase 7 — VFS and kernel abstractions

Build VFS objects and lifetime rules, path traversal, mount semantics, caching,
device/process pseudo-filesystem interfaces, and filesystem-independent block
interfaces. The VFS hierarchy, mount traversal, block interface, and bounded
write-through cache, and device/process pseudo-filesystems are implemented.
The scheduler round-robin policy and process handle table are now separately
compiled kernel components with explicit build/link ownership; implementation
files are no longer embedded into unrelated translation units.
Mounted VFS lookup now handles `.`, `..`, and paths ending at a mountpoint,
returning the mounted root with correct reference ownership; the QEMU VFS
probe covers mountpoint-root traversal.
FAT32 filesystem support now includes BPB validation, cluster-chain traversal,
8.3 and validated long-filename directory lookup, bounded file reads, and a
VFS read-only file adapter through the existing storage-device interface.
Directory-relative lookup and file reads now traverse validated subdirectory
cluster chains; the FAT12 path remains unchanged.
VFS nodes now support an explicit private-data destructor, and both FAT VFS
adapters release their per-file state with the node lifetime.
VFS read handlers and private data are now single-assignment, and node release
uses an atomic underflow-safe decrement before teardown.
The kernel heap now rejects alignment/size overflow, invalid pointers, and
double frees, and returns a physical frame if page mapping fails; the boot
probe exercises invalid and repeated frees.
Physical frame ownership now uses a separate allocation bitmap and a lock, so
reserved frames cannot be released through the public free API and concurrent
allocation/free accounting remains serialized.
Process handle tables now serialize open, lookup, and close operations with
their own lock, making descriptor rights checks safe across kernel threads.
The process registry now serializes duplicate-ID checks, insertion, lookup,
and removal across concurrent lifecycle operations.
The bootable `dist/os.img` is now a standards-compliant FAT32 superfloppy
with two FAT copies, backup boot/FSInfo sectors, and valid directory metadata;
`fsck.fat`, `mdir`, and the QEMU UEFI/AHCI/NVMe paths all validate the same
image.
Keep this phase independent of concrete hardware drivers.

The filesystem expansion phase has started after the completed UEFI milestone:
the kernel now contains a bounded read-only exFAT parser and VFS adapter with
boot-region validation, FAT/no-FAT-chain traversal, UTF-16 directory-name
decoding, and file reads, backed by an independent in-memory contract test.
ext4, XFS, and Btrfs filesystem work is in progress before drivers resume.

## Phase 8 — Kernel drivers

Only after Phases 6 and 7 are complete, implement PCI driver probes and
resource ownership. Resource ownership is now implemented for the ATA probe;
storage drivers, filesystems, USB, input, display, and other hardware drivers
continue in dependency order. The read-only FAT12 image filesystem and input
event queue, PS/2 keyboard backend, framebuffer surface, USB descriptor layer,
UHCI controller initialization and BIOS ownership handoff, plus an ungated
queue-head/transfer-descriptor control-transfer layer, USB HID boot-keyboard
report decoding,
NVMe admin-queue initialization, bounded admin-command submission, Identify
Controller validation, namespace sector read/write I/O, and the COM1
serial driver and UEFI GOP framebuffer handoff are now implemented and gated.
The QEMU gate also instantiates and validates a PIIX3 UHCI controller and
root-hub port scan, plus an AHCI controller attached to a test disk image;
the QEMU gate now requires a detected AHCI controller, a link-ready port, and
command-list/FIS engine initialization and an ATA IDENTIFY DMA command
submission with bounded completion/error polling. The AHCI READ/WRITE DMA paths now
uses the required 128-byte command-table header layout, validates the SATA
device signature, and are gated by real LBA0 FAT32 read and sector write/read-back
signature checks. Full controller I/O coverage, including remaining
USB/NVMe/network error and interrupt paths, is still required before the kernel
completion gate.
AHCI now also exposes bounded multi-sector READ/WRITE DMA transfers within one
page and the QEMU gate verifies a two-sector write/read-back operation.
The identified AHCI disk is now registered with the generic storage layer;
the FAT32 and VFS probes therefore exercise the AHCI-backed storage device.
Driver enumeration and hardware
coverage remain incomplete. The QEMU gate now supplies an emulated e1000 NIC
so its controller path and bounded TX/RX descriptor operations can be exercised;
the boot probe submits a real test frame to the TX ring.
The e1000 path now has independent build/link ownership, enables bounded
RX/TX completion causes, and exposes a polling service that acknowledges
causes and reclaims completed TX entries.
UHCI control transfers now build a bounded multi-packet endpoint-0 TD chain,
validate the setup transfer length, alternate data toggles, and verify every
TD before releasing DMA frames.
UHCI TD status handling now uses the controller’s low-speed and error-bit
definitions; the QEMU gate performs a real USB keyboard device-descriptor
control transfer and validates the returned descriptor.
The UHCI path now enumerates the connected device through SET_ADDRESS,
post-address descriptor/configuration reads, and SET_CONFIGURATION; QEMU
validates the configured-device path.
The UHCI driver now exposes bounded interrupt-transfer scheduling with
endpoint toggles; the boot path discovers an interrupt-IN endpoint from the
configuration descriptors and exercises the USB HID polling path.
PCI now assigns bounded low-MMIO addresses to unmapped or above-4-GiB memory
BARs, allowing the NVMe admin path to operate under the current identity map.
NVMe namespace I/O now supports bounded multi-sector transfers within one DMA
page and is covered by a real two-sector write/read-back QEMU check.
The scheduler now has an explicit core boundary
and round-robin policy module. Process pending/blocked signal state and explicit
termination status are now implemented; signal delivery policy remains bounded
to the kernel process object until userland exists. ACPI parsing is now owned by
the kernel ACPI driver boundary rather than an empty driver scaffold. The boot
path now tracks ordered initialization stages and validates the firmware boot
contract through a dedicated boundary. CR3 activation is now isolated behind
the x86_64 memory architecture boundary. The unused BIOS scaffold directory
was removed; BIOS remains a separate later boot path.

## Phase 9 — Userspace boundary

Design and implement syscall entry/exit, ABI validation, process address spaces, executable loading, privilege transitions, process/thread semantics, handles or descriptor policy, signals/events as selected, and safe copy-to/from-user mechanisms.

## Kernel completion gate — required before userland

`userland/` remains empty until the kernel is complete as a usable operating-system kernel. Every planned kernel subsystem and directory must either contain its real implementation, build integration, focused tests, and QEMU/integration evidence, or be deliberately removed from the architecture and roadmap. Empty directories are roadmap scaffolding, not completed work.

Process thread ownership now enforces nonzero per-process thread IDs and
rejects duplicate IDs before allocation; callers can resolve an owned thread
through `process_thread_lookup`.

AP IDT gates now use the selector belonging to the active AP trampoline code
segment, while the BSP retains its kernel selector; the two-CPU QEMU gate
reaches the AP online marker without a table-load fault.

This gate includes all non-driver kernel core services and VFS abstractions,
then the complete driver phase, with build integration, focused tests, and QEMU
evidence for each. The current syscall/ring-3 probe and ATA driver are
milestone groundwork only; they do not pass this gate.

## Filesystem milestone before drivers

FAT32 and exFAT have read-only parsers and VFS file adapters. exFAT now
validates the primary and backup boot-region checksums, supports bounded
directory-relative lookup and reads, validates directory entry-set checksums,
and compares validated UTF-8 names with their on-disk UTF-16 names. Ext4 now has
read-only inode, directory, direct/indirect-block reads, and extent-tree file
reads through a VFS adapter. Ext4 now resolves inode tables across multiple
block groups, supports 64-bit block-count/inode-table metadata, and combines
the on-disk low/high inode size fields. XFS and
Btrfs now have strict read-only superblock and geometry
mount layers with contract tests. XFS now maps allocation-group inode numbers,
reads v1/v2 inodes, supports short-form directories, inline files, extent
records, and a VFS file adapter. XFS extent reads now decode the extent-state
flag correctly and return zeroes for sparse gaps and unwritten extents. Btrfs
validates its CRC32C superblock checksum
and supported checksum type, validates tree-node checksums/identity, and reads
bounded leaf items through single-stripe system-chunk logical-to-physical
mapping, loads additional single-stripe mappings from the chunk tree, resolves
the standard FS_TREE root item, validates CRC32C checksums for filesystem
sector data through the checksum tree, and selects a valid primary or mirror
superblock. Btrfs
now decodes bounded inline and uncompressed regular EXTENT_DATA records,
extracts inode metadata, and reads mapped data including unaligned byte
ranges, sparse holes, and multi-extent files. Btrfs now performs hashed
DIR_ITEM lookup with variable-length entry checks and packed hash-collision
entries; its multi-extent read path is integrated into a read-only VFS file
adapter. Redundant same-device chunk layouts are now accepted by selecting a
validated mirror stripe with read fallback after checksum failure. Btrfs now
resolves matching filesystem devices by device ID and maps bounded RAID1
mirrors across separate registered storage devices, with the same read
fallback behavior. Encryption and additional compression edge cases remain
before the filesystem milestone is complete. Btrfs regular extents now support bounded zlib-wrapped
DEFLATE streams, including stored, fixed-Huffman, and dynamic-Huffman blocks,
with Adler-32 validation and sector-checksum-protected input. The remaining
compression edge cases remain before the filesystem milestone is complete.
Btrfs regular extents now also support native LZO length-header and
sector-segment framing with bounded LZO1X decoding. A
format is not considered complete merely because its superblock is recognized.
Zstandard frame parsing now has strict raw/RLE block contracts plus compressed
blocks using direct-weight Huffman literals, with an FSE-compressed Huffman-weight
decoder path now implemented. Bounded FSE table construction,
reverse-bitstream state decoding, and compressed-block sequence execution are
now implemented and
contracted, including the two-state interleaved stream form. FSE
normalized-header parsing is now also implemented and contracted, and the
reusable initialized/peek/update stream API is now implemented and contracted.
The read-only Btrfs extent path now dispatches Zstandard records and has a
sector-checksummed compressed-extent contract.
The predefined LL/offset/ML distributions and RLE sequence-table mode are now
implemented and contracted. A shared sequence-table selector now handles
predefined, RLE, FSE-compressed, and repeat modes with bounded consumption.
Zstandard sequence-section count/mode header parsing is now implemented and
contracted; checksum verification is complete for frames that carry a checksum.
An end-to-end generated FSE-weight literal vector remains to be added to the
contract suite.
The bounded overlap-safe match-copy primitive is now
implemented and contracted, including destination-underflow rejection.
Zstandard sequence code expansion for literal lengths, match lengths, and
offsets is now implemented and contracted. Sequence execution now copies
literal runs and performs validated overlapping matches; FSE symbol wiring and
complete compressed-block sequence integration remain. FSE states are now
wired into one-sequence decoding with offset, match-length, and literal-length
extra-bit consumption and optional state updates.
The block-output layer now executes all decoded sequences and appends trailing
literals with bounded total-output validation.
Zstandard sequence-table preparation now consumes and validates LL/offset/ML
mode tables in order, including inherited repeat tables.
The multi-sequence coordinator now decodes exactly the declared count and
applies the final-sequence state-update rule.

## Phase 10 — Userland begins after the kernel gate

Only after the kernel completion gate passes populate `userland/`. Begin with the minimum runtime and init environment needed to exercise the real kernel ABI, then expand toward libraries, shell/tooling, and system services.

## Phase 10 — BIOS boot path

Implement legacy BIOS support as a separate loader path. It should normalize legacy machine startup into the same kernel boot contract used by UEFI rather than creating a BIOS-specific kernel architecture.

## Long-term work

Continue hardening SMP, memory reclamation, storage, USB, networking, graphics, security, debugging, crash diagnostics, performance tooling, power management, hardware compatibility, and userland while maintaining explicit subsystem boundaries and automated regression coverage.
