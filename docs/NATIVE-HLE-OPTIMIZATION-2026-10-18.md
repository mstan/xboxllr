# Optimized native Blinx and the HLE register boundary

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

Next optimization scope is the measured graphics-state/resource synchronization
cost. Cache only after locating all host-state mutations/invalidation at Reset,
state blocks, movie passes and resource aliases; retain guest state writes and
validate real GPU output. A dirty-state shortcut without those invalidations
would paint the architecture into exactly the corner this work is avoiding.
Native CRT/math replacements and local register promotion remain later options
when sampling identifies a concrete hot routine and its exact caller contract.
Full native original-kernel execution and complete restore/save-state support
remain independent open tasks, not prerequisites to this HLE speedup.
