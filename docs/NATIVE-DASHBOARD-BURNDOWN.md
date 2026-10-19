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
| Latest actual native boot | Reaches next XAPI initializer; guarded at ordinal 47, HalRegisterShutdownNotification |
| Driver incremental admission | 25 authored checks pass; selected families, actual admissions and failed-bind state are checked before configure/build/run |
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

Private logs and source-bound receipts are retained under
`build/dashboard-native-driver-20261019`; failed compiler inputs and the original
generation/preparation receipts remain available there. Temporary compiler
scratch is on Z; original dashboard content remains read-only.

## Remaining service inventory

Reconciliation of the conservative 67-import list removed the implemented image
name export, HMAC and four reader/writer-lock exports. The remaining 61 imports
are **54 functions and seven data exports**. This is a conservative subset:
imports delegated only under an original-device build must also be reconciled
for the selected native route. Ordinal 47 is one such newly reached gap; its
presence in an IO-LLE dispatch case does not provide native registration.
This inventory is not a
requirement to implement every optional path before an owner menu preview.
Implement coherent families when execution or a documented caller requires
them; keep unexercised DVD, network and firmware operations deferred.

| Family | Remaining imports | Foundation / required distinction |
|---|---|---|
| Clock and scheduling | 99, 126, 127, 238; data 156 | Shared clocks and scheduler; 64-bit EDX:EAX returns and observable tick storage |
| Handle synchronization | 192, 193, 221, 222, 230, 235; data 16 | Extend actual handle ownership; semaphore/mutant state and atomic signal/wait |
| Named objects | 247 | Shared namespace; qualify object identity and lifetime |
| File metadata/lifetime | 210, 195, 91 | Confined host metadata; dismount requires actual mount lifetime handling |
| System memory | 167, 172 | Shared VM budget and ownership; distinguish system and contiguous allocations |
| Strings/memory | 260, 308, 313, 269 | Encoding, allocation ownership and matched-byte-count contracts |
| Diagnostics/exceptions | 8, 302, 312, 95 | Logging and real exception ownership; bugcheck remains fatal |
| Board/settings/power | 9, 25, 29, 49, 228, 358, 360, 365; data 41, 42 | Explicit virtual-device identity; firmware/persistent writes stay guarded |
| Crypto/integrity | 338, 339, 344, 345, 346, 347, 349; data 353, 355 | Existing SHA/HMAC; qualify cipher/RSA/key ownership separately |
| Driver/IRP/interrupt | 62, 74, 81, 83, 84, 85, 86, 87, 153, 176, 359; data 357 | Coherent device and request ownership; no general driver admission yet |
| Floating-point context | 139, 142 | Preserve modeled guest state, not just host registers |
| Network hardware | 252, 253 | Defer until reached; no invented PHY/link success |

The seven unimplemented data exports are 16, 41, 42, 156, 353, 355 and 357.
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
framework submodule pin and its own game release. Jet Set Radio Future is title
3 after dashboard and Blinx. Public packaging, complete attribution and license
resolution remain separate release work; this private port is not a publication.
