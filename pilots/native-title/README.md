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
copy through the title's own code. Native utility-drive setup now mounts a
persistent `Z:` cache on partition 3. Cold launch has a real null LaunchDataPage
pointer cell; Blinx configures real file caching (zero then 64 pages). The native
renderer now creates a real 640x480 D3D11 device and supplies an owned front
surface to `GetBackBuffer2(-1)`. Blinx now passes its initial graphics-state
setup and reads the full 131116-byte Arial font TGA from installed content.
The decoder now performs real native/guest nonlocal recovery after the JPEG
probe rejects the TGA, creates the real 128x256 A4R4G4B4 font texture and uploads
all nine mip levels. Native GPU readback shows the font atlas; the top level
matches the TGA within its four-bit quantization (maximum 16/255 channel error).
Blinx reads/parses the 2332-byte font metrics, creates two 112 KiB vertex
buffers and registers six caller-owned buffers against real contiguous CPU
storage. It then reads `sys_black.dds` and `sys_white.dds`, compiles all 91
requested vertex programs and creates 30 pixel-shader definition objects.
It now passes its `MEM_NOZERO` heap commitment, loads `pika.dds`, three flare
textures and `font_cgothic_bold.dds`, and uploads their mip chains. Both DXT5
textures reach all nine levels, including 2x2 and 1x1 tails. Actual GPU readback
of `pika.dds` level zero matches the complete original compressed DDS payload.
The real memory-statistics query and front-surface descriptor now pass. A
truncated initializer has been recovered and re-lifted from the original bytes;
Blinx now passes native `XInitDevices` and device enumeration, opens the real
Windows audio device through `DirectSoundCreate`, and starts its three media
workers. Guest vertical-blank waits now complete at the configured nominal
60 Hz. Blinx creates three 65536-byte stereo PCM16 buffers at 44100 Hz and
locks/unlocks their first four bytes, then loads the complete 15188-byte
`media/dsp.bin`. It now installs the real DSP graph through XAudio2, creates
two more pixel definitions (32 total), and stops at a 640x480 render-target
texture, format `0x12` and usage `1`.
No title buffer Play or audible game output is established by those runs.
`PILOT_AUDIO=ORIGINAL` still executes the original sound-object allocations and
contiguous-page setup, stopping at `KfRaiseIrql` (ordinal 160) in audio-driver
initialization. `HalBootSMCVideoMode` is a real data export with
the same composite-cable value as the LLE SMC. The `PILOT_INPUT=ORIGINAL`
selection still executes original USB initialization and stops at
`IoCreateDevice` (ordinal 65). Title draws and audible output are unqualified.
The last ORIGINAL-renderer measurement, before the contiguous-memory service,
stopped at `MmAllocateContiguousMemoryEx`; it has not been remeasured since.
The OPTICAL build retains
and executes the original verification helper, stopping on unsupported raw
optical access. No Blinx draws, movies, menu or level yet. Function-entry counts
are diagnostics, not coverage or performance measurements.

The owner's current goal is serial: complete Blinx before further JSRF work.
JSRF's last measured result remains 12 title-thread entries at
`NtAllocateVirtualMemory`; it has not been rebuilt/run against the new VM code.

The native implementations reached so far cover thread creation, thread-handle
closure, recursive critical sections, DPC/timer structure initialization,
EEPROM user-settings queries and NTSTATUS-to-DOS-error conversion. Timer arming,
expiry and DPC delivery are implemented as described below. Callback delivery
is exercised by the contract tests. No callbacks, draws or completions are discarded.

Twenty-nine contract tests pass: concurrent threads with separate registers/TLS and
recursive exclusion, handle closure/errors and EEPROM buffer/checksum behavior;
timer/DPC/event-wait contracts; real VM reservation/commit/free/protection/query;
installed-media identity and actual file I/O; writable storage and link lifetimes;
deferred section residency and shared-page ownership;
native utility-drive persistence, formatting, eviction and failure handling;
real read-through file-cache sizing, coherence, position and budget handling;
real donor GPU clear/triangle/present pixels, guest graphics ABI and ownership;
texture GPU pixels/mips, shared views, changed-data upload and parent lifetimes;
DXT1/3/5 compressed blocks and mip tails, texture/surface descriptions,
pixel-definition ownership/copy/HRESULT/OOM, and real `MEM_NOZERO` commitment;
memory-statistics changes with actual commitment, reservation, image, stack,
cache and pool lifetimes, including shared-page small pool blocks;
native input packing, real Windows control mapping, connection transitions,
stale-handle rejection, polling cache, rumble error propagation and 100 open/close
cycles with a test-only host provider; exact reasons for unsupported input stops;
real Windows PCM buffer storage/playback/cursors/looping, wrapped locks,
properties, ownership and explicit unsupported-audio failures;
initial/nested/self thread suspension, repeated pause/resume, priority/boost
controls and retained object references surviving guest handle closure;
created/registered vertex resources, actual GPU buffer readback, external
header/backing ownership and bounded contiguous-memory allocation/release;
real shader compilation, typed/multiple-stream GPU input, paired issue and
GPU output compared against the existing LLE vertex interpreter;
native nonlocal recovery, frame lifetime and deliberate failure conditions;
plus explicit instruction, indirect-call and
kernel-call failure paths. Passing
the other title's XBE to the Blinx artifact is rejected before guest execution.
The main LLE build's runtime source and build selection are untouched.

## Reuse and scope

### Native input checkpoint (2026-10-07)

Bind `tools/native_input_binding.py --xbe <default.xbe> --output <repo>/build/blinx
--symbols <default_xdk_symbols.json>`, then select `-DPILOT_INPUT=NATIVE`.
Full regeneration accepts `--input-symbols`. `ORIGINAL` retains all eight
original XAPI bodies and their USB driver calls. Selection is at build time;
neither mode executes the other in production.

The host mapping adapts MIT sp00nznet `1409a7d` `src/input/xinput_device.c`;
the notice is [input-sp00nz-MIT.txt](input-sp00nz-MIT.txt). We audited Phobos
`63f18f0`'s XAPI/model/host split and NoRain `7adaf21`'s controller backend as
references. No GPL input implementation is copied. The private pilot's existing
CPU-helper license restriction still applies.

The guest adapter is independently implemented from the inspected XDK4831
routines and the maintained OHCI Controller S model. It serializes a 22-byte
state; **25-byte capabilities** (subtype at 0, reserved WORD at 1, input payload
at 3, rumble at 21); and 70-byte feedback (status at 0, event handle at 4,
report header at 64, motors at 66/68). The donor's desktop-style capability
layout and its zeroed control capabilities are unsuitable here. Capability
controls/resolutions now come from the actual host query, using the
[Windows capability contract](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_capabilities).

Four real Windows XInput ports feed a Controller S mapping. There are no
configured MU/IR peripherals or automatic keyboard controller. Digital face
buttons map to analog endpoints 0/255; shoulders map to black/white; triggers
and axes retain their values. This does not supply pressure-sensitive face
buttons that the host lacks. The same 18-byte control payload is used by the
LLE XID report; full driver-vs-native differential execution is not claimed.

Current/changed/previous masks remain guest-visible. Enumeration preserves
the original insertion/removal semantics, including removal and reinsertion
between reported snapshots. A removed device invalidates existing handles;
reconnection requires a new handle. Preallocation limits and one live open per
port are enforced; failed opens call the original thread `SetLastError` helper.

Auto polling caches state for the requested interval (default 8 ms). Empty
ports are sampled at most every 250 ms, bounding normal query-driven hotplug
latency; transitions entirely between host polls are not observable. Packet
numbers change with mapped state. Rumble completes synchronously after the real
Windows operation and propagates its status. Close releases the handle and
stops its live controller's rumble. USB transfer latency is not reproduced.
Manual polling, non-gamepad host types, unknown classes, repeated initialization
and feedback requiring event-handle resolution stop explicitly. They are
remaining coverage, not successful no-ops.

Twenty-four CTests pass, including real conversion code driven by a test-only
host API provider, output guards, poll caching, reconnection, capability errors,
feedback failure, 100 handle lifetimes and exact unsupported-stop reasons.
Fresh/repeated actual Blinx launches reach the same original audio-driver
`KfRaiseIrql` stop (20,463,434 / 20,463,342 title-thread entries). No host pad
was attached during those runs; physical interaction/rumble and attract input
interruption remain unvalidated. The ORIGINAL input run still reaches
`IoCreateDevice`, confirming the preserved selection, not completed USB support.

Private evidence: `build/blinx/input-family-validation.aot-private.json`,
`run-input-family-fresh.log`, `run-input-family-repeat.log`,
`run-input-original.log`, and `build/input-family-contract-tests.log`.
Sound initialization now also reads a real `HalBootSMCVideoMode` ULONG at
ordinal 356, configured to composite (6) like `machine/smbus.cpp`.
`MmQueryAllocationSize` returns the actual rounded extent for an owned
contiguous base; other allocation classes/addresses remain explicit stops.
The next coherent family is native DirectSound device/buffer/stream services
with real mixing/output and completion semantics. No audio-driver IRQ service
is being acknowledged merely to advance startup.

### Earlier service reuse

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

The next Blinx family is pixel shaders, followed by binding and draw submission.
The native texture adapter must synchronize changed guest bytes at binding/draw
boundaries too, because Xbox unlock can be inline and guest pointers persist.
The whole native renderer now owns graphics creation and front/back resources.
Utility-drive selection/mounting now
runs through the complete native library boundary below, and file-cache control
has actual storage/read behavior. The original utility routine remains available
at build time.
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

## Native graphics checkpoint

The optional `PILOT_RENDERER=D3D11` replaces all 129 identified D3D8 entry
points with the shared adapter. Only qualified methods return; the rest stop.
`ORIGINAL` executes the preserved generated bodies. This is a build-time choice,
with no live switch or duplicate production execution. Revision addresses come
from a private whole-XBE/hash-bound symbol manifest; original device publication
is decoded from the reviewed CreateDevice body, not embedded in shared services.

The first qualified subset is device creation, full-target clear, viewport,
interval-one swap, front/back surface acquisition/reference ownership and
release with no outstanding surface references. Creation supports adapter zero,
hardware device, flags zero/hardware vertex processing, one non-MSAA A/X8R8G8B8
buffer, D24S8 automatic depth, default/60Hz refresh, interval one and no supplied
surfaces. Unsupported presentation configurations return failure before creating
anything. Native output uses a hidden window, RGBA8 target and progressive host
presentation; it is not analog scanout/timing qualification. The window remains
hidden until title scene qualification. No game frame rate is measured here.

Guest presentation parameters are 17 DWORDs; native HWND/COM pointers are never
cast into guest structures. Xbox clear flags are translated explicitly. Front
surface captures retain the actual image submitted at present; subsequent back
buffer draws do not change it. Pre-first-present contents remain undefined.
Guest surface headers contain linear format/dimensions and real reference
ownership. Their data address reserves GPU-owned residence in guest VA; direct
CPU access faults until a real lock/readback contract is qualified. GPU resources
are host-owned and are not yet included in the pilot's retail-memory budget.
Device release with outstanding resources, reset, partial-channel/rectangle
clear, front/back-buffer locks, and unimplemented state/draw methods stop explicitly.

The MIT donor provides the D3D11 device/state/shader/resource code, not the Xbox
32-bit guest ABI. Preparation excludes 112 unqualified COM bodies, including
upstream success-only implementations, and hardens reached failures. Its direct
backend smoke test draws a triangle with explicit no-texture state and validates
actual readback pixels. Guest tests check success/failure stack cleanup, callee
registers, null output support, clear flag translation, distinct front/back
pixels, references and three release/recreate cycles with no retained guest
allocations. These are native API/GPU tests plus static original-routine and
external layout references, **not an executed LLE differential**. Full driver
state, default-state parity and arbitrary inline driver access remain unqualified.
Blinx has since passed its initial cull/depth/blend/texture-coordinate/viewport
setup, but still has not drawn. The state extension below records that boundary.

Prepare and select privately after generating the title:

```powershell
py -3 tools/native_d3d_donor.py --donor <pinned-main-sp00nznet-checkout> --vsh-reference <pinned-nv2a_vsh_cpu-checkout> --output build/sp00nz-d3d
py -3 tools/native_d3d_binding.py --xbe <original-NTSC-default.xbe> --symbols <default_xdk_symbols.json> --output build/blinx
cmake -S pilots/native-title -B build/blinx/host -DPILOT_RENDERER=D3D11 -DPILOT_D3D_SOURCE=<absolute-path-to-build/sp00nz-d3d>
cmake --build build/blinx/host --config Release --parallel 2
ctest --test-dir build/blinx/host -C Release --output-on-failure
```

Fresh regeneration can supply `--d3d-symbols` to `native_pilot.py`; stale wrappers
are cleared before lifting. Preserve the existing installed-media/native-cache
selections for Blinx. Donor source/prepared hashes and licenses are retained in
`build/sp00nz-d3d`; logs and repeat/reference-selection evidence are under
`build/blinx/graphics-runs.aot-private.json` and `build/graphics-contract-tests.log`.

### Native render-state and sampler extension

`graphics_states.c` translates Xbox state values to the donor's host-facing
state interface. `graphics_state.inc` owns guest state publication and the
deferred-array batch hook. The original initializer's two immutable default
tables are found through a reviewed function hash and decoded table references;
their contents and guest addresses stay in private generated bindings. The
three slots explicitly skipped by the original initialization loop remain
skipped. The native depth-enable default follows the actual allocated depth
target. Recreating the device resets the owned mode fields.

Qualified mappings cover the reached cull/depth/blend/color states, comparison
and stencil encodings, supported fog/ambient fields and bounded texture/sampler
states. Raw `SetRenderState_Simple` takes ECX/EDX packet/value arguments and
does not update the guest render-state cache; named setters do. The batch hook
samples deferred arrays before fixture draws and rejects changed unqualified
fields. **Guest draw entry points remain unsupported** and must call this hook
when integrated. Full lighting/material/default-pipeline equivalence is not
asserted merely because a donor shader accepts these state values.

The reached shader constant mode is `1`. Its reviewed original branch only
records the mode and clears the no-reserved-constants flag, with no GPU packet.
The native device already owns a real 192-float4 constant buffer. This bounded
branch preserves those guest fields; mode zero's fixed-pipeline resets, other
transitions, constant upload and programmable shader execution are unqualified
and stop at their respective unimplemented entry points.

Prepared donor fixes preserve diffuse argument zero (previously replaced with
TEXTURE/CURRENT defaults), implement W addressing in private donor slot 31
without colliding with its COLORSIGN slot, apply border/LOD values, and retain
all basic min/mag/mip filtering combinations. They reuse the donor's real D3D11
state/shader implementation. Pixel fixtures check depth rejection/disable,
channel masking, alpha blend/test, winding culling and direct guest texture
state changes; GPU sampler descriptors check actual applied settings. Alpha
blend tolerates two 8-bit UNORM levels; border float tolerance is 1e-6. These
are bounded native fixtures, not an executed hardware/LLE image comparison.

Evidence: `build/graphics-states-tests.log`, `build/graphics-states-pixels.log`,
`build/blinx/graphics-state-validation.aot-private.json`. Fresh/repeat Blinx
runs now complete the real font-file read and stop at guest setjmp recovery.

## Native file-cache contract

`file_cache.inc` implements `FscGetCacheSize` (35), `FscSetCacheSize` (37) and
`FscInvalidateIdleBlocks` (36). Size is in 4096-byte pages: default 16, maximum
2048. Resize allocates/releases real buffers; successful zero size disables the
native cache. Invalid sizes and allocation/budget exhaustion return failures
without changing the existing pool or cached bytes. The cache is read-through
and write-through: no dirty data or invented disk completion exists.

Cached file ranges carry handle identity and valid byte intervals. A hit copies
actual previously read bytes and advances the synchronous file position; a miss
uses one native read even for large requests, then retains a bounded trailing
range. No rounded read can hide EOF/partial/error results. Per-file locks cover
cursor-dependent operations. The cache lock protects buffers and resizing, and
is released during native reads. Invalidation discards all currently idle data;
no cached pointer escapes a call. Writes, size/disposition changes and close
invalidate the affected handle. Cache only handles that deny write sharing;
files permitting an external writer use native reads so host changes stay visible.
Unbuffered/unsupported I/O modes retain the existing explicit scope limits.

Cache bytes are charged to the same 64 MiB admission budget as guest VM and
section commits. Host metadata and temporary old/new buffers during replacement
are outside that guest budget; complete retail physical/MMU accounting remains
unqualified. The replacement algorithm and host filesystem caching differ from
FATX/XDVDFS kernel internals. No cache-size tuning is applied to the host OS.

The new contract test uses real file bytes and an instrumented native read entry
to verify hits actually avoid reads. It checks cursor/subrange/EOF behavior,
real commit/release, zero-size bypass, invalidation, failure preservation,
shared VM budget, concurrent reads/resizes, writes/truncation, and an actual
external writer. Eleven total CTests pass. Blinx itself now disables caching,
requests 64 pages successfully, and reaches graphics device creation. No title
cache-hit rate, rendered frame or FPS claim follows from the fixtures.

ABI/default/maximum references: nxdk and external Cxbx `EmuKrnlFs.cpp`.
Main sp00nznet/Phobos `kernel_bridge.c` only retain a size, and Cxbx's allocator
is unimplemented; those bodies are not adopted. Host behavior follows
[NtReadFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile).
These are API/native tests, not a full executed LLE differential.

## Native utility-drive contract

`cache.inc` implements the complete reviewed `XMountUtilityDrive(BOOL)` boundary:
EAX is BOOL, the return consumes eight guest stack bytes, EBX/ESI/EDI/EBP are
preserved, success leaves LastError alone, and failures call the title's actual
`XapiSetLastNTError`. The existing error helper supplies its own TLS layout and
NTSTATUS conversion. There is no duplicate execution of the original mount.

Three real cache directories (partitions 3–5) retain title ownership across
processes in `native-cache-v1.bin` under the supplied local state directory.
Existing-title mounts preserve bytes; clean requests and eviction remove the
selected directory's contents and create a real `\\??\\Z:` namespace entry.
Empty slots precede least-recently-used eviction. A checksummed, versioned
manifest is held exclusively. Formatting first flushes an invalid-slot record,
so failed/interrupted cleanup cannot later be accepted as a valid cache. An
existing corrupt manifest fails rather than silently erasing storage.

Recursive cleanup validates the exact absolute resolved partition path, then
uses pinned directory handles, relative native opens and no-reparse traversal.
Read-only child attributes are cleared for actual deletion. Open guest handles,
host sharing errors and namespace collisions report failure. Namespace collision
is checked before formatting. Only the chosen partition is cleared; installed
content and user-data partitions are retained. Native partitions share host
capacity and advertise 16 KiB logical allocation units; other cache units fail
explicitly. The native ownership format and LRU policy do not promise retail
raw-sector layout, exact FATX replacement order, quotas or physical timings.

`tools/native_cache_binding.py` uses the shared `native_binding.py` integrity/
wrapper machinery, with a reviewed whole-XBE/body hash and identified stdcall
symbol. The error helper is independently hash-checked. No title addresses
appear in the shared service. Generate the wrapper with
`--cache-symbols <default_xdk_symbols.json>` in `native_pilot.py`, or run:

```powershell
py -3 tools/native_cache_binding.py --xbe <default.xbe> --output build/blinx --symbols <default_xdk_symbols.json>
cmake -S pilots/native-title -B build/blinx/host -DPILOT_CACHE=NATIVE
```

`PILOT_CACHE=ORIGINAL` selects the original generated routine. That selection
was rebuilt and executed, reaching raw HDD partition 0; its raw I/O/FATX path
is not yet implemented in this private pilot. The normal LLE machine remains
unchanged. There is no live switching or executed LLE differential claim.

Ten CTests include actual Z: byte routing, independent persisted-byte reads,
reopen, recursive/read-only/many-entry cleanup, eviction isolation, locked-file
failure/recovery, corruption rejection, LastError callback/ABI and stable host
handle counts. Symlink creation is unavailable without privilege on this host;
that fixture reports a skip. A real Blinx run with its manifest locked executes
the original error helper and returns failure. Fresh/repeated native launches
pass utility mounting; restoring a true cold-launch `LaunchDataPage == NULL`
export advances further to filesystem-cache control. Warm launch data and
`HalReturnToFirmware` remain unsupported. A null pointer describes absence of
a previous launch, rather than a fabricated launch packet.

Contract sources: the original owned XBE's complete routine, nxdk's data-export
declaration, Cxbx's external cold-launch pointer initialization, and Microsoft's
[NtQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile)
and file-information contracts. No GPL filesystem implementation is imported.

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

## Native texture checkpoint

`graphics_texture.inc` owns guest 20-byte texture/24-byte surface headers and
shared CPU storage. Creation uses actual pinned MIT donor D3D11 textures/SRVs;
native pointers stay private. A surface view retains its parent; final release
uploads that mip through the donor's real unswizzle/upload path. Unchanged
bytes avoid a redundant upload. Releasing the parent before its views preserves
storage until the last owning view is released. Descriptors use Xbox's seven
DWORD layout, not the donor's desktop eight-DWORD structure.

The current bound accepts power-of-two 2D sample-only textures, four color
formats (4 through 7), and valid partial/full mip chains through 4096 dimensions.
Other usage/types/formats stop. CPU allocations count against the guest budget;
GPU and upload-shadow copies use additional host storage. Cache/tiled lock
aliases return a canonical CPU-accessible guest pointer, with the same raw
swizzled bytes. Physical-address aliases, GPU-written textures, external
registered resources and sampling draws remain unqualified. A future draw must
synchronize persistent CPU writes even without another lock or rebind.

`GetDeviceCaps` copies the reviewed original table into the full 212-byte guest
output. Capabilities describe the emulated device; unimplemented methods still
stop. `GetDisplayMode` writes the actual configured dimensions/logical refresh,
the guest linear color format, and the enhanced renderer's progressive flag.
Its five-DWORD Xbox layout differs from desktop D3D. Reporting progressive output
and canonical lock pointers are documented HLE differences; ORIGINAL rendering
remains a separate build selection.

Set `XLLE_PILOT_TEXTURE_CAPTURE` to an existing private directory for diagnostic
GPU readback after each changed mip upload. Each file has eight little-endian
DWORDs (magic 0x58544558, version 1, native handle, mip, width, height, guest
format, payload bytes), followed by linear GPU storage bytes. Capture is absent
from normal upload execution. Never commit the resulting game-derived files.
Tests independently construct rectangular Morton images and compare GPU reads
for all four formats and the nine-level 128x256 chain, including changed writes,
view aliasing, parent lifetime, RAM failure cleanup and recreation.

## Native vertex storage checkpoint

The adapter borrows the main MIT donor's real vertex-buffer allocation,
Lock/Unlock upload and destruction. Its ignored Lock range/flags are hardened,
and missing upload destinations terminate. The GL `vb_Unlock` bodies found in
Phobos and NoRain return success without uploading and are excluded. Only
whole-buffer native uploads are used here; guest Lock2 returns actual shared
CPU bytes, preserving repeated/persistent pointer access. Future draw submission
must synchronize changed bytes even without a new Lock or stream rebind.

Created resources own a 12-byte guest header, guest-budgeted CPU allocation and
native D3D11 buffer. Registered vertex resources retain the caller's header and
contiguous allocation, including nonzero offsets and shared backing. Register
applies the original Data-address transformation; final Release destroys native
ownership while leaving external header/storage with the caller. Contiguous
storage cannot be freed while registered resources retain it. GPU readback
fixtures check actual bytes before/after persistent writes, external aliases,
reference counts, cleanup, allocation failure and device recreation.

`physical_memory.inc` implements the nxdk-declared `MmAllocateContiguousMemory`
and `MmAllocateContiguousMemoryEx` ABI plus `MmFreeContiguousMemory`. It owns
real committed bytes at the Xbox 0x80000000 CPU alias, respects page/alignment
and inclusive physical-address bounds, and shares the existing 64 MiB budget.
It supports READWRITE and WRITE_COMBINE|READWRITE; cache policy is logical while
native upload supplies the CPU/GPU handoff. This bounded physical namespace
does not model arbitrary CPU aliases, page tables or device DMA. It makes no
full-MMU or executed LLE-differential claim. Unsupported contracts still stop.

The actual title now allocates two 114688-byte vertex buffers, registers six
external buffers, and performs another 8192-byte contiguous allocation. The
first vertex-shader request follows real reads of both system DDS textures.
All three desktop CreateVertexShader adapters ignore the declaration, and the
main adapter expects raw microcode rather than this call's four-byte Xbox
header. Phobos's separate HLE layer handles those differences and is a useful
behavioral reference; its GPL implementation is not copied. The next section
records the native contract now exercised by Blinx.

## Native vertex shader checkpoint

Actual Blinx now creates all 91 vertex shaders, with programs from 11 to 135
instruction slots, and stops at pixel-shader creation. Fresh/repeated launches
reach 19818529/19818437 entries. This is initialization progress, not a frame,
attract-sequence or cadence result. The shaders retain real compiled D3D11
objects and input layouts; creation does not return a placeholder handle.
Guest handles are odd 32-bit guest addresses. The adapter preserves the
HRESULT/stack contract, output bounds, declaration fields/constants and owned
guest storage. Device release destroys the native objects and guest headers.
Constant application, shader binding and title draw submission remain
unqualified; storing a declaration constant at creation does not apply it early.

The main and NoRain donor HLSL decoders extract opcodes from word zero instead
of word one and cannot decode these actual programs correctly. This path uses
the existing LLE floor's `nv2a_vsh_cpu` decoder instead: pin `561fe80`, equivalent
source tree to the main build's `863fc0f` pin. Preparation checks the revision,
retains the Unlicense and the decoder header's MIT notice, and records source
hashes. The GPU translation adapts the main MIT donor's native HLSL operation
and register approach, handling separate output masks and paired operations
that read their inputs before either writes. RCC sign/range and all EXP/LOG
components follow the bounded LLE operation contract. Production executes
native GPU arithmetic; it does not execute the CPU reference alongside it.

Declarations retain actual register/stream/offset/format data. The reached
formats include float3/float4, packed 11/11/10 normalized normals, packed ARGB
colors and signed-short pairs. Native input layouts handle both streams used
by these programs. Additional admitted float/short/normalized-short formats
are listed in `graphics_shader_source.inc`; other formats, declaration-only
shaders and vertex-state programs still stop explicitly. Native floating-point
rounding is permitted. Extreme numeric inputs and full hardware rasterization
are not certified by the current comparison.

`pilot_shader_test` compares actual D3D11 vertex outputs against the LLE CPU
interpreter in a separate diagnostic process. It captures raw outputs through
[D3D11 stream output](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-creategeometryshaderwithstreamoutput),
before rasterization, using a deferred context that preserves immediate state.
The synthetic fixture also checks independently specified packed/color/short
input results and a paired read-before-write dependency. Malformed opcode and
early-END fixtures must stop before the borrowed decoder's opcode table access.

All 91 actual captured Blinx programs pass on eight test vertices each:
27664 finite output components, maximum `abs(native-reference)/max(1,abs(reference))`
of `5.96046448e-08`, against an allowed `2e-4`. This is an executed differential
for bounded vertex-program outputs, not a full rendered-frame LLE comparison.
Optional `XLLE_PILOT_SHADER_CAPTURE=<existing private directory>` records real
declarations, microcode and generated HLSL for replay by `pilot_shader_test`.
Captures remain ignored under `build/`; ordinary title execution omits capture
and readback. Evidence: `build/shader-contract-tests.log`,
`build/blinx/shader-reference-results.log` and
`build/blinx/shader-validation.aot-private.json`.

## Reproduce privately

### Vertical blank and actual sound-buffer progress

`D3DDevice_BlockUntilVerticalBlank` is qualified by the original 36-byte body:
it clears a notification event and calls the nonalertable, indefinite
`KeWaitForSingleObject`. It submits no graphics work. The native whole-API
implementation waits for the next edge of a common monotonic display epoch,
using [Windows waitable timers](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).
It releases the graphics ownership lock while waiting. Each waiter retains its
own deadline, so one thread cannot consume another's wakeup by immediately
waiting again. Integer deadline calculation avoids repeated rounded sleeps and
accumulated phase drift. Host failures stop; success follows elapsed time.

The nominal 60 Hz matches the current machine LLE clock in
`src/machine/machine.cpp`. This is not physical NTSC field timing or measured
game cadence; interlace, PAL, callbacks and mode changes remain unqualified.
Callback registration still stops explicitly. Device release with active waits
also stops. Ordinary native presentation has not yet been unified with this
clock; that remains necessary before making an original-speed claim.

Phobos's multi-waiter notification-event findings informed the contract.
Its implementation was not copied. The main sp00nz donor's fixed 16 ms IRQ
path was also inspected; the native API does not use it. NoRain supplied no
additional whole-API wait implementation. Existing graphics tests now cover
deadline boundaries, long uptime, concurrent waiters, actual timed guest calls,
register/stack preservation and exact timer-handle reclamation. All 27 CTests
pass. Private evidence is `build/vblank-contract-tests.log`,
`build/blinx/vblank-validation.aot-private.json` and
`build/blinx/run-vblank-{fresh,repeat}.log`.

That checkpoint's effects-image stop is now passed by the integrated
[MIT DSP core and native transport](../../docs/NATIVE-DSP-DONOR-2026-10-07.md).

### Native audio and media-worker checkpoint

Select `-DPILOT_AUDIO=NATIVE` after running `tools/native_audio_binding.py`
with `--xbe`, `--output` and `--symbols`; full preparation accepts
`--audio-symbols`. The binding checks the full NTSC image and each original
body, verifies stdcall cleanup and derives the object vtables from reviewed
constructor bytes. Its 21 entries retain the original bodies for `ORIGINAL`.
Runtime service code contains no title function addresses.

The initial adapter reuses the actual Windows DirectSound8 engine for 2D PCM storage,
mixing, rate conversion, looping and device-paced cursors. It supports mono or
stereo, 8/16-bit PCM, shared device ownership, guest-owned or caller-owned data,
contiguous/wrapped lock uploads, play/stop/seek, volume/headroom and
frequency/pitch. Errors propagate from real allocations and device operations.
There is no silent output/clock fallback. Native COM pointers stay outside
guest memory. Xbox descriptors are **24 bytes**, not the desktop descriptor
or the donor's 28-byte declaration. The device interface is at object +8 and
the buffer interface at +28; original bytes establish the reference-count,
vtable and public-call shapes.

Current bounded differences and missing coverage:

- Host mixing/resampling and output latency can differ from the Xbox APU.
  Buffers use the original default 600-hundredths-dB 2D headroom. LOCDEFER may
  allocate native storage earlier. Cursors reflect actual host audio progress.
- Borrowed data is uploaded on attachment and Play; writes through Lock/Unlock
  upload their real spans. Arbitrary concurrent writes through a retained raw
  data pointer are not qualified as coherent streaming. Detached zero-length
  storage, partial loop regions, Xbox ADPCM, streams, mix-bin routing
  and 3D remain unqualified. Reached unsupported API/format requests stop.
- The LLE APU currently proves bounded voice/idle/IRQ behavior, not full PCM
  output. These tests establish ABI, ownership and actual host playback; they
  are **not** a full hardware/LLE audio differential. Game-audio equivalence
  and perceptual tolerances remain open until title audio is exercised.

All three donors were inspected for this family. MIT sp00nznet `1409a7d` and
NoRain `7adaf21` have useful API organization but also success stubs and missing
stream/effect work. Phobos `63f18f0` has device-clock/buffer/packet architecture,
but its GPL implementations and silent unsupported paths were not imported.
Windows DirectSound provides the reusable engine here; the Xbox adapter is
independently authored. Microsoft documents the
[buffer creation](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418039(v=vs.85))
and [actual play/write cursor contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418062(v=vs.85)).
Xbox IMA ADPCM must not be passed to XAudio2 as Microsoft ADPCM.

### Integrated effects transport

`PILOT_AUDIO_BACKEND=DSOUND` retains the first backend. Build-time `XAUDIO2_DSP`
selects actual XAudio2 voices and the pinned guarded MIT processor for downloaded
effects. Prepare the donor and build its locked library:

```powershell
py -3 tools/native_dsp_donor.py --donor <pinned-donor> --output build/dsp-donor
& "$env:USERPROFILE/.cargo/bin/cargo.exe" build --release --locked -p dsp56300-emu-ffi --manifest-path build/dsp-donor/Cargo.toml --target-dir build/dsp56300
```

Set `PILOT_DSP_SOURCE` to the prepared directory and `PILOT_DSP_LIBRARY` to
`build/dsp56300/release/dsp56300_emu_ffi.lib`. Rerun `native_audio_binding.py`
to add reviewed decoder/location metadata to an existing generation. Whole
`PILOT_AUDIO=ORIGINAL` remains available; backends never switch during execution.
Replacing an effects image changes owned graph data at the native processing
boundary. The DSP audit records transport latency, first-use compilation,
output mapping and remaining hardware qualification.

Private actual-asset qualification uses `pilot_dsp_test <encrypted-image>
<generated-seed-hex> <private-original-decoder-output>` and
`pilot_contract_test audio-effects <encrypted-image>`. These test real graph
execution/native output, optional arguments and replacement/lifetime behavior.
No title asset or key is committed. The shared adapter is independently authored;
the private CPU pilot retains its existing GPL dependency and release limits.

Media-worker startup now uses real thread-object references, priority/boost
controls and suspend/resume counts. A reference owns a duplicated native handle
and survives `NtClose`; `PsThreadObjectType` is an opaque type identity, with
unqualified private layout access faulting. Windows priority classes approximate
guest relative priorities; exact retail scheduling order is not claimed.
Suspension takes effect at generated function entries and backward-branch
targets, after any in-flight native service returns. It never freezes a host
thread inside an allocator, I/O or runtime lock. Self-suspension waits without
holding the ownership lock. The existing 63 lifetime thread-slot bound remains;
thread-slot reclamation and broader object types are still open.

`tools/native_thread_checkpoints.py --output <repo>/build/blinx` upgrades an
existing hash-verified generation; full preparation and extent recovery retain
the pass. This image has 7354 loop targets. Four synthetic tests verify scope,
conditional branches, missing-target rejection and idempotence; six extent
tests also pass. Normal checkpoints perform a TLS/count read, with a blocking
wait only when suspension is requested. This is not instruction-history
comparison or duplicate guest execution.

Evidence: `build/audio-family-contract-tests.log`,
`build/blinx/audio-family-validation.aot-private.json`,
`run-audio-family-{fresh,repeat}.log` and `run-audio-original.log` in that private
directory. The PCM fixture copies exact bytes through real host buffers, plays
a one-shot to completion, observes advancing cursors, loops/stops and returns
guest storage to its baseline. It does not establish audible game output.
That earlier checkpoint stopped at vertical-blank waiting. The newer checkpoint
above establishes actual title buffer creation, with effects installation still
pending. Title draws, playback, movies and attract remain unvalidated.

### Memory accounting and input-startup checkpoint

`MmQueryStatistics` follows the nine-DWORD, 36-byte
[nxdk ABI](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h).
It reports this runtime's actual committed guest pages and reservation ledger,
including image residence, contiguous allocations, kernel/context backing,
thread/dispatcher stacks and the real file cache. Host graphics storage and
implementation overhead remain outside the guest 64 MiB budget. Cxbx's external
field semantics informed the mapping; its implementation is not incorporated.
The values are not retail-kernel allocator overhead or host free-memory values.
Snapshot locks cover concurrent guest commitment and allocation metadata.

The pool service implements `ExAllocatePool`, `ExAllocatePoolWithTag`,
`ExQueryPoolBlockSize` and `ExFreePool` with real guest memory and ownership.
Small allocations share committed pages; large allocations are page-aligned.
Guest payload sizes follow the reviewed 32-byte block/eight-byte overhead
contract below the 4056-byte boundary. An independent host ledger replaces
private kernel allocator structures. Final page owners decommit storage; budget
failure returns null without disturbing live allocations. Unknown/double frees
and zero-sized allocations stop. Pool pages participate in the same admission
and statistics model; no allocation success is fabricated.

Original `Get2DSurfaceDesc` also revealed a prior descriptor error: usage is
derived from format capability flags at level zero, even for sampled textures.
Those flags now come from the hash-bound image's table in the generated binding.
Display surfaces report their actual configured dimensions, pitch, format and
multisampling. Tests check output guards and the texture-versus-surface cases.

`tools/native_function_extent.py` incrementally re-lifts a reviewed extended
function using the pinned Phobos translator. It verifies image/source hashes,
rejects overlaps/non-executable ranges/incomplete decode, and changes no game
bytes. In this image, initializer `0x000e8c30` actually ends at `0x000e8c69`:
the old discovery stopped before the loop reached by its forward jump.
`build/blinx/function-extents.aot-private.json` retains the reviewed correction;
full `native_pilot.py` preparation replays it. Six synthetic tests exercise the
extent guards and metadata replay. This remains the private GPL-dependent CPU
pilot; the recovery tool does not change its distribution status.

Evidence: `build/blinx/memory-family-validation.aot-private.json`,
`build/memory-family-contract-tests.log` and `build/blinx/extent-recovery/`.
The next coherent boundary is native input initialization/enumeration at the
Xbox API, with actual host controller state, while retaining original USB
driver execution in the reference path. Returning success from `IoCreateDevice`
without a functioning device is not an input implementation.

### Pixel definitions and compressed texture progress

The original NTSC `CreatePixelShader` at `0x00136200` allocates 252 bytes,
initializes two ownership fields to one and copies the complete 240-byte
definition behind its pointer field. It performs no GPU submission or shader
compilation. The native adapter preserves that layout, a separately owned copy,
the two-argument stdcall/HRESULT contract and untouched output on allocation
failure. `XLLE_PILOT_PIXEL_CAPTURE=<existing private directory>` optionally
records definitions. Compilation is deferred until use; unqualified binding
still stops. These 30 creations are not 30 validated native pixel programs.

The MIT donor already maps Xbox DXT1/3/5 to native BC1/2/3 and uploads compressed
blocks directly. The adapter now preserves the original block-row lock pitch,
at least one block per mip, logical descriptor dimensions and shared view
ownership. Original `Get2DSurfaceDesc`, `GetSurfaceLevel2` and lock helpers
provide the guest layout reference; native block formats follow
[D3D11 block compression](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11).
Tests read back actual GPU block bytes at every mip, including 1x1 tails, change
data through persistent views and verify reupload and release. Compressed
subrectangle locks and compressed base dimensions below 4 remain explicit
unsupported cases. Ordinary uploads perform no diagnostic GPU readback.

Xbox `MEM_NOZERO` (`0x00800000`, nxdk's header) permits unspecified initial
contents. Native commitment may still provide zeroed pages; it preserves existing
committed contents, real access protection, allocation failures and the shared
64 MiB budget. The Xbox bit is never passed to Windows as `MEM_ROTATE`.
This is a documented HLE initialization choice, not a promise to reproduce
physical-RAM remnants.

Private evidence: `build/blinx/run-pixel-dxt-desc.log`,
`build/blinx/dxt-capture/`, `build/blinx/pixel-capture/` and
`build/blinx/pixel-texture-validation.aot-private.json`. Fresh/repeated runs
reached the same `MmQueryStatistics` barrier at that revision. No frame, attract loop or
original-speed result follows from this startup checkpoint.

### Decoder recovery

`tools/native_jump_binding.py --xbe <default.xbe> --output <repo>/build/blinx
--symbols <default_xdk_symbols.json>` qualifies the original CRT by body and
image hashes. It retains guest register save/restore, SEH and notification work,
then transfers the native stack using the MIT donor's adapted jump-slot bridge.
Generated caller returns release their slots; nonlocal transfer invalidates
younger slots. The current bound supports three identified setjmp callers and
two longjmp callsites. Unknown shapes, recursive jumps during unwinding,
unmatched/corrupted buffers and capacity exhaustion stop. General guest exception
dispatch/global unwinding is still unqualified where it reaches missing services.

`tools/native_cpu_binding.py` with the same arguments binds the decoder's whole
zero-argument cdecl MMX query. Select `-DPILOT_CPU_PROBE=NATIVE` to query the
configured Xbox CPUID profile; `ORIGINAL` retains the original flag-toggle
probe. Native output is the Boolean capability, with callee-save registers and
stack cleanup preserved. Incidental EFLAGS mutation is outside that HLE API
contract. The general arithmetic-flags lowering remains unsupported.

For full regeneration, pass both `--jump-symbols` and `--cpu-symbols` to
`tools/native_pilot.py` alongside the existing media/cache/graphics bindings.
Runtime fixtures exercise real host jumps and failure contracts; actual Blinx
executes the guest CRT and returns from JPEG rejection into texture setup.
This is not a claim of an executed LLE differential or a complete SEH model.

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
