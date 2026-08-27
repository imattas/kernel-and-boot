# Design

## Architectural model

The kernel is **modular monolithic**. Core services and drivers execute in kernel space for efficient direct interaction, but the source tree and interfaces are separated aggressively enough that subsystem ownership remains clear.

The initial architecture is x86_64 only. Architecture-dependent mechanisms live below `kernel/arch/x86_64/`; generic policy and higher-level kernel services should remain outside that tree whenever a clean boundary exists.

## Boot architecture

`boot/UEFI/` is the primary bootloader. It is designed as a real loader with its own architecture support, EFI-facing layer, loader logic, memory handling, protocol definitions, storage support, and video support. Its eventual responsibilities include discovering firmware facilities, locating and validating the kernel, preparing boot-time memory, collecting platform information, selecting graphics state when requested, constructing a versioned boot-information contract, leaving UEFI boot services safely, establishing the machine state required by the kernel, and transferring control.

`boot/BIOS/` is intentionally reserved for later development. BIOS should not force BIOS-specific assumptions into the kernel. Both loaders should eventually converge on a common documented kernel-entry contract.

The UEFI-to-kernel entry normalizes the stack to the x86_64 C ABI alignment
before calling `kernel_main`, so compiler-generated aligned accesses remain valid
for all kernel subsystems.

The first UEFI milestone builds a PE32+ EFI application at
`build/image/esp/EFI/BOOT/BOOTX64.EFI` and packages it into a FAT12 ESP image
at `dist/os.img`. The application receives the UEFI image handle and system
table, discovers the filesystem that contains the loader, and reports loader
progress through the firmware console protocol.

The loader's current file contract is a root-directory `KERNEL.ELF` ELF64
ET_DYN image. It validates the machine type and every loadable segment's
bounds, allocates page-aligned `EfiLoaderData` memory, zero-fills segment
memory, copies file-backed bytes, and invokes the relocated entry address.
Before transfer, the loader captures the memory map and calls
`ExitBootServices`. It passes the retained map pointer, byte size, descriptor
size, and descriptor version through the shared `os_boot_info_t` contract in
`boot/UEFI/core/boot_info.h`; the kernel must not call UEFI services after
receiving this structure. The prototype reserves physical address `0x02000000`
for the loaded kernel to avoid firmware low-memory regions; replacing this
with a memory-map-selected allocation is a later memory-management task. The
loader invokes the ELF entry using the SysV x86_64 ABI explicitly, while its
own UEFI entry remains Microsoft x64 ABI.

The boot contract also carries the firmware-provided ACPI RSDP address. The
loader prefers the ACPI 2.0 configuration-table entry and falls back to the
ACPI 1.0 entry, allowing kernel MADT discovery without UEFI runtime services.

The architectural entry stub prepares and loads a flat kernel GDT, reloads
CS and data segments, and then calls C initialization. C constructs a 256-entry
IDT, routes the first 32 vectors to assembly exception stubs, and leaves
interrupts disabled until interrupt-controller setup exists. Exception panic
dispatch uses the kernel serial service and does not return.

## Kernel structure

### `arch/x86_64`
Owns processor- and platform-specific mechanisms: earliest kernel entry, CPU state, descriptor tables, exceptions, interrupts, APIC-related architecture work, paging primitives, SMP startup, syscall entry/exit, and architectural timers.

### `core`
Owns architecture-independent kernel initialization, panic handling, diagnostic output, and foundational synchronization infrastructure.

### `mm`
Owns physical memory management, virtual address-space management, kernel heap facilities, and slab/object allocation. The long-term design should distinguish physical ownership, virtual mappings, allocation policy, and architecture-specific page-table mechanics.

The initial physical allocator consumes the retained UEFI memory map and
tracks frames below 4 GiB in a bitmap. UEFI conventional-memory descriptors
become available frames, the loaded kernel range remains reserved, and frame
zero through the first 256 frames stay reserved for early hardware safety.
The allocator is intentionally separate from virtual mapping policy.

The initial heap owns a bounded 2 MiB virtual window at `0x40000000`. It
requests physical frames from the frame allocator, maps them through a 4 KiB
page table, and manages variable-size blocks with explicit headers and
coalescing. The allocator is not used by the loader and has no userspace
visibility.

The initial virtual-memory layer owns page-aligned PML4/PDPT/PD storage in the
kernel, builds an identity map for the first 4 GiB with 2 MiB leaves, and
installs its PML4 in CR3. Its public mapping operation accepts only aligned
2 MiB ranges within that initial physical window; 4 KiB expansion and
higher-half address-space policy remain later work.

### Synchronization and timer foundation

The initial interrupt path remaps and masks the legacy PIC. The BSP programs
the local APIC timer in periodic mode at vector `0x20`. The IRQ stub saves the
general-purpose register set, advances a monotonic tick counter, sends APIC
EOI, and returns with `iretq`. `timer_wait` sleeps with `hlt` until its tick
deadline.
The generic spinlock uses compiler-provided atomic exchange/store operations;
the irqsave variant snapshots RFLAGS, disables local interrupts while taking
the lock, and restores the prior flags after release. This is the initial
SMP-compatible primitive; APIC routing and per-CPU lock ownership are future
work. Local APIC initialization currently validates CPUID APIC support, enables
the xAPIC through `IA32_APIC_BASE`, masks unused local vectors, clears the task
priority, and enables the spurious-interrupt vector. The BSP APIC timer uses
the initial 100 Hz divisor/count configuration. APs install their own early IDT
and enable the same local timer after their online acknowledgement. The generic
timer layer converts ticks to a saturating nanosecond value and uses
overflow-safe deadlines for blocking waits. Calibration and per-AP timekeeping
state remain future work.

The ACPI platform layer validates the firmware RSDP checksums, walks the XSDT,
validates the MADT checksum, and counts enabled legacy-APIC and x2APIC processor
entries. It exports the MADT LAPIC base and CPU count without retaining any
UEFI service dependency. Processor startup and per-CPU state are separate
follow-on components.

### Device model and PCI discovery

The generic device layer owns a bounded static registry of discovered devices;
drivers publish bus identity and class information through this interface rather
than exposing bus scans to unrelated kernel code. The initial PCI driver uses
configuration-mechanism-1 I/O, scans multifunction endpoints, follows PCI-to-PCI
bridges with a visited-bus set, and registers each discovered function. Driver
binding is bounded and explicit: registered drivers match a bus/device and must
successfully probe before ownership is recorded. BAR resource probing, sizing,
allocation, and driver binding policy remain separate follow-on layers.

The storage layer exposes bounded 512-byte block devices with range-checked
reads. The initial ATA PIO driver binds to the PCI IDE class, consumes its
legacy I/O BARs, identifies the primary disk with bounded polling, and services
LBA28 reads. Filesystem parsing and DMA-based drivers remain above this layer.

The initial SMP layer stores up to 64 MADT processor entries with stable
logical IDs, APIC IDs, present state, and online state. A low-memory trampoline
switches APs into the kernel's page tables and long mode; the LAPIC INIT/SIPI
sequence and per-CPU stacks bring APs online one at a time. The BSP uses bounded
startup retries and waits for an explicit online acknowledgement before startup
continues. Richer per-CPU
execution state remains a follow-on component. Each logical CPU owns a
separate 256-entry IDT; the early timer gate is installed in every initialized
table before local APIC timers are enabled.
The DPL3 GDT and TSS/RSP0 are loaded on every online CPU. Scheduler activation
updates the local CPU's TSS RSP0 to the selected task's aligned kernel stack,
so privilege entry does not reuse another task's stack. User entry remains a
kernel-only boundary until the completion gate authorizes a userland launch.

The task foundation provides an architecture-neutral saved context backed by
an x86_64 callee-saved register switch and a bootstrap entry trampoline.
Blocking infrastructure uses intrusive FIFO wait queues protected by
irqsave spinlocks; enqueue, dequeue, removal, and duplicate rejection are
defined independently of future scheduler policy.
The scheduler layer owns the ready queue and task descriptors, while the
architecture-specific context switch remains below it. This keeps task
selection and state transitions separate from x86_64 register mechanics.
The scheduler tracks the current task and exposes cooperative yield/dispatch;
preemptive timer-driven scheduling and idle-task policy are deliberately
separate follow-on mechanisms.
Tasks can transition to blocked state by joining a wait queue and return to the
ready queue through wake-one; the scheduler still requires an idle task before
it can safely replace a blocked current task at runtime. An explicit idle task
is now selected when the ready queue is empty.
Kernel task objects own their stack allocation and initialized context; task
destruction refuses queued or running tasks so ownership cannot be reclaimed
while execution still references it.
The boot diagnostic executes one heap-backed kernel task through the saved
context path before terminating and reclaiming it.
Address-space objects own their root and intermediate user-range paging frames,
inherit the kernel mappings, and expose explicit CR3 activation. Mapped user
pages are caller-owned physical frames; destruction reclaims only paging
structures allocated by the address-space object.
The initial user-image loader accepts only validated x86_64 ELF executables,
reuses pages shared by load segments, rejects malformed ranges, allocates
zeroed physical pages for PT_LOAD regions, and records executable entry points.
Privilege transition is implemented as a kernel boundary but normal boot
validation deliberately stops before launching ring 3 until the kernel
completion gate is closed.
The BSP privilege path has DPL3 code/data descriptors, a TSS RSP0 stack, and
an `iretq` transition into a validated user image. Vector `0x80` is a DPL3
interrupt gate with a bounded dispatcher and validated user copies. The ring-3
launch remains disabled in normal boot validation until the kernel completion
gate is explicitly closed.

### `sched`
Owns scheduler mechanisms and policies. The current implementation lives in
`core/task`; architecture-specific context-transition code remains under the
architecture tree. A separate `sched` directory is not required until policy
or scheduler mechanisms are split into independent implementations.

### `ipc`
Owns bounded kernel communication channels. Channels copy fixed-size messages
under IRQ-safe locking, preserve FIFO order, report sender identity, reject
oversized or unavailable capacity, and allow queued messages to drain after
close. Blocking send and receive operations join scheduler wait queues when
the bounded queue is full or empty; close wakes all blocked peers while
preserving the queued-message drain rule.

### `fs`
Owns the VFS and filesystem-independent caching plus kernel-provided pseudo filesystems such as device and process views. The initial VFS layer provides reference-counted hierarchy nodes, duplicate-safe child insertion, removal, and absolute path lookup with `.` and `..` handling. `fs/block` provides bounded, callback-backed sector I/O without depending on a concrete hardware driver, and `fs/cache` provides a bounded write-through sector cache. `devfs` exposes registered devices as read-only VFS device nodes, while `procfs` provides a read-only `/self/pid` pseudo-file through the VFS read callback. The FAT12 reader validates the generated image BPB, FAT, root entries, and cluster reads, and its VFS adapter exposes read-only file nodes.

### `drivers`
Owns hardware and firmware-facing drivers. The initial decomposition reserves areas for ACPI, PCI, storage, NVMe, AHCI, USB, input, serial, display, and firmware integration. The ATA PIO driver now exposes bounded read/write storage operations through the storage contract and claims its active PCI resources. AHCI discovery enables the controller only for matching PCI SATA devices and records implemented ports. The input layer provides a bounded IRQ-safe event queue, and the PS/2 keyboard backend initializes the controller and emits set-1 scancode events. The display layer provides a bounds-checked packed-pixel framebuffer surface. USB descriptor parsing validates device and endpoint descriptors before controller binding.

### `device`
Owns the kernel's higher-level device model: registration, lifetime, hierarchy, classes, exclusive resource ownership, and the boundary between hardware drivers and consumers.

### `exec`
Owns executable loading and the mechanisms required to create an address space suitable for eventual userspace programs. `exec/exec.h` is the process-facing loader boundary; validated ELF loading remains reusable behind it.

### `syscall`
Owns architecture-independent syscall definitions, validation, dispatch, and ABI-facing kernel services. The stable syscall number/error definitions live in `kernel/syscall/abi.h`; machine entry/exit remains in `arch/x86_64/syscall`.

### `time`
Owns generic timekeeping, clocks, timers, and policy built on top of architecture/hardware timer sources. `time/clock.h` exposes the monotonic clock and timer frequency without leaking the x86_64 timer interface to consumers.

### `lib`
Owns freestanding kernel utility code that is genuinely reusable across subsystems and does not belong to a more specific owner.

### `debug`
Owns kernel debugging infrastructure, assertions, tracing, symbol-related facilities, and future diagnostic mechanisms. The assertion/range contract routes fatal invariant failures through the centralized panic path.

### `security`
Owns process credentials, capability bits, and deterministic owner/group/other
access checks. Security context is part of every process object so syscall and
VFS policy can enforce the same boundary. Cross-process signal delivery is
limited to same-UID callers or callers holding `CAP_SYS_ADMIN`.

## Toolchain

The preferred C compiler is Clang/LLVM in a freestanding configuration. NASM is used for handwritten x86_64 assembly. GNU Make is the top-level build interface. QEMU is the primary emulator for automated development and testing.

## Disk and boot-media direction

The preferred modern image layout is GPT with an EFI System Partition formatted as FAT32. The UEFI loader should load the kernel from the ESP during early development. Root-filesystem design is intentionally deferred until VFS and userland requirements are sufficiently defined.

## Build and distribution layout

Generated files are isolated from source code by design. `build/` is the sole tree for compilation, linking, generation, staging, test, and diagnostic artifacts. This includes objects, kernel/bootloader ELF or binary forms, `BOOTX64.EFI`, dependency files, generated headers, linker maps, symbols, logs, temporary filesystems, ESP contents, and test products. UEFI image assembly should stage its filesystem beneath `build/image/esp/`.

`dist/` is reserved for completed distributable output only. The normal final artifact is `dist/os.img`, using the planned GPT + FAT32 ESP layout. QEMU and integration tests should boot this final image. No build process may emit generated artifacts into `boot/`, `kernel/`, `userland/`, `scripts/`, or `docs/`. Future BIOS support follows the identical output contract.

The intended Make semantics are: `make`/`make all` build into `build/`; `make image` creates `dist/os.img`; `make clean` removes `build/`; and `make distclean` removes both output trees.

## Testing direction

Tests are separated into C, assembly, shell, and integration areas. Early tests should emphasize host-testable pure logic where possible and QEMU-based integration tests for architecture, boot, memory, interrupt, and device behavior. Serial output and emulator exit mechanisms should eventually make automated boot tests deterministic.

## Userland

`userland/` remains empty during the initial kernel/bootloader work. Before implementation begins, the project should establish a stable enough syscall ABI, executable format policy, process model, virtual-memory model, VFS semantics, and initial program-loading path.
