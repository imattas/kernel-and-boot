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
a default halt handler, and per-CPU GDT/TSS activation after SMP startup.

Complete. The root Makefile compiles and links a freestanding x86_64 C/NASM contract test entirely under `build/tests/`, builds a first-party PE32+ UEFI loader, builds a relocatable kernel ELF, and packages both into a validated FAT32 `dist/os.img`. The loader uses shared UEFI protocol/context declarations, independently linked console, firmware, file, ELF, and memory-map modules, a controlled NASM entry shim, firmware-sized file reads, watchdog disable, strong ELF validation, and growable memory-map capture before `ExitBootServices`. `make test` verifies the complete loader-to-kernel serial path under OVMF.
UEFI console diagnostics are now independently built and linked under `build/uefi/`, while the loader retains a minimal orchestration entry point. The OVMF gate continues to pass after this extraction.
ACPI table selection and GOP framebuffer discovery are now independently
implemented in the UEFI firmware module and linked through the same loader
context; a NASM entry shim, firmware-sized file loading, watchdog control, and
growable memory-map capture remain covered by the OVMF gate.
Unused empty UEFI and BIOS scaffold directories have been removed; source
ownership now follows the implemented UEFI core and the explicitly deferred
BIOS path.

## Phase 1 — UEFI bootloader foundation

Create the UEFI executable environment and loader architecture. Establish firmware interaction, filesystem access, kernel image discovery/loading, memory-map acquisition, graphics discovery, boot-information structures, safe `ExitBootServices` handling, and a controlled x86_64 kernel handoff.

## Phase 2 — Architectural kernel entry

The syscall ABI now exposes validated waiting for an exited process, including
retained process lookup, user-status copyout, self-wait rejection, and cleanup
of the retained process object.
Process construction now has an atomic kernel helper that owns ELF loading,
the complete user stack, and the initial user task, with teardown on failure.
Process handles now optionally own release callbacks, and close/process teardown
detaches entries before invoking callbacks so object destruction cannot run under
the handle-table lock.
Retained handle references now keep an entry alive across concurrent close,
defer its release callback until the last reference is dropped, and block
process teardown until those retained descriptors are released.

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
enters an interruptible idle loop. Per-CPU lookup is APIC-identified; the BSP
owns the system-wide tick clock while each logical CPU also records its own
bounded tick count. Tick counts are exposed as a monotonic nanosecond clock
with overflow-safe waits; the BSP and AP now calibrate their periodic APIC timer against the PIT with a
bounded fallback. The
kernel now has intrusive, spinlock-protected FIFO wait queues with duplicate
enqueue rejection, removal, dequeue, and task-state definitions. The scheduler
core now owns a ready queue and transitions task descriptors between ready and
running states while remaining independent of scheduling policy.
Current-task ownership, cooperative yield/dispatch, and timer preemption now
sit above the queue. Kernel task objects can
now own heap-backed stacks and initialized architecture contexts, with safe
rejection of queued or running destruction. Address-space-backed user threads
are now bound to their process address space, and user-page unmapping reclaims
empty owned page-table levels with a QEMU regression probe.
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
BSP and AP DPL3 descriptors now load per-CPU GDTs and TSS/RSP0 state after
SMP trampoline entry. Scheduler activation routes each selected task's aligned
kernel stack into the local TSS RSP0; actual userland launch remains later.
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
AP startup now publishes the AP as online only after its per-CPU IDT, GDT,
TSS, and timer are initialized, waits for the BSP interrupt setup release,
and enables CR4 OSFXSR/OSXMMEXCPT before entering compiled kernel code.

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
User-copy helpers now hold the address-space lock across validation and byte
access, preventing a concurrent map update or unmap from invalidating a
validated syscall buffer before use.
Bounded anonymous user mappings now allocate zeroed physical frames, track
ownership in the address space, roll back partial ranges, and expose validated
map/unmap syscall operations; userland launch remains deferred.
User memory protection now has a range-level API and syscall that prevalidates
the complete mapped range, updates W^X page policy under the address-space lock,
and invalidates active TLB entries; failed ranges remain unchanged.
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
IPC channels now integrate with scheduler wait queues: blocking senders and
receivers sleep on full/empty queues, successful transfers wake the opposite
side, and close wakes all waiters while queued messages remain drainable.
QEMU now exercises a real blocked receiver and producer task across a context
switch, including wakeup, payload delivery, task exit, and channel close.
The QEMU target now truncates its serial log before each run so validation
cannot pass from stale output left by an earlier boot.
The QEMU milestone gate now allows 30 seconds for a complete boot under host
load, preventing a valid scheduler probe from being truncated by the harness
timeout.

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
Handles now carry a generation tag, so closing and reopening a slot cannot
make an old numeric handle access the replacement object.
VFS file descriptions now retain their node, serialize per-open offsets, enforce
read/write rights, and can be installed as owned process handles with safe close
and retained-reference behavior.
Path-based descriptor opening now resolves a retained VFS node before creating
the owned file handle, releasing the lookup reference on every path.
Directory descriptors now support serialized retained-child iteration through a
bounded `readdir` contract while regular-file reads remain type-safe.
Processes now own a retained root/working-directory namespace, and the kernel
ABI provides validated `open`, `read`, `write`, and `close` operations over VFS
file descriptions without starting ring 3.
The ABI now also provides validated descriptor seek and directory enumeration,
including typed dirent copyout and directory/file operation separation.
Heap-backed IPC endpoints can now be owned by process handles, with validated
message copy-in/copy-out and create/send/receive syscall coverage.
Blocking IPC send/receive operations are now exposed through the same ABI with
validated buffers preserved across the wait-capable path.
The syscall ABI now exposes cooperative scheduler yield, completing the first
explicit task-scheduling operation available to future user tasks.
Processes now support root-confined working-directory changes through a
validated `chdir` syscall; non-directory targets are rejected.
The namespace ABI now reconstructs and validates the current path through a
root-bounded `getcwd` syscall, including retained ancestry during traversal.
The descriptor ABI now exposes retained-handle `fstat`, returning a locked
snapshot of VFS type, mode, and owner metadata through validated copyout.
Write-capable descriptors now support a validated `truncate` syscall; the
filesystem callback updates persistent size and clamps the shared file offset.
VFS file descriptions now support retained descriptor duplication, preserving
shared offsets and object lifetime after either descriptor is closed.
VFS path resolution now distinguishes absolute root paths from working-directory
relative paths, with `..` confined at the process namespace root.
VFS-backed `open` and `chdir` now enforce Unix owner/group/other mode bits
against the calling process credentials, including directory search permission.
Access-aware VFS path traversal now requires search permission on every
directory crossed, preventing inaccessible parent directories from being used
as a path-resolution side channel.
Current-process exit now publishes an exit status, wakes signal and exit
waiters, and terminates the scheduler task through a dedicated exit syscall;
the kernel gate now exercises a successful ring-3 write/exit invocation before
userland is populated.
The process registry now serializes duplicate-ID checks, insertion, lookup,
and removal across concurrent lifecycle operations.
The bootable `dist/os.img` is now a standards-compliant FAT32 superfloppy
with two FAT copies, backup boot/FSInfo sectors, and valid directory metadata;
`fsck.fat`, `mdir`, and the QEMU UEFI/AHCI/NVMe paths all validate the same
image.
Keep this phase independent of concrete hardware drivers.

The filesystem expansion phase has started after the completed UEFI milestone:
the kernel now contains a bounded exFAT parser and VFS adapter with
boot-region validation, FAT/no-FAT-chain traversal, UTF-16 directory-name
decoding, and file reads, backed by an independent in-memory contract test.
The ext4, XFS, and Btrfs filesystem read/write milestones continue in bounded
filesystem expansion; filesystem expansion is tracked separately from the
kernel completion gate.

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
AHCI now also exposes bounded multi-sector READ/WRITE DMA transfers with a
two-entry PRDT spanning two DMA pages; the QEMU gate verifies a sixteen-sector
write/read-back operation across that boundary.
AHCI public and storage-backed I/O now split larger valid requests into bounded
8-sector DMA commands, and QEMU verifies a ten-sector request across that
command boundary.
AHCI command timeouts now stop and restart the port engine with bounded CR/FR
quiescence and error-state clearing before DMA buffers are released.
The identified AHCI disk is now registered with the generic storage layer;
the FAT32 and VFS probes therefore exercise the AHCI-backed storage device.
ATA PIO writes now issue and await the appropriate LBA48 or legacy FLUSH CACHE
command before reporting success, matching the durability contract of the DMA
storage backends.
AHCI probe binding now requires a link-ready SATA port with the supported device
signature, and releases the BAR instead of exposing an unusable controller.
AHCI probe validation now rejects I/O BARs before treating the ABAR as MMIO.
AHCI port discovery now verifies that every implemented port’s register block
fits inside the published ABAR size before dereferencing its MMIO registers.
The generic storage registry now serializes registration, enumeration, and
backend dispatch, rejects duplicate device names, and has hosted contract-test
build support without executing privileged interrupt-state instructions.
Storage registration now rejects empty names and devices whose advertised
capacity would overflow the 64-bit byte range, preserving a safe contract for
all filesystem adapters and future DMA backends.
The PS/2 keyboard backend now consumes Set-1 extended prefixes and emits
distinct extended key codes while retaining make/break values.
PS/2 initialization now validates the controller self-test and keyboard-port
test, configures keyboard IRQ delivery and port enablement, flushes stale
controller output, selects Set-1 scancodes, and explicitly enables scanning.
The PS/2 IRQ stub now sends a local-APIC EOI for IOAPIC delivery and retains a
PIC EOI for the legacy fallback, preventing either route from blocking later
keyboard events.
Interrupt initialization now routes PS/2 IRQ1 to vector 33 through the IOAPIC
when ACPI provides a valid route, and masks the PIC path only after that route
is installed.
The ATA PIO fallback now selects validated LBA48 task-file commands when the
device advertises 48-bit addressing, while retaining the bounded LBA28 path.
ATA PCI probing now rejects published memory BARs and port addresses outside
the x86 I/O range before resource claim; absent BARs retain validated legacy
IDE defaults.
ATA PIO probing now also requires aligned task-file/control BARs with register
spans large enough for the complete ATA register windows before port access.
ATA PIO task-file access and shared IDENTIFY metadata are now serialized with
an IRQ-safe driver lock, preventing concurrent SMP callers from interleaving
register programming or observing partial geometry state. ATA PIO writes now
issue and validate the appropriate cache-flush command before reporting
completion, and IDENTIFY rejects ATAPI/non-LBA disk types. The ATA backend now
also exposes bounded multi-sector read/write operations, with QEMU verifying
two-sector PIO write/read-back through the real boot disk.
Driver enumeration and hardware
coverage remain incomplete. The QEMU gate now supplies an emulated e1000 NIC
so its controller path and bounded TX/RX descriptor operations can be exercised;
the boot probe submits a real test frame to the TX ring.
UHCI interrupt delivery remains on the shared legacy dispatcher because QEMU
can place UHCI and e1000 on one legacy GSI; the dispatcher explicitly services
both device handlers before issuing APIC EOI.
UHCI control and interrupt transfers now reclaim all allocated DMA frames on
controller stop/restart failures and treat restart failure as a transfer error.
The shared legacy ISR now increments e1000 delivery accounting only when an
enabled e1000 interrupt cause is present, so UHCI-only interrupts cannot be
mistaken for network interrupt delivery.
The e1000 path now has independent build/link ownership, enables bounded
RX/TX completion causes, and exposes a polling service that acknowledges
causes and reclaims completed TX entries. TX/RX ring state is serialized with
an interrupt-safe driver lock so polling and future interrupt delivery cannot
race descriptor ownership. RX delivery rejects malformed, zero-length, and
oversized descriptors instead of silently truncating packets.
RX delivery now assembles bounded packets spanning multiple completed
descriptors, recycles every consumed descriptor, and rejects incomplete,
errored, and capacity-overflowing frames without exposing partial data.
The e1000 receive control register now selects 4096-byte buffers to match the
DMA frame allocation and packet-length contract.
The driver now reports the controller's hardware link state; link-down is
nonfatal, while the QEMU gate verifies the emulated link-up state.
The driver reports whether PCI MSI or ACPI-MADT IOAPIC legacy routing was
enabled. The standard QEMU e1000 model exposes no usable MSI capability, so
the QEMU gate now validates the routed legacy interrupt path instead of
accepting polling as the driver milestone.
The first bounded Ethernet-II layer now constructs and parses canonical
14-byte-header frames, enforces the 1500-byte payload and 1518-byte frame
limits, pads short frames to the hardware minimum, and rejects non-EtherType
length fields before higher network protocols consume a frame.
The ARP packet codec now validates Ethernet/IPv4 hardware and protocol sizes,
operation codes, and the fixed 28-byte layout while constructing bounded
requests and replies for the Ethernet layer.
The bounded ARP cache now serializes updates and lookups, replaces the oldest
entry when full, refreshes existing mappings, and expires entries using a
caller-supplied monotonic timestamp.
The bounded IPv4 layer now builds and validates version/IHL, total length,
TTL, protocol, addresses, and Internet checksums, while rejecting fragmented
packets until a reassembly component is available.
The bounded UDP layer now constructs and validates port/length fields and the
IPv4 pseudo-header checksum, with explicit corruption rejection before payload
delivery.
The bounded ICMPv4 echo layer now constructs and validates request/reply
headers, identifiers, sequences, payloads, and Internet checksums, rejecting
corrupted control packets before dispatch.
The bounded IPv4 route table now canonicalizes network prefixes, serializes
route updates and lookups, selects longest-prefix matches with metric
tie-breaking, and supports explicit route removal.
The bounded Ethernet packet queue now serializes producer/consumer ownership,
copies complete MTU-sized frames, reports queue depth and drops, and rejects
invalid frame sizes without allowing overflow.
The e1000 network adapter now validates Ethernet frames at the protocol
boundary before transmission and provides bounded polling that transfers
completed hardware RX frames into the packet queue.
The e1000 probe now validates the hardware Receive Address registers and
publishes the controller MAC, which the Ethernet path uses instead of a
fabricated source address.
The e1000 probe now requires a BAR large enough to contain every register it
accesses, including the receive-address registers, and rejects BAR ranges that
cross the supported below-4-GiB MMIO window.
The e1000 matcher now binds only known legacy 82540/82545-compatible device
IDs, preventing unsupported Intel Ethernet generations from entering the
legacy register and descriptor path.
PCI BAR-size probing now preserves 32-bit mask width instead of sign-extending
it into a bogus 64-bit size, allowing valid below-4-GiB devices to bind to the
correct bounded MMIO window.
The network adapter now exposes a typed decoder that validates and dispatches
Ethernet frames through ARP, IPv4, UDP, and ICMP layers without allowing
malformed nested payloads to reach protocol consumers.
Bounded IPv4 fragment reassembly now accepts out-of-order fragments, rejects
overlap and inconsistent extents, expires stale slots, and delivers only a
complete payload from its SMP-safe four-slot table.
The bounded UDP endpoint table now provides locked bind/unbind, exact-address
and wildcard delivery, FIFO datagram queues, and explicit overflow accounting
for the future kernel socket boundary.
The network decoder now delivers validated nested UDP frames into bound kernel
endpoints, completing the first hardware-independent receive path above the
e1000 adapter boundary.
The network stack now validates TCP segment headers and pseudo-header
checksums, exposes bounded SYN/ACK establishment and in-order payload/FIN
state transitions, and routes TCP frames through the common network decoder.
The QEMU kernel probe covers the handshake, payload sequencing, and reset path.
Bounded TCP endpoints now bind local addresses and ports, match established
peers, queue validated in-order stream payloads, expose overflow accounting,
and return connection-control results to the network boundary. The QEMU probe
delivers a complete framed handshake and reads the queued stream data back.
TCP endpoints now generate checksummed outbound ACK/PSH data and FIN segments,
advance send sequence space only after successful construction, and track
FIN-WAIT/LAST-ACK transitions with bounded acknowledgment validation.
TCP endpoint tables now support active opens, generating SYN segments and
accepting a validated SYN-ACK with the correct ACK response and sequence state.
The QEMU probe covers the client-side handshake path.
Listening endpoints now remain available while each SYN allocates a bounded
child connection, and the endpoint API reports accepted children through
`tcp_endpoint_accept`. Exact established-peer matching takes precedence over
the listener; the QEMU probe covers two passive opens and service delivery
through the accepted child.
SYN retransmission for a child in `SYN_RECEIVED` now returns the original
SYN-ACK without allocating a duplicate endpoint, preserving passive-open
recovery under packet loss.
The network service now generates standards-shaped TCP resets for validated
segments with no matching endpoint, including correct sequence/acknowledgment
selection for ACK-bearing and sequence-consuming inputs, while suppressing
responses to incoming resets.
TCP connections now track the unacknowledged send edge and peer-advertised
window, advance it only on valid acknowledgements, and reject transmissions
that exceed the bounded flow-control window.
Outbound TCP segments now retain a bounded retransmission record, expose
timeout-driven retry polling with a three-retry ceiling, and clear the record
only when a valid acknowledgement reaches the stored sequence end.
The persistent network service now polls those records, resolves the peer
hardware address from the ARP cache, rebuilds the IPv4/Ethernet envelope, and
transmits due retries through e1000.
TCP orderly shutdown now validates peer FINs, acknowledges them through
CLOSE-WAIT, emits the local FIN through LAST-ACK, and reaches CLOSED only after
the final acknowledgement.
Established TCP connections now return bounded duplicate ACKs for valid
out-of-order or duplicate segments, preserving the current receive edge for
retransmission recovery instead of silently discarding the peer’s progress.
The network service loop now forwards validated UDP frames from the hardware
packet queue into bound endpoint tables, and the QEMU boot probe exercises that
end-to-end queue-to-endpoint delivery path.
The network service now consumes fragmented IPv4 packets through the bounded
reassembly table, rebuilds a validated packet only after complete delivery,
and keeps incomplete or malformed fragment sets out of protocol consumers.
The QEMU boot probe now feeds an out-of-order two-fragment UDP datagram through
the service and verifies endpoint delivery after reassembly.
The validated network service now runs as a persistent preemptible kernel task
after boot, keeping e1000 receive polling, protocol dispatch, and reply
transmission active beyond the initialization probes.
The persistent network service now dispatches validated TCP frames into the
kernel TCP endpoint table alongside UDP, with the QEMU service probe covering
SYN, ACK, and subsequent outbound stream generation through the service queue.
The service now builds checksummed TCP responses with reversed Ethernet/IP
addresses and transmits valid SYN-ACK/ACK control replies through e1000; the
QEMU probe validates the response framing contract.
IPv4 reassembly now has an explicit periodic expiry operation, and the runtime
network service purges stale incomplete datagrams even when no new fragments
arrive.
UHCI HID interrupt reports now feed a persistent preemptible input service task;
PS/2 remains available through its routed interrupt handlers and the shared
kernel input queue.
The e1000 ISR now records delivery, and the post-interrupt-enable QEMU probe
transmits a frame and requires an observed interrupt before continuing.
The e1000 ISR now preserves cleared interrupt causes for the polling/service
path, preventing the accounting read from hiding completion work from service.
e1000 TX completion service now records hardware descriptor error bits instead
of silently treating errored completions as successful reclamation; the QEMU
probe requires a clean completion.
e1000 interrupt handling now enables and accounts for link-state-change and
receive-overrun causes instead of dropping those device events.
The e1000 initialization path now enables the complete declared interrupt
mask, including link-change and receive-overrun causes, so those handlers are
reachable on real hardware rather than only represented in software.
e1000 descriptor publication now uses explicit x86 DMA write/read barriers
around TX doorbells and RX ownership inspection, preventing compiler or CPU
reordering across hardware descriptor transitions.
e1000 TX submission now rechecks the live link state while holding the driver
lock and rejects frames during link-down intervals instead of queueing work
that the controller cannot transmit.
e1000 TX/RX descriptor rings are now accessed through volatile hardware-owned
views, preventing compiler caching of DMA ownership bits in addition to the
explicit CPU ordering barriers.
e1000 RX descriptor recycling now publishes cleared ownership/status fields
with a DMA write barrier before advancing RDT, covering both valid packets and
discarded or malformed packet chains.
RTC CMOS transactions now serialize the index/data port pair with an
IRQ-safe lock, reject malformed BCD digits, and require two matching stable
samples so SMP callers cannot observe a torn calendar value across rollover.
UHCI control transfers now build a bounded multi-packet endpoint-0 TD chain,
validate the setup transfer length, alternate data toggles, and verify every
TD before releasing DMA frames.
UHCI transfers now verify controller HALTED state before releasing transfer
descriptors or data buffers after a stop/timeout path.
UHCI control and interrupt transfers also validate controller-level USB error
status after quiescence instead of trusting TD status alone.
UHCI transfer DMA is quarantined when a post-submit stop cannot confirm HALTED,
disabling further I/O rather than freeing descriptors that may still be owned
by the controller.
UHCI asynchronous transfer completion is now consumed from the IRQ handler and
handed back through the existing poll API, while retaining polling fallback when
the controller does not deliver an interrupt.
USB HID decoding is now an independently compiled and linked driver component;
the USB descriptor parser no longer embeds another source file.
USB endpoint parsing now rejects reserved `wMaxPacketSize` bits and zero
interrupt intervals before publishing endpoint metadata.
USB device-descriptor parsing now validates endpoint-0 maximum packet sizes
against the USB-defined values before accepting a device.
UHCI now programs its PCI legacy IRQ through the shared e1000/UHCI ISR and
enables completion/error causes when ACPI routing is available. The current
QEMU synchronous HID transfer remains the polling path.
Control and interrupt transfer chains now set UHCI interrupt-on-completion on
their terminal TD, allowing the enabled completion IRQ to be generated.
The post-`sti` QEMU control-transfer probe requires the UHCI completion
interrupt counter to advance, validating actual interrupt delivery.
UHCI TD status handling now uses the controller’s low-speed and error-bit
definitions, with low-speed selected from the connected root-port status; the QEMU gate performs a real USB keyboard device-descriptor
control transfer and validates the returned descriptor.
The UHCI path now enumerates the connected device through SET_ADDRESS,
post-address descriptor/configuration reads, and SET_CONFIGURATION; QEMU
validates the configured-device path.
The UHCI driver now exposes bounded interrupt-transfer scheduling with
endpoint toggles; the boot path discovers an interrupt-IN endpoint from the
configuration descriptors and exercises the USB HID polling path.
UHCI interrupt transfers now reject endpoint values before token bit-packing,
preventing invalid endpoint numbers from being silently truncated.
The UHCI data-transfer engine is now also exposed through a bounded bulk-transfer
API with endpoint validation and persistent DATA-toggle ownership.
UHCI interrupt transfers now accept the USB endpoint direction bit while still
validating the four-bit endpoint number, enabling real interrupt-IN HID paths.
The UHCI HID startup probe now retries bounded interrupt polls to absorb
transient device scheduling latency before selecting the fallback status.
UHCI control/interrupt transfers and ISR status acknowledgement now share an
IRQ-safe driver lock, preventing SMP callers and interrupt delivery from
interleaving controller state transitions.
UHCI probing now requires a complete 0x20-byte I/O BAR within the 16-bit x86
I/O-port range, preventing BAR-address truncation from redirecting MMIO-style
accesses to unrelated ports.
The boot path now attempts a HID interrupt poll, tolerates an idle keyboard’s
valid no-report response, and feeds any completed decoded report into the
shared input event queue.
USB HID keyboard decoding now validates and exposes all six boot-report key
slots through a bounded event array; the boot probe covers multi-key reports
and duplicate-key rejection.
USB HID keyboard polling now tracks the previous six-key report and emits
bounded press/release transitions, preventing repeated held-key reports from
being misclassified as new presses.
HID transition emission now timestamps key presses consistently and guards the
fixed 20-event output capacity before publishing a state update.
The HID state tracker now also emits transitions for all eight boot-report
modifier bits using stable modifier key codes.
The shared input queue now supports atomic bounded event batches, so one
decoded mouse or HID report cannot be partially published when the queue is
near capacity.
Framebuffer initialization and pixel operations now share an IRQ-safe lock,
so console and display callers cannot interleave writes to the firmware GOP
surface on SMP systems.
PS/2 keyboard and mouse polling now serialize the shared controller data and
packet state with one IRQ-safe lock, preventing concurrent IRQ paths from
interleaving port reads or partial packet assembly.
USB descriptor endpoint registration now rejects duplicate endpoint addresses,
keeping transfer routing unambiguous for each configured device.
USB HID keyboard and mouse events now sample one monotonic kernel tick per
report, matching PS/2 event timestamp semantics.
PS/2 mouse initialization now verifies the controller auxiliary-port test
before enabling mouse commands, preventing an unavailable second port from
being published as an active input backend.
PS/2 mouse initialization now also issues Get Device ID and accepts only known
three-byte/standard mouse IDs before publishing the backend; extended
four-button protocols remain rejected until their packet formats are supported.
The standard IntelliMouse wheel (device ID 3) now uses a bounded four-byte
packet path and emits a wheel axis event; unsupported five-button packets remain
rejected.
PCI now assigns bounded low-MMIO addresses to unmapped or above-4-GiB memory
BARs, allowing the NVMe admin path to operate under the current identity map.
PCI enumeration now enables memory/I/O space and bus mastering before BAR
drivers probe, making DMA activation an explicit kernel-owned contract.
The device model now rejects duplicate PCI bus/slot/function identities and
duplicate driver names before binding.
Device publication now accepts only supported PCI devices and clears any
caller-supplied driver/resource-owner state before exposing the device to the
binding layer.
PCI device publication now rejects same-space BAR ranges that overflow or
overlap, preventing distinct resource claims from aliasing one hardware range.
The QEMU boot probe exercises rejection of overlapping PCI resource ranges.
Driver registration now rejects empty names and unsupported bus identifiers,
keeping the published driver table compatible with the device model’s actual
binding domain.
The exported PCI configuration accessors now reject invalid slot/function
selectors and unaligned or out-of-range dword offsets before touching the
configuration I/O ports.
Device registration, driver registration, enumeration, and binding now use
dedicated IRQ-safe synchronization, while probe callbacks do not hold the
resource ownership lock.
Driver binding snapshots the published driver table under the registry lock,
so concurrent registration cannot expose a partially written callback entry.
Device resource claim, release, and ownership queries now serialize through an
IRQ-safe ownership lock, preventing concurrent driver probes from claiming the
same BAR.
The block-device registry now rejects duplicate device names and serializes
registration-state lookup before returning stable registered descriptors.
Storage I/O now snapshots the validated callback under the registry lock and
releases that lock before invoking hardware, preventing callback re-entry
deadlocks and keeping registry operations independent of device latency.
The block cache now performs cache-miss reads and write-through device I/O
outside its metadata lock, then merges bounded results under lock.
The cache contract now verifies misses, hits, write-through coherence, sector
invalidation, LRU eviction, and device-wide invalidation against a backing
block device.
Task wait nodes now record their owning queue, so removal from the wrong queue
cannot corrupt either queue's linked list.
Task wait nodes now have their own lock, acquired after the queue lock, so
concurrent moves between different wait queues cannot race ownership metadata
or linked-list pointers.
Wait-node initialization is now explicit and shared by task objects and static
kernel waiters, preventing uninitialized lock state during early boot probes.
Scheduler initialization now precedes process-thread lifecycle operations, so
thread startup and teardown never touch an uninitialized ready queue.
The kernel Makefile now explicitly tracks the wait-queue header for
process-thread compilation, preventing stale object layouts after task ABI
changes.
The FAT32 contract target now links its required spinlock implementation,
keeping the host-side cache-concurrency test independently reproducible.
ACPI IOAPIC discovery now retains bounded multi-IOAPIC GSI ranges and routes
each legacy PCI IRQ through the controller that owns its GSI.
ACPI IOAPIC discovery now derives each controller’s actual redirection range
from its version register, preventing an out-of-range GSI from being assigned
to the preceding controller merely because its base was lower.
ACPI now validates page-aligned LAPIC/IOAPIC MMIO bases and rejects overlapping
IOAPIC GSI ranges before publishing interrupt-routing state.
ACPI intake now bounds RSDP, XSDT, and referenced table lengths and rejects
unsupported or overflowing table addresses before dereferencing them.
ACPI table ranges now also remain wholly below the 4-GiB identity-map limit;
32-bit addresses whose lengths cross that boundary are rejected.
PCI now programs a validated single-entry MSI-X table when available, with
MSI and ACPI IOAPIC fallback retained for devices without usable MSI-X.
PCI MSI-X table validation now rejects I/O BARs and uses overflow-safe resource
offset arithmetic before touching the table entry.
PCI MSI and MSI-X setup now rejects capability offsets whose variable-length
payload would wrap configuration space, and MSI-X rejects table addresses that
would leave the below-4-GiB identity-mapped MMIO window.
PCI capability walking now rejects malformed, unaligned, and out-of-range next
links instead of silently masking them into a different configuration offset.
PCI configuration-port transactions are now serialized with an IRQ-safe lock,
preventing concurrent SMP probes from interleaving `0xCF8/0xCFC` accesses.
PCI enumeration now verifies that memory space, I/O space, and bus mastering
remain enabled after configuration; devices that reject activation are not
published to the driver-binding layer.
NVMe namespace I/O now supports bounded multi-sector transfers across four DMA
pages using PRP1, PRP2, and a bounded PRP list, with a real thirty-two-sector
write/read-back QEMU check. Admin and
namespace queue state is serialized with an interrupt-safe lock; timed-out
commands disable the controller and wait for `CSTS.RDY` to clear before PRP
buffers are released, while completed device-error statuses are consumed
without unnecessarily destroying a healthy queue.
NVMe now has a dedicated MSI/legacy IRQ vector and ISR accounting; the QEMU
gate requires post-`sti` namespace I/O to observe delivery when routing is
enabled. The ISR uses the controller/vector ownership boundary, while the
locked command path remains responsible for phase validation and completion
consumption.
NVMe write APIs now issue and validate a namespace Flush command after data
completion, so successful writes include the device durability boundary.
NVMe timeout handling now quarantines an in-flight PRP when controller abort
cannot confirm `CSTS.RDY=0`, preventing DMA use-after-free and rejecting later
queue commands on the wedged controller.
NVMe recovery now permanently disables a controller after any timeout and
releases admin and I/O queue frames only after `CSTS.RDY=0` confirms quiescence.
NVMe initialization failures now use the same bounded controller-stop contract
before releasing failed admin/I/O queues; if readiness cannot be cleared, the
queues remain quarantined and the controller stays disabled instead of risking
DMA use-after-free.
NVMe public I/O now splits larger valid requests into bounded 8-sector commands,
and QEMU verifies a ten-sector write/read-back across that boundary.
NVMe probing now validates the CAP-advertised doorbell stride against the full
BAR and rejects BAR ranges crossing the supported below-4-GiB MMIO window before
controller enablement.
AHCI now enables controller/port interrupt causes through a dedicated
MSI/legacy vector, acknowledges port causes in its ISR, and the QEMU gate
requires post-`sti` disk I/O to observe delivery when routing is enabled.
AHCI public command paths now serialize command-table/DMA state with an
IRQ-safe driver lock, preventing concurrent callers from reusing active DMA
buffers. Command completion now validates task-file, port interrupt, and SATA
error status; only error causes are cleared during completion so normal
completion causes remain observable by the IRQ path.
AHCI command teardown now uses one ownership helper; a timeout whose port engine
cannot quiesce quarantines further I/O and retains DMA buffers instead of freeing
memory that a wedged HBA could still access.
AHCI now provisions independent command-list and received-FIS memory for every
link-ready implemented SATA port instead of silently skipping all but the first.
AHCI command completions now retain task-file, interrupt-status, and serial-error
diagnostics and count failed completions; the boot disk gate asserts a clean
completion-error count after identify and read/write coverage.
AHCI command-table and PRDT ownership transitions now use explicit x86 DMA
write/read barriers around command issuance and completion observation.
AHCI write entry points now issue and validate ATA FLUSH CACHE commands after
successful DMA writes, extending the durability boundary to AHCI-backed
storage devices.
The storage interface now supports optional per-device contexts, and AHCI
registers each identified port as an independently routable block device;
QEMU verifies secondary-disk read/write through the generic storage layer.
AHCI storage-device publication is now serialized with driver state, preventing
concurrent registration callers from publishing duplicate port devices.
The scheduler now has an explicit core boundary
and round-robin policy module. Process pending/blocked signal state and explicit
termination status are now implemented; signal delivery policy remains bounded
to the kernel process object until userland exists. ACPI parsing is now owned by
the kernel ACPI driver boundary rather than an empty driver scaffold. The boot
path now tracks ordered initialization stages and validates the firmware boot
contract through a dedicated boundary. Initialization-stage transitions now
require the immediate next phase, with a boot-time rejection probe for skipped
phases. CR3 activation is now isolated behind the x86_64 memory architecture
boundary, and the paging primitive is now
independently compiled instead of embedded in the virtual-address-space layer.
The unused BIOS scaffold directory
was removed; BIOS remains a separate later boot path.
The kernel heap now serializes allocation, page commitment, split/coalesce, and
free validation with an IRQ-safe lock for SMP-safe ownership of its block list.
Slab caches now provide the same IRQ-safe serialization for object allocation,
freeing, and availability queries.
Virtual memory mapping, address-space lifecycle, page-flag updates, unmapping,
and user-range validation now share an IRQ-safe address-space lock, protecting
page-table and owned-frame metadata across SMP callers.
User address-space mappings now reject flags outside the defined writable,
user, and executable set, with a boot-time invalid-flag regression probe.
The ELF user-image loader now rejects program-header permission bits outside
the defined read/write/execute flags, with a boot-time malformed-image probe.
It also enforces power-of-two PT_LOAD alignment and file/virtual-offset
congruence, rejecting malformed executable alignment metadata at boot.
Process user stacks now allocate a bounded eight-page writable region with
overflow-safe bounds, partial-allocation rollback, and complete teardown;
the QEMU process probe validates the full stack range.
The task layer now validates an executable entry and writable user stack,
prepares a kernel context associated with the process address space, and
provides a deferred privilege-transition bootstrap path; the boot probe creates
and reclaims this context without starting ring 3.
Process-thread creation now owns that user context directly, requires a READY
process, and exercises invalid-entry rejection plus teardown through the normal
process-thread lifetime path.
User-task bootstrap contexts now retain their owning process and activate that
process before the deferred privilege transition, ensuring future ring-3
syscalls observe the correct process namespace and descriptor table.
User-thread creation now rejects the noncanonical upper boundary at exactly
`0x0001000000000000`, keeping both entry and stack validation within the
currently supported canonical user address range.
Process stack mapping applies the same strict upper-bound check before frame
allocation, preventing a stack range that ends at the noncanonical boundary
from being installed and then rolled back later.
Processes now expose an exit wait primitive: waiters block on a dedicated
queue, termination publishes the exit status and wakes them, and destruction
refuses to reclaim a process while exit waiters remain.
Scheduler current-task, idle-task, preemption, counter, and lifecycle state now
use an IRQ-safe scheduler lock around shared metadata and queue transitions.
Scheduler block and wake transitions now hold that lock across wait-queue
ownership and state publication, preventing a concurrent wake from being lost
between enqueue and `TASK_BLOCKED` publication.
Process-thread startup now uses a scheduler-owned ready-state transition, so a
thread cannot pass a stale state check and be queued after it is already running
or waiting.
Scheduler host-context validity is now tracked separately from transient run
state, preserving return-to-caller behavior across repeated task runs.
Process-table startup now has an explicit initializer that resets table,
current-process, and lock state before process/thread creation.
Process state, image/stack ownership, signal state, and process-owned thread
lists now use per-process IRQ-safe locking, with locked teardown helpers that
avoid recursive lock acquisition.
Process termination now reads current-process ownership through the protected
process-table accessor instead of racing activation against an unlocked
global-pointer read.
Current-process publication and lookup now use the process-table lock, so
syscall and lifecycle paths cannot race on the active process pointer.
Cross-process signal lookup now retains a process reference under the table
lock and releases it after delivery; process teardown drops its table-owned
reference, preventing concurrent target lookup from becoming a use-after-free.
The QEMU process lifecycle probe now retains a target across teardown and
releases it only after table removal, exercising the deferred object lifetime.
Retained process lookup now rejects reference-count saturation instead of
wrapping ownership to zero.
Cross-process signal syscalls now require the same UID or
`SECURITY_CAP_SYS_ADMIN`; self-signaling remains valid for every live process.
Blocking signal waits now use the scheduler wait-queue handoff, and QEMU
verifies a waiter waking on signal delivery before process teardown.
Process termination now wakes all blocked signal waiters, while final process
destruction refuses to reclaim an object that still has signal waiters.
Physical-memory initialization now rejects zero-sized or overflowing kernel
ranges before reserving kernel frames, avoiding bitmap corruption from a bad
firmware boot contract.
VFS node read callbacks and private destructors now publish through the node
lock, preventing concurrent metadata readers from observing partial setup.
VFS child removal now revalidates parent ownership and child emptiness while
holding the parent lock, closing a concurrent mutation window.
VFS path traversal now confines `..` resolution to the caller-supplied root,
including mounted lookups, preventing absolute paths from escaping their VFS
namespace.
VFS child attachment and removal now serialize child ownership and child-count
checks, preventing a node from being concurrently claimed by two directories.
VFS attachment now rejects ancestor cycles, preserving a tree rather than
allowing an ancestor to be inserted below one of its descendants.
VFS reclamation now serializes child-count and private-destructor publication,
then runs destruction outside the node lock before releasing the allocation.
VFS node retain/release now prevents reference resurrection and marks nodes
destroying under lock before private-data teardown, closing the SMP lifetime
race at zero references.
Credential access checks now reject permission values outside the supported
Unix mode mask instead of silently ignoring malformed bits.
The signal-next syscall now validates its writable user destination before
dequeueing a pending signal, preventing invalid pointers from losing signals.
Signal syscall arguments now reject values outside their declared 32-bit and
signal-number ranges instead of silently truncating oversized inputs.
The QEMU kernel probe now verifies that a failed signal dequeue leaves the
pending signal available for a subsequent valid destination.
Stateful read, readdir, and process-wait syscalls now validate writable user
destinations before consuming file state or entering a wait, preventing an
invalid output pointer from losing data or advancing a directory cursor.
File seeking now accepts either read or write descriptor rights, matching the
file-description contract for write-only handles; the QEMU syscall probe covers
repositioning a write-only file before close.
APIC PIT calibration now uses a bounded wait budget and retains a validated
fallback count, preventing slow firmware emulation from blocking kernel boot.
The timer layer now records bounded per-CPU tick counters while retaining the
BSP-derived monotonic clock used by the kernel ABI.
UEFI memory-map retries now release rejected candidate pools and validate boot
services before dereferencing them, keeping ExitBootServices preparation
bounded without leaking retry buffers.
The kernel firmware contract now rejects non-integral memory-map lengths and
kernel ranges outside the supported below-4-GiB physical window, with a boot
path rejection probe.
Successful memory-map replacement now releases the original pool, and the
UEFI loader releases the relocated kernel-file buffer before ExitBootServices.
The ELF loader now falls back from its preferred physical base to a bounded
below-4-GiB allocation when that address is unavailable.
UEFI GOP handoff now validates pitch/height multiplication, framebuffer size,
and address-range overflow before publishing framebuffer metadata.
UEFI and kernel framebuffer handoff now also require the complete published
pixel span to remain below 4 GiB, matching the kernel’s current identity map.
The framebuffer surface independently rejects overflowing row geometry and uses
64-bit pixel offsets for clear, pixel, and rectangle operations.
UEFI failure paths now close opened filesystem handles, release pool buffers,
and free rejected kernel-page allocations, preventing boot-time resource leaks.
UEFI ELF validation now rejects writable-executable load segments before kernel
memory allocation, preserving a W^X boot image contract.

## Phase 9 — Userspace boundary

Design and implement syscall entry/exit, ABI validation, process address spaces, executable loading, privilege transitions, process/thread semantics, handles or descriptor policy, signals/events as selected, and safe copy-to/from-user mechanisms.

## Kernel completion gate — required before userland

`userland/` remains empty until the kernel is complete as a usable operating-system kernel. Every planned kernel subsystem and directory must either contain its real implementation, build integration, focused tests, and QEMU/integration evidence, or be deliberately removed from the architecture and roadmap. Empty directories are roadmap scaffolding, not completed work.

Process thread ownership now enforces nonzero per-process thread IDs and
rejects duplicate IDs before allocation; callers can resolve an owned thread
through `process_thread_lookup`.
Thread lookup now returns a retained reference and exposes an explicit release
operation, allowing thread teardown to unlink and destroy its task without
freeing a concurrently looked-up thread object.
Thread retained-lookup acquisition rejects reference-count saturation instead
of wrapping ownership to zero.
Process destruction refuses to reclaim a process while any retained thread
lookup remains, preserving the owning process lifetime until thread release.
The process also tracks retained lookups after thread unlink, so destroying a
thread cannot hide a live thread reference from process teardown.
The boot probe verifies process destruction is rejected until an unlinked
retained thread reference is released.

AP IDT gates now use the selector belonging to the active AP trampoline code
segment, while the BSP retains its kernel selector; the two-CPU QEMU gate
reaches the AP online marker without a table-load fault.

x86-64 exception stubs now normalize CPU-pushed and synthetic error-code
frames before entering the panic path. Panic diagnostics report the fault
vector, error code, instruction pointer, code selector, and flags, preserving
actionable architectural state for later crash-dump and recovery work.
The same exception entry path now preserves all general-purpose registers and
prints them in a deterministic order, making architectural fault reports
useful for postmortem debugging without changing the normal interrupt return
path.

This gate includes all non-driver kernel core services and VFS abstractions,
then the complete driver phase, with build integration, focused tests, and QEMU
evidence for each. The syscall/ring-3 probe and ATA driver now have runtime
evidence, but individual milestone evidence does not by itself pass this gate.

## Filesystem milestone before drivers

FAT32 and exFAT have filesystem parsers and VFS file adapters. ExFAT remains
bounded-write capable for existing file extents, while FAT32 supports bounded in-place writes to existing file
extents, including sector and cluster crossings, through its VFS adapter;
FAT32 append growth now allocates free clusters, mirrors FAT updates, updates
the directory size/first-cluster fields, and rolls back new chain state when
metadata or data writes fail. FAT32 truncation now safely shrinks regular files,
updates directory metadata, terminates the retained chain, and releases detached
clusters; arbitrary hole-punching remains later.
The FAT32 VFS adapter now exposes bounded in-place writes, append growth, and
safe shrink/truncation, serializing per-file size updates around the filesystem
write path. VFS nodes now have an explicit regular-file truncation callback.
The FAT32 contract now exercises append growth through `vfs_node_write()` and
reads the appended bytes back through the VFS adapter.
Image validation now also runs `fsck.fat -n` when available, adding a real
filesystem-consistency check for the Windows-mountable FAT32 artifact.
FAT32 in-place writes now serialize the complete read-modify-write operation
with a filesystem write lock, preventing concurrent writers from interleaving
sector updates.
FAT32 mount validation now uses 64-bit geometry arithmetic, verifies the image
fits its registered block device, and rejects FAT tables too small for the
published cluster count.
FAT32 FAT-cache and cluster-chain traversal now use a per-filesystem IRQ-safe
lock, preventing concurrent readers from mixing cached FAT sectors.
ExFAT mount validation now rejects invalid shift fields before evaluation and
requires the FAT region to contain entries for every declared cluster.
ExFAT VFS regular files now support bounded sector read-modify-write updates
within existing contiguous or FAT-linked extents; allocation, append, and
truncate remain separate metadata operations.
The exFAT contract now exercises an actual write followed by a readback on the
synthetic filesystem image.
FAT32, exFAT, ext4, XFS, and Btrfs VFS file adapters now serialize direct node
reads and writes with per-file IRQ-safe locks, so concurrent filesystem users
cannot interleave adapter operations.
ExFAT lookup now accepts zero-length regular files, including files with no
allocated data cluster, and the contract covers this case.
validates the primary and backup boot-region checksums, supports bounded
directory-relative lookup and reads, validates directory entry-set checksums,
and compares validated UTF-8 names with their on-disk UTF-16 names. Ext4 now has
inode, directory, direct/indirect-block reads, and extent-tree file
reads through a VFS adapter. Ext4 now resolves inode tables across multiple
block groups, supports 64-bit block-count/inode-table metadata, and combines
the on-disk low/high inode size fields. XFS and
Ext4 mount and directory paths now reject oversized block-size exponents,
device-capacity mismatches, arithmetic overflow, and truncated directory block
counts before issuing block I/O.
Ext4 VFS regular files now support bounded read-modify-write updates to already
allocated direct, indirect, or extent-mapped blocks; sparse allocation and
file-size-changing metadata operations remain separate.
Btrfs now has strict superblock and geometry
mount layers with contract tests. XFS now maps allocation-group inode numbers,
reads v1/v2 inodes, supports short-form directories, inline files, extent
records, and a VFS file adapter. XFS extent reads now decode the extent-state
flag correctly and return zeroes for sparse gaps and unwritten extents. XFS
regular files now support bounded read-modify-write updates for inline data and
mapped written extents through the VFS adapter; sparse and unwritten extents
remain protected.
Btrfs regular files now support bounded read-modify-write updates for
uncompressed, mapped extents, publish mirrored data and checksum-tree metadata
when available, and update the checksum-tree entry; compressed, sparse, and metadata-changing writes remain
protected.
Btrfs inline regular-file data now also supports bounded in-place updates with
tree-node checksum regeneration; allocation, compression, and size-changing
metadata operations remain separate.
Btrfs VFS file attachment now accepts an explicit on-disk directory inode,
allowing validated files below the filesystem root to be exposed through the
same adapter while preserving the inode permission bits.
XFS VFS file attachment now likewise accepts an explicit directory inode, so
validated files in nested short-form directories use the same adapter boundary.
Ext4 VFS file attachment now accepts an explicit directory inode and validates
the inode type while carrying its on-disk permission bits into the VFS node.
Btrfs
validates its CRC32C superblock checksum
and supported checksum type, validates tree-node checksums/identity, and reads
bounded leaf items through single-stripe system-chunk logical-to-physical
mapping, loads additional single-stripe mappings from the chunk tree, resolves
the standard FS_TREE root item, validates CRC32C checksums for filesystem
sector data through the checksum tree, and selects a valid primary or mirror
superblock. Btrfs
mapping and checksum paths now use strict chunk-end semantics and reject
address arithmetic overflow before fallback reads.
now decodes bounded inline and uncompressed regular EXTENT_DATA records,
extracts inode metadata, and reads mapped data including unaligned byte
ranges, sparse holes, and multi-extent files. Btrfs now performs hashed
DIR_ITEM lookup with variable-length entry checks and packed hash-collision
entries; its multi-extent read path is integrated into a VFS file adapter.
Redundant same-device chunk layouts are now accepted by selecting a
validated mirror stripe with read fallback after checksum failure. Btrfs now
resolves matching filesystem devices by device ID and maps bounded RAID1
mirrors across separate registered storage devices, with the same read
fallback behavior. Encryption remains outside the current filesystem
milestone. Btrfs regular extents now support bounded zlib-wrapped
DEFLATE streams, including stored, fixed-Huffman, and dynamic-Huffman blocks,
with Adler-32 validation and sector-checksum-protected input. Additional
compression formats remain bounded and read-only by design.
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
The Btrfs extent path now dispatches Zstandard records and has a
sector-checksummed compressed-extent contract.
The predefined LL/offset/ML distributions and RLE sequence-table mode are now
implemented and contracted. A shared sequence-table selector now handles
predefined, RLE, FSE-compressed, and repeat modes with bounded consumption.
Zstandard sequence-section count/mode header parsing is now implemented and
contracted; checksum verification now uses the format's XXH64 low-32-bit value
and is covered by both a generated compressed frame and a checksum rejection
case. A generated FSE-weight literal vector now exercises the complete
compressed-literal, sequence, match-history, and checksum path. Four-stream
literal blocks and all compressed-literal size formats are now decoded with
bounded jump-table and per-stream output validation.
Treeless literals now reuse validated Huffman weights across compressed blocks;
sequence FSE tables and repeat-offset history also persist across blocks, and
cross-block matches are bounded against the cumulative frame output. The
generated multi-block contract covers this path, including offset extra-bit
widths beyond 16 bits.
The bounded overlap-safe match-copy primitive is now
implemented and contracted, including destination-underflow rejection.
Zstandard sequence code expansion for literal lengths, match lengths, and
offsets is now implemented and contracted. Sequence execution now copies
literal runs and performs validated overlapping matches with persistent repeat
offset history; FSE symbol wiring and complete compressed-block sequence
integration are now exercised by the generated frame. FSE states are now
wired into sequence decoding with offset, match-length, and literal-length
extra-bit consumption and optional state updates.
The block-output layer now executes all decoded sequences and appends trailing
literals with bounded total-output validation.
Zstandard sequence-table preparation now consumes and validates LL/offset/ML
mode tables in order, including inherited repeat tables.
The multi-sequence coordinator now decodes exactly the declared count and
applies the final-sequence state-update rule.
The network service now generates bounded ICMP echo replies from validated
Ethernet/IPv4 requests, swapping addresses and preserving identifiers,
sequences, and payloads through the complete protocol stack.
It now also answers validated ARP requests for the configured local IPv4
address with correctly addressed Ethernet/ARP replies.
ARP parsing accepts the Ethernet minimum-frame padding while still requiring
the complete fixed-size ARP wire payload.
AHCI sector I/O now uses ATA 48-bit DMA commands and rejects LBAs outside the
48-bit address range instead of silently truncating them.
NVMe I/O now enforces the namespace size reported by IDENTIFY before PRP
allocation and command submission.
PS/2 mouse packets now have a validated decoder that rejects bad sync and
overflow bits and emits button, X-axis, and Y-axis input events.
The input queue now rejects event-type values outside the enum range,
including negative/corrupt values, with a boot-time regression probe.
Keyboard polling now checks the controller source bit, so a keyboard IRQ path
cannot consume mouse data from the shared output buffer; mouse status is read
once per poll and packet state is reset when mouse scanning is enabled.
USB HID boot-mouse reports now decode into validated button, X-axis, and
Y-axis input events.
The network service now drains bounded NIC input, learns validated ARP
senders, and transmits ARP and ICMP echo replies through the adapter boundary.
The generic block layer now has a storage-backed adapter, with boot-time
verification reading the FAT32 image through the registered storage device.
Mounted VFS path traversal now lets `..` escape a mounted filesystem root back
to its mountpoint hierarchy.
The kernel now reads and validates the CMOS real-time clock with bounded update
window handling, BCD/binary decoding, and 12/24-hour conversion.
ACPI now validates and exposes the firmware reset register as a guarded kernel
service when the FADT supplies a supported system-I/O GAS, and reports the
capability absence without treating optional firmware support as a boot error.
ACPI reset publication now requires the FADT reset GAS to describe an exact
8-bit, byte-access system-I/O register, matching the kernel’s reset write.
The VFS now has a common bounded filesystem probe dispatcher covering FAT32,
exFAT, ext4, XFS, and Btrfs above the storage-device interface.
The block cache now supports whole-device invalidation for storage lifecycle
events and boot validates refetch after invalidation.
VFS node destruction now retries after the final child is removed, preventing
directories released before their children from becoming permanent leaks.
VFS now exposes retained directory-child iteration for safe enumeration by
filesystem adapters and future userland traversal.
VFS removal now unlocks both nodes when a non-child is supplied, with a boot
regression probe covering that failure path before later traversal operations.
VFS mount validation now rejects self-mounts and mounts whose root contains the
mountpoint, preventing cyclic mounted-path traversal.
Mounted `..` traversal now snapshots the mountpoint parent under the node lock
and acquires its reference before releasing that lock, preventing a concurrent
detach from producing a stale parent pointer.
VFS child attach and removal now use a canonical node lock order, while child
lookups rely on the parent structural lock, preventing parent/child lock
inversion during concurrent tree mutation; lookup reference acquisition remains
atomic against independent node release operations. Relative `..` traversal
also snapshots the parent under the child lock before acquiring its reference,
closing the stale-parent window during concurrent detach.
VFS nodes now expose bounded write callbacks alongside reads, with boot-time
write-path validation.
Physical frame allocation now zero-fills reclaimed pages, with boot-time
reallocation validation preventing stale frame contents from crossing owners.
The physical allocator now supports zero-filled contiguous frame runs with
range-checked release for future DMA allocations.
Oversized contiguous-frame requests are now rejected before the bitmap scan,
preventing an unsigned scan-bound underflow from walking outside the allocator
range.
UHCI and e1000 now use an explicit shared PCI interrupt dispatcher because
legacy IRQ lines may be shared; each handler independently filters its device
status before acknowledging its own controller.
The UHCI QEMU gate now distinguishes successful control-transfer support and
IRQ-path configuration from optional IOC delivery, which this emulated device
does not raise reliably.
UHCI interrupt-IN transfers now have a persistent bounded frame-list slot and
completion poller; idle NAKs remain scheduled, while completed reports are
copied, toggles advanced, and DMA frames reclaimed before resubmission.
The persistent transfer slot now retains the full bounded packet-count range,
including 4096-byte transfers split across one-byte maximum packets; both
synchronous and persistent UHCI paths allocate enough contiguous TD frames for
that range instead of overflowing a single 4 KiB descriptor page.
Persistent interrupt scheduling now honors each USB interrupt endpoint's
declared `bInterval` instead of submitting the queue head on every frame.
UHCI bulk transfers now have a dedicated validated submission path and
asynchronous queue-head anchor rather than entering the periodic interrupt
schedule; bulk queue ownership is isolated from HID interrupt transfers.
UHCI frame-list, queue-head, and transfer-descriptor ownership now uses
explicit x86 DMA write/read barriers across controller start and completion
polling.
UHCI bulk submission now accepts standards-valid zero-length packets as a
 single TD, while retaining strict nonzero payload validation for interrupt
 transfers.
USB endpoint descriptor validation now rejects packet sizes that UHCI cannot
represent and rejects nonstandard full-speed bulk packet sizes before endpoint
metadata reaches a transfer driver.
NVMe interrupt completion state is now published with atomic ordering, and
interrupt accounting no longer races with SMP readers; command waiters clear
the pending completion marker when consuming a CQ entry.
NVMe admin, I/O, and flush completions now retain the last device status and
count non-success completions, with the boot storage gate asserting a clean
completion-error count after its read/write coverage.
NVMe submission queues and completion queues now use explicit x86 DMA
write/read barriers around doorbells and ownership polling.
NVMe admin command submission now retains a volatile hardware-owned command
view through field writes, preventing compiler caching of queue entries before
the submission doorbell.
PCI bridge enumeration now walks each validated secondary-to-subordinate bus
range with the existing cycle guard, instead of probing only the first child
bus and silently missing devices behind deeper bridge topologies.
Bridge traversal also validates the bridge's primary-bus field against the
currently scanned bus before following child ranges, rejecting malformed
firmware topology metadata without probing an unrelated bus.
The kernel now exercises its privilege boundary with a mapped user image that
enters ring 3, invokes the user-callable exit gate, returns through the
scheduler, and is reaped without populating `userland/`.
The ring-3 probe now performs a real user `write` syscall from a user-mapped
buffer before exiting, covering the syscall register ABI and copy-from-user
path during the same scheduler return test.
Process exit now clears the global current-process context before waking
waiters, and the ring-3 gate verifies that exited processes are not reused by
subsequent kernel work.
The ring-3 lifecycle gate now also reaps the exited process, releasing its
user image, address space, stack pages, handles, and registry entry.
The virtual-memory layer now exposes an explicit kernel-root activation path so
an exited user address space is never destroyed while it remains the active
page root.
RTC initialization now explicitly initializes its CMOS transaction lock, and
RTC reads require two bounded, matching full-date samples so midnight/year
rollovers cannot publish mixed calendar fields.
The ring-3 entry stub now initializes user argument registers instead of
leaking the kernel bootstrap's entry pointer into the first syscall argument;
the exit transition probe validates the corrected boundary.
User-task startup now recognizes an already-active address space instead of
rejecting the process and exiting before the first ring-3 instruction.
AHCI now preserves each port's interrupt-status bits across IRQ acknowledgement
until the active command consumes them, preventing the interrupt handler from
erasing DMA error evidence before completion validation.
The UEFI memory-map capture now rejects malformed successful firmware results
unless descriptor size and map length satisfy the boot contract, and bounds
replacement-map publication to the allocated buffer.
The UEFI loader now retries the final `ExitBootServices` handoff across a
bounded series of freshly captured memory-map keys, covering transient map-key
invalidation without looping indefinitely.
Timer waits now clamp intervals to the signed wrap-safe range, preventing an
overflowed deadline from being interpreted as an already-completed wait.

## Phase 10 — Userland begins after the kernel gate

Only after the kernel completion gate passes populate `userland/`. Begin with the minimum runtime and init environment needed to exercise the real kernel ABI, then expand toward libraries, shell/tooling, and system services.

## Phase 10 — BIOS boot path

Implement legacy BIOS support as a separate loader path. It should normalize legacy machine startup into the same kernel boot contract used by UEFI rather than creating a BIOS-specific kernel architecture.

## Long-term work

Continue hardening SMP, memory reclamation, storage, USB, networking, graphics, security, debugging, crash diagnostics, performance tooling, power management, hardware compatibility, and userland while maintaining explicit subsystem boundaries and automated regression coverage.
