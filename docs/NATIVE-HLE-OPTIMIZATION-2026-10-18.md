# Optimized native Blinx and the HLE register boundary

## Stage 1 entrance texture lookup follow-up

The owner reported visible slowdown around the Stage 1-1 entrance ring while
audio continued normally. Short isolated observations at adaptive 21:9,
1080p, FXAA and 8x anisotropy reproduce a graphics-heavy CPU workload. An
independent eight-second leaf sample includes 21/388 samples in texture-owner
lookup and 20/388 in deferred-state synchronization. Leaf samples and service
timings are diagnostic estimates, not exact cycle attribution or GPU timings.
They do not establish that particles alone cause the slowdown.

Texture lookup now uses a 1 KiB direct-mapped slot hint before the retained
linear ownership search. Each hit checks the current table capacity and live
header; it stores neither object pointers nor ownership references. Collisions,
release/reuse, table relocation and alias replacement retain the original
lookup semantics. Texture-byte comparisons/uploads and every actual draw,
visibility query, effect, GPU completion and simulation step remain intact.
The 520-resource real GPU fixture now warms hints before growth and checks
reverse lookup across collisions and two complete release/reuse cycles.
Four graphics ABI/depth/alias/unsupported checks pass in 3.08 seconds.

Sequential observations with profiling and GPU readbacks disabled use the
same authentic copied QA save, original controller actions and four logical
CPUs at normal priority. Measurements align to the first initialized spawn
position, excluding selector/loading time. The 3-16 second windows give:

| Build | CPU ms/game update | Game updates/s | Mean host CPU load |
| --- | ---: | ---: | ---: |
| Previous owner build | 39.714 | 24.20 | 50.1% |
| Texture lookup candidate | 36.751 | 26.50 | 47.7% |

This is about 7.5% less process CPU time per update in this bounded sample,
not a sustained FPS guarantee. The initial four seconds also improve from
42.969 to 38.125 CPU ms/update, with differing host load. Both builds still
have room for improvement. Deferred-state work, draw submission and host
contention remain follow-ups under `beads-xbox.1.100` / `beads-xbox.1.7.3`;
the bounded 9/9 correctness scope is unchanged. No hardware was used.

Private evidence is retained under
`build/blinx/gameplay-coverage-20261017/entrance-perf-001` through `006`.
`002` missed selector input and was retired without a performance result;
`003` is the leaf observation; `005`/`006` are the unprofiled comparison.
All automated children are muted, isolated from owner saves and retired.
Previous executable SHA256 is
`207bfa1ebdef0d79158d168096d457a802c73882f5c12c8de37a0f0b94a94129`;
candidate is
`8dbd192a42e6f11c1b574e43ca92f383624cedd71a719370d9db60f9bbcb7a59`.

## Earlier compiler and register assessment

The gameplay snapshot was already committed before this work: framework
`6bb5fc0` and private BlinxRecomp `e2f57af`. Both repositories started clean.
Work continues on `perf/blinx-native-optimized-20261018`, tracked by
`beads-xbox.1.100`. The qualified reference floor remains separate from this
native HLE performance work. No Xbox hardware was accessed.

## Applied changes

The native title target previously forced `/Od` even in Release. It now defaults
to the build-time `PILOT_NATIVE_OPTIMIZATION=OPTIMIZED` profile: MSVC `/O2`
with `/fp:precise`, or GCC/Clang `-O2 -fno-strict-aliasing`. Shared guest memory
contains overlapping byte, word and floating accesses. No fast-math, direct
assembly or optional host instruction requirement was introduced. `BRINGUP`
retains `/Od`/`-O0`. The startup log reports the profile; the private
`native-build-profile-Release.aot-private.json` records compiler, register-header
hash and selected services. This is diagnostic provenance, not evidence of
cross-build/netplay compatibility.

The pinned CPU donor already emits a flat indirect-call table, but the runtime
never initialized it. Its whole-address span would allocate 416,842,952 bytes
on this 64-bit host because of one distant entry. The independent
`tools/native_dispatch.py` transformation adopts the newer donor's clustering
approach: index the largest cluster separated by gaps of at least 1 MiB;
resolve outliers through the complete sorted inventory. Allocation is bounded
to 32 MiB; failure keeps the exact binary lookup. This is a host lookup choice,
not switching CPU execution backends. Manual/kernel override ordering remains
unchanged. Initialization occurs once before guest threads run.

For Blinx, 8,871 entries occupy a 1,539,581-byte guest address span; the native
pointer index uses **12,316,648 bytes**. The distant 8,872nd entry stays valid.
Startup logs the selected lookup and index size. Old generated inventories
continue using binary lookup until the compact transformation is applied.

Incremental function recovery also retained an obsolete numeric search bound:
8,863 despite 8,872 actual sorted rows. This hid the final nine entries from
lookup. The count now derives from the C array; subsequent recovery refreshes
both count and compact range. Fresh generation applies the same transformation.
The original CPU donor remains pinned to `63f18f0`; no wholesale lifter/runtime
upgrade or unreviewed service replacement was made.

## Measured result and validation

One muted, hidden game ran at a time on logical CPUs 8/10/12/15 (four physical
cores), normal priority, guest CPU15. Each process loaded its own copy of the
same authentic completed save, used original selector input to enter Round1
Stage1, waited 100 game updates, then measured 180 stationary updates. Player
position remained `(96,-20,-42)`, screen9, unpaused. The widescreen mod selected
adaptive 21:9 presentation; the internal scene allocation remains 640x480.
These counters measure original main-loop updates, **not desktop present FPS**.

| Profile | CPU ms/update | Updates/s | Wall seconds | Host logical CPU sample |
| --- | ---: | ---: | ---: | ---: |
| Preserved `/Od`, binary lookup | 54.25 | 18.38 | 9.793 | 48.3% |
| `/O2 /fp:precise`, binary lookup | 38.45 | 26.13 | 6.887 | 64.8% |
| `/O2 /fp:precise`, compact lookup + count correction | 39.06 | 27.06 | 6.651 | 60.2% |

Compiler optimization reduced sampled process CPU per update by approximately
29%; the final profile remains about 28% below the baseline. Changing host
load, short samples and process-wide CPU accounting limit the comparison.
The compact table's extra performance effect is unresolved: do not attribute
the small wall-time difference to it or claim a CPU-time saving from it.
This is one scene, not a campaign-wide FPS guarantee or an isolated widescreen
cost benchmark. A failed first harness launch requested an out-of-range capture
interval and stopped as intended; it is excluded from the samples.

Executable SHA-256 identities:

- Baseline: `57ca004c37364219658dd31af37b4a31112b377696abc590d07393ee84b056d1`.
- Compiler-only: `61c646ab5b0cb1dd2620cf28ce953f8891025981efe126d9dfc1775c5e374ea7`.
- Final: `a7ae3b2be65a568134db1032dc490d0c65b747cd2ca8b44acd4f7b732ecfb98f`.

Validation is deliberately batched:

- 32/32 focused native runtime/ABI checks pass on the optimized build.
- Six dispatch checks include compiled optimized C for exact pointers, holes,
  unaligned addresses, high/low outliers, allocation failure, repeat init and
  the memory cap; stale inventory refresh and malformed inputs are covered.
- The actual 8,872-row generated inventory, with authored distinct stand-in
  bodies, passes 26,621 pointer comparisons against the complete binary search.
  This checks dispatch, not semantics of those CPU bodies. GCC supplies a
  second compiler for the new C; the actual title is compiled with MSVC.
- 13/13 original-function extent checks pass.
- Final optimized title reaches original Stage1 Results and writes its original
  6,000-byte save, then a fresh optimized process authenticates that save and
  reaches the stage selector without diagnostic guest writes. The completion
  run uses the previously authorized HP/position assists; original completion,
  scoring and save code execute. No completion/unlock flags are fabricated.
- All three measured title runs and the fresh load have no terminal guards.

The saved payload hash is
`f3f6a467a9d043759eb2c3c3272dcea8c06590cdba28c2883f39586d0728f506`.
Private evidence lives under `build/blinx/native-optimization-20261018` and
`build/blinx/gameplay-coverage-20261017/perf-*`; owned code/media/save bytes
remain ignored. Generated manifest changed from `2552fbe1...` to `be2bd2cd...`
only for the reviewed dispatch transformation/record; the register header
remains `84554b7c...`. No full campaign rerun is claimed for this compiler change.

## What the refreshed donors offer

Reviewed separate checkouts, leaving existing dependency pins intact:

| Source | Exact reviewed commit | Applicable finding |
| --- | --- | --- |
| [Phobos](https://github.com/phobos665/xboxrecomp/commit/7bf9c188f6f4dd5e31dec8c477ae6dedcc0b48d1) | `7bf9c188f6f4dd5e31dec8c477ae6dedcc0b48d1` | Compact indirect dispatch, native sampling, actual-return-based HLE ABI binding and unified TLS CPU layout. |
| [sp00nznet](https://github.com/sp00nznet/xboxrecomp/commit/eb0aa1d5fcb1ca34cdb6470fcfa471d87f431d73) | `eb0aa1d5fcb1ca34cdb6470fcfa471d87f431d73` | Current v0.14 portability/runtime/lifter reference; not a new accuracy guarantee for our title or reason to replace the qualified kernel floor. |
| [NoRain211](https://github.com/NoRain211/xboxrecomp/commit/74649ef5b0f023e7b878043bc6de2534c55e717e) | `74649ef5b0f023e7b878043bc6de2534c55e717e` | Unchanged since the earlier adoption audit; CPU/ABI reference, not an additional title HLE implementation package. |

Phobos's [single TLS CPU structure](https://github.com/phobos665/xboxrecomp/blob/7bf9c188f6f4dd5e31dec8c477ae6dedcc0b48d1/templates/runtime/recomp_cpu.h)
primarily removes repeated Mach-O `_tlv_get_addr` calls. Its own explanation
says Windows already uses a cheap GS-based access. It does not remove the
register semantics. A versioned shared register declaration is architecturally
useful, especially for future hosts, but no Windows speed gain is established
and changing its binary layout requires coordinated generated/runtime headers.

The [HLE binder](https://github.com/phobos665/xboxrecomp/blob/7bf9c188f6f4dd5e31dec8c477ae6dedcc0b48d1/tools/recomp/hle.py)
matches implemented exports and reads actual `ret` cleanup, with register-argument
handling for LTCG functions. Borrow that discipline at thin, revision-bound
adapters. Do not assume SDK name/signature alone proves a calling convention.
We already have source-bound family adapters; importing a whole donor kernel
would lose stronger local contracts without an identified benefit.

An independent external 50Hz, eight-second leaf sampler followed the
[donor profiler's design](https://github.com/phobos665/xboxrecomp/blob/7bf9c188f6f4dd5e31dec8c477ae6dedcc0b48d1/src/kernel/recomp_sample.c):
capture briefly, resume before resolving symbols, use the matching PDB.
The game thread's largest resolved hits were native graphics state setters,
resource lookup, texture preparation and deferred synchronization; translated
game functions also appear. The sample includes driver/OS addresses and waits,
and supplies no inclusive stack attribution. It does not establish exact cost
percentages. It supports investigating graphics synchronization before a broad
register-layout rewrite. Existing ABI diagnostics are already disabled by
default; turning them off again would accomplish nothing. Function-entry trace
hooks still carry required scheduler checkpoints, so removing them wholesale
would break the current suspension contract.

## Register and HLE contract policy

HLE may use native arguments, locals and host objects internally. At its entry,
return, callbacks, waits, exceptions and memory/device boundaries, it must retain
everything the selected guest caller can observe. We do not require every
intermediate device register write to be reenacted by a whole native service.
Raw accesses that remain exposed still need their modeled LLE contract.

| State | Current native representation | Constraint when optimizing |
| --- | --- | --- |
| EAX/ECX/EDX | Per-thread guest values | Marshal actual register/stack parameters; preserve declared results and any proven implicit arguments. A generic host ABI is insufficient. |
| EBX/ESI/EDI | Per-thread shared guest values | Retain across generated calls and service boundaries; some original routines pass implicit values here. Do not blindly turn them into function locals. |
| ESP and guest return slots | Guest memory and explicit stack adjustment | Preserve original return addresses, caller/callee cleanup, callbacks, stack reads and error/exception effects. |
| EBP/SEH and FS/TLS | Function frame locals plus thread/frame bridges | Keep original frame ownership and per-thread storage; serialized/native-call continuations are separate work. |
| Arithmetic flags and DF | Lazy operands/local flag fallbacks plus per-thread DF | Elide only after valid dataflow analysis. Cross-call/interrupt or observable PUSHFD/POPFD state cannot be invented. The broader full-native LLE flag tier is still open. |
| x87 | Per-thread double-backed stack, guest control word/status helpers | Preserve qualified rounding/control/stack effects. This is not a general proof of exact 80-bit exceptions/precision. No fast-math. |
| SSE/MMX | Per-thread vector/scalar helpers | Retain lane widths, ordering, control/state ownership and portable fallback. Broader architectural edge cases need their own evidence. |
| Device registers | Maintained machine models; native service ownership behind reviewed adapters | Keep CPU-visible state, reads, resource bytes/lifetimes, completion and callback ordering. Avoid per-register emulation inside a whole HLE service unless callers observe it. |

The current fast gameplay CPU is already statically compiled; **there is no
x86 interpreter fallback in this profile**. Unsupported targets/instructions
stop. The separate original-kernel LLE reference uses Unicorn where selected.
Eight thousand dispatch entries are not a percentage of all possible CPU
semantics or campaign behavior.

The [deferred state cache](NATIVE-DEFERRED-STATE-CACHE-2026-10-22.md) now removes
repeated conversion of unchanged state, with actual guest-byte comparisons,
native mutation revisions and reset invalidation. A same-process Blinx 2-1
comparison measured 21% lower process CPU cost per update. A steady 30 Hz
entrance remains unproven under concurrent machine load; this is not a claim
that all graphics costs are resolved.

Further graphics-state/resource optimization must locate all host-state mutations/invalidation at Reset,
state blocks, movie passes and resource aliases; retain guest state writes and
validate real GPU output. A dirty-state shortcut without those invalidations
would paint the architecture into exactly the corner this work is avoiding.
Native CRT/math replacements and local register promotion remain later options
when sampling identifies a concrete hot routine and its exact caller contract.
Full native original-kernel execution and complete restore/save-state support
remain independent open tasks, not prerequisites to this HLE speedup.
