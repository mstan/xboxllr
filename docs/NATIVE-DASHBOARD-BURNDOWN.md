# Native dashboard port

Tracking: `beads-xbox.1.137`. This is a separate native-title port; the bounded
Blinx attract qualification does not qualify dashboard startup or menus.

The root `NATIVE_TITLE` build selects the same native CPU and shared D3D11,
input, audio, files, memory and scheduling services used by Blinx. Exact-image
profiles provide XDK calling conventions and layouts. The old full-machine
dashboard runner is historical evidence and is not an owner preview fallback.

## Current evidence

| Check | Status |
|---|---|
| Exact original dashboard 5960 identity and generated CPU | Prepared: 5,576 functions; missing direct targets remain explicit faults |
| Original D3D body/RET/layout admission | 101 methods, including register arguments; focused profile tests pass |
| Original input/audio admission | Eight input and nineteen audio entry points; isolated service contracts pass |
| Existing Blinx binding/header compatibility | Preserved; focused binding, syntax and input/audio/mixer checks pass |
| Optimized native executable | Built with MSVC `/O2`, four allowed cores, Normal priority |
| Compiler source partition | 101 units; exact function and preamble bytes retained; 64-function/512 KiB budgets |
| Shared HMAC, reader/writer locks, register bridge and core contracts | Four focused CTest cases pass |
| Legacy graphics lifecycle fixture under this profile | Explicitly skipped: required original APIs are absent |
| First actual native boot | Entry and title thread execute; initial attempt guarded at an original-kernel PE read |
| Native startup compatibility adapter | Reviewed original component/ABI, six binder checks and integrated CTest pass; actual native boot now passes this component |
| Native shutdown registration | Original list comparison, independent review and optimized integrated CTest pass; actual boot now passes ordinal 47 |
| Native kernel clock | Tick data, PM performance queries and native TSC share one epoch; integrated clock CTest passes and actual boot now passes CRT constructors |
| Named file metadata | Original 56-byte ABI and confined host queries tested/reviewed; actual title now takes the content-directory success branch and absent NoisyCamera result |
| Empty optical owner | Explicit dashboard policy; original-software comparison, independent remap review and integrated CTest pass; existing media/async contracts also pass |
| Factory setting reads | Nonencrypted `100..103`, shared first-read cache and checksum; original-software comparisons, review and integrated CTest pass; actual dashboard passes `103` |
| Content and writable drive ownership | Title creates C:/Y:/X:/T: itself; actual save-directory creation succeeds on writable partition 1 while content stays read-only on partition 2 |
| Additional absent input classes | Exact profile-owned keyboard object; original query comparison, independent review and modern/legacy integrated CTest pass; actual dashboard passes enumeration |
| Latest actual native boot | Reaches 1,604 entries and passes keyboard enumeration; guarded at encrypted game-region setting `104` |
| Driver incremental admission and QA launches | 30 authored checks pass; selected families, actual admissions, failed-bind state and content-drive owner policy are checked; run-only replays are frozen and captures are separate from performance measurements |
| Incremental attribution files | All 146 existing notice files retain their contents and timestamps after the new copy-if-different build path |
| Menu rendering/input/audio/performance | Unverified |
| Credits asset mod under native execution | Pending native baseline; old full-machine evidence does not transfer automatically |
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

The current optimized executable is
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

Private logs and source-bound receipts are retained under
`build/dashboard-native-driver-20261019`; failed compiler inputs and the original
generation/preparation receipts remain available there. Temporary compiler
scratch is on Z; original dashboard content remains read-only.

## Remaining service inventory

An exact source audit of all 160 imports after empty-optical integration finds
**87 bounded native owners, 15 imports available only through disabled LLE paths,
and 58 absent imports**. That leaves 73 gaps: 66 functions and seven data exports.
This supersedes the earlier conservative 61-import subset. Source
ownership does not establish full API conformance or that execution reached it.
The source-hashed private audit is retained alongside the build; the older
post-shutdown snapshot remains unchanged. A bounded source owner is distinct
from an API reached by actual title execution.

This inventory does not require every optional path before an owner menu preview.
Implement coherent families when execution or a documented caller requires
them; keep unexercised DVD, network and firmware operations deferred.

| Family | Remaining imports | Foundation / required distinction |
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

The framework is XboxLLR. Its eventual releases contain the recompiler tool and
the dashboard application. Blinx remains a separate repository with a tested
framework submodule pin and its own game release. After the native dashboard is
usable, rebuild and regression-test Blinx against that same framework revision.
Both titles must be good on the shared revision before proceeding. Jet Set Radio
Future is title 3 after that joint dashboard/Blinx checkpoint.
Public packaging, complete attribution and license
resolution remain separate release work; this private port is not a publication.
