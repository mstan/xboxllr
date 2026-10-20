# Native dashboard port

Tracking: `beads-xbox.1.137`. This is a separate native-title port; the bounded
Blinx attract qualification does not qualify dashboard startup or menus.

The root `NATIVE_TITLE` build selects the same native CPU and shared D3D11,
input, audio, files, memory and scheduling services used by Blinx. Exact-image
profiles provide XDK calling conventions and layouts. The old full-machine
dashboard runner is historical evidence and is not an owner preview fallback.

## Ordered work queue

This is the active delivery checklist. Work from the first incomplete row;
the detailed evidence and historical component inventory below support it.
Rows have different sizes, so their count is not a completion percentage.

| ID | Work item | Status | Pass condition |
|---|---|---|---|
| DB01 | Reach native dashboard graphics startup | Passed for reached startup | Actual native execution loads assets, opens native audio and passes the animation and inline NULL-texture boundaries. Latest run: `9cfa3b45...`, 255389 stopped-thread entries. |
| DB02 | Consume the dashboard's original vertex layouts | Passed for reached layouts | Three original declaration-only layout classes consume packed normals/color/UV and cached transforms on the GPU. Pixel/reference and ownership checks pass; actual startup binds the original object and advances into mesh submission. Lighting remains disabled for this reached caller. |
| DB03 | Draw the first real dashboard menu | **Active** | Actual native run presents a recognizable menu with visible, stable content; retain an image and run log. Original archive texture and vertex ownership now pass; the next required batch is indexed mesh binding/submission. |
| DB04 | Make the ordinary menus usable | Pending | Exercise controller navigation, opening/back transitions and supported offline menu paths; record failures explicitly. Determine concrete routes from the actual menu after DB03. |
| DB05 | Check dashboard audio and responsiveness | Pending | Verify audio progression/output and measure native responsiveness under ordinary load; resolve freezes and material stalls. A created audio device alone does not pass this row. |
| DB06 | Validate the native Credits mod | Pending | Open the bottom Credits entry in the actual native dashboard, verify the intended credits/legal content, then return to ordinary menus. |
| DB07 | Validate both titles on the final shared framework | Pending | Accept a FINAL dashboard build and run isolated Blinx attract, Stage 1/save/Results/reload, adaptive 21:9/32:9 and Retry checks against the same framework revision. Preserve existing owner builds and saves. |
| DB08 | Hand both builds to the owner | Pending | Leave usable dashboard and Blinx previews ready in normal windows with normal audio and no automated deadline; provide locations and remaining nonessential deferrals. |

Current next action: **DB03**, indexed mesh submission. Actual startup now
adopts its original declaration-only vertex object, a nine-level compressed
archive texture and an archive vertex buffer through shared native owners.
It next reaches `D3DDevice_SetIndices`; this and `DrawIndexedVertices` have
exact-image ABI/body admissions but need shared runtime consumers. Implement
index binding, original index storage/offset rules and real indexed drawing
together, preserving existing nonindexed Blinx draws and resource lifetimes.

Keep object adoption, input conversion and transform handling in one compatible
graphics batch. Record the implementation, focused checks, actual run and next
failure at each checkpoint. Close a row only when its full pass condition is
observed. Add newly reached requirements beneath the owning row; put safely
deferred precision/enhancement questions in the existing deferral ledger.
Build-system experiments follow this queue when measurements show they are
necessary to unblock iteration.

## Playtest delivery burndown

The latest actual native run passes network startup and the cached-update
query, creates the requested Gaussian graphics device, completes graphics
initializer state setup, loads `default.xip`, passes the bounded random-byte
request and begins loading ambient audio. Native mutex startup now passes and
the native audio device opens. Its mix-bin headroom setup now passes; the
notification-enabled ADPCM ambient buffers are created and all three sets of
workers issue subsequent stream reads. The periodic native delay now passes;
the main thread opens the dashboard font and loads another section. Ordinary
audio pause/resume now passes, and the remaining startup sound-effect buffers
load. The shared inline command-buffer owner now passes the reached
`D3D_MakeRequestedSpace` request. The missing animation virtual-method root
`0004eaf0` now executes natively. Inline NULL texture bindings now pass on all
four stages. Original declaration-only vertex objects now bind through a
typed GPU wrapper around the existing fixed-function core, with packed normal,
color/UV and original cached transforms. Original archive textures and vertex
buffers now retain their real contiguous CPU backing and use existing native
uploads/buffers. Actual startup advances to `D3DDevice_SetIndices`; shared
indexed binding/drawing is the next compatible graphics batch. The explicit
offline virtual-link query passes. No native menu
frame has been captured. This is implementation work; original hardware
is not required for the current blocker. Entry counts are diagnostics, not a
completion percentage: replacing an original subsystem changes how many
functions execute.

| Delivery milestone | Status | Evidence or remaining work |
|---|---|---|
| Shared native title build and CPU execution | Done for current startup | Optimized executable enters the actual dashboard and runs its title thread |
| Reached platform startup services | Done for current startup | Clocks, shutdown registration, file metadata, drive ownership, empty optical drive, settings and absent keyboard pass actual execution |
| Offline network initialization | Done for bounded lifecycle | Native startup passes; real host lifetime and unsupported consumer guards are retained |
| Offline cached-update handling | Passed for absent mounted cache | Actual read-only probe finds absence and query returns the original no-update result; existing cache formats remain guarded |
| Graphics initialization and initial assets | Passed in actual startup | Native state setup returns and the original dashboard loads `default.xip`; exact RGBA8 dithering remains an explicit precision deferral |
| Offline random-byte request | Passed for bounded startup use | Real host entropy, original XOR/owner/error contract; 0..64 bytes admitted, original seeded stream deferred |
| Native mutex and mixed-wait services | Implemented; reached startup passes | Real ownership/recursion and leased host references; eight integrated checks pass; predicted worker/teardown execution remains unverified |
| Native audio device headroom | Reached setup passes | Device/ABI/output fixture and PCM/ADPCM-versus-LLE headroom checks pass; diagnostic captures now wait for real completed frames |
| Native ambient buffer notifications | Setup and subsequent reads reached; audible QA pending | Three original ambient ADPCM buffers/workers progress; component checks cover real sample-position events and retained ownership |
| Native worker timing and thread waits | Reached delay passes; teardown components pass | Real delay and CPU handoff, actual thread-exit waits and bounded paired signal/wait; 21 combined checks pass |
| Offline virtual link-state query | Passed for unattached policy | Original wrapper mask/ABI/context and native provider/lifetime checks pass; actual startup advances beyond the link query |
| Native ordinary audio pause/resume | Implemented; actual startup passes | Original software rules, real PCM/ADPCM queued-sample retention, notifications and three backend checks pass; synchronized mode remains guarded |
| Inline graphics command-buffer owner | Implemented; reached space request passes | Profile-proved layout/defaults, real packet delivery/refetch/fences and repeated lifetime checks pass; unsupported methods remain guarded |
| Animation indirect native root | Recovered; actual startup passes | Original 46-byte body, constructor/vtable proof and native x87 lowering; nearby source-proved table audit found no other missing root |
| Inline NULL texture bindings | Implemented; reached startup passes | Four stages, shared resources, original reference reconciliation, native unbind/rebind, two lifetimes and invalid-owner guards pass |
| Original declaration-only vertex layouts | Implemented; actual binding passes | Three source layouts, packed normals/color/UV, unchanged original objects, repeated lifetimes and cached transforms; real GPU pixels versus expanded-input reference pass |
| Original archive resource ownership | Implemented; actual texture/vertex consumption reached | Caller headers/storage retained, real contiguous backing leases, existing GPU uploads, CPU edits, mip chains, original references and release guards pass |
| First rendered native menu | Current blocker | Shared index binding and indexed mesh submission; no actual menu frame yet |
| Usable dashboard | Pending | Verify menu navigation, controller, audio and performance; resolve reached failures |
| Native Credits mod | Loader implemented; navigation pending | Shared section-data mod admission, real preload/reload fixtures and private registry generation pass; actual Credits navigation follows a usable baseline |
| Blinx on the same framework revision | Pending | Rebuild separately, then focused gameplay/save/mod regression |

There is no reliable total ETA or percentage until startup reaches the first
menu and exposes the remaining runtime dependencies. Do not treat the current
guard as the last blocker. Optional online/DVD/firmware paths and public-release
licensing remain separately deferred; not every missing kernel import is a
prerequisite for the offline menu.
The [native-title deferral ledger](NATIVE-TITLE-DEFERRALS.md) records precision
questions and deliberately unavailable enhancements separately from this
delivery burndown.

## Current graphics batch evidence

- Source-proved declaration constructor `5c740` and tables `176660`, `176674`
  and `176688`: three original input layouts, without interpreting packed
  normals as floats or freeing the original shader header.
- Typed fixed-function inputs run on the GPU through the existing renderer
  core. Four input variants and two matrix states match independently expanded
  float inputs in real rendered pixels. Runtime fixtures also cover original
  inline matrix edits and two complete device lifetimes. Original lighting
  ownership and programmable-only input-capture diagnostics are outside this
  reached lighting-disabled path; ordinary GPU image readback is supported.
- Original XPR-style texture headers retain their real cached physical alias.
  Focused ownership fixtures cover five formats, four distinct mip levels,
  CPU edits without rebinding, original inline references, bound retention and
  teardown without freeing the caller's header/storage. GPU aliases retain
  their separate live-image behavior.
- Original registered vertex buffers reuse the existing shared Register
  backing lease and native buffer owner. Both construction routes pass real
  drawing, retained CPU edits, offset/reference and bound-lifetime checks.
- Ten integrated Windows checks pass in 3.34 seconds, including 14 rejection
  cases, the existing programmable-shader GPU/reference check, audio pause,
  worker waits and scheduler. Warm builds retain all 104 generated CPU objects;
  the last test rebuild took 4.40 seconds. No interpreter was added.
- Actual muted native executable `a4bd95083caa6104...` adopts the original
  vertex declaration, uploads the 256x256 nine-level DXT3 texture and binds the
  32768-byte archive vertex buffer. It stops at `D3DDevice_SetIndices`
  (`1453b0`), before a menu frame. Retained private run:
  `build/dashboard-native-cache-20261019/host/runs/20261019-194320-8hy0bmep`.

## Current evidence

| Check | Status |
|---|---|
| Exact original dashboard 5960 identity and generated CPU | Built: 5,582 functions; missing direct targets remain explicit faults |
| Original D3D body/RET/layout admission | 101 methods, including register arguments; focused profile tests pass |
| Original input/audio admission | Eight input and nineteen audio entry points; isolated service contracts pass |
| Existing Blinx binding/header compatibility | Preserved; focused binding, syntax and input/audio/mixer checks pass |
| Optimized native executable | Built with MSVC `/O2`, four allowed cores, Normal priority |
| Compiler source partition | 101 units; exact function and preamble bytes retained; 64-function/512 KiB budgets |
| Indirect CPU coverage | Optimized build has 5,582 functions and zero translation failures; six source-proved matrix-stack methods added through title-owned seeds; actual execution of those methods remains pending |
| Shared HMAC, reader/writer locks, register bridge and core contracts | Four focused CTest cases pass |
| Legacy graphics lifecycle fixture under this profile | Explicitly skipped: required original APIs are absent |
| First actual native boot | Entry and title thread execute; initial attempt guarded at an original-kernel PE read |
| Native startup compatibility adapter | Reviewed original component/ABI, six binder checks and integrated CTest pass; actual native boot now passes this component |
| Native shutdown registration | Original list comparison, independent review and optimized integrated CTest pass; actual boot now passes ordinal 47 |
| Native kernel clock | Tick data, PM performance queries and native TSC share one epoch; integrated clock CTest passes and actual boot now passes CRT constructors |
| Named file metadata | Original 56-byte ABI and confined host queries tested/reviewed; actual title now takes the content-directory success branch and absent NoisyCamera result |
| Empty optical owner | Explicit dashboard policy; original-software comparison, independent remap review and integrated CTest pass; existing media/async contracts also pass |
| Factory setting reads | Nonencrypted `100..103`, shared first-read cache and checksum; original-software comparisons, review and integrated CTest pass; actual dashboard passes `103` |
| Boot-derived game region | Separate authenticated EEPROM-bound input and immutable startup identity; original component checks, independent review and integrated CTest pass; actual dashboard passes `104` |
| Content and writable drive ownership | Title creates C:/Y:/X:/T: itself; actual save-directory creation succeeds on writable partition 1 while content stays read-only on partition 2 |
| Additional absent input classes | Exact profile-owned keyboard object; original query comparison, independent review and modern/legacy integrated CTest pass; actual dashboard passes enumeration |
| Offline native network lifecycle | Startup/cleanup and real host reference ownership implemented and reviewed; actual startup now passes without entering the original NIC/raw-disk stack; other network operations remain guarded |
| D3D creation and presentation surfaces | Actual native device creates the 1280x960 raster; guest surface allocation/projection setup returns. Real Gaussian pixels, optional depth and legacy visual mods pass focused GPU checks |
| Initializer-state component and integration | Passed | Real zero raster bias, original cache roles, tracked native precision, inactive consumer bounds and Reset ownership; five combined CTests pass in 6.53 seconds |
| Bounded random service integration | Passed | Twelve binder tests and nine original component cases; five combined network CTests pass in 0.64 seconds |
| Latest actual native boot | Executable `20fe9248` advances beyond the offline link query into further initialization; explicit audio `Pause` guard, no captured frame |
| Driver incremental admission and QA launches | 33 authored checks pass; selected families, actual admissions, failed-bind state, content-drive owner and link policy are checked; run-only replays are frozen and captures are separate from performance measurements |
| Incremental attribution files | All 150 staged notice files match their sources; real MSBuild/Ninja checks cover unchanged, changed, missing and newly configured files |
| Menu rendering/input/audio/performance | Unverified |
| Credits asset mod under native execution | Shared loader view and title-owned registry implemented/reviewed; actual native navigation remains pending |
| Owner native dashboard preview | Not ready |

The initial native executable is
`aef1352f812a3b8cbadd913f9a5feb9d746aeb83cc650bd846698d17e43d30f7`.
Its first muted run stopped with exit 2 before the 15-second deadline, at guest
read `8001003c`, reached through `72966 -> 72902`. Those routines inspect or
patch an original kernel and change the guest code-segment descriptor. Mapping
a token PE header would leave privileged GDT operations unresolved. A shared,
explicit native startup contract was the next implementation boundary.

The startup boundary now has a shared native selector owner with exact-image
body, caller, operand-role and import-ordinal admission. Its original offline
component and optimized native ABI/guard fixture pass. The integrated executable
is `4e8fae722835f908e3b236587037de227376060759b0fbd8800a0a576cc1fc10`;
the original startup bodies remain available. Its next actual native run passed
this component and reached `6fb47`, then stopped at
`HalRegisterShutdownNotification` (ordinal 47). The driver returned exit 2 and
recorded that this was an early failure, not a deadline stop. See
[startup scope](NATIVE-KERNEL-STARTUP.md).

The next executable, `e28598ddb09f78a8d1ad2914bc2417948e2b71905481b81623fc421a56ba35a1`,
also passes shutdown registration and reaches CRT constructors. The next failure
is a read of `fe000270`: the unimplemented `KeTickCount` data import still points
at a callable thunk. A shared native clock owner is in progress. Shutdown
registration has real guest links and retained callback ownership; callback
delivery and firmware transitions remain guarded. See
[shutdown scope](NATIVE-KERNEL-SHUTDOWN.md).

The clock/metadata executable is
`dbf4492863fe99cce0268ce787e172dd31d413703af0bfc6392e8fa2be2b6f7e`.
It uses the shared native clock: PM performance queries run at 3,375,000 Hz;
native TSC runs at 733,333,333 Hz; volatile millisecond tick publication has an
independent host timer. The original clock failure is cleared. The actual run
reaches dashboard main initialization and stops at `IoDismountVolumeByName`
(`3a2ff`, ordinal 91), before the tray-state query. See
[clock scope](NATIVE-KERNEL-CLOCK.md) and
[metadata scope](NATIVE-FILE-METADATA-2026-10-19.md).

The completed optical executable is
`4b42152c913603d4bb70dc8f0461bdcdbe338bbe2072a4b152f77b528ca098a8`.
It selects the shared immutable empty-drive policy: tray state `0x10`, change
count zero, and an absent optical-volume error for dismount. Integrated optical,
clock and shutdown tests pass. The extracted production path resolver also
passes existing metadata, directory, async-read and handle-lifetime contracts.
The first candidate admitted only the device-name form; the actual caller uses
`\??\CdRom0:`. Additional original-kernel calls establish that this DOS name
also is absent, without an existing symbolic link. The corrected native run
passes ordinal 91 and the tray query, then reaches `2dd20 -> 6c56b -> 7c010`.
It stops at the factory AV-region setting (`ExQueryNonVolatileSetting`, index
`0x103`), outside the existing user-setting implementation. No menu or
performance result is implied.
See [empty optical scope](NATIVE-EMPTY-OPTICAL-2026-10-19.md).

The factory/namespace executable is
`7a3a0545a1ed535db22ef931ea760f9d18a16dac88d51b534638225ff762b71c`.
The shared factory-setting owner covers the original AV-region query without
changing user-setting ownership. A declarative `content.drive_owner = "TITLE"`
also lets the original dashboard create its DOS links instead of colliding
with an initial host C: link. Actual execution requests 256 cache pages,
creates C:/Y:/X:/T:, creates its three save/music directories, and reaches
native input initialization. The next guard is the original keyboard-device
object at `1592a8`; this class is outside the current input profile. Menu,
rendering and native frame-rate qualification remain pending. See
[factory-setting scope](NATIVE-FACTORY-SETTINGS-2026-10-19.md) and
[drive ownership](NATIVE-TITLE-DRIVER.md).

The keyboard executable is
`8f1c622f030200377206a4ba69fb74989b6daa42f9c442dd855b996de241f5b0`.
All selected binding families were revalidated after admitting the exact absent
keyboard object. The actual native run `20261019-114209-s4zk7ror` uses a frozen
neutral controller replay, passes keyboard enumeration and proceeds to temporary
audio-file checks. Existing directories correctly return name collision on this
subsequent run; absent temporary files return name not found. At 1,604 entries,
the next guard is `ExQueryNonVolatileSetting` index `104` through
`6cde8 -> 7c010`. This encrypted region setting requires its own shared owner;
the nonencrypted factory-read implementation does not admit it. No frame was
captured, so menus, audio playback and frame rate remain unverified. See
[absent input scope](NATIVE-INPUT-ABSENT-CLASSES-2026-10-19.md).

The boot-region optimized executable is
`9c7d981d8fde952e7b1e49c09c3824f80236b039ee8755637f0bc0e0084b67fe`.
Run `20261019-120231-_8rw7q61` passes region `104`, opens the original
`y:\xodash\xonlinedash.xbe`, and successfully reads its two header ranges
(376 and 464 bytes). It reaches network initialization and stops at 1,710
entries on `\Device\Harddisk0\partition0`, access `c0100000`, options `10`,
through `13f81c -> 130bc6`. No raw-disk or relaxed file-option admission is
implied. The next coherent implementation is an explicitly selected, shared
initialized-but-offline network service owner, with unadmitted original
private-object consumers guarded before access. Original XOnline startup
failure aborts dashboard startup, so simply returning an offline error would
not reach menus. No menu frame or audio-playback result is available yet.
See [boot region scope](NATIVE-CONSOLE-SETTINGS-2026-10-19.md).

The latest combined network/presentation executable is
`084e712ce62ef90cf95c97017e62e74dde33b532f56fd3a00b8c13a015447ffa`.
Run `20261019-123620-vbyqu7sz` passes native offline network startup and stops
at the next guarded network operation, the cached dashboard-update query
`116931`, at 1,622 entries. It exits 2 before its 20-second limit and captures
no frame. The previous raw partition-0 startup path is no longer entered;
raw-disk ownership and file-option guards remain unchanged. The lower entry
count reflects replacing the original network implementation, not backwards
progress. Both source batches passed independent review and the optimized
combined build; six integrated lifecycle/surface/settings/input CTests pass
in 1.24 seconds. Graphics creation and surface behavior remain unverified in
the actual title until execution reaches them. See
[offline network scope](NATIVE-OFFLINE-NETWORK-2026-10-19.md) and
[presentation surfaces](NATIVE-PRESENTATION-SURFACES-2026-10-19.md).

Source look-ahead also resolves the original matrix-stack constructor's vtable
at `138f0`. Six methods were absent from discovery, including the identity-load
method called during `d04af` initialization. Title-owned
`titles/dashboard/native-seeds.json` and its image-bound contract now retain
these complete original method entries plus the previously qualified audio
AddRef entry. Fresh preparation in `build/dashboard-native-cache-20261019`
discovers 5,582 functions, preserves all seven declared extents, and reports
zero translation failures. These are generated original CPU bodies, without
runtime title branches or replacement success handlers. The 26 unresolved
direct-target guards remain. This is next-candidate preparation, not a claim
that the actual title has reached these methods or rendered a frame.

The cache-query optimized candidate is
`76f29b71faeb2b82d761cf61cbfe3538b5087433f9f3c2faa9679cefdef17770`.
Run `20261019-130828-kyngn6_o` passes all earlier startup paths and enters the
new query, but guards on its cache-probe result at 1,622 entries, before a
frame. All eight integrated tests pass in 1.80 seconds and both the query and
filesystem adapter pass independent review. A separate read-only NT probe of
the actual mounted partition root finds ordinary, case-insensitive directory
metadata and genuine CACHE absence, so the integration discrepancy remains
under investigation. Do not describe the query as passed by the actual title
or the filesystem as unsupported without further evidence. All 5,576 retained
generated function sections match the preceding candidate after line-ending
and trailing-whitespace normalization; only the six declared methods are new.

The runtime-only diagnostic candidate `a51c8af8...` identifies the probe failure:
parent FileCaseSensitiveInformation (71) returns `ACCESS_DENIED`. The original
fixture and separate root check used `CreateFileW`, which did not prove the
same granted rights as production's `NtCreateFile`. Direct NT handle tests
reproduce the missing attribute-read permission. Mounted partition roots now
explicitly request `FILE_READ_ATTRIBUTES`, preserving share/options/disposition
and write permissions. The corrected fixture uses production NT creation and
checks granted access. The filesystem probe, cached query, installed-media and
storage regressions pass together in 0.65 seconds; the narrow fix also passes
independent review. Earlier Win32-root evidence is retained with a private
supersession record.

The cached-update executable was
`eb97515fe0f7da49febfd5d89d1b2920e81f18bd116e1fd13da77b953195b873`.
Run `20261019-131757-mib9tn_r` obtains genuine CACHE absence and passes the
cached-update query into `2ccf0` graphics initialization. Device creation receives
`640,480,7,1,2222,1,0,0,0,0,20,0,1,0,0,0,0` (hexadecimal DWORDs except the
first two dimensions stated in decimal). The shared backend currently admits
only no multisampling, automatic depth and zero presentation flags, so it
returns unsupported. Original cleanup reaches guarded `HalReturnToFirmware`
at 1,671 entries. The next implementation boundary is coherent supersampling,
optional-depth and presentation-flag support; firmware-return success would
not fix the creation failure. No frame, menu, audio or FPS result is implied.

Private logs and source-bound receipts are retained under
`build/dashboard-native-driver-20261019`; failed compiler inputs and the original
generation/preparation receipts remain available there. Temporary compiler
scratch is on Z; original dashboard content remains read-only.

The presentation candidate `f108a9cd...` subsequently creates the requested
1280x960 raster and guest surfaces, then stops at dither packet `00040310` in
original `2c660`. The next candidate is
`c8b7f766fbf653485db6e685328ccf7c9a0363a6603a7c66149a415a3de20ed9`.
Run `20261019-142602-dexjff80` passes that initializer, loads the original XIPS
section and `default.xip` assets, and reaches an explicit offline-network guard
at `00130746` after 186,936 native function entries. It exits 2 before the
20-second deadline with no captured frame. Entry count includes repeat calls;
it is not a unique-function coverage metric. The native build contains no
CPU interpreter fallback. The initializer and existing presentation/visual
checks pass together: five CTests, 6.53 seconds. See
[initializer scope](NATIVE-GRAPHICS-INITIALIZER-2026-10-19.md) for the precision
approximation and retained guards. This is still not a menu or performance pass.

The random-service executable is
`d85abb4edb389588e0160ae6b304183bbb76d1571fa8eb3be385a081ea08d069`.
Run `20261019-144648-ci74w2ha` passes the original four-byte request, opens
the ambient audio WAV and reads its headers, then stops at `NtCreateMutant`
(ordinal 192) after 193,932 native entries. It exits 2 before the 20-second
deadline with no captured frame. The new network header is the only change
among 119 generated files; CPU bodies and wrappers are unchanged. Original
task/notification APIs remain guarded: source lookahead observes absent task
handles at this constructor, rather than fabricating completed tasks. See
[bounded random service](NATIVE-NETWORK-RANDOM-2026-10-19.md). Actual audio
playback, menu rendering and performance remain unverified.

## Retained service inventory

An exact source audit of all 160 imports after empty-optical integration finds
**87 bounded native owners, 15 imports available only through disabled LLE paths,
and 58 absent imports**. That left 73 gaps: 66 functions and seven data exports.
This supersedes the earlier conservative 61-import subset. Source
ownership does not establish full API conformance or that execution reached it.
The source-hashed private audit is retained alongside the build; the older
post-shutdown snapshot remains unchanged. A bounded source owner is distinct
from an API reached by actual title execution.

This is the earlier source-hashed snapshot, before the mutant, mixed-wait and
worker-delay batches. It is not a current open-import count: subsequent native
owners implement 192/213/221, mixed 235, bounded 99/230 and actual thread-handle
wait transport. The table retains that snapshot's gaps for provenance; the
delivery table above records the current reached blocker. Unreached capabilities
and guarded modes are not counted as completed APIs.

This inventory does not require every optional path before an owner menu preview.
Implement coherent families when execution or a documented caller requires
them; keep unexercised DVD, network and firmware operations deferred.

| Family | Imports in that gap snapshot | Foundation / required distinction |
|---|---|---|
| Clock and scheduling | 99, 238 | Shared clocks now exist; delay/APC semantics and ready-thread yielding remain guarded |
| Handle synchronization | 192, 193, 221, 222, 230, 235; data 16 | Extend actual handle ownership; semaphore/mutant state and atomic signal/wait |
| Named objects | 247 | Shared namespace; qualify object identity and lifetime |
| File metadata/lifetime | 195, 196, 200; data 40 | Named metadata and absent optical-volume handling now exist; deletion, mounted volume/cache ownership and device-control contracts remain |
| System memory | 167, 172, 173, 175 | Shared VM budget and ownership; distinguish system and contiguous allocations |
| Strings/memory | 260, 308, 313, 269 | Encoding, allocation ownership and matched-byte-count contracts |
| Diagnostics/exceptions | 8, 302, 312, 95 | Logging and real exception ownership; bugcheck remains fatal |
| Board/settings/power | 25, 29, 44, 49, 228, 358, 360, 365; data 41, 42 | Immutable empty tray now exists; other virtual-device identity and firmware/persistent writes stay guarded |
| AV/GPU kernel bridge | 2, 3, 168 | Shared native renderer exists; original device exports need separate ownership |
| Crypto/integrity | 338, 339, 344, 345, 346, 347, 349; data 353, 355 | Existing SHA/HMAC; qualify cipher/RSA/key ownership separately |
| Driver/IRP/interrupt | 46, 62, 65, 74, 81, 83, 84, 85, 86, 87, 98, 100, 109, 129, 151, 153, 160, 161, 176, 359; data 357 | Coherent device and request ownership; no general driver admission yet |
| Floating-point context | 139, 142 | Preserve modeled guest state, not just host registers |
| Network hardware | 252, 253 | Defer until reached; no invented PHY/link success |

The seven unimplemented data exports are 16, 40, 41, 42, 353, 355 and 357.
They need explicit data/ownership admission; the existing default callable thunk
is not an implementation of a data export. Ordinal 347 is callable, not data.
ABI classification was checked against local nxdk kernel definitions.

Three oversized-shift compiler warnings are a separate donor lowering issue in
currently unproved `.data`/`DVDTHUNK` discovery candidates. Their reachability is
unverified; they did not cause the observed startup failure. They remain tracked
for CPU lowering/discovery work rather than being counted as passed execution.
The concrete shift/discovery follow-up is `beads-xbox.1.138`. Broader segment
fault semantics and kernel-version identity are retained as LLE-D08/D09 in the
[deferral queue](../pilots/native-title/LLE-DEFERRED-QUESTIONS.md); reached startup
behavior remains implementation work in `.1.137`.

## Delivery order

The inline NULL-texture batch advances the actual dashboard beyond method
`1b0c`. Executable `9cfa3b45...` stops at an unowned original vertex handle
`00ba4df1`, with 255389 stopped-title-thread entries, exit 2 before its deadline
and no menu capture. Original caller `5c740` directly constructs a declaration-
only object using the original declaration parsers; it bypasses the admitted
CreateVertexShader entry. Source-derived layouts contain float3 positions and
packed normals, with optional color or texture coordinates. Adopting the object
and consuming its fixed-function layout remain required implementation work.

Seven combined Windows checks pass in 1.85 seconds. They include real native
stage unbinding and rebinding, all four inline stage packets sharing one texture,
preserved guest references/use fence, two complete device lifetimes, and explicit
guards for live guest bindings, missing original releases and enabled texture
control. The optimized combined rebuild took 82.74 seconds without an explicit
configure, retaining all 104 generated CPU objects. Variable load prevents a
controlled speedup claim. The shared consumer contains no title address.

The [incremental native recovery workflow](NATIVE-TITLE-DRIVER.md) now lowers
the actual animation method `0004eaf0..0004eb1e`, retaining the original x87
operations and original native callees. Eight nearby constructor-proved tables
with 184 slots were audited; only this root was absent. This is a bounded
lookahead, not whole-title coverage. Fresh discovery seeds retain the same root.
The actual executable `572a3a3d...` advances beyond it and stops at guarded
inline graphics method `1b0c`, value zero, with 255316 stopped-title-thread
entries, exit 2 before the deadline and no menu capture.

Forty-two driver/recovery checks and seven combined Windows checks pass (the
Windows checks take 1.76 seconds). The build reused 100 of 103 existing CPU
objects, rebuilt dispatch and two units whose line endings had changed, and
added one native unit. The workflow now preserves line-ending-only differences
byte for byte to avoid those two unrelated rebuilds on future recoveries.
This observed configure took 59.07 seconds and compile/link took 70.54 seconds;
host load varies, so these are not controlled speedup comparisons. The shared
recovery workflow retains failure isolation, source/body hashes and exact
service admissions rather than adopting an edited tree implicitly.

The [inline command-buffer batch](NATIVE-INLINE-GRAPHICS.md) advances the actual
dashboard beyond MakeRequestedSpace. Executable `461f0c76...` stops at unresolved
indirect target `0004eaf0`, with 254635 stopped-title-thread entries, exit 2 before
the deadline and no captured menu. Fourteen original/profile/migration checks,
strict GCC/MSVC parser builds and eleven combined Windows checks pass (2.46s).
The new owner preserves API/packet ordering and uses real native GPU fences;
staging-buffer reuse does not claim GPU completion. All 103 generated CPU
objects remain unchanged. The next batch concerns missing native function
roots, not original-hardware validation.

The [ordinary audio pause batch](NATIVE-AUDIO-PAUSE.md) passes the actual startup
boundary. Executable `5f226640...` loads the remaining sound effects and stops
at `D3D_MakeRequestedSpace`, with 254179 stopped-title-thread entries, exit 2
before its deadline and no captured menu. Eleven combined Windows checks pass
in 2.03 seconds, including real XAudio2 queued samples after a paused ring edit,
DirectSound and maintained APU voice state. Ten profile and 34 driver checks
pass. Only the audio metadata header changed among 119 generated files; all
103 generated CPU objects were reused. The shared `build --with-checks` route
now builds compatible targets and writes the launch receipt in one invocation;
the final fixture-edit rebuild took 2.35 seconds under the observed host load.
This is not a controlled speedup comparison or final runtime performance claim.

The link-status batch now passes its actual startup boundary using an explicitly
unattached virtual adapter. The [link contract](NATIVE-NETWORK-LINK.md) preserves
the source-derived raw-bit mask, lifecycle lease and strict provider failures.
Fifteen binder checks, strict portable core, real Windows dispatcher/host checks
and ten combined CTests pass (5.22 seconds). The new executable `20fe9248...`
reaches `IDirectSoundBuffer_Pause` instead. Raw PHY and connected-network behavior
remain separate from this offline policy; pause/resume semantics are required
implementation work, and menu rendering remains unverified.

The next worker/timing batch advances actual startup beyond the periodic audio
delay: all three ambient ADPCM buffers/workers are created, subsequent file
reads occur, and the main thread opens `fonts/Xbox.xtf` and loads another
section. Executable `cbb7ffa0...` now stops at an explicitly guarded network
consumer, `0007e125`; no first menu frame is claimed. The [worker wait
contracts](NATIVE-WORKER-WAITS.md) retain the original component references,
host-specific zero-delay status correction and the bounded handoff/clock
deferrals. All 21 combined checks pass in 6.18 seconds. A source-fix rebuild
of title and fixture took 12.50 seconds, with all 103 generated CPU objects
reused; variable host load prevents claiming a controlled speedup ratio.

The retained dashboard cache now uses the shared development
[`ITERATIVE` link policy](NATIVE-TITLE-DRIVER.md), retaining `/O2` and all 103
generated CPU objects. A no-change driver build measured 1.19 seconds without
notice-copy commands or recompilation; the first incremental database link
took 150.35 seconds. These are different workloads under variable host load,
not a measured speedup ratio for an edited build. All 150 staged notice files
match their sources. The new executable reaches the same explicit periodic
audio-worker delay guard (99); this build change does not close that functional
gap or establish menu performance. Final delivery still requires FINAL linking
and the same-framework Blinx regression.

Credits remain a title-owned mod. The shared, exact-image
[section-data provider](NATIVE-SECTION-MODS.md) now supplies a separate loader
view for the revised XIPS data while preserving original XBE identity and
executable bodies. Preload, deferred reload, boundary-page ownership and the
actual private registry admission pass; the revised archives and registry are
prepared in an isolated content/state stage. Enable that stage after the native
baseline works, then validate actual Credits navigation and ordinary menus.
The old modified XBE is not admitted under the original native-body profile.

The framework is XboxLLR. Its eventual releases contain the recompiler tool and
the dashboard application. Blinx remains a separate repository with a tested
framework submodule pin and its own game release. After the native dashboard is
usable, rebuild and regression-test Blinx against that same framework revision.
Both titles must be good on the shared revision before proceeding. Jet Set Radio
Future is title 3 after that joint dashboard/Blinx checkpoint.
Public packaging, complete attribution and license
resolution remain separate release work; this private port is not a publication.
