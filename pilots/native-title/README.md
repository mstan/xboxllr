# Native title reference pilot

`beads-xbox.1.61`, 2026-10-06. This standalone experiment uses Phobos
`63f18f0334fbd8c00313c4bb565accf00627a4fd` discovery, lifting and CPU helper header.
It is outside the normal xboxlle build. **The generated executable is a private
GPL-dependent reference artifact, not the intended non-GPL shipping product.**
See [license scope](../../LICENSE-SCOPE.md). There is no install/package target.

## Observed result

The original NTSC Blinx XDK 4831 and JSRF XDK 4134 both execute their own XBE
entries, create real host-backed title threads, and run early XAPI startup.
Blinx now passes timer arming, its initial CRT heap reservation/commit, and
installed-media verification against the actual NAS content. It passes the HDD
allocation-unit check, creates per-title TDATA/UDATA and persists 72 bytes of
`TitleMeta.xbx`. A fresh launch now loads the deferred title-image section,
writes `TitleImage.xbx` byte-for-byte equal to its original 10240-byte payload,
and unloads the section. A repeated launch preserves the file and skips that
copy through the title's own code. Both now reach raw HDD partition 0 during
utility-drive setup. The OPTICAL build retains
and executes the original verification helper, stopping on unsupported raw
optical access. No graphics, movies, menu or level yet. Function-entry counts
are diagnostics, not coverage or performance measurements.

The owner's current goal is serial: complete Blinx before further JSRF work.
JSRF's last measured result remains 12 title-thread entries at
`NtAllocateVirtualMemory`; it has not been rebuilt/run against the new VM code.

The native implementations reached so far cover thread creation, thread-handle
closure, recursive critical sections, DPC/timer structure initialization,
EEPROM user-settings queries and NTSTATUS-to-DOS-error conversion. Timer arming,
expiry and DPC delivery are implemented as described below. Callback delivery
is exercised by the contract tests. No callbacks, draws or completions are discarded.

Nine contract tests pass: concurrent threads with separate registers/TLS and
recursive exclusion, handle closure/errors and EEPROM buffer/checksum behavior;
timer/DPC/event-wait contracts; real VM reservation/commit/free/protection/query;
installed-media identity and actual file I/O; writable storage and link lifetimes;
deferred section residency and shared-page ownership;
plus explicit instruction, indirect-call and
kernel-call failure paths. Passing
the other title's XBE to the Blinx artifact is rejected before guest execution.
The main LLE build's runtime source and build selection are untouched.

## Reuse and scope

- Phobos supplies the actual x86 discovery/lowering and register ABI. Both
  titles used their own entries and discoveries, with no donor address seeds.
  Each generated set had 14 unresolved direct targets. Their success-return
  bodies are replaced by terminal unsupported faults. Unknown indirect calls,
  unsupported instructions and missing services also stop.
- Phobos's host-lock mapping and kernel bridge signatures inform the adapter.
  Its kernel/device runtime is not linked. Cxbx `585c49a` supplies Xbox
  `RTL_CRITICAL_SECTION`, `KDPC`, `KTIMER` and EEPROM field contracts. For example,
  the Xbox critical section is 28 bytes; the timer header size is ten DWORDs.
  Host Windows structs are not written over guest structs.
- Only preload XBE sections are committed initially; Blinx's 42 deferred
  sections and JSRF's two are not loaded ahead of time. Shared boundary pages
  can already be committed. Explicit section load/unload now owns deferred
  residency as described below. JSRF has not been run against this new service.
- Each created thread receives its own stack, TLS template/zero-fill, KPCR,
  thread identity and CPU register state. Diagnostics are thread-local too.
  The bootstrap may close its thread handle and return while the title runs.
- The exploratory launch environment reports retail kernel 1.0.5838.1 and
  NV2A A1/MCPX B1 identity. This does not certify every export of that kernel.
  User settings come from the supplied 256-byte EEPROM with checksum handling.
  Encrypted/factory queries and other data exports remain unsupported.
- Native VM reservation/commit/free/protection/query is bounded as described
  below. Complete retail physical RAM accounting, scheduling/IRQL and exceptions
  are not yet qualified. Donor double-backed x87 and other CPU limits remain.
  PUSHFD/POPFD stop because arithmetic flags are not exported/imported by the
  current donor helpers. This is not a faithful replacement for the CPU floor.
- Compilation disables host optimization for short bring-up cycles. No FPS,
  full AOT coverage, or D3D8 HLE performance claim follows from this result.

The next Blinx dependency is utility-drive cache selection/mounting. Its
symbol-identified XapiSelectCachePartition currently opens raw HDD partition 0;
a genuine native cache service is the next natural library boundary.
The native installed-media contract below replaces the earlier optical check
with the owner's explicit authorization. The original check requests SCSI MODE
SENSE page 0x3e through IOCTL 0x4d014; that IOCTL has not executed locally. The NAS
contains both the original extracted title and ISO, plus a separate Rev 1 ISO;
pin the original input, not Rev 1. Use existing references, keeping GPL bodies external.
The inspected MIT sp00nznet code is a candidate shipping implementation source
after file provenance checks. D3D8 remains the performance target; first-scene
bindings still need Blinx's local indexed draws and JSRF's older GetBackBuffer
out-pointer ABI. A complete reference machine is not a prerequisite to those
HLE implementations, and dashboard speed does not gate title work.

## Installed-media contract

`media.inc` replaces the complete reviewed XAPI media-verification helper,
**not** `NtDeviceIoControlFile`. The original boundary takes no arguments,
returns NTSTATUS in EAX, consumes the four-byte return address, preserves
EBX/ESI/EDI/EBP, and returns no guest resource. Its temporary device handle does
not escape. The installed implementation keeps that interface and has a distinct,
explicitly authorized policy: matching installed content is accepted without
proving physical DVD authentication. It does not populate drive packets.

The mounted directory must open, `default.xbe` must be readable, and its streamed
SHA-256 must equal the exact image used to generate this executable. A successful
mount retains real root/identity handles for the process lifetime, denying
write/delete sharing on the identity. Repeat calls revalidate rather than
allocating more handles. Missing media returns STATUS_NO_MEDIA_IN_DEVICE;
wrong identity returns STATUS_WRONG_VOLUME; failed file access retains the host
NT error where available. All other assets are read on demand; their presence
and identity have **not** been certified by checking `default.xbe` alone.

Native file services implement read-only synchronous `NtOpenFile`, FILE_OPEN
through `NtCreateFile`, `NtReadFile`, standard/position query, position set and
`NtClose`. The host NT I/O manager supplies real seek/read/EOF/share semantics;
the adapter owns guest handles and marshals the eight-byte Xbox IO_STATUS_BLOCK.
Directory-relative opens survive closing the parent handle. Guest paths accept
ASCII D: and Device/CdRom0 names; traversal, alternate streams and reparse
traversal are rejected. The byte source is the actual installed directory.
Read-only media never becomes a save/cache directory.

Pending work remains explicit: async I/O and APC/event delivery, directory
enumeration, additional info classes, raw optical access,
and non-ASCII Xbox path mapping. Unsupported modes stop. The host filesystem
supplies file allocation metadata; FATX/XDVDFS allocation equivalence is not
yet qualified. Invalid guest-pointer exception recovery is not implemented.
HDD logical-volume geometry is explicitly translated as described below.

`tools/native_media_binding.py` checks generated-artifact integrity, identifies
the reviewed helper by its original byte hash plus whole-XBE hash, and requires
its discovered XapiInitProcess caller. It renames only the private generated
definition and emits a build-selected wrapper; direct and indirect callers keep
the same symbol. Shared service code has no revision addresses. Unknown images
or helper bodies fail binding rather than guessing. A new revision needs a new
contract review. Original generated code remains in the optical build.

For an existing private generated Blinx tree:

```powershell
py -3 tools/native_media_binding.py --xbe <original-default.xbe> --output build/blinx --symbols <default_xdk_symbols.json>
& 'C:\Program Files\CMake\bin\cmake.exe' -S pilots/native-title -B build/blinx/host -DPILOT_MEDIA=INSTALLED
& 'C:\Program Files\CMake\bin\cmake.exe' --build build/blinx/host --config Release --parallel 2
& build/blinx/host/Release/xlle_native_title.exe <original-default.xbe> <eeprom.bin> <installed-content-directory>
```

Fresh generation accepts `--media-symbols <default_xdk_symbols.json>` with the
usual `tools/native_pilot.py` arguments. Configure `-DPILOT_MEDIA=OPTICAL` to
select the original verification routine. Neither a launch flag nor an
environment variable switches implementations. Only the content path is a
launch input. Both configurations were built and run. The OPTICAL configuration
currently stops at unsupported raw optical access: preserved executable code
and ABI inspection are **not an executed LLE authentication equivalence proof**.

Validation: seven CTests, repeated actual NAS-backed Blinx startup, missing-media
title failure, unknown-binding rejection, and the original optical routine run.
The media contract test covers wrong/missing identity, nonvolatile registers and
stack, write denial, real bytes/EOF/position, bounded query writes, root-relative
handles, stale handles and 300 opens/closes without host-handle growth. Actual
missing-content startup returns NO_MEDIA but its subsequent error UI faults on
an unsupported kernel data import; that UI is not validated. No FPS claim.

References: MIT sp00nznet `1409a7d` file/path services supplied architecture and
pitfalls; [nxdk's Xbox declarations](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h)
and [Microsoft NtReadFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile)
describe the ABI and native file-position contract. No donor service bodies were
imported. The private Phobos CPU-helper licensing limitation is unchanged.

## Native HDD and namespace contract

The optional fifth argument is an **existing local writable state directory**.
The runtime creates/opens real `Partition1` through `Partition5` directories
there. It rejects a remote/reparse root and a root overlapping installed content.
Use an isolated directory such as `build/blinx/storage`; no source media or LLE
disk-image writes are required. Mounts are selected by actual guest device paths
and inherited through relative directory handles. There are no title addresses
or fixed title IDs in the service. For example:

```powershell
New-Item -ItemType Directory -Force build/blinx/storage | Out-Null
& build/blinx/host/Release/xlle_native_title.exe <original-default.xbe> <eeprom.bin> <installed-content-directory> build/blinx/storage
```

Create dispositions, writes, flushes, allocation/end-of-file/disposition changes
and file metadata operate through native NT services on owned handles. Host
failures propagate; there is no flush-success fallback. Standard, position and
network-open query structures use aligned host staging because the Xbox caller
can provide a four-byte-aligned stack object while desktop NT requires eight.
For metadata query compatibility, native handles also request READ_ATTRIBUTES;
this does not grant READ_DATA to a guest write-only handle. Tests exercise both.
Guest IO_STATUS_BLOCK remains eight bytes, and no native pointers escape.

The HLE volume policy uses 512-byte sectors and 32-sector logical allocation
units. Total/free byte capacities come from `NtQueryVolumeInformationFile` on
the **actual caller's mounted handle**, converted down to 16 KiB units with
less than one unit of rounding. Query failure stays failure. Native partitions
share host capacity; stock partition quotas are not emulated. File allocation
metadata and timestamps keep host semantics, so exact FATX space consumption
and timestamps are not qualified. The existing LLE image's E: FATX header was
read without mutation and reports 32 sectors per cluster too; this supports the
unit choice but does not constitute a running LLE API differential. These
policies are explicit HLE tolerances, not claims about physical host geometry.

`namespace.inc` owns actual case-insensitive symbolic-link objects. Create
detects collisions; delete removes the name; previously opened query handles
retain the old target even when that name is recreated. Closing releases object
ownership. Short target buffers report required length without overwriting
the buffer. File opens resolve the current namespace, including chained links,
with bounded cycle failure. D:, T: and U: are established by the title's actual
calls rather than silently recreated after deletion. ASCII names and absolute
link objects are the current bounded scope; other object types remain unsupported.
The adapter also marshals RtlInitAnsiString/RtlEqualString through host NT RTL.

Eight CTests pass, including file bytes persisted across close and independently
read by the host, flush/truncation, volume rounding, aligned/unaligned metadata,
access isolation, bounded writes, link handle lifetime and 300 link lifecycle
iterations. Actual fresh and repeated Blinx launches preserve the same 72-byte
TitleMeta content and reach the same required deferred-section load. This proves
startup progress, not complete save-game, cache-partition or attract-loop support.

Donor comparison: sp00nznet `1409a7d` and Phobos `63f18f0` encode the 16 KiB
startup requirement; the inspected NoRain code still reports 4 KiB. All three
have a fixed-capacity success fallback on host query failure and ignore errors
from host flush. Those behaviors are not adopted. The useful native-volume,
path and symbolic-link approach is adapted to the bounded ABI above using host
NT services; no GPL service bodies or libfatx implementation are imported.
[NtQueryVolumeInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvolumeinformationfile)
and [FILE_FS_SIZE_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_fs_size_information)
document the host contract; nxdk declares the Xbox structures and ordinals.

## Deferred section residency

`sections.inc` maintains per-section references and a separate page-ownership
ledger for the immutable, identity-checked XBE. Initial preload sections and
header pages own their actual page ranges; exported 16-bit boundary counters
are initialized accordingly. The old loader had set section references without
initializing those boundary counters. Deferred sections gain ownership only on
their first successful load. The same API handles any discovered section; no
title-specific addresses or section IDs appear in its implementation.

On the first load, real host pages are committed, raw bytes are copied from the
verified input, and the virtual tail is zero-filled. Additional references do
not overwrite modified bytes or add physical ownership. Final unload decommits
only pages without another section/header owner. Fragments on still-shared pages
remain unchanged, including the unloading section's fragment. A subsequent
load restores the original payload and zero-fill. Guest section and boundary
counts track successful state changes; failed admission preserves them.

The adapter shares the existing 64 MiB commitment admission check with native
VM operations. That remains a bounded host-commit count, not complete retail
physical accounting. Image pages retain the pilot's RW backing; XBE read-only/
execute permission enforcement, loaded-code invalidation and full guest fault
delivery are not qualified. External edits to section reference fields stop
explicitly. The API is bounded to original validated image descriptors and
non-overlapping section byte ranges, while page overlap is supported.

Nine CTests pass. The section test covers real data, zero-fill, repeated-reference
preservation, reload, shared head/tail pages, aliased boundary counters, actual
decommit faults, concurrent load/unload ownership, invalid descriptors, overflow
and memory-budget failure preservation. Actual Blinx performs a load, a real
file write and an unload; a separate host comparison proves all 10240 output
bytes equal the original section. A second process run keeps the same file.
This does not yet validate executable-overlay use or the full attract sequence.

nxdk supplies the function declarations. Cxbx's section implementation was an
external behavioral reference for exported counters and shared-page lifetime;
its implementation is not imported. The inspected sp00nznet section routines
only change counts under an all-resident loader assumption, so they were not
adopted. Whole-image identity verification protects the immutable byte source;
the pilot does not separately emulate retail section-signature verification.

## Native virtual-memory checkpoint

`virtual_memory.inc` owns guest reservation ranges, delegating real page
commitment, protection and decommit to the host NT services. Xbox NT functions
have fewer arguments than desktop NT; the adapter marshals guest 32-bit fields
and returns a 28-byte memory-query structure. Successful reservation does not
commit or clear its pages. Recommit preserves existing bytes; decommit followed
by recommit zero-fills. Release removes guest ownership and allows safe reuse.
Reservations use 64 KiB alignment, commitment uses 4 KiB pages, and ZeroBits /
top-down allocation constrain guest VA selection. Failed calls retain outputs.

The XBE image range, including deferred sections, and the synthetic execution
contexts are excluded from heap allocation. A 64 MiB ceiling counts observed
host-committed guest pages, including loaded image/stack pages, when admitting
VM commits. This is **not complete retail physical-page accounting**: kernel
residency, page tables, physical contiguity and concurrent synthetic context
allocations are not modeled by it. Reservations may exceed physical RAM; tests
reserve 128 MiB without committing it. At most 512 reservation records exist.

Tests check real inaccessible/read-only access faults under MSVC, page rounding,
query boundaries, retained neighbors, decommit/recommit zero-fill, release/reuse,
allocation conflicts, address limits, budget failure and ABI stack cleanup.
Guard/cache protections and reset/nozero/large-page allocations stop explicitly;
image/synthetic-context protection and lifecycle are outside this heap scope.
This is API/host-backed qualification, not an executed LLE differential test or
a complete guest MMU. The existing private CPU helper limitations still apply.

References: nxdk Xbox prototypes/flags, Cxbx guest ranges/alignment, and Microsoft
[NtAllocateVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntallocatevirtualmemory),
[NtFreeVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntfreevirtualmemory),
[NtQueryVirtualMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryvirtualmemory).
The sp00nznet `1409a7d` memory bridge was inspected; its commit-as-no-op and
reservation-clamping behavior were not adopted. No donor memory bodies imported.

## Native timer/DPC checkpoint

`dispatcher.inc` independently implements the bounded caller contract using
Windows waitable timers, a serialized DPC worker with its own guest stack/KPCR,
and synchronized guest dispatcher state. The host timer primitives preserve
absolute wall-clock versus relative interval semantics without a 10 ms polling
loop. Guest waits subscribe while holding the same lock used to change state;
successful waits reserve their result before a producer can reset the event.

- Timer initialization/set/set-ex/cancel, one-shot and periodic expiry. Rearming
  clears signal state and replaces the previous deadline. Cancellation removes
  future expiry, preserving already queued/running DPC work and signal state.
- DPC initialization/insertion/removal and `KeIsExecutingDpc`. Callbacks receive
  their object/context/arguments, with timer callbacks receiving system time.
  Duplicate pending insertion is rejected; callbacks may cancel or rearm timers.
- Notification/synchronization event initialization, set/reset and timer/event
  single/multiple waits. WaitAll consumes synchronization signals only once all
  objects are ready. Timeouts use actual absolute/relative host deadlines.
- Tests execute real callbacks, periodic self-cancellation, queued callback
  cancellation races and real blocked waiters. They check ABI stack cleanup,
  guest fields, callback arguments, caller-register isolation and timeout results.

This is bounded HLE qualification against API documentation and Xbox layouts,
**not an executed LLE differential test**. Full single-CPU scheduling, APCs,
interrupt masking, retail timer-list linkage and priority boosts are not modeled.
The DPC worker owns a synthetic execution context; it does not borrow a paused
title thread. Blocking DPC waits, alertable waits, unsupported dispatcher object
types, and `KeSetEvent(Wait=TRUE)` stop explicitly. Active timer/queued DPC limits
are 128/256; exhaustion stops rather than discarding work. The new service
implementation adds no emulator source dependency, but the overall pilot still
uses its GPL-derived CPU header and remains private reference-only.

Contract references: Xbox layouts/ordinals in Cxbx `585c49a` and Microsoft
[KeSetTimerEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesettimerex),
[KeCancelTimer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kecanceltimer),
[KeInsertQueueDpc](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keinsertqueuedpc),
[KeWaitForSingleObject](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kewaitforsingleobject).
The inspected sp00nznet `1409a7d` timer bridge supplied a useful architecture
reference. Its absolute-as-immediate handling and fixed polling approach were
not adopted. No GPL service bodies were incorporated.

## Reproduce privately

From the repository root, using your pinned donor checkout and owned inputs:

```powershell
py -3 tools/native_pilot.py --donor <phobos-checkout> --xbe <default.xbe> --output <repo>/build/blinx --data-sections 'MDL*,MAP[0-9]*'
cmake -S pilots/native-title -B build/blinx/host -G 'Visual Studio 17 2022' -A x64 -DPILOT_GENERATED=<repo>/build/blinx/gen
cmake --build build/blinx/host --config Release --parallel 4
ctest --test-dir build/blinx/host -C Release --output-on-failure
build/blinx/host/Release/xlle_native_title.exe <default.xbe> <eeprom.bin>
```

Use a separate `build/jsrf` directory and omit `--data-sections` for JSRF.
Generated code, game bytes and logs stay ignored under `build/`. Each executable
checks the complete XBE SHA-256 before loading. The output directory also pins
its image identity so two `default.xbe` inputs cannot silently share discovery.
The data-section exclusions are an explicit Blinx analysis hypothesis, not a
runtime patch or a universal rule. Unknown transfers remain visible failures.

Local evidence: `build/blinx/run-timers.log`, `build/jsrf/run-timers.log`,
`build/timer-contract-tests.log`, and each `timer-validation.aot-private.json`.
The earlier `run-final.log` and `generation.aot-private.json` retain the previous
startup/generation evidence. The newer Blinx VM evidence is
`build/blinx/run-memory-final.log`, `build/memory-contract-tests.log` and
`build/blinx/memory-validation.aot-private.json`. No source-share writes
or game patches were needed. This pilot never executes the donor's fake DVD
authentication or unconditional GPU acknowledgement paths.
