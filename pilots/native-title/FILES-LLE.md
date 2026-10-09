# Files, cache and installed-media LLE

Bounded serial family `beads-xbox.1.108` is qualified, after memory `.106`.
Four of nine family gates are complete; five remain open. The maintained
original file/cache route reaches Demo play, Start/title return and Demo reentry.
Independent original contract cases, cross-process persistence and the final
native correction regression pass. Scope and remaining tolerances are explicit
below; this is not exhaustive Xbox filesystem qualification.

## Executable floor and independent evidence

`tools/native_filesystem_reference.py` executes the original kernel's Nt file,
FATX, filesystem-cache, allocator, ISR, DPC and completion routines. Its
`xlle_storage_reference` library reuses the existing IDE and PIC models; it
does not implement another filesystem or substitute an IRP result. The original
driver builds DMA descriptors and the modeled controller transfers real sectors.
Unknown ports, exceptions, invalid gates and execution limits stop the run.

The source image is opened read-only. Writes use the existing controller's
volatile sector overlay; this qualifies within-run write/flush/reopen behavior,
not cross-process persistence. The original software is private owned input.
The maintained hardware library remains a private reference dependency with its
existing license restrictions, outside the intended non-GPL product.

Inputs for the completed qualification:

- Initialized 64 MiB RAM capture SHA-256:
  `31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`.
- Owned 8 GiB HDD SHA-256:
  `8af2d1bb3f292bef122facdd7f9166f06b23ad69d1316bc63e674ed8ffba55dd`.
- Independently extracted comparison file: 1,961,984 bytes, SHA-256
  `832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d`.
  It is a convenient existing file on the owned disk; no dashboard application
  execution or dashboard work is involved.

The runner raises its isolated caller from priority 8 to 31 using original
`KeSetPriorityThread`. This prevents unrelated captured boot workers from
preempting the probe. No ready queue, wait result or completion is edited.
Scheduling under contention and precise interrupt latency remain outside this
fixture. The captured IDT and original handlers perform actual dispatch/EOI.

```powershell
cmake --build build/visibility-lle --target xlle_storage_reference --parallel 4
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_filesystem_reference.py --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_storage_reference.dll --dependencies C:/msys64/mingw64/bin --hdd ../machine/xbox-hdd-stockdash.img --expected ../machine/hdd/C/xboxdash.xbe --output build/blinx/filesystem-qualified.aot-private.json
```

The completed run records 756 original interrupt deliveries, 6,544 sectors read
and 24 sectors written to the overlay. Qualified cases include:

- Complete original file reads matching the independent file hash, explicit
  offsets, cursor progression, short final reads and EOF.
- Open/missing-name/close/repeated-close and create collision outcomes.
- Real cache resizing through 0/1/2/16/32/2048 pages, rejection of 2049 pages,
  invalidation and subsequent actual reads.
- Original asynchronous reads with an explicit event, real completed DMA,
  immediate EOF/invalid-offset/alignment rejection and zero-length success.
- A 9,001-byte FATX create/write/flush/close/reopen/read/delete sequence with
  generated independent payload bytes and a short-read count check.
- Filtered directory enumeration of that real file, its actual length and the
  end-of-enumeration status; async file notification when no event is supplied.

## Native correction from the floor

An accepted asynchronous request and a rejected request have different effects:

| Original observation | Matching native behavior |
| --- | --- |
| An asynchronous read can return PENDING after real completion has already published bytes/IOSB and signaled its event | Keep actual host pending/completion ownership; do not require artificial delay |
| Immediate EOF or invalid offset clears the supplied event and file notification, preserves IOSB and returns the error | Release private transfer bookkeeping without publishing or signaling completion |
| Completed request with an explicit event signals that event, leaving the file notification unsignaled | Notify the selected event, or the file when no event was supplied; not both |
| Zero-length success publishes zero bytes and signals the supplied event | Retain actual successful completion |

`file_async.inc` previously published pending before knowing whether the host
accepted the request, then synthesized an error completion and signaled both
notification objects. It now publishes pending only for an accepted pending
request and retires immediate rejection without changing the caller's IOSB.
Genuine later failures of accepted pending requests still use the completion
path. Shared read/write tests cover explicit-event versus file notification,
retained ownership, EOF and invalid-offset rejection across repeated lifetimes.

The combined original-memory selection builds and passes **45/46 tests (21.24s)**.
All file/media/storage tests pass. The sole failure is the unchanged minimum
event-wait-duration assertion in existing `.99`; no timing tolerance is altered.
The game-owned native build/doctor pass and **40/40 tests pass (16.52s)** on
runtime `9f151c6`. Actual `local/runs/20261008-182422-attract/` records **190.094s**,
19 captures and no guards. Inspected captures 8/12/17 show Demo play, title
return after Start and Demo reentry. Exit 1 is intentional deadline termination.
Executable SHA-256:
`e04263c4697cf244cad361b4f86ea28cd8b50f882e4f32a559d3c87f189d9f77`.
The game adopts this runtime and the subsequent reference-test/documentation
update; that update does not change the runtime or require another title replay.
This validates the native notification correction, not an original-filesystem
title run, new cadence measurement or endurance plateau.

## Metadata, directory, rejection and namespace bundle

`tools/native_filesystem_contract_reference.py` reuses the same owned original
kernel and read-only/COW HDD. It creates three actual FATX files and asserts
**84 cases plus 64 mount/open/exact-read/close/unlink lifetimes**. The run delivers
666 original interrupts, reads 2,480 sectors and writes 102 overlay sectors.
Every request records output bytes and sentinel IOSBs; expected results are
asserted, not inferred from a successful title run. Run it with the reference
command above, replacing the script name and omitting `--expected`.

| Original Nt/FATX observation | Shared native correction |
| --- | --- |
| Network-open class 34 reports actual EOF for both length fields | Derive both from the actual host file's EOF; do the same for directory entries |
| Adequately sized basic/standard queries (4/5) zero the supplied output then return invalid parameter | Preserve that rejection and zeroing; remove the desktop-only success assumption |
| Bad length/class/handle/access/offset and synchronous EOF preserve IOSB | Keep both caller words intact on immediate errors; accepted asynchronous completion retains its own publication path |
| Class 34 and volume query zero excess supplied space; position query writes only 8 bytes | Match those byte effects and preserve the canary beyond the supplied buffer |
| Directory enumeration returns one ASCII entry, zero next-offset/index, filtered/case-insensitive names, short-buffer overflow with cursor advance | Retain real host cursor/filter work and normalize only the Xbox-visible fields |
| First absent-name query returns `C000000F` without an IOSB update; exhaustion/restart thereafter publishes `80000006,0` | Preserve the distinction, including `*.*` matching extensionless files |
| Truncated symbolic-link query copies the prefix that fits, sets Length to bytes copied and reports full required length | Copy real target bytes even on buffer-too-small; retain open object after unlink |

The native fixture independently checks actual persisted bytes, access denial,
output bounds, metadata/volume layouts, directory cursor/filter effects,
namespace ownership and repeated lifetimes. The combined native suite passes
**37/38 (17.61s)**; all file tests pass. The sole failure is the existing `.99`
minimum event-wait-duration assertion; its tolerance is unchanged.

Final game-owned adoption of runtime `52bdfed8f549d19986b9513c55584b8fd2c165a7`
passes doctor/build and **40/40 tests (17.84s)**. Actual
`BlinxRecomp/local/runs/20261008-192931-attract/` runs **190.109s**, with 19
captures and no guards. Inspected captures 8/12/17/19 show Demo play, title after
Start, Demo reentry and continued gameplay. The child is intentionally stopped
at the harness deadline (exit 1), not reported as a crash. Executable SHA-256:
`7649af349ff879e8d2e899654b353f421d94d85c78da68262a4a15cdc63b08b8`.
This completes the bounded family alongside the original-route and persistence
evidence below. No new speed or host-memory plateau claim is made (`.100/.107`).

## Bounded contract and explicit tolerances

The selectable reference actually runs original file/cache/namespace/utility
operations in Blinx, with DMA, ISR/DPC and completion work. The installed title
contains all 3,197 files (2,235,000,402 bytes), independently verified after image
close/reopen. The original title route and cross-process durability evidence
below remain valid; this batch changes the native replacement, not that route.

- Installed-media authentication is the approved title-identity/mount policy,
  backed by real matching files. The optical reference remains separate; no
  fabricated drive-authentication response is returned.
- Native storage uses actual host capacity, timestamps and directory order.
  Data bytes, EOF, case-insensitive lookup, transfer counts and ownership remain
  contract requirements. This does not make host cluster allocation the Xbox's
  reported file AllocationSize.
- A zero-page original cache cannot perform the tested unaligned read
  (`C0000017`); native may read without allocating that internal cache. Host
  resource use and available capacity differ; HLE does not manufacture resource
  failure merely to reproduce a less efficient implementation's allocation.
- Original successful EOF/allocation changes report IOSB Information 512 when
  they write metadata and 0 on repeated no-ops. Native retains its actual
  metadata-operation count; this field is not a file-data transfer count here.
  Real read/write byte counts and the resulting file length remain exact.
- Original directory length 63 underflows its header calculation and writes a
  full entry beyond the supplied length. The fixture retains this evidence.
  Native rejects lengths below the 64-byte header with length mismatch and
  untouched output/IOSB. Reproducing that malformed-input overflow is excluded.
- Async accepted completion versus immediate rejection, event/file selection,
  close/drain and exercised replay writes are qualified. APC delivery, true
  outstanding LLE requests at the native bridge, cancellation, injected disk
  faults and arbitrary pending-error timing remain unqualified. Existing guards
  stay explicit; no failure-completion or cancellation behavior is fabricated.
- Original route performance is unrestricted. Full-console scheduling under
  contention, crash/power-loss durability, optical authentication and unrelated
  filesystem classes are outside this bounded attract-family qualification.

Phobos remains the native file/path donor reference. Its current Fsc bridge
only stores a page count and cannot provide this executable cache floor; its
host-file strategies remain useful after qualifying their observable boundary.
No new donor implementation body is copied by this checkpoint. The latest
directory/metadata review specifically avoids treating Phobos's desktop class
4/5 acceptance or rounded allocation sizes as an original correctness oracle.

## Original files in the title runtime

`PILOT_FILES=LLE` selects `files_lle.inc` plus the shared original kernel/RAM/PIC
transport in `io_device_lle.c`. It requires original memory/device services,
`PILOT_CACHE=ORIGINAL` and the approved `PILOT_MEDIA=INSTALLED` policy. Renderer,
audio and input may remain native. All selections are made at build time.

The disk library is loaded from explicit private paths. Its writable constructor
opens a separately provisioned HDD; the existing reference constructor remains
read-only/COW. DMA follows the same original physical pages and native aliases
as the allocator. Original IDT gates, ISR/DPC routines and completion code run.
The captured worker uses original priority 31, as in the independent fixture;
this is not qualification of whole-console scheduling under contention.

Actual original handles cross the file/namespace boundary. Native event handles
belong to the separately selected scheduling family. The adapter queries their
real state, uses actual original events during I/O, then transfers the observed
completion state. The current controller completes DMA within the original
call. Unfinished pending requests and guest APC callbacks explicitly stop;
their broader transport remains unqualified. No IOSB, completed count or
notification is manufactured. The original kernel owns byte copying and file
position. Guest CPU scheduling stays exclusive during original CPU execution.

Installed-media policy mounts `D:` on the configured HDD title root and maps an
original `CdRom0` link request to that root. Identity verification reads/hashes
the actual installed `default.xbe` through original Nt/FATX/IDE. This remains
the approved installed-media exception, not optical drive authentication.
Original `XMountUtilityDrive` executes its actual disk/cache work. Its ordinal
40 dependency is a data export: Phobos independently confirms that ABI shape,
and this route takes the value from the original initialized kernel.

Private configuration additions to the existing memory-LLE build:

```text
PILOT_FILES=LLE
PILOT_CACHE=ORIGINAL
PILOT_FILES_LIBRARY=<private build>/libxlle_storage_reference.dll
PILOT_FILES_DEPENDENCIES=C:/msys64/mingw64/bin
PILOT_FILES_HDD=<separate writable installed-title HDD>
PILOT_FILES_TITLE_ROOT=\Device\Harddisk0\Partition1\Titles\4D530013
```

The shared transport test `pilot_files_lle_test <extracted default.xbe>` reads
all 44,724,224 XBE bytes through original FATX/cache/IDE and compares them with
the independent extracted input. The existing COW fixture still passes with
756 original interrupt deliveries, 6,544 read sectors and 24 overlay writes.
The combined existing contract suite passes 46/46 in 22.44s; its file tests
exercise native files, while the new standalone test exercises original files.
Do not conflate those two test selections.

Actual title evidence: `files-lle-attract-qualification` ran **360.203s**, with
35 captures and no guards, reaching Demo play, later movies and another cycle.
Review then retained guest CPU ownership during original file execution.
The final `files-lle-final-attract` run passed **240.157s**, 23 captures, no
guards. Inspected captures 8/13/18/23 show Demo, title after Start, Demo reentry
and continued gameplay. Final executable SHA-256:
`f0f5058b563c88758550525a97e21af7a9487abe050e6a58a41535d990450be3`.
Both exit codes 1 are intentional harness deadline termination. This is
correctness-route progress, not a new cadence measurement or endurance plateau;
host retention remains `.107`. The native standalone suite passes 38/38 in
17.32s; game-owned native adoption/regression is recorded separately.

Cross-process persistence is also exercised through that same runtime bridge:

```text
pilot_files_lle_test <extracted default.xbe> write
pilot_files_lle_test <extracted default.xbe> verify-delete
```

The first process creates a new fixture, writes/flushes/closes 9,001 deterministic
bytes. An independent offline FATX read matches them. The second process reads
the exact persisted bytes, applies original delete disposition, closes and
confirms the name is missing. Both processes independently repeat the complete
XBE comparison. This qualifies ordinary cross-process durability, not host
power-loss guarantees. The image must not be in use by a title during these
tests. Existing fixtures are never silently overwritten.

`tools/native_hdd_install.py` uses externally prepared pyfatx solely as an asset
tool. It refuses existing outputs, verifies the selected XBE and all file
hashes, writes actual FATX entries, then closes/reopens and verifies every file.
The qualified private v3 image initially hashes to
`0fe3e1985c3bf31130838705ebca90bc1d0ef3fef17f7cdf680664e7dbc46b9f`.
Runtime writes legitimately change that hash.

The external `mborgerson/fatx` checkout is pinned to
`baed16e3a2d62fc247f2bd6a3ed5deffe75da150`. Its directory allocator mishandles
the last slot/end marker at a cluster boundary, first observed at the 257th
entry in Blinx's media directory. A local preparation-only repair advances the
entry and end marker into the next cluster correctly. A 600-file directory
survives close/reopen before the full installation is attempted. Rebuilding
the static library also requires relinking the cached CFFI extension. Modified
source identity is retained in the private image manifest. Neither that GPL
implementation nor its patch is incorporated into the shipping runtime.
