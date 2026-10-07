# Phobos: Blinx launch, shared title contracts and the LLE foundation

Audit: `beads-xbox.1.60`; native pilot: `beads-xbox.1.61`. Owner direction,
2026-10-06: prioritize entering
Blinx and retain dashboard speed
as an active goal rather than a prerequisite. Follow the shared
[HLE policy](../../../recomp-template/HLE.md): build-selected implementations,
compatible caller behavior, freedom to remove internal work, no live handoff
requirement and no stubs.

The later owner attract goal supersedes this audit's initial parallel-title
proposal: finish and validate Blinx end to end before beginning further JSRF
work. Current checkpoints are in [the burndown](BLINX-BURNDOWN.md).

## Decision

Make Phobos the primary donor for the next **native title launch and whole D3D8
service** investigation. Do not restrict reuse to another graphics backend
behind our existing device pipeline. Keep xboxlle's maintained machine build
as the LLE reference. A donor-led HLE title build is a valid candidate product;
the repository containing it is not an architectural requirement.

Putting the entire running LLE machine inside Phobos is not a drop-in change.
The two runtimes own memory, scheduling, CPU state and devices differently.
Build separate implementations with shared title identities and service
contracts, then reuse implementation modules where their dependencies fit.
Do not reconcile two live machine states on every operation.

This audit establishes useful reuse boundaries and concrete local binding gaps.
It does **not** establish a local Blinx boot, a playable donor build, a speedup,
or full LLE/HLE interchangeability for either game. No donor runtime stubs were
incorporated. The existing optical authentication stop remains unresolved.

## Latest source and actual upstream claim

Fetched Phobos `main` on 2026-10-06 and checked out an isolated audit worktree:
`refs/xboxrecomp-phobos665-review-20261006`, commit
[`63f18f0334fbd8c00313c4bb565accf00627a4fd`](https://github.com/phobos665/xboxrecomp/commit/63f18f0334fbd8c00313c4bb565accf00627a4fd).
The previous graphics review used `7884426`. The new range changes 57 files,
including Blinx scaffolding, data-section classification, discovery/lifter
fixes, graphics behavior and virtual-memory handling. Older donor snapshots
and parked experiments remain intact.

Upstream's [working notes](https://github.com/phobos665/xboxrecomp/blob/63f18f0334fbd8c00313c4bb565accf00627a4fd/CLAUDE.md)
report Blinx, XDK 4831, reaching a drawn first level with movies playing on
5 October. The checked-in title entry is `0x000F4D97`. No exact donor Blinx XBE
hash or region is recorded in the inspected title profile. The latest PR's
"PAL" title refers to **Max Payne**; it also includes the Blinx work. This does
not disprove the owner's PAL Blinx report, but it is insufficient to identify
the exact disc. No Blinx frame-rate or complete-audio claim follows.

The [Blinx rendering account](https://github.com/phobos665/xboxrecomp/blob/63f18f0334fbd8c00313c4bb565accf00627a4fd/docs/technical/shadow-mode.md)
identifies three useful fixes: declarations using a nonzero base stream,
render-target feedback via a host copy, and empty texture-stage behavior. The
last rule is explicitly an inference from the title's picture, not a hardware
measurement. These are candidate shared implementations, not game-name checks.

The checked-in [JSRF investigation](https://github.com/phobos665/xboxrecomp/blob/63f18f0334fbd8c00313c4bb565accf00627a4fd/docs/technical/third-title-jsrf.md)
still ends with an unresolved object-pointer/cleanup discrepancy. It records
real generalization fixes, including a LOCK/refcount branch and TLS handling;
it does not support a JSRF gameplay claim at this pin.

## Your inputs: usable now, different revisions

The share `\\server\games\xbox` is accessible. It contains extracted Blinx and
JSRF directories **and** their ISOs; no new ISO needs to be built for this work.
The extracted XBEs were read again, SHA-256 checked against the prior inventory,
and parsed with both our bounded parser and the donor parser. Analysis writes
and local input copies are private; nothing was written into the source share.

| Input | XDK | Actual entry | Donor title entry | Non-preload section bytes |
| --- | --- | --- | --- | ---: |
| Blinx original NA, `e6b7cd8a5527...` | 4831 | `0x000F48D5` | `0x000F4D97` | 40,579,248 across 42 sections |
| Blinx NA Rev 1, `6f7dcd702f1f...` | 5558 | `0x000F362A` | `0x000F4D97` | 40,600,152 across 42 sections |
| JSRF NA, `f4441696fdd0...` | 4134 | `0x00147FF3` | `0x00148023` | 14,336 across 2 sections |

All three inspected local certificates report region mask 1 and DVD-X2 media.
For the donor experiment, prefer the **original XDK 4831 Blinx** rather than
silently substituting the XDK 5558 Rev 1 used in the optical probe.

There is a demonstrated profile hazard. Donor discovery seeds are selected by
title ID, not XBE hash. Its Blinx seed `0x00181800` is documented as the base of
`PSGSFD_I`; in our original NA copy it lands in **PSGSFD_B**. Our actual
`PSGSFD_I` base is `0x00181320`, and `PSGSFD_P` starts at `0x00181FE0`. On Rev 1,
all four donor Blinx seeds land in **XGRPH**. Merely landing in an executable
section or on an instruction boundary does not validate a seed.

Bind discovery seeds, overrides and generated code to the full XBE hash.
Regenerate addresses and validate their roles for each revision. A title ID
selects shared knowledge; it must not authorize an address patch. Do not assume
one constant address delta repairs a different link.

An extracted directory is sufficient input for Phobos's native loader and
file-backed services. Retail firmware's physical-media route has additional
requirements. Creating an XDVDFS image from a directory cannot recreate missing
original disc-authentication metadata. Existing ISOs remain useful for the
machine build; exact PAL reproduction would require that matching XBE/content,
but it is not necessary to begin with the supplied NA revisions.

## Applied checks on both local games

Used the existing real XbSymbolDatabase CLI at its matching donor submodule pin
`20eced544726f5558c5a408458f38a086cc4e543`, then ran Phobos's HLE and device-reference
audits against the local binaries:

| Measurement | Blinx NA 4831 | JSRF NA 4134 |
| --- | ---: | ---: |
| XDK symbols found | 287 | 363 |
| Named D3D8 functions | 129 | 131 |
| Donor HLE names resolving in this XBE | 48 of 89 | 48 of 89 |
| D3D device-global references found | 122 | 107 |
| Such references classified outside D3D | 0 | 0 |
| Kernel imports | 115 | 120 |
| Imports lacking a function/data route in donor | 0 | 0 |

There are **41 shared resolving HLE names and 110 shared imported ordinals**.
This is a concrete reason to use both titles early. Import presence is not
dynamic reachability, and a bridge existing is not evidence of its semantics.
The 89-name denominator includes services a given title need not use; 48/89 is
not a percentage of game compatibility.

Two findings affect the first integration directly:

- Neither `DrawIndexedVertices` nor its UP form was named in our Blinx XBE.
  JSRF resolves both. Blindly applying the current donor wrappers would miss
  Blinx's indexed draws. Recover the local bodies/bindings before claiming
  coverage; do not silently drop those draws.
- JSRF exposes `GetBackBuffer(BackBuffer, Type, ppBackBuffer)`, whereas Blinx
  exposes `GetBackBuffer2(BackBuffer)` returning the pointer. These need
  distinct ABI adapters to a shared surface implementation, not name aliasing.

`BeginPush` was **not named in either image**, so raw command submission remains
unmeasured. The zero outside-reference count is useful static evidence, not
proof that all GPU access passes through intercepted APIs. Title-written
commands must be handled, or clearly fault as unsupported when reached.

Ran donor discovery on the local Blinx with its own entry and **no donor address
seeds**, retaining linked library sections and marking `MDL*`/`MAP[0-9]*` as
candidate data. It completed in 31.01 seconds, finding 501,420 instructions and
8,855 functions. This is analysis output, not executed or AOT-covered code.
Those data exclusions remain a scoped hypothesis for this image; static
discovery does not prove that an unknown indirect branch cannot enter them.
The corresponding unseeded JSRF discovery completed in 40.95 seconds with
586,309 instructions and 8,847 functions. Neither run generated a title
executable or exercised guest gameplay.

Focused upstream checks passed: **105 tests and 14 subtests** across section
selection, function boundaries, jump tables, 16-bit push/pop, device-reference
analysis, kernel routing, HLE bindings/variables/LTCG and flag tracking. These
checks validate their stated cases, not complete CPU or service fidelity.

## What to borrow, and what needs a different implementation

| Donor area and source | Useful mechanism | Required adaptation / unresolved contract |
| --- | --- | --- |
| `tools/xdk_symbols`, `tools/recomp/hle.py`, `src/hle/hle.h` | Name-based XDK identification, real `ret N` cleanup, register/stack argument adapters | Hash-bound profiles; resolve aliases/interior entries; keep game/SDK addresses out of shared bodies |
| `tools/disasm/{loader,functions,engine}.py` | Exclude known data, discover library callbacks and correct function tails/switches | Reuse findings with our CPU floor; reject unsupported semantics rather than emitting silent no-ops |
| `src/hle/hle_d3d8*.c` plus `src/d3d` | Whole draw/resource/state services, host vertex programs, stream layouts, render-target feedback | Own the selected D3D contract, guest-visible resources and completion; account for raw command writers |
| `src/d3d/d3d8_device.c`, shader/resource caches | Ring buffers, cached host state, meaningful shader keys, content-based vertex-program reuse | Invalidate on changed inputs and preserve ownership; measure an actual scene |
| `src/kernel/xbox_memory_layout.c`, `bridge_XeSection` | Identifies Blinx's section residency problem | Implement real section load/unload/refcount/zero-fill/reload behavior; don't silently treat extra host mapping as the retail RAM contract |
| `src/kernel/kernel_bridge.c`, file/event services | Working service bodies and guest-pointer/handle adapters where applicable | Audit reached effects and allocation ownership; a routed or named export can still be incomplete |
| DirectSound buffer/stream services | Native audio processing with completion tied to actual consumption | Retain callbacks, buffer lifetimes and sample delivery; qualify on reached Blinx/JSRF workloads |

The new Blinx memory workaround is material: Phobos makes all XBE sections
resident and expands the mapped address range when non-preload data exceeds
1 MiB. It leaves reported RAM unchanged. `bridge_XeSection` updates reference
counts but does not restore original section bytes on reload. Upstream itself
records that limitation. Host caching is legitimate HLE, but reload contents,
allocation failures, shared boundary pages and observable memory accounting
still belong to the caller contract. LLE can execute the original loader;
an HLE loader can cache immutable bytes and commit them efficiently.

Several donor paths cannot be imported unchanged: unresolved calls and missing
kernel bridges can return zero; the media response sets authentication/CDF
success without a drive model; the NV2A acknowledgement thread mirrors GET to
PUT and completion counters independently of required-work validation. A real
HLE implementation may remove polling and physical protocol steps, but must
perform the required operation and publish outputs before a dependent observer
sees completion. Unsupported reached behavior must stop explicitly.

Many D3D wrappers still call `HLE_CALL_ORIGINAL` before host rendering. This
helps explain what guest state is currently consumed, but is not the intended
shipping design for our replacement. We need neither shadow execution nor a
duplicate LLE device kept current in the HLE build. Expand the owned boundary
where necessary to remove those internal dependencies.

The donor CPU is also not our faithful CPU backend: direct guest-VA pointer
access and thread-local register state differ from our MMU/Unicorn-owned state;
its x87 representation has documented limits. The current
`recomp_eflags_push`/`recomp_eflags_pop` helpers preserve AC/ID/DF and provide
fixed IF on push, rather than saving/restoring arithmetic flags. The flag tests
check dependency tracking, not complete PUSHFD/POPFD equivalence. Passing those
tests does not qualify that behavior as LLE. Borrow corrected discovery/lowering
cases with conformance checks, rather than replacing the CPU floor wholesale.

## Is grafting our LLE into Phobos less work?

| Shape | Assessment |
| --- | --- |
| Use Phobos unchanged as the whole product | Fastest way to reproduce its own demonstration, but does not meet our no-stub or maintained-LLE requirements |
| Put the complete xboxlle machine under its existing kernel/memory runtime | Two owners of threads, mappings, handles, IRQs and device completion; substantial adapter/rewrite work, no demonstrated saving |
| Keep separate machine/native builds and share qualified services and bindings | Recommended next spike. Allows donor-led HLE without paying for every LLE internal operation or inventing live state transfer |
| Import only another host graphics backend behind our current pipeline | Already tested in a parked branch; does not remove guest XDK, device decode and CPU boundary costs |

The compiled donor functions currently use direct guest-address memory macros,
host-thread TLS and native calls. Our AOT executor borrows architectural state
from Unicorn, translates RAM access and tracks invalidation. A wrapper around
one function-pointer type does not reconcile these semantics. Similarly, a host
event handle cannot be handed to a running retail kernel's scheduler.

This is a reason to replace a **whole selected service owner**, not a rule
against native services. A kernel-HLE build may own its own scheduling and
handles; its LLE counterpart executes the original kernel. They must satisfy
the same supported caller behavior, but need no identical internal state or
cross-build savestate format. Until a boundary is qualified, label the native
pilot as exploratory rather than claiming a complete interchangeable floor.

Phobos's published TimeSplitters 2 experiment reports 12.4 to 5.0 ms per frame
after seven combined changes, including buffer/state caches, avoiding repeated
shader work and reduced logging. That supports this direction, not a forecast
for our titles. Its baseline, renderer replay and combined-change limits are
documented in [the donor performance report](https://github.com/phobos665/xboxrecomp/blob/63f18f0334fbd8c00313c4bb565accf00627a4fd/docs/technical/ts2-performance-plan.md).

## Next work, with observable exits

Owner clarification after this audit: HLE-first delivery is authorized. LLE is
a bounded integration/reference implementation and can advance alongside HLE;
exhaustive device fidelity is not a prerequisite. Scope correctness claims to
the exercised caller contract, retain known small gaps, and fix details as they
matter. Required operations must still execute; unsupported reached work stops.

Later distribution clarification: GPL code is reference-only for the intended
non-GPL product. This supersedes the recommendation to port Phobos's service
bodies directly without a file-level license check. The private native pilot
can exercise its generator/runtime header as reference material; shipping
implementations must use suitably licensed code or independent implementations
of the observed contract. See [license scope](../LICENSE-SCOPE.md).

1. **Blinx native-title pilot.** Use the original
   NA XDK 4831 copy and exact hashes. Recover the missing indexed-draw binding
   and revision-specific discovery roots. Reuse donor generation where it
   produces supported semantics; unsupported code/services stop or use a real,
   deliberately integrated CPU implementation. Do not ship generated no-op
   stubs. Exit: actual XBE entry/CRT progress and the first named failing call,
   followed by menu/movie/level milestones as reached.
2. **Real loader and launch contract.** Qualify section lifecycle, guest-visible
   memory, TLS, handles and the title's required media behavior. The native
   loader may skip firmware mechanics, but must supply a supported post-load
   environment. A folder is not automatic proof of an authenticated physical
   disc. Retain the separate optical LLE investigation; do not force every HLE
   scene iteration through firmware boot or DVD command emulation.
3. **Whole D3D8 replacement with donor internals.** Build-selected native
   state/resources/draws/presentation, with correct observable completion.
   Reuse the three new Blinx rendering fixes when exercised. Preserve all
   required draws; raw push-buffer use is a real coverage item. Exit: an
   interactive scene with known visual/completion scope and measured frame cost.
4. **Generalize with JSRF after Blinx's attract goal is validated.** Exercise its
   older XDK adapters, allocation/TLS behavior and actual surface-return ABI.
   Reuse shared services; do not build two independent runtimes. A confirmed
   title-specific binding is allowed; shared code must not contain guessed
   per-game addresses or unsupported-success cases.
5. **Recheck the dashboard after shared changes.** Keep the original-speed goal
   open. Revisit the preserved renderer/service/cache branches only when the
   reached workload satisfies their documented retry conditions. No dashboard
   60 FPS prerequisite and no promised incidental speedup.

Full AOT remains the final CPU goal. Discovery statistics, host presents,
instruction reductions and upstream reports are not substitutes for local
title progress and measured useful work.

## Reproduction and private evidence

Main checkout `build/phobos-audit-20261006/` contains local XBE copies, parser
metadata, per-title symbol and HLE reports, device-reference results and
discovery logs. `build/phobos_input_audit_20261006.py` reproduces the input and
kernel-routing comparison. Exact hashes remain in the private comparison JSON
and the earlier [input audit](RESULTS-AOT-FOUNDATIONS-2026-10-02.md).

The symbol scanner is `build/xbsdb/projects/cli/XbSymbolDatabaseCLI.exe`.
Run Phobos `tools.xdk_symbols` with that explicit CLI, then `tools.hle_audit`
and `tools.hle_audit.device_refs` on each private XBE. The latter explicitly
reports the missing `BeginPush` evidence. All borrowed source remains pinned
in the donor checkout; this audit imports no new runtime source or game code.
