# STATUS

## 2026-10-03 - Bulk AOT register transfer preserves the full dashboard route

The opt-in `--aot-bulk-registers` path replaces twenty individual public register
API calls per native invocation with bulk transfer through the pinned core's
own state and lazy-flag helpers. Context, memory, event and code guards remain
active; extended CPU state and native coverage are unchanged. The public API
remains the default and explicit opt-out. No stubs were added.

All **79 tests pass**, including real floor/API/bulk instruction-prefix checks
and extended-state handoffs at O0/O3. Both **27B** routes preserve all **270
checkpoints, 28 captures and native/idle/device counter rows**, also matching
the previous reference. All three donor register models were reviewed; phobos's
new fog-coordinate fix is recorded as a separate renderer candidate.

Performance is **not qualified**: both functional runs had competing builds,
and the subsequent strict timing attempt refused to launch on the busy host.
The optimization remains experimental. Full AOT and sustained 60 FPS remain
open; menu performance still precedes Blinx. See the
[implementation and evidence](docs/RESULTS-AOT-INTEGER-TRANSFER-2026-10-03.md),
tracked in `beads-xbox.1.43`.

## 2026-10-03 - Menu60 renderer coverage expands; performance gate remains open

The experimental xemu shader path now handles owned decoded DXT1/DXT2-3,
single-level TENT mip filtering and effect-free unused PROJECT2D stages.
The full 27B menu/Settings/return route executes **5,349 donor draws / 1,357,402
triangles**, up from 1,035 / 806,426. That is **63.48% of generated batches**,
with 3,077 genuine reference-texture fallbacks. No stubs were added.

All **77 tests pass**, including actual GPU minification/content-replacement
checks. The same executable's reference opt-out exactly reproduces all **270
checkpoints and ten images**. Enhanced CPU/device history and native counters
remain equal apart from rendered DMA bytes. Initial R0-alpha reads now decline
until their reference contract is qualified (`beads-xbox.1.42`).

This is coverage progress, **not a demonstrated speedup or 60 FPS result**.
Competing builds invalidate both final timings; their last window observes
7.554 reference / 6.866 enhanced guest vblanks per host second. Whole-scene
numerical tolerance also remains open, including newly changed Clock/Settings
captures. Native CPU body/services, vertex processing and host submission remain
substantial measured costs. Menu60 stays active before Blinx, with partial AOT
allowed. [Detailed evidence](docs/RESULTS-XEMU-PSH-2026-10-03.md#menu60-follow-up-decoded-textures-and-unused-stages).

> The single source of "what is true right now." No competing status lives elsewhere.
> Newest entry first. Dates are absolute.

## 2026-10-03 - All three xboxrecomp donors refreshed for menu performance

The owner requires continued assessment and useful borrowing from sp00nznet,
NoRain211 and phobos665, including relevant title runtimes. Earlier assessment
named all three but had uneven depth. Remote HEAD checks retain sp00nznet
`1409a7d` and NoRain211 `7adaf21`; phobos advanced to `f01b20d`. The associated
DOAXBV runtime advanced to `aa1d3c8` from our September audit. New detached
worktrees preserve both older reviews.

Concrete candidates now include sp00nznet's content-checked DXT decode cache;
phobos's Vulkan state tracking, RHI/resource code and disk shader cache;
NoRain's carry/jump-table compiler fixes; and DOAXBV's compressed uploads,
buffer rings and presentation worker. Their specific coherence/completion and
ownership limitations are recorded alongside the useful code. Stubbed portions
do not disqualify an entire donor. The DOAXBV donor now reports playable 60 FPS
sessions; we have not reproduced those private-input runs.

Fresh selected donor tests pass: NoRain **12 tests plus 4 subtests**; phobos
**14 flag-join/backedge tests plus 1 incremental-write test**. These are compiler
and tooling checks. No donor backend/game benchmark or new runtime integration
is claimed. Dashboard 60 FPS remains the active gate before Blinx. See the
[all-donor assessment](docs/RENDERER-REUSE-2026-10-02.md), tracked in
`beads-xbox.1.41`; the recurring workflow is also in NORTH-STAR and the gameplan.

## 2026-10-03 - Sustained 60 FPS menu is the gate before Blinx

The owner's latest direction supersedes the earlier Blinx-next order. Active
work is original-speed 60 FPS in the animated stock dashboard main menu with
working selection/input. Compatible enhanced/HLE paths and partial AOT remain
allowed, with a runnable LLE opt-out and no stubs. Full AOT and every dashboard
interior remain separate later gates. Blinx launch now depends on menu issue
`beads-xbox.1.17` in central tracking.

No new speedup is claimed. The preceding single timing pair measured **6.919
reference / 6.478 enhanced guest vblanks per host second** in the steady final
window; distinct displayed FPS and input response remain unqualified. Re-reading
the rejection trace identifies DXT2/3 format `0x0E` at the first texture decline:
the floor decodes it to owned ARGB, but donor admission excludes it. This is a
concrete coverage candidate, not proof that texture declines dominate time.
Next qualification pairs that boundary with steady-window CPU/GPU/resolve costs.

The [gameplan](docs/GOAL-AOT-DASHBOARD.md#active-menu-performance-gate) records
the menu workload, evidence gaps and reordered burndown. Runtime code is unchanged
by this priority update; the preceding implementation passed 77/77 tests.

## 2026-10-03 - Borrowed xemu pixel shaders run the full dashboard route

The experimental `--nv2a-renderer xemu` path executes xemu's pinned production
pixel-shader generator through the existing owned-packet/resident-surface
contract. The 27B dashboard/Settings/return route completes with **1,035 donor
draws / 806,426 triangles**, and 7,391 generated batches use reference shaders.
Startup opt-out is `opengl` or `software`. No donor machine/device shims or
placeholder shader outputs were incorporated. The full suite passes **77/77**.

The reference still matches all prior checkpoint logs and ten images. Enhanced
execution matches CPU/input/device-event/DMA-write counts, with recorded rendered
byte differences. Clock and Settings-interior captures are exact; six main-menu
captures differ, up to 30/255 in a channel. Whole-scene tolerance remains open.

The first sequential timing pair is **311.280 s enhanced / 245.645 s reference**.
It has host variation before donor draws and is not a qualified backend-cost
measurement; there is no speedup or 60 FPS claim. Keep the path experimental.
This completes the live borrowed-component checkpoint. Its then-proposed
Blinx-next ordering is superseded by the menu-first entry above. Findings and limits:
[results](docs/RESULTS-XEMU-PSH-2026-10-03.md),
[contract](docs/RENDERER-XEMU-PSH.md). Tracking: `beads-xbox.1.15.7`.

## 2026-10-03 - Delivery plan now prioritizes enhanced execution and game progress

The owner's clarified target is a faithful runnable LLE floor with interchangeable
enhanced/HLE implementations for any subsystem. Qualified enhanced paths are the
shipping default, with global/per-subsystem reference opt-outs. Implementations
may differ internally; observable state, errors, ordering and declared numerical/
presentation tolerances define compatibility. Stubs remain forbidden.

The [revised gameplan](docs/GOAL-AOT-DASHBOARD.md) puts a borrowed enhanced
renderer and a real Blinx launch with the existing hybrid CPU path ahead of broad
AOT coverage expansion. Full AOT with dynarec removed remains a final gate in
both profiles. The PSX/Tomba renderer facade, software fallback and BIOS-HLE
dispatch were inspected as architectural precedents. This is a planning change:
no renderer transplant, new HLE service, game boot or 60 FPS result is claimed.
Implementation subsequently resumed at the owner's request, starting with the
borrowed renderer boundary (`beads-xbox.1.15.7`). Planning: `beads-xbox.1.40`.

## 2026-10-03 - Memory integer AOT raises main-menu coverage to 77.60%

General memory-read arithmetic/comparisons, register NEG/NOT and LEAVE qualify
40 additional regions, preserving all 246 prior regions. Existing RAM, code,
event and context guards remain active; ABI 5 is unchanged. All **76 public
tests** and **four census scalar/native-required tests** pass, including strict
native boundaries and precise read refusal.

Floor/native O0/O2 complete the **27B** route with all **270 checkpoint rows and
ten images byte-identical**, also matching the prior reference. Device/DMA/
IRQ/TSC endpoints and native adapter counters agree. Main-menu idle native
coverage rises **70.53% to 77.60%**, and Settings entry **76.72% to 82.08%**.
Full-route native work reaches **1,964,141,698 instructions**, with unchanged
idle skips and six reconciled census intervals.

Attempts and handoffs increase; this is a coverage result with no new speed
claim. Full AOT, required menu interiors, UI audio and original-speed 60 FPS
remain open. Keep OpenGL and prioritize qualified renderer reuse. See
[memory integer evidence](docs/RESULTS-AOT-MEMORY-INTEGER-2026-10-03.md), tracked
in `beads-xbox.1.39`. The ordinary build keeps private input cleared.

## 2026-10-03 - Host profiles now separate the full post-boot route

The host sampler previously merged every sample after 10.5B while labelling that
bucket as ending at 12B. It now preallocates 32 windows, reports actual sampled
extrema and marks the final range unbounded. The symbolizer preserves recorded
bounds and marks old clamped tails as having an unknown end.

All **76 public tests** pass, including a real Windows sampling/report probe.
A quiet **27B** dashboard run preserves all **270 checkpoints, ten images and
every native/device counter row** against the prior reference. Its **101,462
verified samples** are fully accounted across **18 windows**, with no overflow
or sampler failures. The steady main-menu window identifies native admission/
register access and generic-combiner/vertex work; library caller pairs also
identify render submissions, uploads and readbacks.

This improves attribution, not native coverage or product speed. Full AOT,
required menu interiors, UI audio and original-speed 60 FPS remain open. See
[range qualification and findings](docs/RESULTS-HOST-SAMPLER-WINDOWS-2026-10-03.md),
tracked in `beads-xbox.1.38`. The ordinary build has private input cleared.

## 2026-10-03 - Exact AOT membership cuts qualified startup wall time by 17.39%

An exact sparse byte index replaces repeated tree searches in the native-entry
membership query. Actual execution keeps its context, translation, live-code
and decoded-entry guards. The 246-region input uses 41,984 bytes of membership
storage plus allocator metadata; the binary grows 512 bytes. The switch is now
ON by default, with the prior lookup available for comparison.

All **74 public tests** pass. Full **27B O0/O2** runs preserve the reference's
**270 checkpoint rows, ten images and every native adapter counter**. Six quiet,
uninstrumented interleaved startup timing legs retain identical images, guest
deadlines, native work and device/DMA/IRQ/TSC counters. Total 12B wall-time median
falls **80.0442 to 66.1236 seconds (17.39%)**; minimum falls **17.04%**. Both pass
the 5% retention gate with no override or excluded pairs. The final four windows
improve only 2.25% at the median; this does not establish menu or displayed FPS.
The final public build clears private input and passes all 74 tests again. A
subsequent clean 27B diagnostic profile preserves every checkpoint/image and
identifies substantial native-body and renderer costs in the post-boot intervals.
The sampler's clamped final range needs truthful bounds (`beads-xbox.1.38`).

See [qualification and measurement](docs/RESULTS-AOT-MEMBERSHIP-2026-10-03.md),
tracked in `beads-xbox.1.37`. Native coverage is unchanged. Full menu AOT, required
menu interiors, UI audio and original-speed 60 FPS remain open. Keep the working
OpenGL renderer and prioritize qualified donor reuse.

## 2026-10-03 - Scalar AOT raises executed main-menu coverage to 70.53%

General shifts, all short/near branch conditions, MOVZX and immediate stores
qualify **246 real code regions**. ABI 5 adds exact byte/word RAM reads with
page, alias, permission and code-identity guards. Public tests pass **74/74**;
strict machine tests prove no hidden fallback at the checked boundaries.

Floor/native O0/O2 complete the **27B** route with all **270 checkpoint rows and
ten images byte-identical**, also matching the prior reference. Device/DMA
history endpoints and native adapter counters agree. Main-menu idle native
coverage rises from **47.92% to 70.53%** of executed instructions; Settings entry
rises from **61.32% to 76.72%**. Full-route native work reaches **1,815,586,408**
instructions, with unchanged idle skips and six reconciled census intervals.

The wider selection increases native/fallback handoffs substantially, including
two regions that never pass admission. This is a correctness/coverage result;
native performance must be measured and improved separately. Full AOT, required
menu interiors, UI audio and original-speed 60 FPS remain open. An existing
error-code CPU exception delivery gap is now tracked as `beads-xbox.1.36`.

See [scalar evidence and overhead findings](docs/RESULTS-AOT-SCALAR-2026-10-03.md).
Keep the working OpenGL renderer and prioritize qualified donor reuse. Source
integration is recorded in `beads-xbox.1.35` on
`perf/measurement-first-2026-09`; private artifacts remain excluded.

## 2026-10-03 - General kernel integer AOT raises Settings-entry coverage to 61.32%

Qualified ROL/ROR32, OR32 and BSWAP lowering adds five measured kernel regions.
Independent count/flag/alias tests and strict machine execution pass; all **72
public tests** pass, with native-required census tests **2/2**. Final floor/native
O0/O2 complete the **27B** Settings route with all **270 checkpoint rows and ten
images byte-identical**, also matching the previous expanded-x87 floor. Native
partitions and every adapter counter agree.

Settings-entry native coverage rises from **29.33% to 61.32% of executed
instructions**. Main-menu idle remains **47.92%**. Across the full route, another
**507,548,876** instructions execute natively, for **1,379,683,118** total; idle
skips are unchanged. Six intervals reconcile with 54,522 census identities and
zero overflow. This is dynamic coverage evidence, not a 60 FPS result.

See [kernel integer evidence and remaining gaps](docs/RESULTS-AOT-KERNEL-INTEGER-2026-10-03.md).
Full menu AOT, menu interiors, UI audio and original-speed 60 FPS remain open.
The retained OpenGL renderer and qualified donor reuse remain the rendering
direction. This checkpoint groups the accumulated implementation and evidence
for integration on `perf/measurement-first-2026-09`; commit status is recorded
in `beads-xbox.1.34` and the active goal issue. Private artifacts stay excluded.

## 2026-10-03 - Expanded x87 AOT reaches 47.92% of executed main-menu idle

General register multiply/subtract/free and TEST32 lowering now qualify five
real dashboard regions. Independent cases exposed and corrected FFREE pending
exception handling, extended denormal flags and memory-multiply exception
priority before native admission. Public tests pass **72/72**; census x87 tests
pass **3/3**. Unqualified exception/stack/memory cases retain explicit fallback.

Final floor/native O0/O2 complete the **27B** Settings-entry/return route with
**270 checkpoint rows and all ten images byte-identical**, also matching the
previous qualified floor. Native partitions and all adapter counters agree.
Native work rises by **170,378,215** instructions to **872,134,242**, with idle
skips unchanged. Main-menu idle coverage rises from **39.33% to 47.92% of
executed instructions**; all six phase intervals reconcile, with 56,589 census
identities and zero overflow.

See [expanded x87 evidence and limits](docs/RESULTS-AOT-X87-EXPANSION-2026-10-03.md).
Full menu AOT, required menu interiors, UI audio and original-speed 60 FPS remain
open. Retain the working OpenGL renderer and prioritize qualified donor reuse;
a graphics API change is not a goal. These instrumented comparisons make no
performance claim. Source changes remain uncommitted.

## 2026-10-03 - General x87 AOT executes the dashboard math loop

Fixed x87 services now execute the measured dashboard math region using shared
80-bit SoftFloat state. Independent fixtures first exposed and corrected floor
status, pending-exception and precise-store defects. Native operations preserve
rounding/precision, tags, environment and memory/code coherence; unsupported
stack/control/memory contexts decline without effects. Public tests pass
**72/72**, including strict native O0/O3 and independent rounding expectations.

The updated floor and native O0/O2 complete the **27B** Settings-entry/return route
with all **270 checkpoints and ten images identical** to the previous qualified
run. Native instruction partitions and adapter counters also agree. The new
x87 region executes 263,882,564
instructions natively. Main-menu idle native coverage rises from **26.04% to
39.33% of executed instructions**, with idle skips unchanged. The refreshed
census has 57,227 identities, zero overflow, and six reconciled intervals.

See [x87 implementation, evidence and limits](docs/RESULTS-AOT-X87-2026-10-03.md).
Broader x87 exception/stack semantics, kernel/memory coverage, full menu AOT,
required menu interiors, UI audio and original-speed 60 FPS remain open.
Renderer reuse policy is unchanged. These instrumented comparisons make no
performance claim. Source changes remain uncommitted.

## 2026-10-03 - All idle phases qualified against fully executed dashboard

The guarded idle optimization now covers all eight instruction boundaries,
including the STI interrupt shadow, and requires cached RAM translations and
valid CPU/debug/observer state. The **27B** route completes on native O2 plus
skipping, floor plus skipping, and floor executing every idle instruction.
All ten captures and complete device/DMA history digests match each other and
the preceding qualified run. The two skip-enabled arms have identical 270-row
checkpoint streams. Public tests pass **69/69**.

This removes another **4.294 billion idle instructions** from execution. The
native menu loop still executes exactly the same work; its current share is
**26.04% of executed main-menu idle instructions**, with all actual fallback
included. This increase from the four-phase exact-deadline result's 10.02% is
an idle optimization, not broader native instruction support. The refreshed
census retains 57,304 block identities with zero overflow and all six menu
intervals reconcile. The largest remaining main-idle block is x87 math.

See [phase, memory and dashboard evidence](docs/RESULTS-IDLE-PHASES-2026-10-03.md).
Full menu AOT, required menu interiors, UI audio and original-speed 60 FPS remain
open. Renderer reuse policy is unchanged; these concurrent instrumented runs
make no performance claim. Source changes remain uncommitted.

## 2026-10-03 - Native menu loop and matching dashboard execution histories

General AOT MMX transfers and cache hints now execute the measured dashboard
copy loop. At this four-phase checkpoint, native share was **10.02% of executed main-menu idle
instructions**, excluding idle skips. The earlier 26.11% measurement used the
old block scheduler; exact deadlines expose more executed idle work. Pinned
CPU-floor MMX state/fault defects were reproduced
and corrected first. ABI 3 keeps the shared x87/MMX state inside the engine and
uses fixed helpers without hidden guest execution. Public tests pass **68/68**;
the census build also passes an authored table-collision fixture.

The real **27B** floor/O0/O2 route has identical 270-row execution/device/DMA
checkpoint streams and all ten same-clock images. Settings entry and return
were visually verified. Native variants retire 437,873,463 instructions through
the compiled loop and have identical instruction partitions and adapter counters.
The previous event-order gap led to exact fallback deadlines, retained counts
for cached block-length variants, and a reproduced CMPXCHG8B lazy-flag fix.
No guest exception or device stop was bypassed. The input schedule was revised
because the former first button press arrived before the clock UI accepted it.

The repaired census retains all 57,301 observed block identities with zero
overflow; all six menu intervals reconcile. Its replay preserves the qualified
history and images. See [MMX evidence](docs/RESULTS-AOT-MMX-2026-10-03.md) and
[deadline, flag and route qualification](docs/RESULTS-CPU-DEADLINES-2026-10-03.md).
This qualifies observed comparisons, not all RAM or real-console behavior.
Full menu AOT, UI audio and original-speed 60 FPS remain open. Renderer reuse
policy is unchanged. Native remains experimental; source changes remain uncommitted.

## 2026-10-02 - Primary goal pivots to faithful AOT dashboard

The owner made the [AOT dashboard at original-speed 60 FPS](docs/GOAL-AOT-DASHBOARD.md)
the primary goal; Blinx boot is the stretch goal. Reuse qualified existing
renderer code as broadly as practical. The working OpenGL path remains the
baseline; xemu's graphics backends are primary candidates. The
[source assessment](docs/RENDERER-REUSE-2026-10-02.md) records reusable components,
dependency/contract work and excluded stubbed wrappers. No new renderer runtime
has been imported by that assessment.

Compiler implementation continues under `beads-xbox.1.25.5`. Full menu AOT,
complete dashboard device coverage and sustained 60 FPS remain open goals.

The launcher now admits more than 64 AOT regions by default, with indexed range
lookup in the machine adapter. Production-generated 257-region O0/O2 fixtures
complete their declared warm strict scope without CPU fallback, preserve stable
counters and reject changed guest code. Actual launcher probes validate default
257-region, bit-63 and zero selection. See the
[registry evidence and limits](docs/RESULTS-AOT-REGISTRY-2026-10-02.md).
Full Release tests pass 63/63. The existing two-region native dashboard replay
also passes 12B without fault and exactly matches the reference frame and prior
native/fallback/idle counts. This is regression evidence, not full menu AOT or a
new performance claim. Source changes remain uncommitted.

The newer 22B OpenGL census route reaches the inspected Music/Live/Settings
selections, Settings interior and main-menu return. Nine cumulative snapshots
produce six reconciled menu intervals. Neither existing startup AOT region is
executed in those intervals. Prominent first-code gaps are MMX transfers, x87
math and kernel operations; counts do not establish host cost or code-version
stability. General 32-bit LEA lowering now passes independent and differential
O0/O2 tests; full Release remains 63/63. See the
[phase evidence and compiler limits](docs/RESULTS-DASHBOARD-PHASES-2026-10-02.md).

## 2026-10-02 - AOT foundations qualified; faithful Blinx launch remains open

The owner authorized implementation with no stubs, LLE first, and HLE only after
its implemented LLE counterpart and full behavioral contract are qualified.
The new XBE inventory, bounded guest CALL/RET/stack lowering and selected x87
helper qualification pass their checks. An isolated ten-instruction Blinx path
passes O0/O2, including every prefix, full RAM comparison and zero fallback in
an explicitly warmed scope. It is not game initialization or gameplay.
Evidence and limits: [AOT foundations](docs/RESULTS-AOT-FOUNDATIONS-2026-10-02.md).

The drive audit found pre-existing unconditional HDD unlock acceptance and a
freeze-lock no-op. Their initial removal stopped retail startup at 17,940,376
instructions. The follow-up implements independently provisioned password
checking, lockout, freeze, protected media access and reset/IDENTIFY states.
The owner also authorizes an explicit unlocked-drive option. A provisioned
retail startup now completes the 12B clock-accept replay without fault, reaches
the dashboard menu and exactly matches the existing final-image SHA-256. Security
ends enabled/unlocked (`0x0003`). See [HDD security setup, evidence and limits](docs/HDD-SECURITY.md).
Broader drive qualification remains open in `beads-xbox.1.28`. The DVD
model has no media backing and the APU has no audio output. Blinx has not been
booted. Its [launch burndown](docs/BLINX-BURNDOWN.md) has four pending gates,
including the newly identified drive-security repair. The owner supplied Blinx
and JSRF images for the input audit; revisions must remain explicitly pinned.

Validation: the final full Release CTest suite passed 61/61, including ATA
security behavior and credential provisioning/CLI tests. No new performance result or full-game
coverage claim is made. Source changes are still uncommitted in the working tree.

## 2026-09-06 - Stage g completed; faithful 60 FPS with headroom remains the target

The interactive path now uploads native guest scanout bytes, replaces the fixed 33 ms
throttle with guest-vblank pacing, and measures SDL presents separately from guest speed
and flip writes. SDL already had a D3D11/DXGI swapchain; the remaining GL/RAM/SDL transfer
is a separate measured opportunity. `--interactive-present-legacy` restores scalar capture
and the old throttle for same-binary comparisons. Details and commands:
[`PERF-PRESENT-2026-09-06.md`](docs/PERF-PRESENT-2026-09-06.md).

Final noperf, clean interleaved 3+3: median capture cost **453.2 -> 8.8 us** and
capture-to-present return **1345.4 -> 769.3 us**; successful SDL calls **16.50 -> 18.76/s**.
Total 12 B wall improves **1.07% median / 1.02% min**, so the formal 5% gate **REJECTS**.
The preserved-old/new headless comparison is **0.30% / 0.36% slower** (median/min).
Retained as an explicit presentation/measurement capability exception, with that cost
recorded, not as a 5% whole-emulator win. Guest speed remains about 31%; successful SDL
calls can repeat images or be discarded and are not distinct displayed frames.

Validation: CTest **29/29**, report tests **2/2**, whole-pair retry protocol tests pass,
reference golden **31/31**, and every A/B and interactive/headless image comparison
**28/28 byte-identical**. CPU/device, input and interrupt checkpoint timelines agree.
DMA history differs across presentation modes: the first difference was traced to one
identical 1.23 MB framebuffer writeback occurring earlier during interactive capture;
all other entries in the 262,144-entry ring match, with no overlapping intervening writes.
Native interactive repeats its DMA digest across all three legs; headless golden is
unchanged. The detailed report preserves this distinction instead of claiming full
cross-mode determinism.

The owner clarified **faithful LLE at 60 FPS with headroom**, with opt-in HLE matching the
LLE floor. That goal is not achieved. `NORTH-STAR.md` and
[`PERF-CAMPAIGN-2026-09-06.md`](docs/PERF-CAMPAIGN-2026-09-06.md) now require steady animation
and navigation baselines, latency tails, and measured remaining budgets. Twenty percent
headroom is provisional. Read-only GPT-5.5 High audits, checked by the primary agent,
identify a texture-stage admission error candidate and a bounded production TCG metadata
experiment; neither is implemented or counted as a win. Earlier CPU models do not prove
TCG optimal, and rejected work requires changed prerequisites before another trial.

## 2026-09-05 - The pushbuffer run path: the last untouched bucket pair, and where its nanoseconds actually were

One commit on `perf/measurement-first-2026-09` closes `docs/PERF-METHODOLOGY.md` section 5b row 6,
the item the whole GPU arc never went near: `nv2a_dispatch` 4.774 s + `nv2a_pfifo` 2.811 s =
7.585 s, 13.0 % of the phase-7 wall, on 82.7 M method words. The rules are section 3m; the lever
order is section 5b.

**The headline is that the 57.7 ns/word was never the method ladder, and the jump table
(`beads-xbox.1.15.1` stage (a)) and the recorder split were measuring the wrong half.** A new
compile-time-optional micro-partition (`src/machine/nv2a_dispatch_micro.hpp`,
`-DXLLE_DISPATCH_MICRO=ON`, default OFF) breaks one pushbuffer word into eleven sibling slots,
sampled 1 in 64. Over all 83.3 M data words the per-word OVERHEAD is 64.71 ns, and 56 of those
nanoseconds are three things that exist only because the pusher was written one word at a time:
`pull_queue_.push_back` of a 32-byte PushMethod (22.37 ns, 1.863 s), a `std::function` hop into
`uc_mem_read` for four bytes (18.73 ns, 1.560 s), and the `drain_pull_queue` funnel that pops the
entry straight back off (14.89 ns, 1.240 s). The recorder is 3.93 ns; `method_counts_` and the
clear-trace deques are at the measurement floor, i.e. free.

**Two measurement findings came out of building the instrument and both generalise.** A bare
`rdtsc` cannot time a 40 ns interval: the first chained version summed twelve deltas to 12.2 ns
against a 41.4 ns envelope, because unfenced reads float to the head of the reorder window and
collapse toward each other while the real work retires behind them; `lfence` on both sides fixed
it, and `perf_excl::tick_now()` is still unfenced. And the instrument's own per-read cost is
SOLVED rather than assumed, from two sampling modes and the fact that chained deltas telescope
(`W + 12R` and `W + R`, so R = 16.38 ns); the obvious estimator -- a null slot -- is not R, because
it also carries the sampler's own bookkeeping, and using it over-subtracts from every slot. With R
solved the parts sum to 99.5 % of the whole.

**What shipped: three stages, all default ON, each with a negative spelling on the same binary.**
`Nv2a::drain_method_run` consumes a whole method RUN in one pass and is the second funnel that
opens `kNv2aDispatch`; `Nv2a::pushbuffer_host_bytes` resolves one host window per kick, reading
the pushbuffer through the very pointer XboxMachine hands `uc_mem_map_ptr`, so the bytes are
identical by construction; and a bulk append for `0x1800` / `0x1808` / `0x1818` puts a
non-incrementing run straight into `vertex_indices_` without entering `dispatch_method`. On the
canonical run **99.30 % of method words are batched** and 76.8 % skip `dispatch_method` entirely,
with the 0.70 % that still take the strict per-word pusher attributable to a named precondition
(`queue=577 314`, `stall=2 262`, zero DMA or direct-window declines). ARRAY_ELEMENT16, 76.86 % of
all method words, goes from 43.0 ns/word to 5.34.

**The result is byte-exact and it FAILS the 5 % bar.** 3+3 interleaved, one binary, arm A =
`--no-nv2a-pfifo-run-batching --no-nv2a-pfifo-direct-fetch` (run census `runs=0
per-word=83 289 212`, i.e. the exact pre-change pusher): **+4.29 % median / +4.37 % min, 3 of 3
rounds, REJECT against the 5.00 % threshold**; final-four indicator +8.07 % / +8.00 %. Behaviour is
not merely faithful but identical -- 28 of 28 checkpoints byte-identical with zero differing
pixels, every `[2bl]` census line equal, all 24 frame-series hashes equal, and `det_digest`,
`dev_digest`, `dev_events`, `dma_digest`, `dma_writes` and `irq0` equal at all 24 checkpoints, with
final frame SHA `36e87a97...` on both arms. `goldens/golden-noperf-defaults12-2026-09-05d.json` is
reproduced by the reference-configuration leg and NOT re-pinned; against `golden-ref-v2` it carries
the same three-frame four-pixel phase-6 signature section 3l(6) already records. It is retained
under section 3 item 3 on the reading section 3l(3)/(5) used for the BGRA upload staging at
+1.51 %, and the owner can reverse it with two flags.

**The win is where the traffic is, and there is one place the change costs time.** Per-window
medians: +3.5 to +6.0 % across the boot windows 1.0-4.0 B and +5.5 to +9.0 % across the busy
dashboard windows 10.0-12.0 B. In the idle-fast-forward block 6.5-9.5 B -- ~0.6-1.0 s windows at
~60 effective fps with almost no pushbuffer traffic -- arm B is SLOWER in seven of nine windows,
102 ms in total, 0.19 % of the run: small, but too consistent in sign to call noise. It is the
run path's fixed cost showing where there are no words to amortise it over. A minimum-run-length
threshold would remove it and was deliberately not added; see section 3m(5).

**A methodology finding worth more than the 4.29 %.** The first gate set reported +4.44 %. When
the direct-window stage landed the SAME A/B reported +2.55 %, because the flag named only the run
path and the window was helping arm A too -- the per-word arm moved 53.479 to 52.282 s. A flag that
gates one stage of a two-stage lever measures half the lever and reports it as the whole.
`tests/test_dispatch_table_guards.py` now pins that arm A restores every stage, with those numbers
in the failure message.

**The bucket pair, instrumented, same binary, arm A vs arm B:** `nv2a_dispatch` 4.841 -> 2.317 s
with entries 82 712 961 -> 5 578 340 (once per run, not once per word), `nv2a_pfifo` 2.846 ->
0.154 s (-94.6 %), `nv2a_recorder` 0.598 -> 0.139 s; the pair 7.687 -> 2.471 s. That -5.2 s must
NOT be quoted as the win: removing 82.7 M region entries also removes 282 M of the instrument's own
`rdtsc` reads (`clock_reads` 377 887 349 -> 95 755 829), and the trustworthy number is the gate's
2.293 s. What is left in `nv2a_dispatch` is method semantics, not overhead: the per-word overhead
this bead was about is now 0.30 % of the wall.

**The emulation-thread partition after this change** (`build/rb/q12B2`, wall 52.501 s, unaccounted
0.065 %): `tcg_exec` 14.509 (27.6 %), `nv2a_raster_witnessed` 9.172, `nv2a_vertex` 6.799,
`nv2a_raster_generic_sw` 5.892, `nv2a_gl` 5.189, `nv2a_gl_resolve` 3.757, `nv2a_dispatch` 2.316,
`nv2a_texture` 1.755, `nv2a_clear` 1.531, `mmio_dispatch` 0.677, `nv2a_pfifo` 0.158,
`nv2a_recorder` 0.139. The device path no longer has a command-dispatch item in its top six.

**Guards:** `tests/test_dispatch_table_guards.py` grew a section that pins order, counts and fault
points on the run path -- an empty CACHE1 queue as a precondition, the stall predicate re-tested
per word, the ring pushed per word while the two histograms are added per run, each hot handler's
safety bound hoisted so a run that would fault is declined, the clear-trace decline, the
contiguity proof, and the per-word pusher still reachable behind its flag. Seven targeted mutations
are each caught; the pre-change tree fails outright. ctest 28/28.

## 2026-09-05 - GPU phase 7: the last plane is reachable, and the thing that blocks it is named

One commit on `perf/measurement-first-2026-09` closes `beads-xbox.1.15.10` phase 7, the four
items phase 6 left. The rules are `docs/PERF-METHODOLOGY.md` section 3l; the lever order is
section 5b.

**The headline is a blocked lever, and it is blocked for a reason that is now written down.**
Phases 5 and 6 named "free plane `0x03628000`" as the largest remaining GPU-resident item -- 417
colour plus 304 zeta resolves and 2.49 s of emulation-thread raster, all-or-nothing across eight
witnessed families. Phase 7 built both halves it needs and **reached the goal on the census**:
with depth support in the generated executor and all eight families rerouted through it, planes
`0x03628000` and `0x033d0000` both fall to `cpu_draws=0`, every one of the 304 transferring zeta
resolves disappears, guest-memory surface uploads go 391 / 915.4 MB to 4 / 8.3 MB, `uses-depth`
declines go 512 to 0, and one 12 G leg goes 54.542 to 52.374 s (**-3.98 %**). **And five of the
eight families move the image**, so it ships DEFAULT OFF.

**The cause is measured, not guessed, and it is not in the new code.** Rerouting a witnessed
family sends it down the ladder's own strict generic fallback, and the generic evaluator is not
equal to the pinned specializations for five of the eight. Single-bit legs to 5.1 G:
`pitch_multi_texture`, `pitch_three_texture` and `texf_projected_fan` are byte-exact (0 differing
pixels, 2 475 of the 3 181 draws); `pitch_tex5_projected` differs on 286 151 pixels at max delta
255, `pitch_additive` on 204 691, `pitch_tex5_diffuse` on 20 875, `pitch_single_texture` on
18 883, `tex7_fan` on 14 560. Re-running the worst one with the GPU executor masked off
reproduces the **same 286 151 pixels**, which puts the fault in the software generic evaluator.
For two of the five the evaluator does not implement the batch's texture-shader program at all
and says so in its own census (`unsupported-texture-shader-program` = 126 and 304, exactly those
draw counts) -- and the refusal is deliberate: `src/machine/nv2a.cpp:10503-10520` records that
letting the generic per-ICW formula loose on a single-stage combiner with a non-{0,1}
`SHADER_STAGE_PROGRAM` produced a wrong answer for `tex7_fan`. Three of eight is worth nothing
under the first-consumer rule, so `--nv2a-gpu-generic-shader-witnessed` defaults to 0 and exists
for bisection.

**Depth in the generated executor is exact, and on its own it is a regression.** The executor now
attaches the resident zeta plane on the same `(address, size)` key the witnessed executor uses,
and the ROP performs the guest's depth comparison against `gl_FragDepth`. A depth batch
deliberately does NOT take the in-shader blend path: writing `gl_FragDepth` forces late-Z, so an
`imageStore` inside the fragment shader would write colour for a fragment that then loses the
depth test. With the reroute off it is byte-identical on all 10 checkpoints to 5.0 G -- and it
makes things slower, because the 527 batches it moves become GPU producers on a plane that still
has 3 181 CPU consumers: 3 G window, colour resolves 566 to 761 and zeta 180 to 375. Default off,
and the flag comment says why.

**What did ship: the upload staging.** The generated executor's 1 831.6 MB of texture and
479.3 MB of colour-surface upload per run went through a per-pixel byte shuffle. A guest
A8R8G8B8 word is little-endian, so its bytes in memory already are `GL_BGRA`: a texture now needs
no staging buffer at all and a colour surface only its vertical flip as a row copy. Gate on the
shipping binary `a3e98dc1`, 3+3 interleaved, arm A = `--no-nv2a-gpu-generic-shader-bgra-upload`:
54.539 / 54.456 s to 53.718 / 53.643 s, **+1.51 % median / +1.49 % min, 3 of 3 rounds, FAILS the
5 % bar**, retained under section 3 item 3 as a pure simplification. All 28 checkpoints are
byte-identical between the arms and every determinism digest matches including `dma_digest`.

**Batching was not built, and is recorded as undetermined rather than disproven.** 792 generated
draws are 0.26 % of `nv2a_gl`'s 304 888 entries in a 5.319 s bucket, and the reroute leg issued
4 003 of them while being 2.2 s faster overall. Neither is a controlled A/B; both are ceilings far
below the method's MDE, so no gate could resolve the lever today.

**Golden: `goldens/golden-noperf-defaults12-2026-09-05d.json` is reproduced and NOT re-pinned.**
The reference-configuration leg passes 31 of 31 checks and is byte-identical to `golden-ref-v2`
on 25 of 28 checkpoints, differing on the same three phase-6 frames by the same four pixels.
Guards: `tests/test_gpu_profiles_guards.py` section 16, 17 new checks (240 total, was 223), 11 of
11 applicable negative controls caught, each mutated on the real source file and restored by
SHA-256. Four call-site-count guards moved with the change and are updated with the reason.
`ctest` 28/28.

**The emulation-thread partition after phase 7** (`--perf-accounting`, wall 58.444 s, unaccounted
0.161 %) -- the floor estimate, and what is left in it is CPU emulation, command dispatch and
driver overhead rather than raster: `tcg_exec` 14.690 (25.1 %), `nv2a_raster_witnessed` 9.162
(15.7 %), `nv2a_vertex` 6.739 (11.5 %), `nv2a_raster_generic_sw` 5.806 (9.9 %), `nv2a_gl` 5.319
(9.1 %), `nv2a_dispatch` 4.774 (8.2 %), `nv2a_gl_resolve` 3.791 (6.5 %), `nv2a_pfifo` 2.811
(4.8 %), `nv2a_texture` 1.760 (3.0 %), `nv2a_clear` 1.528 (2.6 %). **`nv2a_dispatch` plus
`nv2a_pfifo` is 7.585 s, 13.0 % of the wall, on 82.7 M entries at 57.7 ns each, and nothing in
the whole GPU arc has ever touched it.**

## 2026-09-05 - GPU phases 5-6 land (+5.75 %), and the CPU tier is measured out of the running

Three commits on `perf/measurement-first-2026-09` close `beads-xbox.1.15.10` phases 5 and 6 and
answer owner decision 11 ("if Unicorn must be replaced, replace it") with numbers. The rules are
`docs/PERF-METHODOLOGY.md` sections 3i, 3j and 5b; the CPU-tier evaluation is
`docs/DESIGN-CPU-TIER.md`.

**Phase 6: the generated register-combiner executor, and the only change this week to clear the
5 % bar.** `src/host/nv2a_combiner_glsl.{hpp,cpp}` compiles a combiner-state struct into a
fragment program whose expressions are the software rasterizer's own fragment body, transcribed
statement for statement, with the phase-1 exactness rules (int64 edge functions, the same
top-left rule at the same 1/16-pixel sample points, device-side unswizzle, explicit bilinear
against `texelFetch`). Four 3+3 interleaved sets on three binaries all pass; the decision set on
the shipping binary `3a612b6d` is **+5.75 % median / +6.01 % min** on the total 12 B wall, 3 of 3
rounds, 12 of 12 across the four sets. It removes all 704 transferring
`software-generic-color-surface` resolves and 1 339 software batches from plane `0x03AA4000`;
`nv2a_gl_resolve` 5.387 -> 3.717 s and `nv2a_raster_generic_sw` 7.508 -> 6.144 s on a matched
instrumented pair. Behaviour: 25 of 28 checkpoints byte-identical between the arms, three tail
frames differing by **four pixels in total**, largest connected component 1 px; the
reference-configuration leg passes `goldens/golden-noperf-defaults12-2026-09-05d.json` **31 of
31**, and unlike the witnessed executor the generated one really runs in that configuration.

**The finding inside it: GL fixed-function blending is not bit-faithful, and the error is
structural rather than precision.** A differential verifier running both paths from the same
starting image reported exactly **1 LSB on about 90 % of the plane, per batch, in both
directions** - 38 LSB over 79 % of the frame end to end, because the dashboard composites about
five hundred blended layers. GL quantizes the source colour and the blend factor to the
framebuffer's 8 bits before evaluating `Cs*Sf + Cd*Df`; the software path evaluates on
unquantized floats and rounds only the result. Three plausible causes were **disproved first,
with numbers** - FP32 interpolation (253 072 differing pixels vs 253 071 in double),
fixed-function coverage (244 069 either way), and resident-surface aliasing (0 pixels, though
the missing guard was added anyway). The fix is programmable blending: blend, colour mask and
the X8R8G8B8 alpha rule inside the shader against an `rgba8` image binding under
`GL_ARB_fragment_shader_interlock`, with the ROP told to write nothing so there is exactly one
writer. The verifier then reports 0 to 2 differing pixels per batch.

**Phase 5: two censuses that re-ranked the work, and a vertex change that failed the bar and was
kept anyway.** `[host] gpu-resolve-cost` times every residency query and credits it by whether
it actually wrote guest RAM. That **kills the "148 317 no-op residency queries, about 0.9 s"
item at 16 ms** - 0.028 % of the run, declined on measurement rather than built - and replaces it
with the real one: a transferring resolve costs 2.2-2.9 ms and there are 2 258 of them,
**6.37 s, 11 % of the wall, the largest single identified item in the run**.
`[host] gpu-residency-plane` counts GPU-taken and CPU-executed draws per destination plane,
which is the question the per-profile break table cannot answer. The vertex work
(`--nv2a-vertex-program-cache`, `--nv2a-vertex-fast-fetch`) is **-2.62 % median / -2.42 % min**,
6 of 6 rounds across two sets - **below the 5 % bar**, retained under section 3 item 3 as a pure
simplification (fast-fetch, 0.97 s of the 1.15 s) plus one owner call (the cache, 0.18 s). It is
byte-exact: all 28 checkpoint PPMs identical between the arms and every determinism digest
identical including `dma_digest`.

**And the measurement that outlives both: the VSH interpreter is the next vertex lever.**
`--nv2a-vertex-census` splits `nv2a_vertex` into parse (202 -> 18 ms, cached), attribute fetch
(2.682 -> 1.300 s, hoisted) and **VSH interpretation, 38 337 230 vertices at 118 ns = 4.54 s,
which neither cache touches** - 62 % of the bucket. `vsh_context_vertices` is 0, so the 3.7 kB
full-execution-state path is disproved as a lever rather than merely unsuspected. Tracked as
`beads-xbox.1.15.11`.

**The CPU tier: all three replacement options measured, none justified.** Under owner decision 11,
`docs/DESIGN-CPU-TIER.md` evaluates tuned TCG, WHPX and a custom JIT against `tcg_exec`, which is
**14.700 s of a 63.909 s instrumented emulation thread - 23.0 %**, so deleting the CPU tier
outright is a 1.30x whole-run speedup and nothing more.
*(A)* `tools/bench/block_size_bench.cpp` sweeps block size at four hook shapes and destroys the
premise the "6x slower than bare Unicorn" figure rested on: the benchmark that produced 2 991 M
insn/s runs **34 instructions per block** and production runs **3.172**, so per-block cost is
amortized over ten times fewer instructions. Removing the block hook *entirely* still leaves
about 9.90 s, and the best single patch (emitting the icount into the TB) is worth 0.995 s.
*(B)* `tools/bench/whpx_bench.cpp` measures WHPX on this host directly - one partition, one vCPU,
real GDT/IDT in guest RAM, exact exit-count assertions, 5 interleaved reps. Guest code is fast
(MIXED **21 768 M insn/s**, 7.3x TCG) and **it does not matter**: an MMIO exit costs **3199 ns**
bare and 3693 ns through `WHvEmulatorTryMmioEmulation`, and the canonical run takes **17 546 710**
of them. That is **56.1 s of exits alone**, for a floor of **56.6-59.1 s** against the 15.43 s
(`tcg_exec` + `mmio_dispatch`) it would replace - **3.7x to 4.4x slower**. Rejected on
measurement. A second, independent blocker is recorded: on AMD/SVM WHPX gives no instruction
length on an MMIO exit, so the port would owe a full x86 decoder before it could even be wrong.
*(C)* A custom x86->x64 JIT is bounded by the same 8.25 s bucket that is genuine work; design and
beads only. **Do not retry (B)** without an MMIO count under ~5 M per 12 B instructions or a
hypervisor release with a materially cheaper exit round trip.

**Validation of the committed tree.** Release rebuilt clean from the split intermediate and the
final tree; `ctest` **28/28** including twelve source-level guard tests (`gpu_profiles_guards`
223 checks, `vertex_pipeline_guards` 36 checks with 18 of 18 negative controls caught). The
binary that produced the phase-6 gate and the golden check is `3a612b6d`; relinking the identical
tree yields `23810e9e`, which is the link-time-stamp effect already recorded below and not a
content change.

## 2026-09-05 - Measurement-first performance week: 12 B replay 227 s -> 58.5 s, 11.7 presented flips/s

The week of 2026-09-02..05 replaced guesswork with instruments, then used them. Everything
below is on branch `perf/measurement-first-2026-09`. The rules it was run under are
`docs/PERF-METHODOLOGY.md`; what each stage measured, including the rejections, is
`docs/PERF-AUDIT-2026-09-02.md`; the reference identities now live in `goldens/`.

**The metric changed, and that is the most important line here.** The gate is the **total 12 B
replay wall, median AND min, both required to improve by 5 %**, from interleaved A1 B1 A2 B2
legs in ONE session on a quiet host. The old final-four gate window is generic-combiner and
GL-tail dominated and had hidden a 4-7 % witnessed-raster win; it is now reported as an
indicator and never gated. `effective_fps` is 60 x guest speed and is NOT presented fps. And
under owner decision 9 the acceptance standard is **behavioural faithfulness, not
byte-exactness**: byte-locks remain the cheapest regression detector, but a small explained
pixel drift is a question to answer, not a blocker.

**Where the time actually went, measured rather than inferred.** `src/machine/perf_excl.hpp`
makes exactly one bucket accrue at any instant and publishes the shortfall as `unaccounted_ns`
(0.045 % on the first run). The first partition of a 227.0 s run: witnessed rasterizer 104.3 s
(46.0 %), `tcg_exec` 82.2 s (36.2 %), everything else 40.5 s. So NV2A 60.7 %, TCG 36.2 %, all
other overhead 0.5 % - the old "68-78 % is the device path" inference was right in direction
and high in size, and the dispatch if-chain premise (209 ns/word) did not survive contact:
dispatch + recorder is 66 ns/word, a 2.4 % ceiling.

**Four things landed, in this order.** (1) The NV2A CPU path: NV097 jump table and per-profile
compile-time loop specialization, +3.46 % median / +4.00 % min with every witnessed-raster
window +4.3 to +6.7 %; attribution says the win is the specialization, the table is ~1 %.
(2) The seam and threading: a `DrawJob` latched at the draw-batch boundary with the fragment
code textually unchanged, then span-parallel rasterization where all N workers walk interleaved
rows of the SAME job - exact by construction and byte-locked (threads 0 vs 4, 24 checkpoint
PPMs and every counter identical). Deferral to a single consumer was implemented, measured at
+0.39 %, and REJECTED: worker busy 0.489 and emulation thread blocked 0.449 put the ceiling at
4 %. Span parallelism is 154.475 -> 71.336 s median (-53.8 %) against the inline fallback; the
knee sweep chose N = 12, and N = 16 is a regression because the barrier goes from 3.7 % to
22.5 % of elapsed. (3) The CPU tier: block hooks with per-TB icount (`tcg_exec` 50.0 -> 28.0 s),
a guest TSC derived from the retired-instruction clock instead of the host TSC, and idle
fast-forward retiring 4,918,497,264 instructions in 14,801 skips - 40.99 % of the run and
99.36 % of all idle instructions. Chained, `tcg_exec` is 77.3 -> 28.0 -> 14.3 s, **5.4x**, and
the CPU tier is finished as a lever. (4) The GPU executor: the witnessed profiles as GL fragment
programs, -5.86 % on the min of the 5+5 set run on the shipping binary, with FP32 shaders
(`gpu_draw_ns` 10.746 -> 2.364 s), a 256-triangle flush bound chosen from a sweep, and a 4-deep
SSBO ring.

**Two defects found by the instruments, both load-bearing.** The replay was **mutating its own
input disk**: `attach_hdd` opened the image `"r+b"` and the retail boot increments the BRFR
refurb boot counter at LBA 3 within the first 18 M instructions of every run, so no two runs
started from the same disk. It was localised with three new always-on digests down to two bytes
at `0x000d48c8` / `0x000d4b04`, `0x35` vs `0x36`, and fixed with a copy-on-write attach (default
on, `--hdd-writeback` opts back in). That single finding retires every earlier "correlated but
not determined by" line about speed, draw threads, the RTC and the guest TSC. Separately, the
GL `GL_TIME_ELAPSED` bracket around the synchronous `glReadPixels` was measuring GPU IDLE time:
`gpu_resolve` read 146.6 s and is 2.46 s once `glFlush` follows `glEndQuery`. Real GPU busy is
about 4 % of the run, which is what justified moving work onto it.

**Where the stock dashboard is now.** Shipping binary `054f6585`, shipping defaults
(`--nv2a-draw-threads` auto = 12, profile hashes off, GPU executor on, FP32 shaders, idle
fast-forward on, copy-on-write HDD): the canonical 12 B replay is **58.5-59.4 s at 11.7
presented flips/s** across nine arm-B legs of three interleaved sets, every one reproducing the
golden final frame SHA `36e87a97...` and scanout FNV-64 `b7e78a1ac17ac5e8`. The
single-threaded software-raster reference configuration is 147.5 s at 4.71 flips/s. Against the
227 s of the first instrumented run at the start of the week that is **3.9x**. The
10-presented-fps milestone is therefore met on the bounded replay; the live SDL half, which
carries a 33 ms present throttle and a scalar scanout the replay never exercises, has not been
measured and is the next milestone (`NORTH-STAR.md`).

**Behavioural acceptance of the defaults.** Against the reference leg: final frame SHA and
scanout FNV identical on all measured legs; 24 of 28 checkpoint PPMs byte-identical and 4
differing by isolated single pixels (worst frame 0.018 % of pixels, largest connected component
1 px in every case), whose mechanism is measured - FP32 barycentric interpolation in the
projected fragment shader - not assumed; XID endpoint2 461, irq0 16,342 and retired
12,000,000,001 identical between arms.

**Rejections recorded, with numbers.** Stage (c) early depth rejection is a null (+0.21 %
median; 11 of 12 depth-testing profiles already test depth before any texture fetch, and an
earlier +6.8 % reading was host contamination). Deferred single-consumer threading is rejected
at +0.39 %. Phase-4 transfer-volume work failed its gate. Sampling a rendered surface from the
resident texture was declined without being built, its ceiling measured at 0.6 % of the wall
because only 230 of 148,547 texture-source calls move bytes. Two levers were retained BELOW the
5 % bar and labelled as such: per-plane resident-slot selection (+0.37 %) because it is a
correctness fix - the old selector overwrote a still-valid different colour surface 342 times
per run without resolving it - and `kPitchProjectedColor` on the GPU (-3.15 %) as an owner call.

**Validation of the committed tree.** Release rebuilt clean; `ctest` 27/27 including all eleven
source-level guard tests; one canonical 12 B replay at the shipping defaults
(`build/commit-verify-12b.report.json`, git HEAD `5ca85f4`) exit 0 with empty stderr, final
frame SHA `36e87a97...`, retired 12,000,000,001 at `0x8001e3e3`, XID endpoint2 461, irq0 16342,
flips 694, presented 11.740 flips/s, total 12 B wall 59.114 s. Twenty of the 24 frame-series
checkpoint hashes match `goldens/golden-noperf-defaults12-2026-09-05d.json` exactly and the four
that differ are exactly the documented 2.5 / 3.0 / 3.5 / 4.0 B checkpoints - the single-pixel
FP32 interpolation scatter, reproduced rather than newly appeared. The leg was flagged
`contaminated` by the harness (40 foreign python processes), so its wall time is a sanity check
against the 58.5-59.4 s band and NOT a timing measurement.

**Correction worth carrying forward:** a golden record's `exe_sha256` is a build identity, not a
content identity. The mingw linker stamps the link time into the PE header, so relinking an
unchanged tree changes the SHA - `054f6585` and `6fd3714f` are the same sources. Identity
questions have to be settled on behaviour and on the recorded argv, not on the exe hash alone.

## 2026-08-22 - Ox Alpha branch reviewed, coherency-fixed, and revalidated

Codex adversarial review of `opencode/ox-alpha-20260821` found two merge-blocking
issues in the parked Ox Alpha work: the OpenGL state mirror could become stale when
dashboard depth binding used raw `glActiveTexture`/`glBindTexture`, and the
hotpacket alpha-contract probe contradicted its documented mode (`const-mod` set
texel passthrough then unconditionally cleared it). The renderer now routes the
dashboard depth texture through `cache_bind_texture_unit`, texture resident draws
always program deterministic border/alpha uniforms (so no stale uniform can leak
from an extended probe/use into a later ordinary draw), and the hotpacket probe now
uses the captured surface extent/depth contract and preserves passthrough for the
constant-modulated-alpha mode. Production admission remains unchanged for the
blocked `fp=789a30f9` family.

Validation after the fix: Release `xlle`/`test_nv2a` rebuilt clean; all 15
`build-release/test*.exe` binaries passed when run directly. The documented
hotpacket probe for `build/generic-hotpacket-texmod-depth-789a30f9-v1.xgp` now exits
0 and reports the intended forced contract (`passthrough=1 alpha_mod=1 alpha_kill=1`)
with the same residual 70 alpha-channel pixel mismatches, preserving the prior
"blocked by GL interpolation precision" conclusion. Replay evidence:
`build/codex-cachefix-smoke-1b.report.json` exact/empty stderr with SHA
`a6087ec5...` and final FPS 2.904; `build/codex-cachefix-noperf-v1-12b.report.json`
exact/empty stderr with SHA `876b58c2...` and final FPS 3.846; and
`build/codex-cachefix-perf-v1-12b.report.json` exact/empty stderr with SHA
`876b58c2...` and final FPS 3.188. `ctest` remains unusable in this existing
Windows build tree because its generated command path is malformed; direct test
executables are the validation source above.

## 2026-08-21 - Border-clamp resident fixture built; byte-exact promotion blocked by interpolation precision

Implemented the extended resident texture-modulate contract for family `fp=789a30f9`
in the OpenGL renderer behind packet flags that no production admission sets yet
(dormant by default): CLAMP_TO_BORDER bilinear sampling mirroring the software
sampler's tap/border/lerp structure (with `fma()` matching GCC `-march=native`
contraction), constant-modulated output alpha with in-shader `floor(a*255+0.5)`
quantization, and GEQUAL-ref-1 alpha kill via discard.

Hotpacket probe progression for `build/generic-hotpacket-texmod-depth-789a30f9-v1.xgp`:
`621341 -> 2812 -> 70` mismatches across three fixes (border sampling; alpha
contract; in-shader quantization). The residual **70 pixels (0.014% of covered,
64 exactly-one-LSB alpha plus 6 kill-boundary flips)** stem from float32 GPU varying
interpolation versus the software rasterizer's double-precision per-fragment texcoord
interpolation — not reproducible in a GL 4.3 rasterized draw. Byte-exact promotion of
this family through the resident path is therefore BLOCKED under the no-drift
doctrine; whether a documented ±1-LSB opt-in is acceptable is escalated to the
maintainer. The companion primary family (`fp=68d00785`) probe confirms full divergence
(1.57M channels, blending+interpolation), matching prior findings that native resident
primary stays blocked until blend/interpolation exactness exists.

Validation of the dormant state: Release `xlle`/`test_nv2a` build clean, all 15
`build-release/test*.exe` passed, 1B smoke `build/border-clamp-smoke-1b.report.json`
exact/empty stderr with golden SHA `a6087ec5...`, 12B replay
`build/border-clamp-noperf-v1-12b.report.json` exact/empty stderr with final SHA
`876b58c2...`. The extra uniforms are issued only when the extended-contract flags or
border addressing are present, so retained signatures pay zero additional driver
entries. Note for next session: host-measured fps drifted downward across this long
session (identical code measured 3.7-4.4); re-baseline with interleaved runs before
comparing small deltas.

## 2026-08-21 - Texmod-depth contract decoded; resident alpha variant reverted

Continued the hotpacket differential work on family `fp=789a30f9`
(`build/generic-hotpacket-texmod-depth-789a30f9-v1.xgp`):

* Decoded the full state contract from the captured raster words: depth LEQUAL test
  with write enabled, no blending, GEQUAL-1 alpha kill, one combiner stage with
  ColorIcw0 `0x08200000` (texel-passthrough RGB) and AlphaIcw0 `0x11180000`
  (`CONSTANT_COLOR0.a x TEXTURE0.a`, constant0 `0xC4A0FF60`), texture linear ARGB8
  1280x480 pitch `0x1400`, address word stage0 `0x00030404` with border color
  `0xff000000` (CLAMP_TO_BORDER-class addressing).
* Probe infrastructure extended and validated: `XLLE_HOTPACKET_PROBE_KEEP_DEPTH=1`
  preserves captured depth config, `XLLE_HOTPACKET_PROBE_ALPHA_CONTRACT=const-mod`
  drives the texel-passthrough/constant-alpha/alpha-kill contract, and the probe no
  longer force-sets `fixture_color_format_x8=true` (which masked the GL draw's alpha
  and invalidated comparisons for real-alpha families). Packet serialization bumped to
  `XLLEGPB3` with two new fixture fields (`fixture_alpha_constant_modulate`,
  `fixture_alpha_kill_zero`); reader accepts v2 and v3.
* Probe experiments proved: constant-modulated alpha math is CORRECT where applied
  (expected/actual ratio clusters at 96..196-blend values); the dominant divergence is
  ADDRESSING — the resident texture fixture samples REPEAT while this family uses
  CLAMP_TO_BORDER, so texels near/beyond edges differ, and combined with the alpha
  kill most covered pixels keep stale destination content. Extending the resident
  fixture requires border-clamped bilinear sampling (as the witnessed
  clamp-to-border profiles already implement in software).
* A uniform-driven implementation of the alpha contract in the resident fragment
  shader was built and probe-tested, then REVERTED: two 12B runs stayed byte-exact
  (`build/alpha-contract-noperf-v1/v2-12b.report.json`) but measured ~11% below the
  retained GL-cache pair. A post-revert control run also measured low
  (`build/alpha-contract-reverted-12b.report.json`, fps 3.914), so the slowdown tracks
  host-machine drift across this marathon session (unconfirmed attribution, recorded
  per doctrine) rather than proven code cost. The next session must re-establish an
  interleaved A/B/A baseline before trusting any small deltas.

Retained state remains commit `fa9aaba` semantics plus test/packet infrastructure;
retained performance evidence is still the GL-cache pair (~4.33 mean fps, final-four
wall ~32s, exact `876b58c2...`). Next concrete step unchanged: border-clamp bilinear
sampling in the resident texture fixture, then admission signature
`(ColorIcw0==0x08200000, AlphaIcw0==0x11180000)` with the alpha-kill contract, probe
to zero mismatches, full A|B validation on a re-baselined host.

## 2026-08-21 - GL state/location caches cut per-submit driver entries

The OpenGL renderer now mirrors the pipeline state it owns and skips redundant driver
calls across submits: framebuffer binding and color/depth-stencil attachments,
viewport, depth-test enable/func, depth mask, scissor enable/rect, blend enable/func,
color mask, VAO/VBO bindings, active texture unit plus per-unit 2D bindings, and
uniform locations (per-program string-lookup cache; entries dropped on recompile
because drivers may reuse program names). The mirror is primed with an explicitly
established known state at the end of renderer initialization. Every non-render path
that touches the context routes through the cache or invalidates it: resident
resolves attach through cached calls, region uploads bind through unit 0 with mirror
updates, debug probes invalidate at entry, and the per-submit cleanup block plus the
X8 alpha-draw interlude now flow through the cache so the next submit re-establishes
only what actually changed. The three non-resident compute fixture paths also stopped
generating and deleting their shader-storage buffers per submit: a persistent six-
buffer pool is generated lazily and re-specified via glBufferData each submit
(previous behavior: glGenBuffers + glDeleteBuffers around every dispatch — a major
driver-entry and memory-churn source).

A first version of this change failed the 12B replay (`build/gl-cache-noperf-v1-
12b.report.json`, SHA `8ced485f...` versus expected `876b58c2...`, 57,886 differing
final-frame pixels) because the untracked cleanup block desynchronized the mirror;
the fix routed cleanup/alpha-interlude transitions through the cache and restored
exactness. Root cause and evidence retained here deliberately: any future state-cache
extension must account for EVERY context touch, including hygiene resets.

Validation: Release `xlle`/`test_nv2a` build clean; all 15 `build-release/test*.exe`
passed; 1B smoke `build/gl-cache-smoke-1b.report.json` exact/empty stderr with golden
SHA `a6087ec5...`; two no-accounting 12B replays
`build/gl-cache-noperf-v2-12b.report.json` and `build/gl-cache-noperf-v3-12b.report.json`
exact/empty stderr with final SHA `876b58c2...`.

Performance: versus this session's retained scratch-churn state, final FPS improved
3.981/4.044 -> 4.402/4.256 and final-four wall improved 33.94s/34.51s -> 31.96s/~32.3s
(about -6%). Session cumulative against the original baseline
(`build/oxalpha-baseline-a-noperf-v1-12b.report.json`): final FPS 3.68 -> ~4.33 mean
(+18%) with byte-exact output throughout.

## 2026-08-21 - GL syscall-storm attribution and per-site context accounting

Extended observability and settled the navigation-window host-time question:

* Per-call-site `SDL_GL_MakeCurrent` counters (`[perf-gl-context]`, sites in
  `perf_accounting.hpp`, instrumentation in the OpenGL renderer). Measured result over a
  full 12B replay (`build/gl-context-perf-v1-12b.report.json`): resident-submit
  enter+restore ~1.85s of a ~240s run (~76us + ~51us x 14,468 calls) — real overhead but
  NOT the dominant cost. This DISPROVES the earlier sampler-pair reading that blamed
  context switching.
* Sampler raw-stack-scan now accepts only xlle `.text` addresses (PE section header
  parse in `sample_profiler.hpp`), rejecting saved `.data`/`.rdata` pointers that had
  masqueraded as call sites. Re-validated exact: `build/oxalpha-sampler-text-v1.report.json`
  final SHA `876b58c2...`.

Attribution findings for the late dashboard window (10.5B-12B):

1. The dominant ntdll leaf share (~40% in uninstrumented runs) is a SYSCALL STORM, not
   kernel blocking: samples sit exactly on the `ret` after `syscall` of two rapid,
   non-blocking stubs. It exists ONLY with the OpenGL renderer — a software-renderer
   control run shows zero storm (`build/oxalpha-sampler-sw-v1.stdout.log`). The text-filtered
   caller scan places it under `submit_generic_combiner` (non-resident compute; largest),
   `submit_resident_generic_combiner`, and `upload_resident_color_rows` — i.e., the ICD
   performs thread-info-query and memory-free bookkeeping on every submit-path GL entry.
2. `--perf-accounting` itself perturbs the storm (41% -> 18% of window samples in the
   combined run `build/oxalpha-both-v1.stdout.log`), so driver-timing buckets must be read
   with that Heisenberg effect in mind.
3. Disproven this session (do not re-chase without new evidence): MakeCurrent as primary
   blocker; recycled non-resident output buffers as a speed win (exact but neutral:
   `build/output-scratch-noperf-v1-12b.report.json` +0.63% wall vs
   `build/scratch-churn-noperf-v1-12b.report.json`; reverted).

Next optimization direction this points to: reduce the NUMBER of driver entries per frame
(cached GL state transitions across submits/resolves/uploads with conservative
invalidation), rather than making individual calls cheaper; then the coherent resident
tile-bin compute route to remove whole non-resident round trips.

## 2026-08-21 - Recycled surface-snapshot buffers remove per-batch MB-scale churn

The generic-combiner packet's `color_input`/`depth_input` snapshots, the resident
resolve readback buffer (GL side and guest-write side), and the resident upload RGBA
staging are now persistent recycled buffers instead of per-batch allocations:
`Nv2a` swaps member scratch vectors into the packet for the submit scope with an RAII
move-out/move-back guard (`SurfaceScratchSwap`), presenting the same empty-with-
capacity state a fresh packet had; the OpenGL resolve path keeps its `glReadPixels`
target in a member and no longer clears the caller-provided data buffer (paths that
resolve nothing explicitly clear it so the caller's empty-check stays truthful);
resident create-path uploads reuse one staging vector. This removes the multi-megabyte
realloc/zero-fill per submit that the sampler attributed to msvcrt memcpy/memset
(~14% of navigation-window host time).

Validation: Release `xlle`/`test_nv2a` build clean; all 15 `build-release/test*.exe`
passed (an intermediate version that carried stale snapshot sizes into admission
predicates failed two focused accelerated-batch checks in `test_nv2a` and was fixed by
clearing the swapped-in buffers — capacity is kept, visible size returns to zero); 1B
smoke `build/scratch-churn-smoke-1b.report.json` exact/empty stderr with golden SHA
`a6087ec5...`; two independent no-accounting 12B replays
`build/scratch-churn-noperf-v1-12b.report.json` and
`build/scratch-churn-noperf-v2-12b.report.json` exact/empty stderr with final SHA
`876b58c2...`.

Performance: versus this session's retained region-upload state
(`build/region-upload-noperf-v2-12b.report.json`, final FPS 3.740), the two churn
replays measured final FPS 4.044 and 3.981 and final-four wall 33.915s and ~34.6s —
about `-6.85%` wall / `+8%` FPS on the repeat-pair comparison. Cumulative session
progress against the original baseline
(`build/oxalpha-baseline-a-noperf-v1-12b.report.json`, final FPS 3.68, final-four wall
37.063s): final FPS about 4.0 (+9%) with byte-exact output throughout.

## 2026-08-21 - Resident region upload keeps software-written surfaces accelerated

After a generic-combiner software-fallback batch writes guest color memory directly,
`Nv2a` no longer drops residency for the target surface. `PagedRasterSurface` now tracks
the written byte span per batch, and a new renderer contract
(`supports_resident_color_region_upload` + `upload_resident_color_rows`) pushes only the
covered rows back into the exactly-matching valid resident texture (guest-order bytes,
same `kGlBgra` transform as full uploads), preserving the overlap contract by
invalidating any other overlapping resident surface. Non-linear targets, non-resident
targets, and backends without the capability keep the previous blanket-invalidate
behavior; depth invalidation is unchanged.

Validation: Release `xlle`/`test_nv2a` build clean, all 15 `build-release/test*.exe`
passed; 1B smoke `build/region-upload-smoke-1b.report.json` exact/empty stderr with the
documented golden SHA `a6087ec5...`; two independent no-accounting 12B replays
`build/region-upload-noperf-v1-12b.report.json` and
`build/region-upload-noperf-v2-12b.report.json` stayed exact/empty stderr with final SHA
`876b58c2...`; perf-accounting 12B replay `build/region-upload-perf-v1-12b.report.json`
also stayed exact/empty stderr.

Performance: against the same-session retained baseline
`build/oxalpha-baseline-a-noperf-v1-12b.report.json`, final FPS improved `3.68 -> 3.724/
3.740` across the two no-accounting replays (three-run set CV 0.84%) and final-four wall
improved `37.063s -> 36.405s/36.41s (~-1.8%)`; the perf-accounting replay improved
final-four wall `41.124s -> 39.330s (-4.36%)`. Full-surface resident re-uploads fell
from 725 to 143 per run (-80.28%); the new
`software-generic-color-region-upload` resolve context records 651 calls / 449.9MB
(average 0.69MB versus the prior ~4.7MB full-surface uploads).

Measurement tooling added in the same milestone: an opt-in host sampled profiler
(`--host-sample-profile <period_ms>`, `src/host/sample_profiler.hpp`) that samples the
main thread's RIP into per-guest-instruction-window tables (module-attributed, with a
StackWalk64 + raw-stack-scan fallback that attributes blocking leaves to their xlle
callers) and prints `[host-sampler]` reports symbolized offline by
`tools/symbolize_host_profile.py`. The sampler run
`build/oxalpha-sampler-12b-v6.report.json` stayed exact/empty stderr with final SHA
`876b58c2...` at fps 3.592, confirming the probe does not perturb behavior. Its
navigation-window (10.5B-12B) attribution: ~39% blocked inside ntdll syscall stubs under
GL submits (`submit_generic_combiner`, `submit_resident_generic_combiner`,
`ensure_resident_surface`), ~20% `helper_uc_tracecode` + ~14% `code_trampoline`
(per-instruction code-hook machinery, load-bearing for IRQ delivery timing), ~14%
msvcrt memcpy/memset from per-packet vector resize/fill churn and readback row copies,
~2% software raster. These buckets define the next optimization targets.

## 2026-08-13 - Promoted texture tile-bin compute to default

Promoted the previously gated mixed-W texture-modulate tile-bin compute route from
`XLLE_TEXTURE_COMPUTE_TILE_BINS=1` to the normal admission path. The packet now carries an
explicit `fixture_use_tile_bins` bit, so the machine admission predicate and OpenGL renderer
agree without consulting process-global env state. Diagnostic `XLLE_LARGE_TEXTURE_COMPUTE_FIXTURE`
still exists for unrelated large texture-compute experiments.

Validation: Release `xlle`/`test_nv2a` build passed, `test_nv2a` passed, the 1B perf smoke
`build/texture-tilebins-default-smoke-1b.report.json` stayed exact/empty stderr with SHA
`a6087ec5...`, and the no-accounting full 12B replay
`build/texture-tilebins-default-noperf-v1-12b.report.json` stayed exact/empty stderr with SHA
`876b58c2...`.

Performance: versus retained no-accounting default baseline
`build/posttransform-cache-direct1024-noperf-v1-12b.report.json`, final FPS improved
`3.356 -> 3.571` and final-four wall improved `42.880s -> 38.852s` (`-9.39%`). This run is
still slower than the best prior gated tile-bin measurement (`3.876`, `37.216s` final-four wall),
but it confirms the route is a default-speed win and should remain enabled.

## 2026-08-13 - Resident submit split rejects uniform/VAO as primary target

Added `--perf-accounting` resident OpenGL submit sub-buckets for context switching,
vertex packet build, GL state setup, uniforms, vertex attrib setup, cleanup, and GL
error checks. Report/parser/summary support now prints `[perf-renderer-resident-detail]`
and interval columns `rctx`, `rvbld`, `rstate`, `runif`, `rattr`, `rclean`, and `rerr`.

Validation: Release `xlle`/`test_nv2a` build passed, `test_nv2a` passed, Python
parser/summary compile checks passed, the 1B perf smoke
`build/renderer-resident-detail-smoke-1b.report.json` stayed exact/empty stderr with
SHA `a6087ec5...`, and the full 12B perf replay
`build/renderer-resident-detail-v1-12b.report.json` stayed exact/empty stderr with
SHA `876b58c2...`.

Late-window result: final interval `rctx=0.002s`, `rvbld=0.068s`, `rstate=0.004s`,
`runif=0.190s`, `rattr=0.001s`, `rclean=0.001s`, and `rerr=0.016s`. Uniform-location
or VAO caching may be a small cleanup, but it is not a primary route to the 10 fps
target; focus remains on software generic raster, full color resolves, and admission.

## 2026-08-13 - Generic texture setup split keeps focus on resolves/admission

Added `--perf-accounting` texture setup sub-buckets for generic texture readback resolve,
guest DMA read, DXT decode, and swizzle lookup construction. Report/parser/summary support now
prints `[perf-generic-texture-detail]` and interval columns `gtres`, `gtrd`, `gtdec`, `gtswz`,
and `gtother~`.

Validation: Release `xlle`/`test_nv2a` build passed, `test_nv2a` passed, Python parser/summary
compile checks passed, the gated 1B perf smoke
`build/generic-texture-detail-perf-smoke-1b.report.json` stayed exact/empty stderr, and the gated
full 12B perf replay `build/generic-texture-detail-perf-v1-12b.report.json` stayed exact/empty
stderr with final SHA `876b58c2...`.

Late-window result: final interval `gsetup=0.488s`, split into `gtres=0.072s`,
`gtrd=0.059s`, `gtdec=0.211s`, `gtswz=0.000s`, and `gtother~=0.145s`. This confirms the old
swizzle-lookup cache rejection should stay rejected; texture setup is secondary to resident
color resolves/admission in the late dashboard scene.

## 2026-08-13 - Generic admission split identifies resolve/ensure as next targets

Added `--perf-accounting` branch buckets under generic renderer admission: resident support,
resident prepare, resident submit, nonresident support/snapshot/submit/writeback, and fallback
color/depth resolve. The report/parser/summary now emit and parse `[perf-generic-admission]`;
normal no-accounting runs only pay the existing disabled timer guard checks.

Validation: Release `xlle`/`test_nv2a` build passed, `test_nv2a` passed, Python parser/summary
compile checks passed, the gated 1B perf smoke
`build/generic-admission-perf-smoke-1b.report.json` stayed exact/empty stderr, the gated 1B
no-accounting smoke `build/generic-admission-noperf-smoke-1b.report.json` stayed exact/empty
stderr, and the gated full 12B perf replay `build/generic-admission-perf-v1-12b.report.json`
stayed exact/empty stderr with final SHA `876b58c2...`.

Late-window result: final interval still measured below target (`2.806` perf-accounting FPS; do
not compare directly to no-accounting best). `gfbsetup=0.959s` is almost entirely
`gfcol=0.957s`, i.e. fallback color resolves. The resident submit bucket is large
(`grsub=1.095s`), but its renderer sub-buckets show `resolv=0.925s`, `ensure=0.552s`,
`rtex=0.092s`, `rvup=0.029s`, and `rdraw=0.059s`; texture hashing/staging is not the dominant
late bottleneck. The next real optimization should reduce full-surface resident color resolve
traffic or resident-surface ensure overhead without bypassing correctness-sensitive admission.

Rejected follow-up probe: caching the currently attached resident FBO color/depth textures was
correct (`build/resident-fbo-attach-cache-v1-12b.report.json` exact/empty stderr, final SHA
`876b58c2...`) but slower over the final four intervals versus
`build/generic-admission-perf-v1-12b.report.json` (wall `42.975s -> 45.800s`, +6.57%).
Source was reverted.

Rejected follow-up probe: clipping mixed-W large-dashboard resident-primary candidates behind
`XLLE_RESIDENT_PRIMARY_CLIP=1` passed `test_nv2a` with and without the gate and passed the gated
1B smoke `build/resident-primary-clip-v1-smoke-1b.report.json`, but the full 12B replay
`build/resident-primary-clip-v1-12b.report.json` failed correctness (`aea485b6...` vs expected
`876b58c2...`) and was much slower (`1.660` final perf-accounting FPS). Source was reverted.

## 2026-08-13 - Generic-detail accounting shifts target to renderer admission work

Added `--perf-accounting` detail buckets for generic-combiner state setup, renderer/admission
path, and fallback setup, plus report/parser/summary support for a new
`[perf-generic-detail]` line. The instrumentation is inactive unless perf accounting is enabled
and does not change normal replay behavior.

Validation: Release `xlle`/`test_nv2a` build passed, `test_nv2a` passed, Python parser/summary
compile checks passed, and the 1B perf smoke
`build/generic-detail-perf-smoke-1b.report.json` stayed exact/empty stderr. The full 12B perf
replay `build/generic-detail-perf-v1-12b.report.json` stayed exact/empty stderr with final SHA
`876b58c2...`. Its late-window detail shows the old `gother~` time is almost entirely renderer
admission/acceleration-path work rather than state setup: final interval `gother~=3.398s`,
`gaccel=2.414s`, `gfbsetup=0.943s`, `gstate=0.002s`, `gother2~=0.039s`.

Follow-up rejected probe: skipping large-dashboard resident-primary admission for packets with
at least 256 vertices passed `test_nv2a` and a 1B smoke, but the full 12B replay
`build/skip-primary-resident-admit-v2-12b.report.json` failed correctness (`ed1b2deb...` vs
expected `876b58c2...`) and was slower (`2.841` final FPS). Source was reverted. Do not bypass
that admission path without proving the skipped packets cannot be consumed by a correct resident
route later in the same replay.

## 2026-08-13 - Rejected primary passthrough CPU fast branch

Prototyped a gated CPU-side shortcut behind `XLLE_PRIMARY_PASSTHROUGH_FAST=1` for the simple
primary-color passthrough generic-combiner path, alongside retained
`XLLE_TEXTURE_COMPUTE_TILE_BINS=1`. The branch avoided the generic register/final-combiner
scaffolding for `primary_color_passthrough` pixels while preserving the same diffuse
interpolation, alpha test, blend, color-mask, X8 alpha, depth write, and output-hash math.

Validation was exact but slower. `test_nv2a` passed with and without the env gate. The 1B
candidate smoke stayed exact/empty stderr with final SHA `a6087ec5...` and was roughly neutral
against a same-session texture-tilebin baseline (`2.595 -> 2.572` final FPS). The full 12B
replay `build/primary-passthrough-fast-v2-12b.report.json` stayed exact/empty stderr with final
SHA `876b58c2...`, but regressed versus `build/texture-compute-tilebins-flat-v1-12b.report.json`:
final FPS `3.876 -> 3.314`, final-four wall `37.216s -> 40.263s` (`+8.19%`). Source was
reverted. Do not retry this duplicated CPU fast-branch shape without removing materially more
work than generic register/final-combiner setup.

## 2026-08-13 - Rejected resident-primary compute tile-bin route

Prototyped a gated resident-primary exact-compute route using the byte-domain `RGBA8`
load/blend/store shape proven by the diagnostics below. The broad gate
`XLLE_RESIDENT_PRIMARY_COMPUTE_TILE_BINS=1` built and passed `test_nv2a`, and the focused
primary hotpacket `build/generic-hotpacket-primary-be372-v1.xgp` remained exact with
`0` mismatches, but the full 12B stock-dashboard replay failed correctness: final SHA changed
from `876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d` to
`f17a4776fc1e6c151d957c01a20105ecf6842e40c6ea8becee605eeef8e22017`.

A narrower `XLLE_RESIDENT_PRIMARY_BE372_COMPUTE_TILE_BINS=1` variant also built and passed
`test_nv2a`; its 12B replay stayed exact/empty stderr with final SHA `876b58c2...`, but it
regressed versus `build/texture-compute-tilebins-flat-v1-12b.report.json`: final FPS
`3.876 -> 3.714`, final-four wall `37.216s -> 37.470s` (`+0.68%`). Source was reverted.
Conclusion: the byte-domain resident path is admissible for exactness on the captured packet,
but this production shape is not a speedup and the broad admission is still unsafe. The best
retained performance path remains gated `XLLE_TEXTURE_COMPUTE_TILE_BINS=1`.

## 2026-08-13 - RGBA8 image-load/blend exactness needs byte-domain math

Added an env-gated OpenGL diagnostic `XLLE_RGBA8_IMAGE_LOAD_BLEND_PROBE=1` in `test_nv2a` through
`Nv2aRenderer::debug_probe_rgba8_image_load_blend()`. The first float-blend form was not exact:
even with `precise` ordering it hit a one-byte `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` mismatch
(`got=194 expected=195` at probe pixel `8,1` channel `0`). The retained version canonicalizes
`imageLoad(RGBA8)` back to bytes, applies the supported blend factors in byte/integer form
(`ZERO`, `ONE`, `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`), stores `vec4(byte)/255.0`, and compares
readback bytes against the CPU reference for replace/blend and X8/A8 alpha cases.

Validation: Release `xlle`/`test_nv2a` build passed, normal `test_nv2a` passed,
`XLLE_RGBA8_IMAGE_STORE_PROBE=1 build-release/test_nv2a.exe` passed, and
`XLLE_RGBA8_IMAGE_LOAD_BLEND_PROBE=1 build-release/test_nv2a.exe` prints
`RGBA8 image-load/blend probe exact` and passes. This keeps the resident-primary compute route
alive, but only with explicit byte canonicalization and byte-domain blend math; plain normalized
float blending is not admissible.

## 2026-08-13 - RGBA8 compute image-store byte exactness proven

Added an env-gated OpenGL diagnostic `XLLE_RGBA8_IMAGE_STORE_PROBE=1` in `test_nv2a` through
`Nv2aRenderer::debug_probe_rgba8_image_store()`. The probe compiles a tiny GL 4.3 compute shader,
writes a 256x4 grid of representative RGBA byte patterns into an `RGBA8` texture with
`imageStore(vec4(byte)/255.0)`, attaches the texture to an FBO, reads it back as `GL_RGBA`, and
compares every channel byte-for-byte.

Validation: Release `xlle`/`test_nv2a` build passed, normal `test_nv2a` passed,
`XLLE_RGBA8_IMAGE_STORE_PROBE=1 build-release/test_nv2a.exe` prints
`RGBA8 image-store probe exact` and passes, all `build-release/test*.exe` pass, and
`git diff --check` is clean. This does not yet accelerate the dashboard, but it removes one
uncertainty from the resident-primary exact-compute route: explicit byte/255 compute stores into
`RGBA8` can be exact on this host. Next resident-primary work still needs image-load/blend
equivalence and tile-binned resident dispatch before replay admission.

## 2026-08-13 - Resident-primary exact compute needs integer resident storage

Feasibility pass on a possible resident exact-compute route for the hot primary-color
`vertex-expansion` family. The tempting shape is to reuse `kPrimaryColorTriangleComputeSource`
and write its exact result directly into the resident OpenGL target, avoiding the non-resident
compute path's full-surface CPU readback/writeback cost.

The current renderer seam does not have a safe small implementation of that shape. The resident
color target is an `RGBA8` texture populated through normalized GL upload/draw/readback paths, so
the route first needed packet-independent proof that explicit byte/255 compute stores to `RGBA8`
are byte-exact. That store-side proof was added in the later `RGBA8 image-store` diagnostic above.
The remaining unresolved parts are reading/blending an already-resident dirty color target without
CPU resolve and then matching the exact primary-compute shader's operation ordering. If image-load
and blend also prove exact, resident-primary compute can use the existing `RGBA8` target; otherwise
it needs an integer resident color representation such as an R32UI/ARGB-word companion surface.
No production source change was retained by this feasibility pass.

## 2026-08-13 - Rejected source-hashed DXT decode cache

Prototyped a gated generic-combiner DXT decode cache behind `XLLE_GENERIC_DXT_DECODE_CACHE=1`,
combined with the retained `XLLE_TEXTURE_COMPUTE_TILE_BINS=1` path. The cache reread the guest
compressed bytes and keyed decoded ARGB output by DXT type, width, height, pitch, and FNV hash of
the source bytes, so it could not reuse stale decoded output when texture contents changed.

Validation was exact but slower. The 1B smoke
`build/dxt-decode-cache-v2-smoke-1b.report.json` stayed exact/empty stderr with final SHA
`a6087ec5...`, but regressed final FPS `2.987 -> 2.839` versus the direct1024 smoke. The full 12B
replay `build/dxt-decode-cache-v2-12b.report.json` stayed exact/empty stderr with final SHA
`876b58c2...`, but regressed versus the gated texture tile-bin baseline: final FPS
`3.876 -> 3.410`, final-four wall `37.216s -> 40.300s` (`+8.29%`). Source was reverted.
Conclusion: hash + cache lookup/copy costs more than the saved DXT decompression in this path.

## 2026-08-13 - Rejected X8 SRC_ALPHA/ONE_MINUS_SRC_ALPHA write shortcut

Prototyped a gated fast write path for generic-combiner X8 surfaces using the common
`SRC_ALPHA/ONE_MINUS_SRC_ALPHA` blend and all color channels enabled, behind
`XLLE_X8_SRCALPHA_OMSA_FAST=1`, alongside the retained
`XLLE_TEXTURE_COMPUTE_TILE_BINS=1` path. The intent was to remove the temporary destination
float array and unused X8 alpha math for the late primary-color family.

Validation rejected it. The 1B smoke
`build/x8-srcalpha-omsa-fast-smoke-1b.report.json` stayed exact/empty stderr with final SHA
`a6087ec5...`, but was slower (`2.987 -> 2.701` final FPS versus the direct1024 smoke). The full
12B replay `build/x8-srcalpha-omsa-fast-v1-12b.report.json` exited cleanly with empty stderr but
failed correctness: final SHA changed from `876b58c2...` to
`208b0818b8d21d5304f9bbc0e095a749c505e522af09d04f113c7e756ae2bb1d`. Source was reverted. Do
not retry this write shortcut without first proving byte-exact equivalence on captured
hotpackets and matching the original float operation ordering.

## 2026-08-13 - Rejected shader21 two-texture combiner shortcut

Decoded the captured `shader=0x21`, `combiner=3` packet
`build/generic-hotpacket-shader21-comb3-c0e-v1.xgp`. The witnessed state uses two swizzled
ARGB8 PROJECT2D textures and the combiner algebra reduces to `rgb = clamp(tex0 + tex1)` with
`alpha = tex0.a * tex1.a` before the existing SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend.

Prototyped a very narrow CPU-side shortcut behind `XLLE_SHADER21_COMB3_FAST=1`, alongside the
retained `XLLE_TEXTURE_COMPUTE_TILE_BINS=1` path. Validation was exact: 1B smoke
`build/shader21-comb3-fast-smoke-1b.report.json` stayed exact/empty stderr with final SHA
`a6087ec5...`, and full 12B replay `build/shader21-comb3-fast-v1-12b.report.json` stayed
exact/empty stderr with final SHA `876b58c2...`. Performance regressed versus the gated texture
tile-bin baseline: final FPS `3.876 -> 3.415`, final-four wall `37.216s -> 39.882s`
(`+7.16%`). Source was reverted. Conclusion: skipping the generic combiner interpreter here does
not remove enough work to overcome code-shape/cache effects; do not retry this CPU shortcut
shape without removing texture setup/raster loop work as well.

## 2026-08-13 - Native resident primary mismatch is not blend-only

Ran a temporary hotpacket diagnostic on `build/generic-hotpacket-primary-be372-v1.xgp` comparing
resident primary rendering with blending disabled against exact primary compute with blending
disabled. The unblended resident path still differed from exact compute by one LSB in `125,068`
channels (`49,553/44,713/30,802/0`, `gt1=0`, `maxd=1`). The normal blended resident comparison
for this packet was already non-exact by one LSB in `337,986` pixels.

Conclusion: the resident primary blocker is not only fixed-function GL blending/packing; native
GL raster interpolation/output conversion is already one-LSB divergent before blending. Do not
spend more probes on global blend-factor tweaks, shader output bias, or manual blend alone. A
retainable resident-primary route likely needs an integer/exact per-pixel path that writes a
resident surface, or a different CPU work-removal strategy.

## 2026-08-13 - Rejected narrow primary exact-compute tile-bin route

Prototyped a more constrained version of the rejected broad primary tile-bin path: only the
captured top full-state `fp=420f0b01c73c2c93/full=be372882a4961758`, behind
`XLLE_PRIMARY_BE372_COMPUTE_TILE_BINS=1`, using a non-resident primary exact-compute enum plus
the same flat 16x16 tile-list dispatch shape retained for texture compute. Focused packet replay
for `build/generic-hotpacket-primary-be372-v1.xgp` remained exact (`0` mismatches), and the 1B
smoke `build/primary-be372-tilebins-smoke-1b.report.json` stayed exact/empty stderr.

The full 12B no-accounting replay
`build/primary-be372-tilebins-v1-12b.report.json` also stayed exact/empty stderr with final SHA
`876b58c2...`, but regressed versus the gated texture tile-bin baseline:
final FPS `3.876 -> 3.703`, final-four wall `37.216s -> 39.876s` (`+7.15%`). Source was
reverted. Conclusion: even full-state-specific exact primary compute still loses to the CPU
software path because full-surface snapshot/readback and setup dominate the saved primary work.
Primary acceleration still needs resident exactness, not another round-trip compute variant.

## 2026-08-13 - Captured top primary sub-state packet

Captured the hottest final-window primary-color sub-state from
`build/texture-compute-tilebins-flat-perf-v1-12b.report.json`
(`fp=420f0b01c73c2c93`, `full=be372882a4961758`) with
`--debug-generic-packet-dump` into `build/generic-hotpacket-primary-be372-v1.xgp`.
The capture replay `build/primary-substate-be372-capture-12b.report.json` stayed
exact/empty stderr with final SHA `876b58c2...`; the lower final FPS (`3.631`) is
diagnostic capture overhead, not a candidate speed result.

Packet replay result: native resident OpenGL primary rendering is still non-exact for this
state (`337,986` mismatching pixels, `maxd=1`, both positive and negative one-LSB channel
deltas). Exact primary compute is byte-exact for the same packet when run with
`XLLE_PRIMARY_COMPUTE_FIXTURE=1`, `XLLE_LARGE_PRIMARY_COMPUTE_FIXTURE=1`,
`XLLE_HOTPACKET_PROBE_COMPUTE=1`, and `XLLE_HOTPACKET_PROBE_REQUIRE_EXACT=1`
(`0` mismatches). Conclusion: this confirms the earlier pattern on a top sub-state:
resident primary needs a real blend/interpolation exactness fix before widening; exact compute
is correct but remains the expensive fallback class and should not be broadly re-enabled.

## 2026-08-13 - Rejected primary tile-binned exact compute probe

Tested extending the tile-binned exact compute idea to mixed-W untextured primary-color batches
behind `XLLE_PRIMARY_COMPUTE_TILE_BINS=1`, alongside the retained gated texture tile-bin path.
The focused primary hotpacket `build/generic-hotpacket-primary-fp9779-v1.xgp` stayed exact with
`0` mismatches after switching the hotpacket helper to the new non-resident primary compute enum,
and the 1B smoke `build/texture-primary-tilebins-smoke-1b.report.json` was exact/empty stderr.

The full 12B no-accounting replay `build/texture-primary-tilebins-v1-12b.report.json` failed:
final SHA changed from `876b58c2...` to
`0db057c790e30538a246432862c277f8ba731a37f35eb608d288d60d38ee1250`, final FPS regressed
`3.876 -> 1.318`, and final-four wall regressed `37.216s -> 91.772s` (`+146.59%`) versus the
gated texture tile-bin baseline. Source was reverted to the last good texture-only tile-bin
state. Do not retry this broad primary exact-compute admission without narrower packet-level
differential work; a single exact hotpacket is not representative enough.

## 2026-08-13 - Gated tile-binned exact compute probe for mixed-W texture batches

Added an explicit `XLLE_TEXTURE_COMPUTE_TILE_BINS=1` path for the mixed-W large
texture-modulate dashboard family. Nv2a CPU-clips the packet with the same homogeneous clip
rules as the software rasterizer, then the OpenGL exact compute shader builds 16x16 tile
triangle lists so each pixel scans only the triangles whose bounding boxes touch its tile.
The retained default path is unchanged unless the env gate is present.

Packet checks on `build/generic-hotpacket-fp6751-v2-12b.xgp` and
`build/generic-hotpacket-mixedw-texmod-fp02cc-v1.xgp` were exact with
`XLLE_HOTPACKET_PROBE_COMPUTE=1`, `XLLE_HOTPACKET_PROBE_REQUIRE_EXACT=1`, and
`XLLE_TEXTURE_COMPUTE_TILE_BINS=1` (`0` mismatches). The gated 12B no-accounting replay
`build/texture-compute-tilebins-v1-12b.report.json` was exact/empty stderr with final SHA
`876b58c2...`; against the retained report it improved final FPS `3.356 -> 3.575` and
final-four wall `42.880s -> 38.982s` (`-9.09%`).

Follow-up retained inside the gated path: replaced the per-tile `vector<vector<uint32_t>>` bin
builder with a two-pass flat offset/index builder. Hotpacket exactness still held, and the 12B
no-accounting replay `build/texture-compute-tilebins-flat-v1-12b.report.json` stayed exact/empty
stderr while improving the gated result to final FPS `3.876` and final-four wall `37.216s`
(`-4.53%` versus the first gated tile-bin replay, `-13.21%` versus the retained default report).

Do not treat this as a default retained speedup yet. A default-on admission trial was rejected:
same-window 1B reruns showed large machine variance, and the binned path did not fire in the 1B
smoke. Keep it gated until a better automatic admission rule or repeated 12B pair confirms the
late-window win under comparable load. It is suitable for tentative launches with the env gate.

## 2026-08-13 - Rejected additive-X8 software blend fast path

Ran a current 1024-entry-cache perf-accounting replay for fresh attribution:
`build/posttransform-cache-direct1024-perf-v1-12b.report.json` is exact/empty stderr with final
SHA `876b58c2...`. The accounting run itself is not comparable to the no-accounting retained gate
and was slower than the older 256-entry perf run, but the bottleneck shape is unchanged: the final
window remains dominated by generic software raster/setup. In the last interval, generic raster was
`8.587s` of `16.495s` wall, with software raster `5.072s`, generic other `2.994s`, texture setup
`0.521s`, and vertex transform `1.492s`. The hottest families remained the X8 `03aa4000`
texture-modulate and primary-color vertex-expansion rejects.

Tested a narrow X8 `SRC_ALPHA + ONE` software blend fast path that skipped destination unpack and
color-mask handling for the measured hot blend mode. Validation: Release `xlle`/`test_nv2a` built,
`test_nv2a` passed, `git diff --check` passed, and the 1B no-accounting smoke
`build/x8-srcalpha-one-fast-noperf-smoke-1b.report.json` stayed exact/empty stderr with final SHA
`a6087ec5...`. Performance regressed against the retained 1024-entry baseline:
final FPS `2.987 -> 2.906`, final-four wall `22.399s -> 23.288s` (`+3.97%`). Source was reverted
and the retained baseline was rebuilt. Do not retry this per-pixel blend specialization shape
without eliminating more surrounding work.

## 2026-08-13 - Rejected batch-cache admission threshold changes

Tested adjacent admission thresholds for the retained 1024-entry batch-local post-transform
cache. Raising the threshold from `64` to `128` failed the focused cache regression in
`test_nv2a` (`gpu.vertex_attribute_fetches() == fetches_before_reuse + 4`), so it was rejected
without replay spend. Lowering the threshold from `64` to `32` passed `test_nv2a` and
`git diff --check`, and the 1B no-accounting smoke
`build/posttransform-cache-direct1024-min32-noperf-smoke-1b.report.json` remained exact/empty
stderr with final SHA `a6087ec5...`, but regressed against the retained threshold-64 smoke:
final FPS `2.987 -> 2.628`, final-four wall `22.399s -> 24.300s` (`+8.49%`). Source was restored
to the retained `>=64` threshold and rebuilt. The retained best remains final FPS `3.356`.

## 2026-08-13 - Retained 1024-entry batch transform cache

Increased the retained batch-local programmable post-transform cache from `256` to `1024`
direct-mapped entries. The semantic scope is unchanged: it is still inside
`submit_vertex_batch()`, still only for indexed parsed vertex programs that do not write context,
and still keys only on the vertex index within the current batch.

Validation: Release `xlle`/`test_nv2a` built cleanly, `git diff --check` passed,
`build-release\test_nv2a.exe` passed, and all `build-release/test*.exe` passed. The 1B
no-accounting smoke `build/posttransform-cache-direct1024-noperf-smoke-1b.report.json` was
exact/empty stderr with final SHA `a6087ec5...` and improved final FPS `2.841 -> 2.987`
against the direct-256 cache baseline. The full 12B no-accounting replay
`build/posttransform-cache-direct1024-noperf-v1-12b.report.json` was exact/empty stderr with final
SHA `876b58c2...`; final FPS improved `3.095 -> 3.356`, and final-four wall improved
`43.786s -> 42.880s` (`-2.07%`).

Adjacent cache-size probes: `2048` entries stayed exact but failed the final-four gate at 12B
(`43.786s -> 44.130s`, `+0.78%`), and `512` entries stayed exact but regressed the 1B smoke
(`24.405s -> 26.436s`, `+8.32%`), so both were rejected. The >=10 fps goal remains open.

## 2026-08-13 - Rejected resident exact-compute texture path

Tested a resident exact-compute variant for the mixed-W texture-modulate dashboard family. The
prototype kept the exact compute shader's software edge/blend semantics but wrote directly into
the OpenGL resident color texture, avoiding the earlier full-surface CPU readback/writeback path.

Validation was correct but performance was worse. Release `xlle`/`test_nv2a` built cleanly,
`test_nv2a` passed, and `git diff --check` was clean. The 1B no-accounting stock smoke
`build/resident-texture-compute-noperf-smoke-1b.report.json` was exact/empty stderr with final
SHA `a6087ec5...`, but slightly slower than the retained cache baseline (`2.841 -> 2.814` FPS,
final-four wall `+0.49%`). The full 12B no-accounting replay
`build/resident-texture-compute-noperf-v1-12b.report.json` was also exact/empty stderr with final
SHA `876b58c2...`, but regressed final FPS `3.095 -> 2.565` and final-four wall
`43.786s -> 49.581s` (`+13.24%`). Source was reverted and the retained baseline was rebuilt.

Conclusion: exact resident compute avoids the CPU surface round trip, but the current dispatch
shape still has each pixel loop over the expanded triangle list and increases resident uploads
(`725 -> 826`) in the 12B run. Do not revive this shape without tiling/binning or another way to
avoid all-triangles-per-pixel work. Current best remains commit `6c12bb3` plus documentation
commits, final FPS `3.095`.

## 2026-08-13 - Post-transform cache diagnostics shift the next target to exact resident/generic raster work

Temporary perf-only counters on the retained batch-local post-transform cache show the cache is
capturing the expected late-dashboard reuse: by the end of the 12B perf replay it admitted
`109,809,384` vertices, with `73,871,229` hits and `35,938,155` misses (`~67.3%` hit rate).
The diagnostic source hooks were removed.

The matching 12B perf replay was exact/empty stderr with final SHA
`876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d`. Against
`build/vsh-context-direct-perf-v2-12b.report.json`, final FPS improved `2.257 -> 3.019`,
final-four wall `56.485s -> 44.650s` (`-20.95%`), and vertex-transform time
`13.800s -> 3.878s` (`-71.90%`). The remaining large buckets are now generic raster/setup
and resident/offload eligibility, not transform alone.

Rejected follow-up: a prototype that CPU-clipped mixed-W texture-modulate triangles into the
resident OpenGL path stayed exact at the 1B smoke gate, but the full 12B no-accounting replay
changed the final SHA to `5ced34dfe9976069899fb118fd4e761b7dd62e14b4b182e6443228d76a2482d8`
while raising final FPS to `3.99`. This is a promising lead, but it is not retainable until the
packet-level clipping/raster mismatch is isolated and made exact. Source was reverted.

Packet-level follow-up on `build/generic-hotpacket-mixedw-texmod-fp02cc-v1.xgp` showed the
resident graphics path is the non-exact piece: the hotpacket resident path mismatched
`899,838` pixels, while the clipped exact compute fixture produced `0` mismatches. A prototype
that routed mixed-W texture-modulate candidates through the exact compute fixture kept the 1B and
12B no-accounting replay SHAs exact, but it was too slow at 12B: final FPS `3.095 -> 2.366` and
final-four wall `43.786s -> 48.427s` (`+10.60%`). Source was reverted. Conclusion: large-surface
snapshot/readback compute is not the fix; the useful path is making resident graphics exact or
finding a coherent resident/compute route that avoids full-surface round trips.

The same packet-level pattern holds for the untextured primary mixed-W fixture
`build/generic-hotpacket-primary-fp9779-v1.xgp`: resident graphics mismatched `360,538` pixels,
all by one LSB, while the exact compute fixture had `0` mismatches. This reinforces that widened
resident graphics admission is blocked by native GL coverage/interpolation/blend semantics, not
just by texture sampling.

Rejected exact primary compute fallback: a prototype routed only clipped mixed-W large-dashboard
primary-color batches through a non-resident compute fixture. The focused hotpacket remained
exact, and the 1B no-accounting stock smoke stayed exact/empty stderr with final SHA
`a6087ec5...`, but final FPS regressed `2.841 -> 2.414` and final-four wall
`24.405s -> 26.975s` (`+10.53%`) against the retained cache baseline. Source was reverted and no
12B run was spent.

Final-window family aggregation from `build/posttransform-cache-direct256-perf-v1-12b.report.json`
shows the remaining software-raster problem is still dominated by `vertex-expansion` rejects:
about `30.0s` across the final-four top-family lists. The largest actionable groups are
texture-modulate shader-1 `texfmt=08890e2a` on `03aa4000` (`~19.7s`, `~433.8M` fragments) and
untextured shader-0 primary on the same surface/texture state (`~6.3s`, `~284.0M` fragments).
`combiner-words` rejects total about `11.0s`, and no-detail generic families about `4.5s`.

## 2026-08-13 - Retained batch-local post-transform cache lifts late stock dashboard to 3.095 fps

Implemented a conservative NV2A post-transform cache for indexed programmable vertex batches.
It is batch-local only, enabled for non-context-writing parsed vertex programs with at least
64 indexed vertices, and keys on the vertex index inside the submitted batch. That deliberately
avoids cross-batch invalidation hazards from CPU/GPU memory writes while capturing the repeated
indices proven by the vertex-reuse census.

Validation: Release `xlle`/`test_nv2a` build passes, `git diff --check` is clean, all
`build-release/test*.exe` pass, 1B no-accounting and 1B perf-accounting stock smokes are
exact/empty stderr with final SHA `a6087ec5...`, and full 12B no-accounting replay
`build/posttransform-cache-direct256-noperf-v1-12b.report.json` is exact/empty stderr with final
SHA-256 `876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d`.

Impact against `build/vsh-context-direct-noperf-v1-12b.report.json`: final FPS `2.635 -> 3.095`
and final-four wall `49.967s -> 43.786s` (`-12.37%`). The early 1B no-accounting smoke remains
exact but slower (`2.955 -> 2.841`, final-four wall `+7.71%`), so this is a late-dashboard-targeted
optimization rather than a universal startup win. The >=10 fps goal remains open; the next large
targets are still generic software raster/family work, exact resident/tile acceleration, and
further vertex execution work.

## 2026-08-13 - Vertex-reuse census proves a high-value VSH cache target

Ran a temporary, compile-time-only vertex-reuse census in a separate diagnostic build
(`build-vertex-reuse4`, now ignored as generated output). The source-side census hooks were
reverted after the run; retained changes are parser/summarizer support for `[perf-vertex-reuse]`
plus documentation.

The exact 12B diagnostic report `build/vertex-reuse-census-perf-v1-12b.report.json` exits cleanly
with empty stderr and final SHA-256
`876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d`. It answers the main Fable
question: the hot late VSH program `eaad51274b6cda50` has substantial indexed reuse. In the final
late intervals it processes about `22.12M` vertices per 0.5B interval; a 24-entry LRU
post-transform-style cache would hit about `14.71M` of them (`66.5%`), while exact duplicate
indices inside batches account for about `16.29M` (`73.6%`). Max indexed batch size observed for
that program is `14328` vertices.

Implication: implement a correctness-first, bounded post-transform cache for indexed programmable
batches before writing a VSH JIT. The first production attempt should key at least on program
bytes/hash, transform constant version, vertex DMA/attribute descriptor state, and vertex index,
with invalidation on relevant transform/attribute/DMA writes and interpreter fallback on any key
uncertainty. This is a larger lever than scalar VSH interpreter micro-tweaks and directly targets
the dominant late transform work.

## 2026-08-13 - Replay variance summarizer added for optimization gates

Added `tools/summarize_xbox_report_set.py`, a post-processing helper that summarizes repeated
replay reports without launching the emulator. It reports final-SHA consistency, stderr status,
final-FPS mean/stdev/CV, and final-window focus bucket variance using the same `focus_sums`
logic as `tools/compare_xbox_reports.py`.

Validation: `py -3 -m py_compile tools\summarize_xbox_report_set.py` passes, `git diff --check`
is clean, and the tool successfully summarized both the exact no-perf 1B pair
(`build/vsh-context-direct-noperf-smoke-1b.report.json`,
`build/vsh-census-source-reverted-noperf-smoke-1b.report.json`) and the exact 12B perf pair
(`build/vsh-context-direct-perf-v2-12b.report.json`,
`build/vsh-census-perf-v1-12b.report.json`). The latter shows final-four wall CV about `0.84%`,
while derived `generic_other~` and resident-ensure buckets vary much more, reinforcing that
future 2-6% deltas need repeated/error-bar evidence before being accepted or rejected.

## 2026-08-13 - VSH census finding retained as tooling/data only; source hooks reverted

A one-time Claude Fable high critique was run as requested with tools disabled and limited to
strategy review. The useful correction is methodological: several 2-6% slowdowns are plausibly
code-layout/noise, parent dispatch/submit timings are inclusive rather than primary exclusive
hot spots, and the next optimization pass should start from less-distorting measurement plus
larger exact levers.

The temporary VSH program census found a real late-dashboard clue in
`build/vsh-census-perf-v1-12b.report.json`: the run is exact/empty stderr with final SHA
`876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d`, and late intervals are
dominated by VSH program hash `eaad51274b6cda50` (about `22.12M` vertices and `464.59M` step
invocations per late 0.5B interval, `maxspv=21`). However, retaining source-side census hooks was
rejected: even compiled out, the 1B no-accounting gate regressed final-four wall by `+5.74%`.
After reverting the source/CMake hooks, `build/vsh-census-source-reverted-noperf-smoke-1b.report.json`
is exact/empty stderr with final SHA `a6087ec5...`, final FPS `2.988`, and final-four wall within
`+1.01%` of the retained baseline. Only report parser/summarizer support for `[perf-vsh-program]`
records remains.

Next work should not be another scalar micro-tweak. Best candidates: hardware-sampled profiling
of the retained no-perf build to establish exclusive time and noise floor; offline vertex-reuse
analysis for a bit-exact post-transform cache keyed on index/program/constant state; then a
single-program exact specialization for `eaad51274b6cda50` if reuse does not dominate. OpenGL is
still the right launch renderer, but fixed-function OpenGL/Vulkan raster offload is unlikely to
preserve exact SHA semantics; exact GPU compute remains a separate future path only if it operates
on coherent resident storage.

## 2026-08-13 - Stock dashboard performance cursor: exact but still below 10 fps

Retained emulator behavior includes direct
vertex fetch, disabled perf-timer guards, reduced default-path NV2A diagnostic telemetry, and
immutable-context VSH execution. The best retained no-accounting stock-dashboard 12B replay is
`build/vsh-context-direct-noperf-v1-12b.report.json`: exact/empty stderr, final SHA-256
`876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d`, final FPS `2.635`, and
final-four wall `49.967s`. This remains well short of the >=10 fps goal.

The refreshed perf-accounting 12B report `build/vsh-context-direct-perf-v2-12b.report.json`
shows the main remaining costs in the final-four window are generic software raster (`11.490s`),
generic renderer/setup/other overhead (`gother~8.185s` plus setup), and vertex transform
(`13.800s`). Derived self-time estimates make parent dispatch/submit wrappers non-primary
targets (`dispatch_self~1.751s`, `subself~0.518s`).

Rejected follow-up probes after `95fe3ab`: direct pitch-linear generic surface reads/writes,
CPU-side texture0-modulate-primary specialization, and reduced-path resident/texture-modulate
counter gating. Each was exact at the 1B smoke gate but slower, so source was reverted.
Additional exact-but-slower 1B probes from the same cursor were also removed: generic-raster
`std::function` callback replacement (`+12.38%` final-four wall), stack-first clip setup
(`+2.92%`), and direct non-overlapping texture sampling (`+6.77%`).

Follow-up on the renderer-admission hypothesis: pure software rendering is not a better launch
mode for the current path (`build/software-renderer-noperf-smoke-1b.report.json` was exact but
regressed final FPS `2.955 -> 2.454`, final-four wall `+15.11%`). A conservative lazy renderer
packet materialization probe also stayed exact at 1B but regressed final FPS `2.955 -> 2.790`,
final-four wall `+5.32%`; source was reverted. Current focus should remain the large exact
buckets: generic software raster/family specialization, exact resident/tile work, and VSH
execution, not packet-copy or wrapper shaving.

Rejected tile-thread probe: a gated `XLLE_GENERIC_TILE_THREADS=4` generic software raster
prototype partitioned the target into disjoint x-ranges and preserved per-pixel triangle order
inside each worker. It stayed exact at 1B, but the gate-off scaffolding already regressed
final-four wall `22.659s -> 23.769s` (`+4.90%`), and the env-on tile run regressed to
`24.458s` (`+7.94%`, final FPS `2.955 -> 2.875`). Source was reverted.

Rejected follow-up CPU/code-shape probes: `XLLE_PRIMARY_BLEND_FAST=1` specialized the untextured
primary-color/SRC_ALPHA generic path and stayed exact at 1B, but regressed final-four wall to
`23.295s` (`+2.81%`, final FPS `2.955 -> 2.913`); the disabled-gate scaffolding regressed more.
Caching immutable generic texture `SwizzleLookup` tables by `(width,height)` also stayed exact at
1B but regressed final-four wall to `25.521s` (`+12.63%`). Both source probes were reverted.
An early same-plane clip trivial-reject was also exact at 1B but slower (`23.263s`, `+2.67%`,
final FPS `2.955 -> 2.938`) and was reverted.

## 2026-08-09 - A4: synthetic Down-then-A replay reaches a stable black frame; dashboard identified as XBMC

Extending the Down-only proof below to a full Down-then-A sequence, at commit `ed399fe` all six
focused tests pass (`test_xid_down_a_script`, `test_xid_script`, `test_ohci`,
`test_ohci_synthetic_hub`, `test_xid_input`, `test_machine_config`), and a fresh 13B disconnected
reduced-retire replay of the same test-only synthetic hub completes fault-free:

```
.\build-release\xlle.exe --boot ..\machine\flash-dump-ff000000.bin --run2bl ..\machine\bios-extract\bldr.bin --hdd ..\machine\xbox-hdd.img --eeprom ..\machine\eeprom.bin --net-disconnected --reduced-retire-diag --test-synthetic-controller-hub --test-synthetic-controller-hub-input-down-a --test-input-capture-prefix build\frame-synthetic-hub-down-a-rerun --max-insns 13000000000 --frame-dump build\frame-synthetic-hub-down-a-final-rerun-13b.ppm
```

`build/probe-synthetic-hub-down-a-rerun-13b.stdout.log` records the run to instruction budget
("[2bl] ran to instruction budget without fault") in **264.3 s** (4m24s); its stderr companion
(`.stderr.log`) is zero bytes. Final OHCI state: frames=9421 control=000000be hcca=03fdd000,
eds/tds/retired/done=4238/1301/722/691, control/xid-in/xid-endpoint2/xid-out=23/665/664/0,
addr/config=2/1, hub-addr/config/port1=1/1/00000003, rumble=0000/0000.

The endpoint-2 sequence: pre-Down capture at instruction `10981668369` / count 320, hash
`5994d8280fd524cd`; Down PRESS at count 320, RELEASE at count 325 (`11011003995`);
selected-My-Files capture at count 330 / `11040335532`, hash `4c34c2704eb61e54` — matching the
established Down-only 11.5B result below. The selection holds stable at the same hash through
pre-a-stable, count 408 / `11497933715`. A PRESS occurs at count 408 (`analog[0]=0xff`,
`buttons=0`), consumes reports 409-413, and RELEASEs at count 413 / `11527269295`. Post-A the
capture hash changes to `73feb194b7694383` at count 418 / `11556600761`, and stays byte-identical
through post-a-settled (count 496 / `12014203147`) and post-a-final-stable (count 574 /
`12471801363`) to the 13B endpoint (count 664, aggregate 665, configured=1). The
post-a-neutral, post-a-settled, post-a-final-stable, and 13B final PPMs are all byte-identical,
SHA-256 `0C9B6D850BF9A1B0D04FB310A2C15CEF78E123AD126CC6D8FD0DDF7CEE198AA1`. Capture PPMs live at
`build\frame-synthetic-hub-down-a-rerun-pre-down-a-010981668369.ppm`,
`-selected-my-files-011040335532.ppm`, `-pre-a-stable-011497933715.ppm`,
`-post-a-neutral-011556600761.ppm`, `-post-a-settled-012014203147.ppm`,
`-post-a-final-stable-012471801363.ppm`, and
`build\frame-synthetic-hub-down-a-final-rerun-13b.ppm`.

Human-inspected PNG conversions show the pre-A frame is the dashboard main menu (mediacenter
"home" skin: My Programs / My Files / My Pictures / My Music / My Videos / Weather / Settings)
with My Files highlighted, and every post-A frame is **completely black**. The A press is
delivered purely through guest-scheduled endpoint-2 TDs and causes a real dashboard state
transition — XID stays configured with the guest still polling — but the target view renders
black: the "incomplete/blank interface" outcome. This is stable (byte-identical through 13B) and
fault-free; the exact NV2A-level cause of the black render is not yet identified.

New identification: the third-party dashboard on the cloned HDD is XBMC (Xbox Media Center),
installed as `C:\evoxdash.xbe` — confirmed by strings in the XBE and by
`C:\XboxMediaCenter.xml` / `C:\XBMC\` on the image. The stock Microsoft `C:\xboxdash.xbe`
(1,961,984 bytes) is intact alongside it. `machine\xbox-hdd-stockdash.img` is a byte copy of
`xbox-hdd.img` with `C:/evoxdash.xbe` unlinked and rewritten with the stock `C:/xboxdash.xbe`
bytes (1,961,984 bytes) via pyfatx in WSL, read-back verified; `xboxdash.xbe` itself and the
original image are untouched. Its first boot is recorded below.

## 2026-08-09 — A4: stock Microsoft dashboard boots fault-free but stalls on an incomplete black frame

```
.\build-release\xlle.exe --boot ..\machine\flash-dump-ff000000.bin --run2bl ..\machine\bios-extract\bldr.bin --hdd ..\machine\xbox-hdd-stockdash.img --eeprom ..\machine\eeprom.bin --net-disconnected --reduced-retire-diag --frame-series build\frame-stockdash-boot 500000000 --max-insns 11500000000 --frame-dump build\frame-stockdash-boot-final-11.5b.ppm
```

`build\probe-stockdash-boot-11.5b.stdout.log` / `.stderr.log` record exit 0 in **4m26s**, stderr
zero bytes, "[2bl] ran to instruction budget without fault". This run uses the production
direct-XID topology (no synthetic hub): OHCI logs `EP0 addr=1 GET_DESCRIPTOR(config) len=0x80`
outcome=error cc=9, the same short-read behavior established elsewhere.

The stock dashboard genuinely runs: scanout reconfigures to `640x480x32` pitch `2560` at
`PCRTC_START 0x03978000` (XBMC's own boot was `720x480`). Frame-series hashes change at 6.5B
(`0545e67084c18ffb`), change again at 9.5B (`b6f0b57a4922cd33`), then stay stable through the
11.5B final frame (RGBA FNV64 `b6f0b57a4922cd33`). Human inspection: the 6.5B frame is black with
garbage rows at the bottom; the 11.5B final frame is mostly black with several rows of colored
garbage at the top. Neither is a recognizable dashboard.

PGRAPH is doing heavy real work, not idling: vertex batches=449,050, submitted=99,134,424,
triangles=62,857,508; pitch-textured batches=121,296 with 304,256,110 fragments
passing/writing; transform fixed/programmable=147,299/301,751. Two suspects for the incomplete
render: `"NV2A flip: write-increments=663 stalls=663 read-increments=662 notifications=2109
surface=21100000 waiting=1"` with `PCRTC_START` pinned at `03978000`, and `"NV2A immediate
vertices: methods=53427 vertices=0"` — 53,427 immediate-mode method writes producing zero decoded
vertices. This supersedes the "in progress" phrasing above: the stock dashboard boots and renders
fault-free through the full LLE path, but the visible output is an incomplete black frame: the
exact NV2A blocker (flip stall vs. undecoded immediate vertices) is under investigation.

## 2026-08-09 — A4: synthetic Down pulse drives a stable dashboard selection transition

Commits `f2d9d7e` and `6712f74` add the opt-in synthetic hub replay and deterministic input
proof. The no-input synthetic 11.5B control run enumerates hub address/configuration `1/1`
and child XID `2/1`, then records 408 endpoint-2 IN reports (409 historical aggregate reports).
Its baseline PPM SHA-256 is
`D936A993E5441E8D424E3406AE844BE2FA41A179C5F50C239B444009C6382C33`.

The explicitly test-only Down run captures pre-input at instruction `10981668393` / endpoint-2
count 320, presses then releases at `11011004028` / count 325, and completes the five-Down plus
five-neutral report sequence at `11040335577` / count 330. Its post-input and final PPM SHA-256
is `DBCFAD3AE1A8BF40A87C9E8C086E81ED1E9A9C7CA062D5A7B3747FEFE1F0B0F4`; final scanout FNV64 is
`4c34c2704eb61e54`. Visual inspection shows the highlight move from **My Programs** to **My
Files** and that it remains stable through the final frame. Broader panel/controller redraw
means this is not a pixel-perfect frame comparison.

This establishes a guest-visible transition consistent with one D-pad step through the synthetic
test topology only.
It is not real Controller-S hub evidence: `beads-xbox.1.5.8.1` remains open for exact hardware
descriptors and enumeration. No submenu or A-button activation proof exists yet.

## 2026-08-09 — A4: parameterized synthetic Controller-S hub mechanics are unit-proven

OHCI now has an explicitly **test-only** synthetic three-port embedded-hub profile: its
descriptor has zero identity fields and is not a claimed retail descriptor. The production
default remains the byte-identical replay-observed root-attached direct-XID model. Focused
unit flow proves independent hub/child address and configuration state; hub class descriptor
and status; port-1 connection/change/reset/enable plus EP1 change reporting; no devices on
ports 2/3; and downstream XID standard/vendor enumeration with periodic D-pad/A/B delivery
only after a guest TD. This is mechanics coverage only, not a replay or navigation claim.

Isolated Debug and Release builds passed `test_ohci`, `test_ohci_synthetic_hub`,
`test_xid_input`, and `xlle`'s 15 fixture checks. Exact real Controller-S hub descriptors,
identity, and retail enumeration evidence remain blocked on `beads-xbox.1.5.8.1`; no synthetic
result substitutes for that capture.

## 2026-08-08 — A4: full-screen background completes the controller-home composition

The exact IQRS4/IQRS7 four-immediate-vertex family now executes as a separate bounded mode-8
profile. It samples the witnessed 1024x1024 swizzled little-endian A8R8G8B8 image through DMA
A, with stale palette state deliberately passive, and applies the captured alpha-kill,
combiner, nonzero final factors, and source-alpha blend state to either full-clip pitch target.
The zero-final-factor IQRS12 lookalike remains recorder-only. Telemetry and output hashing are
separate from every earlier immediate family.

The accepted **fresh rebuilt Release** 11.5B disconnected reduced-retire replay is fault-free
in **266.058 s**: `build/probe-mode8-fullscreen-a8-executed-11.5b.stdout.log` records the run
and its stderr companion is empty. The profile executes 124/124 candidates with reject mask
`00` and raster reject `0`: 124 batches, 248 triangles, and 42,854,400 each of fragments,
alpha passes, color writes, and samples; last address `03f5bf38`, hash
`d595bb56f28eb6c3`. Earlier profile counts remain unchanged, although downstream destination
interactions rebase their hashes. A discarded 288.861 s stale-binary run is invalid evidence
and is not accepted. The valid run is +24.670 s (+10.22%) over the main-panel replay; this is a
material next optimization target, while remaining a clean single-run measurement with no
concurrent builds and normal noise caveat.

The selected scanout has RGBA FNV64 `5994d8280fd524cd`. The PPM artifact
`build/frame-mode8-fullscreen-a8-executed-11.5b.ppm` has SHA-256
`D936A993E5441E8D424E3406AE844BE2FA41A179C5F50C239B444009C6382C33`; its PNG companion has
SHA-256 `21035B38E17B3C146C1E5E391FDA6BB0CC6956BC033457843B6F43D2300870DB`. The corresponding
diff image and stdout/stderr logs are retained in `build/`. Exact comparison against the
main-panel milestone finds 342,914 changed pixels within `565x390+45+39`. Visual inspection
shows the complete blue/teal background and final controller-home composition. This remains
visual fidelity only: it does **not** establish input or navigation, and the Controller-S
embedded hub still blocks guest polling.

## 2026-08-08 — A4: main-panel background completes the controller home composition

The exact IQRS5/IQRS10 four-immediate-vertex family now executes as a bounded main-panel
mode-8 profile. It samples the witnessed 512x512 swizzled little-endian A8R8G8B8 texture
through DMA A, with palette state deliberately passive, and applies the captured alpha-kill,
combiner, final factors, and source-alpha blend state to the alternating pitch targets. The
zero-final-factor IQRS16 lookalike remains excluded. Its telemetry and output hash are separate
from every earlier immediate family.

The accepted Release 11.5B disconnected reduced-retire replay is fault-free in **241.388 s**:
`build/probe-mode8-main-panel-a8-executed-11.5b.stdout.log` records the run and
`build/probe-mode8-main-panel-a8-executed-11.5b.stderr.log` is empty. The profile executes
124/124 candidates with both reject masks zero: 124 batches, 248 triangles, 33,529,600 each of
fragments and samples, 13,417,544 each of alpha passes and color writes, last address
`03f37018`, and hash `74b351eb7eee0da3`. Earlier profile counters remain unchanged. The 3.759 s
(1.58%) increase over the artwork replay is a single-run measurement subject to noise; both
remain in the roughly four-minute band.

The selected scanout has RGBA FNV64 `84f622fae4176b8d`. The PPM artifact
`build/frame-mode8-main-panel-a8-executed-11.5b.ppm` has SHA-256
`64FB065345EAA9F46B495B7C03E6F1DDFC252DFE87F528E176EDC53FC4B02DD2`; its PNG companion has
SHA-256 `474537626177E7BD5E7A1F626456FFA83DED6BFD1AF6C7363FE3EAF0A04E3879`, and the diff
image accompanies the stdout/stderr logs. Exact comparison against the artwork milestone finds
100,710 changed pixels within `566x391+45+39`. Visual inspection now reveals the full controller
background/main panel while retaining menu text and selection. This is not an input or
navigation claim: the Controller-S embedded-hub topology still blocks guest endpoint polling.

## 2026-08-08 — A4: upper-left artwork completes the mediacenter-home composition

The exact IQRS3/IQRS7 four-immediate-vertex upper-left artwork family now executes as a
separate bounded mode-8 profile. It resolves the witnessed 512x128 swizzled I8 image at T0
through DMA A, expands its immediately preceding little-endian A8R8G8B8 palette before
bilinear filtering, and applies the shared captured alpha-kill, combiner, and source-alpha
blend state to both alternating pitch targets. Telemetry and output hashing remain separate
from the A4 text, row-I8, A8, and menu-I8 families.

The accepted Release 11.5B disconnected reduced-retire replay is fault-free in **237.629 s**:
`build/probe-mode8-artwork-i8-executed-11.5b.stdout.log` records the run and
`build/probe-mode8-artwork-i8-executed-11.5b.stderr.log` is empty. The artwork profile executes
125/125 candidates with reject mask `00` and raster reject `0`: 250 triangles, 4,915,500 each
of fragments and samples, and 2,474,875 each of alpha passes and color writes; its hash is
`54c2429a3605cc5f`. Recorder-only work is 681 states / 1,940 batches. Earlier profile
telemetry and hashes remain unchanged: A4 `b719e98aa60676c3`, row-I8 `27545230ed7deda3`, A8
`21933973698fca35`, and menu-I8 `ecbecc3b80917983`. The 3.169 s increase over the preceding
run is about 1.35%; it is a single-run measurement and therefore subject to noise, while both
remain in the roughly four-minute replay band.

The selected scanout has RGBA FNV64 `825f3c20466439fb`; the artifact
`build/frame-mode8-artwork-i8-executed-11.5b.ppm` has SHA-256
`3CB536A60F20DDAA1D1FD592A7B4443E761C81C35BEDF7032929073CFD43FC08`; the matching image is
`build/frame-mode8-artwork-i8-executed-11.5b.png`. Exact comparison against the prior panel
frame finds 9,287 changed pixels within `338x102+45+39`. Visual inspection shows coherent
**mediacenter home** upper-left logo/art while retaining the selection bar and menu panel.
This is visual fidelity only; it does **not** establish input, navigation, or a guest-owned
selection change. Next: continue the remaining exact visual census and independently close the
Controller-S embedded-hub / endpoint-2 input-topology boundary.

## 2026-08-08 — A4: RS16/RS17 indexed menu column makes the list panel visible

The exact RS16/RS17 four-immediate-vertex family now executes as its own bounded mode-8
profile. It resolves the independently witnessed 64x128 swizzled I8 texture at T0 through
DMA A, expands its contiguous little-endian A8R8G8B8 palette before bilinear filtering, and
uses the captured alpha-kill, combiner, and source-alpha blend state on either alternating
pitch target. The full-state immediate diagnostic introduced in commit `1f6e7a7` correlated
the retained state with each IQRS sample. The captured zero-final-factor lookalike is
deliberately excluded; it remains recorder-only. Telemetry and output hashing remain separate
from the A4 text, row-I8, and RS8/RS13 A8 families.

The accepted Release 11.5B disconnected reduced-retire replay is fault-free in **234.460 s**:
`build/probe-mode8-menu-i8-rs1617-executed-11.5b.stdout.log` records the run and
`build/probe-mode8-menu-i8-rs1617-executed-11.5b.stderr.log` is empty. The menu profile
executes 124/124 candidates with reject mask `00` and raster reject `0`: 124 batches, 248
triangles, and 4,776,480 each of fragments, alpha passes, color writes, and samples; its hash
is `ecbecc3b80917983`. Recorder-only work is 683 states / 2,065 batches. Earlier profile
counts are unchanged, while destination interaction rebases their hashes to A4
`b719e98aa60676c3`, row-I8 `27545230ed7deda3`, and A8 `21933973698fca35`.

The selected scanout has RGBA FNV64 `5ddd492f7bb48be9`; the artifact
`build/frame-mode8-menu-i8-rs1617-executed-11.5b.ppm` has SHA-256
`7125D8EA84C6FD24A4A250094416F0EC6C2DCCFD1CE6882B4DF095877CDFA4B0`; the matching image is
`build/frame-mode8-menu-i8-rs1617-executed-11.5b.png`. Exact comparison against the prior
selected-row frame finds 32,398 changed pixels within `180x214+270+158`. Visual inspection
shows a coherent, subtle dark menu-column panel/border around the list while retaining the
blue selected row. This establishes visual panel visibility only; it does **not** establish
input, navigation, or a guest-owned selection change. Next: the remaining exact visual census
and, independently, the Controller-S embedded-hub / endpoint-2 input topology boundary.

## 2026-08-08 — A4: RS8/RS13 direct-color quads establish default selected-row visibility

The exact RS8/RS13 four-immediate-vertex format-6 family now executes as a separate bounded
mode-8 profile. It samples the witnessed swizzled 128x32 little-endian A8R8G8B8 image directly
through DMA A, with the captured I8 sibling combiner, texture alpha kill, and source-alpha
blend, into either alternating pitch target. `TEXPALETTE0` is retained as passive evidence but
is deliberately not an A8 admission gate; it applies only to the indexed I8 path. Telemetry and
hashing remain separate from the historical A4 and I8 families.

The accepted Release 11.5B disconnected reduced-retire replay is fault-free in **243.144 s**:
`build/probe-mode8-a8-rs813-executed-11.5b.stdout.log` records the run and
`build/probe-mode8-a8-rs813-executed-11.5b.stderr.log` is empty. It admits and executes all 125/125 A8
candidates with reject mask `00` and raster reject `0`: 250 triangles and 652,500 each of
fragments, alpha passes, color writes, and samples; A8 hash `61d45aeeec495fe5`.
Recorder-only work is 685 states / 2,189 batches. I8 remains 875 batches / 1,750 triangles /
4,567,500 fragments, samples, alpha passes, and writes with hash `070a0f9ca1d32663`; A4
remains 875 batches / 15,500 triangles / 1,731,000 fragments and samples / 558,625 alpha
passes and writes. Its output hash is now `1cd0461e8bed58b3` because the newly rendered
destination participates in later blending, rather than because its profile changed.

The final selected scanout has RGBA FNV64 `52613982b9d8fdd9`; the artifact
`build/frame-mode8-a8-rs813-executed-11.5b.ppm` has SHA-256
`1A2FEAA99EB423F710B390D412ED520B014E397ECB359CDD64E96852C5C53817`; the matching image is
`build/frame-mode8-a8-rs813-executed-11.5b.png`. Visual inspection proves a coherent 180x29
selection bar at x=270, y=158 behind **My Programs**. Exact comparison with the preceding
accepted frame finds 4,725 changed pixels and bounding box `180x29+270+158`. This establishes
default selected-row visibility only; it does **not** establish controller input, menu
navigation, or a guest-owned selection change.
The next visual census target is the RS16/RS17 menu-column family, in parallel with the
Controller-S embedded-hub / endpoint-2 input-topology boundary.

## 2026-08-08 — A4: indexed dashboard quads execute; controller topology blocker isolated

The exact immediate mode-8 RS0/RS3 I8+palette family now executes as a second bounded
dashboard profile. It accepts only the witnessed fixed pixel state and one four-vertex
immediate quad while leaving post-VSH position, diffuse, and texture coordinates semantic
inputs. Texture 0 resolves through DMA A as a swizzled 128x32 I8 image; each index selects a
little-endian A8R8G8B8 palette entry before bilinear filtering, texture alpha-kill, the
captured `T0 * V0` combiner, and source-alpha blending. Focused coverage proves rectangular
high-X swizzle, palette-before-filtering and channel order, alpha kill, both alternating
targets, state/shape rejection, and DMA-range failure before writes. Historical A4R4G4B4
counters and hashes remain isolated and exact.

The fault-free 11.5B disconnected-link replay executes all 875 I8 candidates with zero
profile or raster rejections: 1,750 triangles, 4,567,500 fragments/samples, and 4,567,500
alpha passes/color writes, hash `070a0f9ca1d32663`. Recorder-only work falls from 689
states / 3,189 batches to 687 / 2,314. The existing A4 text profile remains exactly
875 batches, 15,500 triangles, 1,731,000 samples, 558,625 writes, and hash
`c4f12fa6ce7488b3`. This additional work does not alter the selected scanout: RGBA FNV64
remains `a77da2bfc5887ee8`, and the PPM remains byte-identical at SHA-256
`4198470101D5DCD66867C392A4B85DDF26C99C0C930E3CB1DCAE3B6E438BE535`. The earlier
selection-highlight interpretation is therefore not visually established at this checkpoint.

USB diagnostics now directly retain the terminal EP0 TD address/flags and post-retirement ED
head. The retail configuration read proves `R=0`, 32/80 bytes, condition code 9, and a halted
ED; a paired focused test proves `R=1` accepts the same short packet and completes status.
Those semantics agree with local OHCI references and must not be weakened. The more
fundamental fidelity error is topology: xboxlle currently exposes the XID directly at the
OHCI root, while a real Controller S contains a three-port hub with XID on downstream port 1
and memory-unit ports 2/3. Exact embedded-hub descriptors remain a real-hardware probe task;
no endpoint-2 poll, menu-state transition, or navigation is claimed.

## 2026-08-08 — A4: exact immediate-mode dashboard text reaches retail scanout

The first recognizable dashboard frame now renders through the retail LLE path. A deliberately
bounded mode-8 profile admits only the witnessed `evoxdash` A4R4G4B4 text family: immediate
vertices traverse the existing programmable VSH, independent four-vertex quads split as
`1,2,0` and `2,3,0`, texture 0 is bilinearly sampled from its swizzled 256x256 16-bit atlas,
and the retained `T0 * V0`, final-combiner, alpha-test, and source-alpha blend state writes the
alternating pitch A8R8G8B8 targets. Other mode-8 families remain recorder-only.

The fault-free disconnected-link retail replay reaches the 11.5B instruction budget and
executes all 875 selected candidates with zero admission rejections: 15,500 triangles,
1,731,000 fragments/samples, and 558,625 alpha passes/color writes. Recorder-only work drops
by exactly 875 batches. The final 720x480 scanout at `0x03c8c000` has RGBA FNV64
`a77da2bfc5887ee8`; the PPM SHA-256 is
`4198470101D5DCD66867C392A4B85DDF26C99C0C930E3CB1DCAE3B6E438BE535`. Visual inspection
shows the real `evoxdash` main-menu labels: My Programs, My Files, My Pictures, My Music,
My Videos, Weather, and Settings. No HLE renderer or host framebuffer substitution is used.

This closes first menu visibility, not A4 rendering or interaction. The frame is intentionally
sparse because the I8/palette and other A4R4G4B4 mode-8 families remain unexecuted. The retail
run still records no endpoint-2 XID IN poll or guest-owned menu-state transition. Next: admit
remaining visual families from captured state as needed, diagnose the configuration/endpoint
scheduling boundary, and prove a real D-pad/A/B report changes the visible menu.

## 2026-08-08 — A4: recognizable boot logo, bounded cube continuation, and a later visual frontier

The normal-RAM Unicorn memory hook is now lazy: the normal retail replay does not install the
load/store callback unless a diagnostic client requests it. A same-binary 500M comparison is
approximately 33.5% faster with identical final PPM output. `--frame-series <path-prefix>
<instruction-interval>` records periodic scanouts for replay analysis; it rejects intervals
below 1,000,000 instructions and names each capture with the actual sample count.

The frame series establishes a clearer visual chronology. At 4.5B instructions the guest
produces a recognizable Xbox X logo. Frames from 4.75B through 6.0B are black; 6.25B begins a
mode transition with noisy lower scanlines; and the fault-free 6.4B endpoint is a near-black
720x480 scanout at `0x03c8c000`. This is not a visible dashboard/menu claim.

Late-boot network experiments explain why simply extending that default replay did not reveal
the menu. With link up, the dashboard reaches an XONLINE/XNET wait whose stock-XBE code uses
a 90,000-tick timeout. Opt-in `--net-disconnected` instead changes the modeled PHY state before
guest execution. The retail driver then completes its bounded 6,000 x 500 us autonegotiation
polls and 1,000 x 500 us link polls; by 11.5B instructions it has launched `evoxdash.xbe` and
the kernel scan finds eight threads. This is an experimental pre-boot link transition, not a
hardware-verified cold-reset cable-out model or a validated UDP peer.

The offline dashboard loop is active but still presents black. Between 9.5B and 11.5B it
dispatches 773,613 PFIFO methods, performs 125 full 720x480 clears, advances 125 flip
increments/stalls and 124 read increments, and alternates PCRTC between `0x03df4000` and
`0x03c8c000`. A bounded `--pfifo-clear-trace 0x03df4000` proves the first matching clear
succeeds, then observes semaphore/flip pacing and real shader/state setup. It also captures
generic immediate 4UB attribute writes at methods `0x1950`, `0x195c`, and `0x1960`. Over the
same interval there are 6,000 new BEGIN/END methods but no new indexed, draw-array,
inline-array, submitted-vertex, or primitive counts.

An extended passive trace now captures the first complete class-`0x97` draw after that clear:
BEGIN mode 8 at PFIFO sequence 36,388,535; four vertices, each with generic 2F texture
coordinates (`0x18c8/0x18cc`), generic 4UB diffuse color (`0x194c`), and a legacy 4F position
commit (`0x1518` through `0x1524`); then END at sequence 36,388,564. All 61 records carry
parser-time GET/PUT and class metadata, and the trace completes without truncation. This
proves the missing modeled immediate-submission path and explains the zero vertex counts; it
does not yet prove that the resulting mode-8 raster state is supported or visible. The final
selected frame is still uniform near-black, so no dashboard menu or navigation is claimed.

The witnessed front end is now implemented without enabling pixels. Generic 2F and 4UB
methods update persistent attributes, while only the observed legacy `SET_VERTEX4F` W method
commits a vertex. Sources cannot mix, mode 8 exits before raster-profile selection, and focused
coverage reproduces the captured white textured quad exactly. The guarded 11.5B replay is
fault-free and executes 232,662 immediate writes as 49,500 vertices in 3,000 batches
(12,375 quads / 24,750 assembled triangles). Indexed/inline counters and every existing
raster counter/hash remain unchanged. Pre-change, immediate-front-end, and guarded endpoint
PPMs have identical SHA-256
`0C9B6D850BF9A1B0D04FB310A2C15CEF78E123AD126CC6D8FD0DDF7CEE198AA1`.
This is submission/transform/assembly progress, not visible dashboard rendering.

The retained pitch-three cube path also has a deliberately bounded exact-X/Y-major continuation:
only `|X| == |Y| > |Z|` in that retained profile selects the X face. Pitch-multi and all-axis
ties retain their prior behavior. The 6.4B replay reports five direction-0 and zero direction-1
tie events, with zero-vector and rescale counters both zero, and completes fault-free. This is a
deterministic replay policy validated by focused tests, not a hardware-proven general cube-map
tie rule.

USB remains configured but not menu-ready: the final replay has no endpoint-2 IN polls and no
guest-proven navigation. The next work remains output-affecting Kelvin state toward a visible
dashboard, then observed endpoint polling and guest-owned D-pad/A/B state changes.

## 2026-08-08 — A4: faster late replays, live XID input, and one more fan family

Long firmware replays may now opt into `--reduced-retire-diag`. The mode removes only the
exact retired-PC/thread observer from the unconditional Unicorn code hook; instruction
timing, device service, interrupt/IF boundaries, fault stops, raster diagnostics, and all
guest-visible behavior remain active. On the same binary and 500M checkpoint it reduced
wall time from 39.562 s to 36.670 s (7.3%) with identical final EIP, scanout hash, device
counters, and raster hashes. The default retains full diagnostics. Two more invasive
ideas—incremental stop-boundary stepping and dirty-page indexing—were rejected after exact
output checks because each was slower. A separate Release build measured 39.763 s versus
43.643 s for the RelWithDebInfo reduced-mode smoke (8.9%).

The leading retained `tex=7`, `shader=0xc61` four-vertex triangle-fan family now executes
through a deliberately narrow semantic profile. Stage 0 PROJECT2D-samples swizzled
A8R8G8B8, modulates RGB by constant `FFA0FF40`, forces source alpha to one, and applies
SRC_ALPHA/ONE additive blending after read-only LEQUAL Z24S8. Focused coverage proves exact
modulated/blended output, depth/stencil preservation, reversed-winding rejection, and that
near-but-different texture/combiner states remain recorder-only rather than being
misclassified.

A fault-free 6.4B Release replay executes 41 batches, 82 triangles, 3,414,998 fragments,
1,951,345 depth passes/color writes/samples, last output address `0x037ea98c`, and profile
hash `d53095d432805a3b`. Recorder-only volume falls from 679 states/1,386 batches to
678/1,345. Final 720x480 scanout remains hash `73feb194b7694383` at `0x03c8c000` and is
still uniformly black, so this is not a recognizable dashboard-frame claim.

Two narrowly retained equivalents then remove another 62 recorder-only batches. The
32-batch shader-`0x63` strip state reuses the existing three-texture cube-pair path only
when opaque-black stage-0 border is paired with its exact witnessed `FF16590D` factor; a
broader first attempt was rejected after it admitted 87 earlier draws and fault-stopped at
4.143B. The 30-batch shader-`0xc61` fan changes only border zero versus opaque black while
its exact address mode clamps to edge. Focused tests compare identical output and work under
both borders using an out-of-range coordinate and a distinct edge texel, while rejecting a
third border value.

The final 6.4B combined replay executes the expected 32 and 30 batches. Three-texture totals
rise to 2,136 batches/202,648 triangles/514,549 fragments/8,195 writes/16,390 samples with
hash `3a169aabaaae6d78`; tex7-fan totals rise to 71/142/14,522,624/2,161,853 writes and
samples with hash `1b30551ecdedb3a2`. Recorder-only volume falls to 676 states/1,283
batches. The selected scanout remains the same one-color black
`73feb194b7694383`, so neither result is presented as dashboard visibility.

`--interactive` now opens the SDL window before emulation and maps keyboard arrows plus
Z/X, Enter, and Backspace to a live Controller-S report at the existing 1 kHz guest-time
poll. Presentation is throttled to 30 Hz wall time with a reusable scanout buffer, and
window close requests a clean CPU stop. The same 6.4B replay shows successful USB address
and configuration but no endpoint-2 traffic: all periodic/control/bulk heads and all HCCA
periodic slots are empty after enumeration. Therefore host input delivery is implemented,
but dashboard polling and even basic menu navigation are not yet proven.

A new capped passive EP0 trace narrows that input blocker further. The guest performs only
GET_DEVICE_DESCRIPTOR(8), SET_ADDRESS(1), GET_CONFIGURATION_DESCRIPTOR(80), and
SET_CONFIGURATION(1); it never requests the XID `0x42` descriptor. The configuration read
receives the complete 32-byte descriptor but traverses an error outcome, consistent with an
OHCI DataUnderrun because the guest TD lacks the rounding bit. The descriptor bytes match
the local xemu/Cxbx Controller-S references. Trace lifecycle/address association is covered
without changing guest-visible scheduling; the next replay must preserve the intermediate
condition code to confirm this hypothesis before any OHCI semantic change.

**Next:** decode the leading 26/25-batch `tex=f`, `shader=0x8421` fan states and continue
toward the selected scanout; confirm the configuration TD's exact intermediate condition
code; then obtain hardware/xemu evidence before altering short-packet behavior. Only a real
endpoint-2 report changing guest-owned menu state counts as D-pad/A/B navigation.

## 2026-08-01 — A4: the LLE boot animation reaches real scanout

The first visible Xbox BIOS frame now travels through the complete low-level path.
Kelvin method `0x1D8C` already retained the guest's clear/parameter-A value, but
PGRAPH MMIO did not expose its hardware shadow at `0x401A88`. The retail kernel's
trapped-NOP `0x304` handler therefore read zero, queued zero as the next display
address, and repeatedly wrote `PCRTC_START=0` even though the animation rendered
elsewhere. PGRAPH now exposes parameter A/B at `0x1A88`/`0x186C`; the retail ISR
consequently queues and displays the guest-selected `0x032A4000` surface.

The boot resolve itself used three output-inert retained-state variants that the
existing semantic inline-blit profile rejected: transparent-black versus
opaque-black border color under U/V CLAMP_TO_EDGE, WRAP versus CLAMP on the
unused P axis of PROJECT2D, and shadow compare 0 versus 7 when no shadow lookup
is performed. Focused coverage proves all three variants retain identical
bilinear output and exact coverage, and also proves the two method-to-PGRAPH
parameter shadows.

All nine direct tests pass. A clean 500M retail replay reaches budget with empty
stderr, keeps PCRTC on `0x032A4000`, executes 29 full 640x480 resolves
(8,908,800 fragments/writes/samples), and produces scanout hash
`bee38a32a69372a1`. The captured 640x480 frame visibly contains the early green
boot-orb glow. This is guest-generated rendering through ROM → retail kernel →
PFIFO/PGRAPH → unified RAM → PCRTC; no D3D interception, HLE renderer, or host
framebuffer substitution is involved. Recorder-only volume at this checkpoint
falls from 58 to 29 batches.

**Next:** capture a later boot-animation frame before the 485th flip, then
continue the leading `tex=7`, `shader=0xc61` fan family and remaining
low-frequency raster census.

## 2026-08-01 — A4: front-culled tex5 pitch draws execute

The four leading retained `tex=5` pitch triangle-list states now execute through
two exact pixel-source signatures under one shared raster envelope. The
`shader=0xc01` path PROJECT2D-divides stage-0 coordinates and bilinearly samples
either the witnessed 16x128 or 256x256 swizzled A8R8G8B8 image with
CLAMP_TO_BORDER opaque black. Its programmed stage-2 cube is validated but has
no route to color, alpha-test, or texture-kill, so it is not fetched. The
shader-off sibling copies perspective-correct diffuse RGBA through its
one-stage combiner. Both paths cull FRONT winding, replace pitch A8R8G8B8
color, pass writable LEQUAL Z24, and preserve stencil; alpha testing and
blending remain disabled.

Focused coverage proves border-filtered projected output `0xFF201008`, direct
D3DCOLOR output `0x80102040`, reversed-winding rejection, LEQUAL depth writes,
stencil preservation, and exact sample/counter behavior. Deliberately invalid
addresses for the output-inert cube and the shader-off texture prove that
neither path performs an irrelevant guest-memory read. All nine direct
executables pass. The 500M smoke reaches no instances fault-free with empty
stderr.

The final 6.4B acceptance replay reaches budget fault-free with empty stderr.
It executes exactly 168 batches across the four retained states, rasterizing
11,706 surviving triangles and 34,627,699 fragments. There are 10,316,807
LEQUAL passes with paired color/Z24 writes and 5,653,620 stage-0 samples; the
new profile hash is `c1bde764442a20dd`. Recorder-only volume falls from 1,871
to 1,703 batches and from 858 to 854 states. Earlier independent raster hashes,
transform `2824162d794cc5de`, and final scanout `73feb194b7694383` remain
unchanged. Later shared-target profiles rebaseline as expected: inline blit
keeps all counts and becomes `f055814e10fd11ef`; inline additive keeps its
fragment/alpha/sample counts, commits 14,756,274 color writes, and becomes
`979e32b12359dba4`.

**Next:** decode the leading 41-batch `tex=7`, `shader=0xc61` four-vertex
triangle-fan family, then its 32-batch `shader=0x63` triangle-strip sibling;
continue the low-frequency census and host-input plumbing.

## 2026-08-01 — A4: disabled-state inline blit variants execute

The linear dashboard-blit profile now classifies state semantically rather than
requiring exact values in registers whose features are disabled. With enable
mask `0x10`, only dithering is active and remains inert for A8R8G8B8; combiner
c0/c1, alpha-test configuration, blend factors/equation, depth
function/write mask, cull orientation, and polygon-offset values cannot affect
the draw. The profile continues to require its exact triangle-list geometry,
640x480 pitch target, 1280x480 linear texture signature, combiner
inputs/outputs, color mask, shade/polygon modes, control, and clip state.

Focused coverage programs the captured `C4A0FF60` c0/c1 pair and
SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend factors while those paths are disabled,
then verifies unchanged `0x80202000` bilinear output, coverage, depth
isolation, and reversed-winding behavior. Two intermediate long replays proved
why both sides of the semantic boundary matter: a combiner-only selector
misclassified an earlier unrelated state at 4.021B, while an exact raster
selector left the target variant recorder-only because of its inert blend
programming. The final output-affecting predicate passes all nine direct tests
and the 500M smoke fault-free with empty stderr.

The final 6.4B acceptance replay reaches budget fault-free with empty stderr.
It executes 62 additional batches across two retained states, bringing the
combined inline-blit profile to 168 batches/triangles and 51,609,600
fragments, writes, and samples with deterministic hash `3f1f5a430082ada4`.
Recorder-only volume falls from 1,933 to 1,871 batches and from 860 to 858
states. Every independent raster hash remains unchanged; pitch-three stays
`a9f6884dc0abadf3`, inline additive stays `763144f45d489441`, transform stays
`2824162d794cc5de`, and final scanout stays `73feb194b7694383`.

**Next:** decode the leading 42-batch `tex=5` pitch triangle-list states,
including the `shader=0xc01` projected/cube path and its combiner-only sibling,
then continue the low-frequency census and host-input plumbing.

## 2026-08-01 — A4: dynamic three-texture pitch family executes

The retained `tex=7`, `shader=0x63` pitch family now executes instead of
remaining recorder-only. Stages 0/1 cube-remap distinct perspective-correct
directions and bilinearly sample the same 64x64 swizzled image. Although stage
2 is enabled and fully programmed, the shader does not fetch or reference it.
The six-stage combiner derives
`source_alpha = 1-(1-max(dot(expand(t0),expand(t1)),0))^2`, then forms RGB as
the fixed `0x0D3308` tint plus `source_alpha` times the witnessed dynamic
stage-3 c0 RGB. With alpha testing disabled, every LEQUAL pass performs
SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending and paired pitch A8R8G8B8/Z24S8
writes while preserving stencil.

Focused coverage verifies the leading `FF000100` factor's exact blended output
`0xB1192F0A` and the later `FF12480B` variant's `0xB1245710`, two cube samples
per depth pass, paired color/depth writes, stencil preservation, independent
counters/hash, and no regression to the earlier two-texture profile. The stage
2 offset deliberately points outside valid RAM, proving that the shader-inert
texture is not fetched. All nine direct tests pass, and the 500M smoke reaches
no instances fault-free with empty stderr.

The final 6.4B acceptance replay reaches budget fault-free with empty stderr.
The semantic dynamic-factor profile covers 2,104 batches across 101 retained
states, rasterizing 199,617 front-facing strip triangles and 512,754
fragments. Of those, 6,400 pass LEQUAL, perform 12,800 samples, and commit
6,400 paired color/Z24 writes with deterministic hash `a9f6884dc0abadf3`.
Recorder-only volume falls from 4,037 to 1,933 batches and from 961 to 860
states. Independent raster hashes, inline-blit `a6fa8d7f361f0383`,
transform `2824162d794cc5de`, and final scanout `73feb194b7694383` remain
unchanged. The later shared-target inline-additive hash rebaselines from
`1567692d051ce1d8` to `763144f45d489441` with its counts unchanged, as
expected from the newly interleaved color/depth writes.

**Next:** broaden the linear inline-blit profile over the leading 47-batch
variant's inert combiner-factor constants, then decode the 42-batch `tex=5`
pitch triangle-list states and continue host-input plumbing.

## 2026-08-01 — A4: inline linear dashboard blits execute

The leading retained inline one-texture state now executes its 106 triangle-list
draws instead of remaining recorder-only. Stage 0 PROJECT2D-divides
perspective-correct coordinates expressed in texels, then bilinearly samples a
1280x480 linear A8R8G8B8 source with its 5,120-byte row pitch and U/V
CLAMP_TO_EDGE addressing. The one-stage combiner copies texture RGB and
interpolated diffuse alpha to a 640x480 pitch A8R8G8B8 target. Culling, alpha,
blend, and depth testing are disabled; the sole enabled dither bit is inert for
the 32-bit target, and the zeta surface is neither resolved nor touched.

Focused coverage verifies an exact four-texel bilinear result `0x80202000`,
diffuse-alpha output, pitch placement, no depth access, deterministic counters
and hash, and equal coverage for reversed winding. The first long replay
executed all 106 decoded draws before stopping on the following draw, which
shares the combiner but has different texture metadata. Profile selection now
includes the witnessed format/address/filter/shape signature, leaving that
distinct family safely recorder-only while keeping DMA offsets flexible. All
nine direct tests pass, and the 500M smoke remains fault-free with empty
stderr.

The final 6.4B acceptance replay reaches budget fault-free with empty stderr.
All 106 retained batches execute as 106 triangles, covering and writing
32,563,200 fragments and performing 32,563,200 texture samples. The
deterministic output hash is `a6fa8d7f361f0383`. Recorder-only volume falls
from 4,143 to 4,037 batches and from 962 to 961 states. Every prior raster hash
and count remains unchanged, including depth `9424b7ed8661f321`, radial
`b57945aef12a9289`, swizzled/pitch textured
`537c8e788aaf4e83`/`6ba31850c2d5eb56`, swizzled/pitch projected
`117e65d4ec81492a`/`4ebdb2f4475cd80c`, pitch single
`e734e675e998af83`, pitch multi `fbe1b6125ce84364`, and inline additive
`1567692d051ce1d8`. Transform and final scanout remain
`2824162d794cc5de` and `73feb194b7694383`.

**Next:** decode the leading remaining 56-batch, three-texture
`shader=0x63` triangle-strip state and its 48-batch factor variants, then
continue the low-frequency census and host-input plumbing.

## 2026-08-01 — A4: pitch two-cube alpha-blend family executes

The leading retained `shader=0x63` state now executes its indexed triangle
strips on the shared 1280x480 pitch A8R8G8B8/Z24S8 targets. Stages 0/1
cube-remap distinct interpolated directions and bilinearly sample the same
64x64 swizzled image. The six-stage combiner dots their expanded RGB values,
turns the clamped positive result into alpha through
`1-(1-dot)^2`, and supplies constant stage-4 RGB `0x0D3308`.
GEQUAL-one alpha gates SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending; passing LEQUAL
fragments update color and Z24 while preserving stencil. Programmed polygon
offset values remain inert because the corresponding enable bit is clear.

Focused coverage verifies exact fractional-alpha output `0xB1192E0A` over a
known destination, texture addressing/filtering, depth/stencil output, two
samples per depth pass, and a second zero-alpha draw that samples but changes
neither plane. The first long replay stopped fail-closed before writes because
the depth-write boolean had been mistaken for the adjacent stencil mask in the
census line. Exact state inspection corrected the gate to the witnessed TRUE
encoding. All nine direct tests pass. The 500M smoke replay remains fault-free
with empty stderr and reaches no instances; a 1B cross-boundary replay executes
the complete family fault-free.

The final 6.4B acceptance replay reaches budget fault-free with empty stderr.
All 120 retained batches execute, rasterizing 11,411 front-facing strip
triangles and 30,808 fragments. Of those, 18,094 pass depth and perform 36,188
cube samples; 16,485 pass alpha and commit paired color/Z24 writes with
deterministic hash `fbe1b6125ce84364`. Recorder-only volume falls from 4,263
to 4,143 batches and from 963 to 962 states. Every prior raster hash and count
is unchanged, as are transform `2824162d794cc5de`, inline
`1567692d051ce1d8`, and final scanout `73feb194b7694383`.

**Next:** decode the leading remaining 106-batch inline one-texture
triangle-list state, then the three-texture `shader=0x63` variants and
host-input plumbing.

## 2026-08-01 — A4: pitch single-texture fan family executes

The leading remaining pitch draw state now executes instead of staying in the
recorder. Its four-vertex triangle fans PROJECT2D-divide stage-0 coordinates
and bilinearly sample the witnessed 1024x1024 swizzled A8R8G8B8 image. U/V
CLAMP_TO_BORDER uses the programmed opaque-black border, while the exact
one-stage combiner copies texture RGB and forces output alpha to one. ONE/ZERO
blending therefore replaces pitch A8R8G8B8 color. The accompanying Z24S8
surface is tested with LEQUAL but remains read-only, including stencil.

The shared sampler now models repeat, clamp-to-edge, and border addressing
explicitly. Focused coverage samples exactly at normalized zero, where bilinear
filtering combines one `0xFF804020` texel with three opaque-black border texels
to produce `0xFF201008`; it also proves two-triangle fan assembly, exact pitch
placement, read-only passing depth/stencil, separate counters, and a
deterministic output hash. All nine direct tests pass. The 500M smoke replay is
fault-free with empty stderr and correctly reaches no instances of this later
family.

The final 6.4B acceptance replay reaches its instruction budget fault-free with
empty stderr. It executes all 121 retained batches and 242 front-facing
triangles, covers and passes 74,342,400 fragments, and performs 74,342,400
samples and color-only writes with deterministic hash `e734e675e998af83`.
Recorder-only volume falls from 4,384 to 4,263 batches. Every prior raster
counter and hash remains unchanged, including depth `9424b7ed8661f321`,
radial `b57945aef12a9289`, swizzled textured `537c8e788aaf4e83`,
pitch textured `6ba31850c2d5eb56`, swizzled projected
`117e65d4ec81492a`, pitch projected `4ebdb2f4475cd80c`, and inline
`1567692d051ce1d8`. Transform and final scanout remain
`2824162d794cc5de` and `73feb194b7694383`.

**Next:** decode and execute the remaining multi-texture `shader=0x63`
pitch family, then continue the low-frequency state census and host-input
plumbing.

## 2026-08-01 — A4: pitch-projected textured family executes

The remaining pitch-surface `0x10c61` family now reuses the validated
three-sample projected-texture path while writing linear 1280x480
A8R8G8B8/Z24S8 targets. Stage 0 PROJECT2D-divides by interpolated W and
repeat-samples its 128x128 swizzled image; stages 1/2 cube-remap and bilinearly
sample the shared image. Per-state stage-5 c0/c1 values supply the RGB weights.
The enabled stage-3 PROJECT3D GREATER result remains combiner-unreferenced and
output-inert. GEQUAL-one alpha, LEQUAL depth, the witnessed all-bits TRUE
depth mask, pitch addressing, and stencil preservation match the preceding
pitch-textured family but retain separate counters and hash.

The first full replay exposed one later stage-0 border encoding. Exact fault
diagnostics showed that only transparent versus opaque black changed while
U/V/P remained REPEAT, so border sampling is unreachable. Focused coverage
uses the opaque value and verifies the unchanged exact dynamically weighted
pixel `0xFF1B3F0D`, pitch placement, Z24/stencil output, three samples per
write, and isolation from the swizzled projected counters. All nine direct
tests pass. The 500M gate remains fault-free with empty stderr and reaches no
pitch-projected batches.

The final 6.4B acceptance replay reaches budget fault-free with empty stderr.
It executes 10,336 pitch-projected batches and 1,693,723 front-facing
triangles, covers 949,449,673 fragments, performs 695,540,724 samples, and
commits 231,846,908 paired color/Z24 writes with deterministic hash
`4ebdb2f4475cd80c`. Recorder-only volume falls from 14,720 to 4,384 batches.
Independent depth, radial, swizzled-textured, swizzled-projected, transform,
and final scanout hashes remain `9424b7ed8661f321`, `b57945aef12a9289`,
`537c8e788aaf4e83`, `117e65d4ec81492a`, `2824162d794cc5de`, and
`73feb194b7694383`. Interleaved pitch-projected Z writes correctly reject
later pitch-textured fragments, re-baselining that profile to 304,256,110
writes/hash `6ba31850c2d5eb56`. The inline profile still commits exactly
15,123,585 writes and re-baselines to `1567692d051ce1d8` because its shared
destination contents change. Final scanout remains the guest clear.

**Next:** execute the leading remaining pitch single-texture family
(`shader=0x1`, 121 fan batches in the top retained state), then the multi-
texture `0x63` family and host-input plumbing.

## 2026-08-01 — A4: dominant pitch-textured color family executes

The dashboard's dominant remaining indexed family now rasterizes its linear
1280x480 A8R8G8B8/Z24S8 targets instead of remaining recorder-only. Shader
`0x10c60` retains the already-validated cube-remapped, bilinear stage-1/2
samples and eight-stage combiner, while stage-6 c0/c1 RGB weights vary between
draw states. The enabled stage-3 PROJECT3D GREATER lookup uses a linear Y16
shadow image, but no combiner input references texture 3; it is therefore
output-inert and performs no guest-memory read. Pitch color/zeta addressing,
LEQUAL Z24 writes, stencil preservation, GEQUAL-one alpha state, and both
witnessed TRUE depth-mask encodings are handled separately from the existing
swizzled profile.

Focused coverage verifies an exact dynamically weighted pixel, pitch placement,
depth/stencil output, two cube samples per write, distinct counters/hash, and
no change to the swizzled sibling. The draw census now distinguishes executed
from recorder-only batches and retains surface offsets plus vertex, inline, and
primitive-mode totals. All nine direct tests pass. The 500M gate remains
fault-free with empty stderr and all earlier hashes unchanged; this family
begins after that boundary.

The full 6.4B acceptance replay reaches budget fault-free with empty stderr. It
executes 121,296 new pitch-textured batches and 4,464,588 front-facing
triangles, covers 2,613,805,422 fragments, performs 833,192,036 cube samples,
and commits 416,596,018 paired color/Z24 writes with deterministic hash
`78a9682b85e41f92`. Recorder-only volume falls from 136,016 to 14,720 batches.
Depth, radial, swizzled-textured, projected-textured, transform, and final
scanout hashes remain `9424b7ed8661f321`, `b57945aef12a9289`,
`537c8e788aaf4e83`, `117e65d4ec81492a`, `2824162d794cc5de`, and
`73feb194b7694383`. The inline profile still commits exactly 15,123,585 writes,
but its hash re-baselines to `610e9157ec10a027` because these earlier pitch
writes now populate the shared additive-blend destination. Final scanout
remains the guest clear.

**Next:** execute the leading remaining pitch profiles, beginning with the
projected-textured `0x10c61` family, then plumb host input into the landed XID
controller.

## 2026-08-01 — A4: NVNet receives external Ethernet through retail RX DMA

NVNet now implements the xemu-witnessed receive lifecycle alongside its existing transmit
engine. An offered frame must pass receiver-enable, DMA-enable, link, and descriptor-AVAIL
gates. Broadcast/multicast/MAC filtering matches the pinned reference. One complete frame
is bounded by the hardware allocation and descriptor capacity, copied into guest memory,
completed with its exact byte count and flags `0x0011`, advanced/wrapped through the RX
ring whose count occupies `RING_SIZE[31:16]`, and published through RX status `0x02` and
IRQ4 while the shared DMA-IDLE bit tracks the transfer.

The host boundary is an opt-in nonblocking UDP layer-2 tunnel: each datagram is exactly one
Ethernet frame. `--net-udp-bind <port> --net-udp-peer <host> <port>` selects it; without
those flags the prior deterministic headless behavior is unchanged. A guest-time 1 kHz
poll leaves datagrams queued in the host socket until the retail driver enables RX and
provides a descriptor, and performs all guest-memory DMA on the emulation thread. Focused
coverage verifies two-descriptor completion/wrap, exact payload/length/flags, IDLE, RX IRQ
assert/ack, no-buffer backpressure, MYADDR filtering, oversized drops, counters, and a real
loopback UDP datagram. All nine direct tests pass.

The normal 6.4B acceptance replay remains fault-free with no ingress and all prior hashes
unchanged. A second 6.4B run queues a 42-byte broadcast ARP frame through the CLI tunnel;
the real dashboard driver enables RX, consumes it through guest descriptor
`0x03c83808`, and completes with `RX offer/desc/pkt/bytes=1/1/1/42`. Final RX IRQ status is
clear under mask `0x5f`, receiver control remains enabled, stderr is empty, and the run
reaches budget without fault. Depth, radial, indexed-textured, projected-textured,
inline-color, transform, and final scanout hashes remain `9424b7ed8661f321`,
`b57945aef12a9289`, `537c8e788aaf4e83`, `117e65d4ec81492a`,
`6363f356e93c6124`, `2824162d794cc5de`, and `73feb194b7694383`.

**Next:** census and execute the remaining low-frequency draw states, then plumb host input
into the landed XID controller for end-to-end dashboard navigation.

## 2026-08-01 — A4: OHCI executes TDs and enumerates an Xbox Controller S

OHCI now executes the guest's periodic, control, and bulk endpoint-descriptor lists instead
of stopping at the register/frame floor. The bounded scheduler reads ED/TD chains from guest
memory, transfers one- and two-page buffers, maintains data toggles and condition codes,
halts failed endpoints, retires TDs into the done list, writes the HCCA frame/done fields,
honors delay interrupts, and raises the standard writeback/root-hub interrupt lifecycle.
OHCI0 root port 1 attaches a deterministic full-speed Controller S; OHCI1 remains empty.

The device implements the standard device/configuration/string requests used during
enumeration, the XID descriptor and capabilities requests, and endpoint 2 interrupt IN/OUT.
Its injectable state produces the exact 20-byte Controller S input report, while the 6-byte
output report retains both rumble motor values. Focused coverage drives real guest-memory
ED/TD chains through device-descriptor fetch, SET_ADDRESS, SET_CONFIGURATION, XID descriptor
fetch, a bulk-scheduled input report, a periodic output report, HCCA done writeback, and IRQ
acknowledgement. All nine direct tests pass.

The 6.4B retail acceptance run is fault-free and performs five ED visits and ten TD
executions/retirements across four control requests, leaving the device configured at
address 1. An extended 8B replay also reaches budget without fault after 2,603 USB frames,
with the same `eds/tds/retired/done=5/10/10/5` and `control=4` counts. This boot window does
not poll endpoint 2 (`xid-in/xid-out=0/0`), so retail endpoint use is not claimed; the input
and rumble paths are proven by the focused descriptor-chain test. The depth, radial,
indexed-textured, projected-textured, inline-color, transform, and final scanout hashes
remain `9424b7ed8661f321`, `b57945aef12a9289`, `537c8e788aaf4e83`,
`117e65d4ec81492a`, `6363f356e93c6124`, `2824162d794cc5de`, and
`73feb194b7694383`.

**Next:** add NVNet RX descriptor DMA/external transport and census the remaining
low-frequency draw states. Host gamepad plumbing remains for dashboard navigation.

## 2026-07-31 — A4: inline pitch/additive color pass executes

The dashboard's witnessed inline color profile now writes its pitch A8R8G8B8 target. The
PROJECT2D path perspective-divides interpolated texture coordinates, clamp-linearly samples
a 256x256 swizzled A8R8G8B8 texture through DMA B, and multiplies texture RGBA by the
dynamic c0 constant in the one-stage combiner. GEQUAL 1 performs the witnessed rounded
alpha test; SRC_ALPHA/ONE ADD blends and saturates all four components into the destination.

The full replay exposed two later variants beyond the 500M census. PROJECT2D ignores P, so
the exact witnessed P-address change from WRAP to CLAMP_TO_EDGE has identical sampling
semantics. The opaque-black border is likewise inert because U/V remain CLAMP_TO_EDGE.
More importantly, depth changes from ALWAYS to LESS while the depth mask remains zero.
That path now reads pitch Z24S8, applies strict LESS, and never writes either depth or
stencil. Focused coverage verifies the exact blended pixel, zero-alpha rejection, both P
encodings, both border encodings, read-only LESS pass/reject, and unchanged Z24S8. The
expanded 64 KiB test pushbuffer remains bounded below all test surfaces. All nine direct
tests pass.

The 500M gate executes 29 inline-color batches and 32,349 writes with hash
`9ea53af177470d97`. The full 6.4B acceptance run is fault-free: 113 batches rasterize 226
front-facing triangles and 43,083,242 fragments, perform 39,818,347 texture samples, pass
alpha 38,224,887 times, and commit 15,123,585 additive color writes with deterministic
hash `6363f356e93c6124`. The depth, radial, indexed textured, projected textured, transform,
and final scanout hashes remain `9424b7ed8661f321`, `b57945aef12a9289`,
`537c8e788aaf4e83`, `117e65d4ec81492a`, `2824162d794cc5de`, and
`73feb194b7694383`. The last inline write `0x0387fb78` is offscreen from final scanout
`0x03c8c000`.

**Next:** extend OHCI into TD execution and XID input, then add NVNet RX/external transport
and census the remaining low-frequency draw states.

## 2026-07-31 — A4: inline-array vertices execute

The dashboard's remaining `NV097_INLINE_ARRAY` submissions now enter the same transform
and primitive pipeline as DMA/indexed geometry. The decoder derives a component-aligned
interleaved stride from all 16 active array formats, accepts xemu's word-padded trailing
bytes, decodes the six already-supported Xbox formats, and retains disabled attributes as
current PGRAPH values. Inline payloads cannot mix with indexed elements in one batch and
remain bounded by the same explicit safety limits. Enabled inline attributes update their
last values after the draw, matching the normal array path without claiming DMA fetches.

Focused coverage interleaves float4 position, packed UB_D3D color, an additional float4,
and three texture vectors. It verifies 63 words become exactly three vertices/six
attributes each, traverse the fixed transform, assemble a triangle, and reproduce the
projected textured raster result with no vertex DMA reads. All nine direct tests pass.

The 500M gate executes 29 dashboard inline batches; the full 6.4B acceptance run executes
all 485 batches fault-free. Their 5,820 words decode into 1,455 vertices and 2,910
attributes. Full totals become 436,941 vertex batches, 48,556,397 submitted/transformed
vertices, and 45,998,077 triangles, with the expanded position-stream hash
`2824162d794cc5de`. The depth, radial, two textured-profile, and final scanout hashes remain
`9424b7ed8661f321`, `b57945aef12a9289`, `537c8e788aaf4e83`,
`117e65d4ec81492a`, and `73feb194b7694383`.

The new state census makes the next draw boundary concrete: inline triangles use a pitch
A8R8G8B8 target, one projected 256x256 swizzled texture, source-alpha/ONE additive
blending, ALWAYS depth with no Z writes, and a one-stage texture-times-constant combiner.

**Next:** execute that inline pitch/additive color profile. OHCI TD/XID input and NVNet
RX/transport remain subsequent A4 work.

## 2026-07-31 — A4: projected textured sibling executes

The lower-volume shader-`0x10c61` profile now executes through all three effective texture
reads and its seven-stage register combiner. Stage 0 PROJECT2D-divides perspective-correct
texture coordinates by their interpolated W, repeat-addresses a 128x128 swizzled
A8R8G8B8 image, and filters bilinearly. Stages 1/2 retain the cube-to-2D remap and
clamp-linear X8R8G8B8 sampling used by the high-volume sibling; stage 3 still returns zero
through shadow-NEVER. The first combiner stage dots expanded stage-0 RGB against stages 1
and 2, the positive second result is raised to the 16th power, and the shared weighted
diffuse/final-factor tail produces A8R8G8B8 while LEQUAL updates Z24 and preserves stencil.

Reading xemu's generated combiner code exposed one correction to the prior sibling: every
written mux/sum destination clamps to signed [-1,1]. The shared weighted intermediate now
does so before diffuse modulation. Focused coverage verifies projective W division, repeat
wrapping above one, four-texel Morton/bilinear sampling in all three images, the dot-product
and clamped weighted result, exact color/Z24 packing, stencil preservation, and farther
depth rejection. All nine direct tests pass.

The 500M census gate and full 6.4B acceptance run are fault-free. All 204 projected-profile
batches rasterize 10,368 front-facing triangles, cover 11,300,410 fragments, perform
2,044,719 samples, and commit 681,573 paired color/Z24 writes with hash
`117e65d4ec81492a`. Because these depth writes precede some high-volume sibling fragments,
that profile now correctly commits 717,366 paired writes/1,434,732 samples with re-baselined
hash `537c8e788aaf4e83`. The depth, radial, transform, and final scanout hashes remain
`9424b7ed8661f321`, `b57945aef12a9289`, `b1bdfde660324e2e`, and
`73feb194b7694383`. Last textured targets `0x0299f394`/`0x029a2980` remain offscreen from
the final `0x03c8c000` scanout.

**Next:** execute the 485 sparse `INLINE_ARRAY` batches and census any remaining
low-frequency draw profiles. OHCI TD/XID input and NVNet RX/transport remain subsequent
A4 work.

## 2026-07-31 — A4: high-volume swizzled textured color pass executes

The next dominant 256x256 A8R8G8B8/Z24S8 profile now writes guest color and zeta memory.
The draw census retains texture DMA A/B, all four offsets/palettes/borders, shadow compare,
and final combiner factors. Its exact shader program disables stage 0, cube-remaps stages
1 and 2 into the same swizzled X8R8G8B8 image, samples both with clamp-to-edge bilinear
filtering, and resolves the stage-3 PROJECT3D shadow lookup to zero because its compare
function is NEVER. Texture directions and diffuse remain perspective-correct while depth
stays screen-linear. A draw-local texture snapshot plus precomputed Morton contributions
keeps coherent sampling bounded.

The witnessed eight-stage register combiner expands the two sampled blue channels, raises
the positive stage-2 result to the 32nd power, applies its stage-6 RGB factors, modulates
smooth diffuse, and scales by the final 63/255 factor before A8R8G8B8 packing. LEQUAL Z24
writes preserve the existing stencil byte. Focused coverage verifies cube-to-2D remapping,
four-texel bilinear filtering, Morton texture and target placement, exact packed color,
stencil preservation, and a farther second draw rejected by depth. All nine direct tests
pass.

The standalone 6.4B checkpoint originally committed 1,747,268 writes with hash
`5056d6d133b67168`. That evidence is superseded by the projected sibling milestone above:
its interleaved depth writes reject later fragments, and the shared weighted intermediate
now includes the hardware clamp recovered from xemu's generated combiner code.

## 2026-07-31 — A4: dominant swizzled radial color pass executes

The dashboard's dominant untextured color profile now executes through guest color and zeta
memory. The existing list/strip/fan coverage core admits its 1024x512 swizzled
A8R8G8B8/Z24S8 targets, resolves both DMA ranges, and accesses them in Xbox Morton order.
Smooth diffuse and passthrough texture coordinates interpolate perspective-correctly; depth
remains screen-linear. The exact three-stage register combiner computes
`v0 * (1 - dot(expand(t0.rgb), expand(t0.rgb)))^2`, final alpha one passes GEQUAL 1,
ONE/ZERO ADD replaces color, LEQUAL writes floored Z24, and the original stencil byte is
preserved. Lazy 4 KiB draw-local pages retain memory coherence without rereading both full
2 MiB targets for every particle batch. State outside this witnessed profile still
fault-stops or remains recorder-only as appropriate.

Focused coverage verifies unequal vertex W perspective correction, nontrivial radial
attenuation, exact Morton placement, A8R8G8B8 packing, floored Z24, preserved stencil, and
a farther second draw rejected by LEQUAL. The page-backed refactor leaves the prior
pitch-Z16 depth hash unchanged, and the direct suite remains 9/9.

The full 6.4B dashboard acceptance run is fault-free. It executes 155,372 radial-color
batches and 7,293,635 front-facing triangles, covers 4,107,334,135 fragments, and commits
727,241,984 paired color/Z24 writes with deterministic FNV-64
`b57945aef12a9289`. The prior depth-prepass hash remains `9424b7ed8661f321`;
all 48,554,942 vertices retain position hash `b1bdfde660324e2e`. The last radial color
write is `0x02f8f00c`, while final scanout remains `0x03c8c000` with unchanged hash
`73feb194b7694383`: this profile is an offscreen/intermediate pass, not yet the visible
dashboard frame.

**Next:** execute the next high-volume 256x256 swizzled A8R8G8B8/Z24S8 profile, including
its four texture stages and eight-stage register combiner, then cover the remaining
textured profiles and `INLINE_ARRAY`. OHCI TD/XID input and NVNet RX/transport remain
subsequent A4 work.

## 2026-07-31 — A4: pitch-Z16 triangle depth prepass executes

The first retained-geometry raster path now mutates guest zeta memory. A draw-time census
captures the exercised surface, clip, depth/cull/polygon, texture, shader-stage, and register
combiner state for every indexed batch. The admitted profile is the dashboard's untextured
pitch-surface Z16 depth prepass: triangle lists, strips, and fans use 4-bit fixed-coordinate
coverage with the top-left rule, CW/back-face culling, screen-linear depth, polygon offset,
clip-range rejection, LEQUAL testing, and little-endian D16 writes through the bound zeta
DMA object. Any depth-only draw outside the witnessed profile fault-stops rather than
silently taking an approximate path.

Programmable oPos now receives the same Kelvin screen-coordinate rounding and signed W
clamp as the fixed path before rasterization. Focused coverage draws a biased triangle into
a 64x64 Z16 surface, checks the exact floored depth, and verifies reverse-winding culling.
The direct suite remains 9/9.

The full 6.4B dashboard acceptance run is fault-free. It executes 142,842 supported depth
batches and 5,243,066 front-facing triangles, covers 476,615,766 fragments, and performs
139,247,115 LEQUAL passes/writes. The deterministic raster-write FNV-64 is
`9424b7ed8661f321`; all 48,554,942 vertices still execute and the corrected combined
position hash is `b1bdfde660324e2e`. The final scanout hash remains
`73feb194b7694383`, as expected: this checkpoint writes zeta only, while the dominant
swizzled color pass remains recorder-only.

**Next:** use the shared triangle coverage/depth foundation to execute the dominant
untextured swizzled A8R8G8B8/Z24S8 radial-combiner pass, then add the exercised textured
passes and `INLINE_ARRAY`. OHCI TD/XID input and NVNet RX/transport remain subsequent A4
work.

## 2026-07-31 — A4: exercised fixed-function Kelvin transforms execute

Kelvin's remaining indexed transform path now executes and retains the same screen-space
position, diffuse/specular, and four texture outputs as the programmable path. Fixed-function
matrix uploads populate the shared Cheops XF context; the dashboard's skinning-off path
multiplies position by the composite matrix, applies the signed NV2A W clamp and four-bit
screen-coordinate truncation, and adds the viewport offset. Material emission/alpha, scene
ambient, color-source/light-control selectors, lighting/specular enables, light mask,
normalization, fog, texgen, and texture-matrix enables are tracked rather than assumed.

The fault-driven census found exactly two exercised color states. The first 15,999 fixed
batches use ambient/material lighting with separate specular; the remaining 127,591 disable
lighting and specular and pass v3 diffuse through. Every fixed batch has skinning,
normalization, fog, texgen, and texture matrices disabled, all eight lights off, and material
source word zero. Any broader fixed-function state still fault-stops at its first batch.
Focused coverage checks both color states, composite/viewport position math, W handling,
screen rounding, retained outputs, and state counters.

The full 6.4B dashboard acceptance run is fault-free and executes all 48,554,942 submitted
vertices: 143,590 fixed-function batches plus 292,866 programmable batches. The combined
position-stream FNV-64 is `e7d742624c1f1793`; vertex/primitive totals remain unchanged, as
does the final scanout hash `73feb194b7694383` because triangle rasterization has not landed.
The direct suite is 9/9.

**Next:** consume retained screen-space output in triangle-list/strip/fan rasterization with
the guest's color/zeta, cull, depth, and blend state, then add `INLINE_ARRAY`. OHCI TD/XID
input and NVNet RX/transport remain subsequent A4 work.

## 2026-07-30 — A4: programmable Kelvin vertex shaders execute on CPU

Kelvin's programmable transform stage now executes instead of stopping at decoded inputs.
The full 136-instruction Cheops program store, 192 float4 constants, program/constant load
pointers, program start, execution mode, and viewport scale/offset aliases are modeled from
the xemu PGRAPH contract. Array-fed attributes populate v0-v15 and execute through xemu's
pinned public-domain NV2A CPU interpreter; oPos, diffuse/specular, and four texture outputs
are retained per transformed batch for the upcoming raster stage. A deterministic FNV-64
over oPos makes transform output observable.

xemu's original `nv2a_vsh_cpu` pin (`561fe80`) is a duplicate commit whose tree no longer
checks out from a fresh clone. The build uses reachable commit `863fc0f`, verified to have
the identical parent, tree, and commit message. Dependency tests are disabled; xboxlle's
focused NV2A test uploads a final `MOV oPos, v0` program and constants, submits mixed
16/32-bit indices, and verifies all three vertices execute.

The full 6.4B dashboard acceptance run is fault-free with four KTHREADs and the unchanged
36,388,327 PFIFO methods, 486 completed flips, and final scanout hash
`73feb194b7694383`. Of 436,456 indexed batches, 292,866 use programmable transforms and
execute 35,477,464 vertices. The position-stream hash is `18b38c709acba547`; the guest
uploads 418,640 program words and 5,683,368 constant words. The remaining 143,590 indexed
batches use fixed-function transform mode and are deliberately counted but not yet
executed. Raster writes likewise remain limited to the already-landed surface clears.
The direct suite remains 9/9.

**Next:** execute the exercised fixed-function transform subset, then consume retained
screen-space outputs in triangle-list/strip/fan rasterization with the guest's viewport,
color/zeta, cull, depth, and blend state. `INLINE_ARRAY`, OHCI TD/XID input, and NVNet
RX/transport remain subsequent A4 work.

## 2026-07-30 — A4: indexed Kelvin vertex front-end executes

The dominant array-fed Kelvin path now executes through primitive assembly. PGRAPH resolves
the two vertex DMA contexts through RAMHT/PRAMIN, tracks all 16 array offset/format slots,
decodes Xbox vertex formats (UB_D3D, UB_OGL, S1, float, S32K, and packed CMP), and validates
every fetched element against the DMA limit and unified-memory mapping. `BEGIN_END`,
`ARRAY_ELEMENT16`, `ARRAY_ELEMENT32`, and `DRAW_ARRAYS` now build the guest's submitted
index stream and account for all NV097 primitive topologies. Attribute fetches preserve the
last element values as real PGRAPH state. No pixel mutation is claimed yet: transform/shader
execution and triangle rasterization remain the next boundary.

The full 6.4B dashboard acceptance run remains fault-free with four KTHREADs, 36,388,327
PFIFO methods, 486 completed flips, and the unchanged final scanout hash
`73feb194b7694383`. It executes 436,456 indexed batches containing 48,554,942 submitted
vertices, performs 99,763,024 validated attribute fetches, and assembles 45,997,592
triangles. The only exercised array format is float; the topology split is 74,117 triangle,
194,873 triangle-strip, and 167,466 triangle-fan batches. Slot 0 is present for every vertex;
slots 1/2/3 dominate the remaining traffic, followed by slot 4 and sparse slots 9/10.
Maximum observed index is 2,049. The 485 remaining begin/end batches use `INLINE_ARRAY`,
which is deliberately still recorder-only.

Focused coverage exercises independent vertex DMA A/B selection, float3 and normalized
UB_D3D attributes, mixed packed-16/32-bit indices, memory fetch, and triangle assembly. The
direct suite remains 9/9.

**Next:** decode and execute the dashboard's exercised transform-program/constant and
viewport state, then rasterize the fetched triangle/strip/fan subset into the existing
color/zeta pitch surfaces. Add `INLINE_ARRAY` after the dominant indexed path produces
pixels; OHCI TD/XID input and NVNet RX/transport remain later A4 items.

## 2026-07-28 — A4: Kelvin surface clears execute in unified memory

PFIFO now resolves `SET_OBJECT` through RAMHT, caches each subchannel's PGRAPH object class,
and gates 3D side effects to the bound Kelvin class 0x97. Object-taking color, zeta, and
semaphore methods resolve their DMA handles before execution. The first software raster
operation models NV097 color/zeta surface format, pitch, offset, clear rectangle, and clear
values, including channel-masked 16/32-bit color writes and Z16/Z24S8 depth/stencil writes.
Every rectangle is validated against pitch, DMA limit, the 27-bit Xbox VRAM aperture, and
the machine's unified-memory callbacks before it is written.

The 6.4B dashboard acceptance run remains fault-free with four KTHREADs and the same
36,388,327 PFIFO methods / 486 completed flips. It executes 852 color clears covering
488,515,072 pixels and 1,968 zeta clears covering 889,978,880 pixels. The selected
720x480x32 target at `0x03c8c000` changes from zero-filled black to the guest's final clear
value (uniform RGB 1,0,1), with RGBA hash `73feb194b7694383`. This is real guest-visible
raster mutation, but not yet dashboard geometry.

Focused coverage exercises Kelvin/RAMHT binding, independent color/zeta DMA translation,
padded pitch, clipped rectangles, full and masked A8R8G8B8 color clears, and Z24S8 clears.
The full direct suite is 9/9. A 500M method census identifies the next concrete rendering
frontier: 53,684 `BEGIN_END` submissions, 1,424,796 `ARRAY_ELEMENT16`, 372
`ARRAY_ELEMENT32`, 87 `DRAW_ARRAYS`, and 348 `INLINE_ARRAY`; 2,062,253 of 2,062,261
methods belong to Kelvin, with only three each for 2D surfaces and image blit.

**Next:** implement the indexed Kelvin vertex path first (`ARRAY_ELEMENT16` dominates),
including exercised vertex-array fetch/format and primitive assembly, then rasterize the
smallest observed triangle subset into the already-correct color/zeta surfaces.

## 2026-07-28 — A4: guest-programmed scanout reaches a host frame

The first faithful framebuffer readout path is landed. NV2A now models the PCRTC scanout
start, PRMCIO VGA CRTC index/data registers used for pitch and depth, and PRAMDAC display
extent/general-control registers. `capture_scanout` validates the resulting unified-memory
range and converts the Xbox's 16-bit RGB565/X1R5G5B5 and 32-bit X8R8G8B8 layouts to RGBA.
The CLI can dump a deterministic PPM with `--frame-dump` or present the captured frame in
the SDL3 shell with `--window`. Focused coverage verifies padded-pitch RGB565 conversion,
register readback, and scanout-address history.

A 6.4B-instruction dashboard acceptance run reaches its budget without fault, with 36,388,327
PFIFO methods, 486/486 flip completions, 1,932 PGRAPH notifications, and four KTHREADs. The
guest ultimately programs a 720x480x32 scanout at physical `0x03c8c000`, pitch 3072; the
captured frame hash is `43890243bf2df383`. It is uniformly black, which establishes the next
boundary precisely: PCRTC/PRMCIO/PRAMDAC selection and host readout work, but the recorded
NV097 clear/draw methods do not yet populate the selected color surface. The direct suite is
9/9.

An independent 8B ceiling run also remains fault-free and observes two completed guest NVNet
packets (684 bytes total) through the headless TX sink. No new runtime fault appears beyond
the prior 6.4B boundary.

**Next:** execute the dashboard's recorded NV097 color-surface state and clear operations in
unified memory, gated by the bound Kelvin object and DMA context, then extend toward the
smallest exercised draw subset. OHCI TD/XID input and NVNet RX/transport remain later A4
items in listed order.

## 2026-07-27 — A4: NVNet TX engine clears the dashboard's first kick

NVNet now executes the xemu-witnessed transmit lifecycle: it reads packed 8-byte ring
descriptors from unified memory, assembles `length+1` fragments, bounds the hardware packet
buffer, submits on LASTPACKET, clears VALID and error flags in guest memory, advances/wraps
current and next pointers, maintains DMA IDLE, and publishes TX status through the existing
W1C/IRQ4 aggregation. With no host network callback attached, completed packets are
explicitly counted and discarded, matching a NIC queue with no connected backend rather
than claiming external delivery. A packet callback is available for a future backend.

`test_nvnet` covers a two-fragment packet, payload assembly, descriptor writeback, ring
wrap, IDLE transition, TX IRQ assertion/acknowledgement, and the existing MDIO/link/reset
floor. The full suite remains 9/9.

The dashboard's first enabled kick at EIP 0x002d74a5 is an empty-ring initialization kick:
the configured descriptor has no VALID bit, so hardware correctly performs no DMA or TX
completion. The 6.4B acceptance run continues past it and reaches the instruction budget
without fault at `KiIdleLoop` (0x8001e3ce), with four KTHREADs, NVNet IRQ mask 0x5f, and no
regression in the 36,388,327-method / 486-flip GPU path.

**Next:** resume with a longer runtime ceiling to expose the next loud dependency, or begin
the first software framebuffer/readout path while the dashboard is live. NVNet RX DMA,
external packet transport, OHCI TD/XID input, and rasterization remain A4 work.

## 2026-07-27 — A4: PFIFO DMA lifecycle and NVNet initialization complete

The dashboard's PFIFO teardown/rebuild sequence is now modeled rather than consumed during
its transient PUT-before-GET state. CACHE1 DMA instance selection is tracked per channel;
the pusher runs only when PUSH0, PUSH1 DMA mode/channel, and DMA_PUSH access/status permit
it; GET/PUT are offsets through the selected DMA object; and consumed GET is mirrored in
both USER and PFIFO register views. `test_nv2a` covers the real gating, address translation,
GET readback, and disabled-pusher PUT-before-GET regression.

That clears the 6.11B command-guard stop and carries `xbmc.exe` through more render/flip and
disk work to its first MCPX Ethernet dependency. A new NVNet floor maps the page containing
the hardware's 0x400-byte aperture at 0xFEF00000 while rejecting padding accesses, preserves
the xemu-witnessed register file, implements descriptor-pointer reset, synchronous MDIO
transactions, PHY address 1/reset values/link transitions, W1C status, and routed IRQ4
aggregation. Enabled packet DMA remains a deliberate loud boundary.

The 6.35B acceptance run completes the dashboard NIC setup: 13 MDIO reads and one write,
MAC/filter and TX/RX ring programming, and PHY link/autonegotiation detection. It stops at
6,275,319,110 retired instructions on the first enabled TX descriptor kick at dashboard EIP
0x002d74a5. GPU work remains healthy at 36,388,327 methods, 486/486 flips, 1,932 PGRAPH
notifications, and 2,961 semaphore writes. The complete suite is 9/9.

**Next:** implement the exercised NVNet TX descriptor DMA/completion path and packet-backend
policy, then continue to the next dashboard dependency. Framebuffer/raster output and OHCI
TD/XID input remain the larger A4 deliverables.

## 2026-07-27 — A4: boot animation completes and the dashboard XBE executes

The AV/display pipeline now completes through the real interrupt path. NV2A executes the
Kelvin flip read/write/modulo methods, queues puller work independently of DMA_GET, stalls on
`FLIP_STALL`, and traps nonzero `NV097_NO_OPERATION` as a PGRAPH software-method
notification. PGRAPH ERROR is aggregated into PMC IRQ 3; the retail kernel handles parameter
0x304, records the pending flip, and its next PCRTC vblank writes `NV_PGRAPH_INCREMENT`.
That advances READ_3D, resumes PFIFO, and executes the deferred semaphore release. A focused
`test_nv2a` covers the notification/IRQ/FIFO-clear and flip-stall/vblank-resume lifecycles.

This fixes the permanent two-buffer throttle without touching the kernel counters. In a 5B
run the guest sustains 399 queued flips / 398 vblank completions, 1,838 PGRAPH notifications,
36.3M methods, and no fault. At 6.089B the boot animation exits after 485/485 flip
completions, the AV worker terminates, disk traffic resumes, and the dashboard creates a
fourth KTHREAD and begins executing `xbmc.exe` user code.

Two follow-on device floors are also landed:

- AC'97 PCM output now consumes the guest's real descriptor ring at 48 kHz stereo cadence,
  advances CIV/PIV/PICB, publishes BCIS/LVBCI/GLOB_STA, and drives routed level IRQ 6.
  This was implemented and tested as a genuine missing device path, then conclusively ruled
  out as the owner of the old AV counter.
- SMC register 0x10 exposes fan-speed telemetry (0..50, existing HLE witness pending a live
  silicon recapture), and both four-port MCPX OHCI controllers expose the standard reset
  register set, disconnected powered root ports, 1 kHz HCCA frame writeback, and IRQ1/IRQ9.
  Endpoint/TD execution and XID devices are not implemented yet.

The 6.1B acceptance run reaches its instruction budget without fault at dashboard user EIP
0x0011f09e, with four KTHREADs, OHCI0 operational (`control=0xbe`, HCCA 0x03fdd000), 12 USB
frames, and continued FATX activity. The suite is 8/8. This is the first confirmed dashboard
XBE-thread execution in xboxlle.

**Next:** continue from the running XBE until the next loud dependency, then implement the
required OHCI TD/XID or NIC path. In parallel with that burndown, turn the already-recorded
NV2A stream into the first visible software framebuffer; no rasterizer exists yet.

## 2026-07-27 — A4: APU voice-idle completion clears the AV wait

The load-bearing MCPX APU path is now implemented end to end. VP `SET_ANTECEDENT_VOICE`,
`VOICE_ON`, and `VOICE_OFF` maintain the guest-resident voice lists and ACTIVE state;
VOICE_OFF posts the guest notifier; a retired-instruction-derived 187.5 Hz frame clock walks
the 2D/3D/MP lists and emits `SE2FE_IDLE_VOICE`; FECV/FEDECMETH/FEDECPARAM/FECTL/ISTS expose
the decoded trap; and the APU drives its routed level IRQ 5 through IEN/GINT aggregation.
FECTL release consumes the requested trap reason so a level-IRQ retry cannot redispatch a
stale voice handle after kernel teardown. A focused `test_apu` covers the complete
VOICE_ON → VOICE_OFF → timed trap → W1C/release/IRQ lifecycle; the suite is now 5/5.

This clears the permanent `0x800637d3–0x800637da` wait without modifying guest RAM
speculatively. The retail kernel takes the exact ISR chain identified in the prior
checkpoint, finds voice 64 while its manager entry is live, invokes the completion routine,
and then legitimately frees the voice object. Clean acceptance runs reach both 200M and
500M retired instructions without fault; the 500M run reports exactly one VOICE_OFF and one
idle-voice trap.

The XBE entry threads have not started yet: KTHREAD count remains three, phase-1 still waits
on the AV worker, and that worker has advanced to a new counter-throttle loop whose call site
is `0x80052f71` (inside `KeStallExecutionProcessor` at `0x80056cb0–0x80056cb9`). The loop
waits while `[0x80061fe0] - [0x80061998] >= 2`. ACI streams 1 and 7 are RUN with no descriptor
engine behind the current shell, making ACI completion/position advancement a leading
candidate, but this next dependency is not yet proven.

## 2026-07-17 — Investigation checkpoint: AV wait is an APU voice-idle completion

The `0x800637d3–0x800637da` wait is now identified conclusively from the exact mapped retail
kernel image. It is not another NV2A fence. The kernel writes `NV1BA0_PIO_VOICE_OFF`
(0xFE820128) at 0x800665f5, marks the voice object asynchronous by setting bit 28 at
0x80066632, and later waits for that bit. The only clearing path is the APU idle-voice
completion routine at 0x800630be/0x8006327d.

The hardware path, cross-checked against the local xemu APU implementation, is: VOICE_OFF
clears the voice's ACTIVE bit; on the next ~5.333 ms APU frame, the voice processor emits
`SE2FE_IDLE_VOICE` (method 0x8000). With FETFORCE1 bit 15 enabled (the kernel does this at
0x800654fe), the front end publishes FECV + FEDECMETH/FEDECPARAM, traps FECTL with requested
reason, raises ISTS.FET, and asserts the APU's routed IRQ 5. The kernel ISR chain
0x8006728d → 0x80066e99 → 0x8006650d → 0x80065e26 → 0x80064d7d invokes the completion
routine and clears bit 28.

Our current APU is only a register-file shell: it returns VP PIO_FREE=0x80 but does not
execute VOICE_OFF, advance APU frames, generate the idle-voice trap, or assert IRQ 5. That is
the precise next implementation task. No speculative guest-RAM modification has been made.

## 2026-07-17 — A4: PFIFO recorder and NV097 semaphore DMA unblock the AV fence

The documented USER-block pushbuffer sink is gone. NV2A now has a synchronous PFIFO pusher
and always-on 4K-method recorder, byte-addressable 1 MB PRAMIN, PFIFO register/channel state,
RAMHT lookup, DMA-object address resolution, and guest-visible execution of the NV097
semaphore context/offset/release methods. The machine wires PFIFO to unified guest memory, so
`NV097_BACK_END_WRITE_SEMAPHORE_RELEASE` writes the requested value through the guest's DMA
object instead of merely advancing DMA_GET. A focused `test_nv2a` covers the complete
RAMHT → DMA object → pushbuffer → guest-RAM write path; the suite is now 4/4.

This resolves the previous permanent AV-worker loop at 0x800570d4–0x800570dc. In a 500M
instruction boot, PFIFO consumes 158,966 methods and performs 14 releases to physical
0x03fb3000 (values 5 through 0x1f), allowing the worker to continue through the boot-animation
command stream. A 5B run confirms the advance is stable and exposes the next real blocker:
the same worker spins at 0x800637d3–0x800637da while bit 28 remains set in `[ecx+8]`
(ECX 0xd0008488, physical 0x1488, value 0x10000041). The translated stack identifies the
immediate return at 0x80066698 and parent frames at 0x80065d76, 0x80066381, 0x80066915, and
0x80062ba7. Phase-1 still waits for AV completion and the XBE thread count remains three.

**Next:** identify the missing GPU-visible completion that clears the kernel AV object's bit
28, model it from NV2A behavior, and rerun until the mapped XBE's entry threads start. The
recorder is landed; CP/method execution beyond semaphore release and the software rasterizer
remain A4 work.

## 2026-07-16 — Repository checkpoint and Arc 1 burndown refreshed

The complete framework history through the dashboard-XBE-load milestone is checkpointed for
the private `mstan/xboxlle` upstream. No emulator changes were pending after `b30b358`; this
checkpoint updates the maintained pickup/status/roadmap documents and records the remaining
work without advancing the technical milestone.

Arc 1 is at **A4 in progress**. A0 and A2 are complete. A1 has a working real-hardware agent
but still owes deterministic xemu-TCG/cosim qualification; A3 runs on Unicorn's dynamic
translator but still owes its page-versioning/invalidation proof. The A4 critical path is:
characterize the AV-worker spin at 0x800570dc, start the mapped XBE's threads, produce the
first software framebuffer, then replace the documented pushbuffer sink with the recorder
and software rasterizer. A5 and Arc 2 have not started.

## 2026-07-16 — A4: kernel mounts the FATX HDD and LOADS the dashboard XBE

The boot now crosses from "kernel idles with nothing to do" to "kernel mounts the boot
device, reads ~6.4 MB off the FATX image over bus-master DMA, and maps the dashboard XBE"
(`LOAD symbols: xbmc.exe` — this softmod image's `xboxdash.xbe` is XBMC — plus 16 per-section
load notifications through the kernel debug service). The XBE's entry threads have not
started yet: the AV worker thread is still inside the kernel's display-mode bring-up
(currently spinning at 0x800570dc), and phase-1 waits on it. That spin is the next target.

New devices / CPU-floor capabilities (all harvested from xemu, silicon-corrected):
- **IDE/ATA controller** (`src/machine/ide.*`): primary channel 0x1F0-0x1F7/0x3F6, bus-master
  DMA at 0xFF60 (BAR4, silicon), drive 0 = FATX HDD image (reports ATA security LOCKED — the
  retail kernel requires it, then SECURITY-UNLOCKs it), drive 1 = ATAPI DVD with no media
  (TEST UNIT READY/REQUEST SENSE/INQUIRY/GESN/GET CONFIGURATION/MODE SENSE incl. the
  ILLEGAL-REQUEST response to the Xbox auth page 0x3E — the xemu witness). Level-triggered
  IRQ 14. Verified: 13,121 sectors read / 1 written, 419 completion IRQs, LBAs marching
  through file data.
- **EEPROM slave at SMBus 0x54** (`XboxEeprom`, backed by machine/eeprom.bin): its ABSENCE
  was the silent phase-1 killer — the kernel retried word-reads, gave up, set an LED error
  pattern and terminated without ever touching the disk. Guest writes stay in-memory.
- **CX25871 TV encoder at SMBus 0x45** (`Cx25871`, register file): the AV worker programs it
  via interrupt-driven SMBus; absent → STATUS_IO_DEVICE_ERROR retry-forever.
- **Guest exception delivery** (`unicorn_floor.*`): Unicorn funnels ALL guest-raised vectors
  (INT n, INT3, CPU exceptions) to UC_HOOK_INTR and has NO native IDT delivery — without a
  hook, the kernel's first `int 0x2d` (DbgPrint) killed the machine with UC_ERR_EXCEPTION.
  We now deliver through the guest IDT via the existing gate-entry machinery; error-code-class
  vectors (#DF/#TS/#NP/#SS/#GP/#PF/#AC) fault-stop loudly until a real path needs them.
  `CpuFloor::on_interrupt()` observer decodes the kernel debug services (DbgPrint text, image
  load/unload names) into the boot log.
- **PIC cascade fix** (`pic.*`): a slave IRQ latched while masked never surfaced when later
  unmasked (master line 2 was only recomputed on raise/ack, not on IMR/EOI/ELCR writes).
  This blocked ALL IDE completion IRQs. `update_cascade()` now runs after every register write.
- **APU shell** (`apu.hpp`, 0xFE800000/512K): xemu register-file semantics + ISTS W1C,
  FEMEMDATA magic write-through, XGSCNT counter, VP NV1BA0_PIO_FREE=0x80 (kernel spins on it).
- **ACI/AC97 shell** (`aci.hpp`, 0xFEC00000/4K): NAM mixer (STAC9700 identity) + NABM bus
  master with GLOB_STA codec-ready, per-stream CIV/LVI/SR/PICB/CR/BDBAR; stream RUN logged
  (no audio engine — deliberate A4 scope).
- **NV2A interrupt chain + frame clock** (`nv2a.*`): PCRTC VBLANK (bit0, W1C) aggregated into
  PMC_INTR bit 24, gated by both INTR_EN regs, level IRQ 3; ~60 Hz frame clock derived from
  retired instructions; PRMCIO 0x3DA input-status-1 retrace bit driven from sub-frame phase;
  USER-block DMA_GET tracks DMA_PUT (**pushbuffer SINK — documented stand-in** until the A4
  recorder/rasterizer); PFIFO RUNOUT/CACHE1 status read EMPTY. Kernel's VBLANK ISR verified
  running every frame (mask → ack → re-enable).
- Silicon captures folded into `pci.cpp` (agent EXEC reads, console live): IDE 00:09.0 full
  config (rev 0xD4, prog-if 0x8A, cmd 0x0005, BAR4=0xFF61, 0x3C=0x01030000, timing regs
  0x44-0x60), LPC 0x60=0x400/0x64=0x0B0C/0x68=0x31000/0x6C=0x0E065491 (dev-9 nibble 0xE =
  IRQ 14), SMC TRAYSTATE=0x00 (closed; xemu's closed-empty is 0x40 — silicon wins). X-code
  decode (XboxBiosTool) proved the init table never touches dev 9 — the kernel programs it.
- `--hdd` / `--eeprom` flags + new diagnostics in `main.cpp`: per-thread instruction
  attribution (KPCR 0x8003da3c CurrentThread poll), non-idle PC ring, every-waiter wait-block
  walk, SMC/absent-slave/EEPROM write logs, IDE/NV2A counters, debug-service decode.

**Next:** characterize the AV worker's spin at 0x800570dc (kernel display bring-up), then the
XBE entry threads should start (KTHREAD count > 3). Then NV2A framebuffer output. ctest 3/3.

## 2026-07-16 — A2e: kernel phase-1 runs to completion (SMBus completion IRQ)

The kernel now boots fully and runs **phase-1 device init to completion** — the phase-1 thread
terminates normally (KTHREAD scan: 0x80001bf8 State=4). System then idles with no dashboard
(that needs the boot device — next).

Cracked the idle-loop block by walking the kernel scheduler/thread structures (layouts from
nxdk's xboxkrnl.h): the phase-1 thread was blocked in KeWaitForSingleObject on a local
notification event, set up by the HAL's **interrupt-driven SMBus read** (transaction started
with GE_HCYC_EN → protocol 0x1a). Our AMD756 completed synchronously and never raised the
completion IRQ → the ISR never signalled the event. Fix: AMD756 now raises a level-triggered
completion interrupt on **IRQ 11** (silicon: LPC cfg 0x64=0x0b0c → SMBus IRQ 0x0b, SCI IRQ
0x0c), deasserting when the ISR clears status. Wired through the PIC.

- Modelled the SMC registers phase-1 reads, all silicon-confirmed: VER "P11" (reg 0x01, cycles
  0x50 0x31 0x31), INTSTATUS 0 (0x11). SMC writes to output-control regs (LED 0x07/0x08, fan
  0x05/0x06, tray, INTEN) now accept+ignore (xemu-style); POWER (0x02) stays a loud stop.
- Observability added to --run2bl: KTHREAD scan (state/priority), wait-object + kernel-stack
  walk using a new `CpuFloor::translate()` (32-bit page-table walk) to read the 0xD0000000
  kernel-stack mapping.
- **Next (A4):** IDE/ATA controller + attach `machine/xbox-hdd.img` (FATX) so the kernel mounts
  the disk and launches `xboxdash.xbe`; then NV2A rendering to display it. ctest 3/3.

## 2026-07-16 — A2e: the real kernel is ALIVE and services timer interrupts

The Xbox kernel now boots to a live, stable state and takes external interrupts. From reset the
whole chain runs: 2BL (SMC challenge → ROM disable → SHA-1 → RC4 → LZX) → kernel entry
0x80010000 → HAL init (SuperIO probe, PIC/PIT/RTC) → memory manager → executive/GPU init →
**KiIdleLoop with working timer interrupts** (250/251 IRQ0 pulses delivered; PIC ends clean,
irr/isr=0 — the clock ISR runs and EOIs).

- **Interrupt/timer stack** (`pic.*`, `pit.*`, `rtc.*`, `port61.hpp`, `open_bus.hpp`): dual
  i8259 (+ELCR, Xbox IRQ map, elcr_mask 0xFF), i8254 PIT (Xbox 1.125 MHz clock, ch0→IRQ0,
  clocked from retired instructions), mc146818 RTC (century 0x7F, 0x55/0xAA CMOS signature).
- **Manual IRQ delivery** (Unicorn has no native IRQ path): `inject_interrupt()` synthesizes the
  protected-mode interrupt-gate entry (IDT via IDTR, push EFLAGS/CS/EIP, clear IF, load handler
  CS:EIP). Three fixes made it work: (1) **page-table translation** — uc_mem_read/write bypass
  the MMU, so the virtual IDT/stack addresses must be walked to physical (`translate()`; this
  was the silent failure); (2) **IF-window delivery** — a code-hook stop fires the IRQ inside
  the idle loop's `sti;nop;cli` window, not at coarse chunk boundaries; (3) **peek-then-ack** —
  confirm inject succeeds before the PIC ack so a failed delivery can't wedge the line.
- **Silicon-confirmed values folded in**: host-bridge cfg 0x84 = 0x03FFFFFF (64 MB size
  register — the X-codes set this and we inject past them, so the kernel was defaulting to
  128 MB and walking the PFN DB off the end of RAM); SMC AVPACK = 0x06 (composite); 64 MB RAM
  aliasing (address bit 26 undecoded). All read from the live console via the agent.
- **NV2A** undecoded-aperture accesses now return xemu-default 0 + log (the 16 MB aperture is
  legitimately sparse) rather than faulting.

**Next:** KiIdleLoop does not yet progress to launching the dashboard — the scheduler isn't
switching to the phase-1 init thread (or it's blocked on a boot device we haven't provided:
IDE/HDD). Investigate thread scheduling / DPC delivery / boot-device path. Then A4 (NV2A/APU/
IDE/USB) toward the dashboard. ctest 3/3.

## 2026-07-16 — Oracle validation pass (console live): v1.1 PM config corrected; NV2A straps captured

With `xlle-agent` up (192.168.4.81:4380, kernel 1.0.4983.6), captured ground truth that closed
gaps and fixed a fidelity bug:
- **This board is a v1.1** and PM is decoded differently than I'd modelled. Silicon: LPC
  (00:01.0) cfg **reg 0x10 (BAR) = 0**, **reg 0x84 = 0x8001** (the v1.1 PMBASE alias),
  reg 0x00 = 0x01B210DE. I had reg 0x10 = 0x8001 (the v1.0 BAR path). The 2BL's
  `cmp [cfg 0x10], 0x8001` is a **board-revision detector** (v1.0 BAR-PM vs v1.1 reg-0x84-PM);
  we were forcing the v1.0 path on a v1.1 board. Fixed `pci.cpp` to match silicon → boot now
  takes the v1.1 path and still reaches the same SuperIO stop (16.24M insns), tests 3/3.
- **NV2A straps from silicon** (replacing xemu stubs/defaults): **PSTRAPS 0xFD101000 =
  0x861D4401** (xemu stubs this to 0; the 2BL discards it but the kernel may not), **PFB_CFG0
  0xFD100200 = 0x03070003**. Both now returned by `nv2a.cpp`.
- Agent read path re-confirmed byte-accurate (PMC_BOOT_0 = 0x02A000A3; RAM 0x10000 = "XBEH").
- Console access kept strictly serialized (single command stream; fragile box). Read-only PCI
  probes done via short cli/…/sti EXEC payloads.

## 2026-07-16 — A2c→A2e: 2BL boots end-to-end; the REAL KERNEL runs to HAL init

The I/O fabric turned the 2BL from an 8086-instruction dead-end into a full firmware boot: it
now runs the entire 2BL and **jumps into the real kernel, which executes ~16.24M instructions
of early/HAL init** before stopping at the first unmodelled device. Concretely, the 2BL:
SMC PIC challenge/handshake → MCPX ROM-overlay disable (LPC cfg 0x80) → SHA-1 hash (implicit
pass, real flash mapped) → RC4 decrypt → **LZX decompress of the kernel (~16M insns)** → jump
to kernel entry at 0x80010000. The kernel then read its own PCI identity (LPC rev 0xB2, PM BAR
0x8001), poked ACPI GPIO, and stopped at `out 0x2e` — the **LPC47M157 SuperIO** config probe.

- **CPU floor gained an I/O fabric** (`src/cpu/unicorn_floor.*`, `include/xlle/cpu_floor.hpp`):
  `on_io` via UC_HOOK_INSN (IN/OUT), `map_mmio` via `uc_mmio_map`, and `request_stop` so a
  device can halt the machine loudly. New `IoEvent`; MMIO events reuse `MemEvent`.
- **Device fabric in the machine** (`src/machine/{io_device,mmio_device}.hpp`, `machine.*`):
  port-I/O + MMIO dispatch by range, **loud fault-stop on any unhandled port/addr** (never a
  silent no-op), each access fed to an always-on ring. `FaultFn` lets a device request the
  stop with exact context.
- **Devices modelled** (all harvested from xemu, verified vs source):
  - **AMD756 SMBus** (`smbus.*`) at I/O 0xC000 + **SMC (PIC16LC)** at addr 0x10 — the challenge
    reads 0x1c–0x1f = 0x52/72/ea/46, handshake completes.
  - **PCI config** (`pci.*`): 0xCF8/0xCFC mechanism + the 10 bus-0 functions with real
    identities; LPC PM BAR pre-set to 0x8001 (retail path), SMBus BAR to 0xC001. LPC cfg-0x80
    bit1 = MCPX ROM-overlay disable (no-op: flash always mapped, overlay is M9).
  - **NV2A shell** (`nv2a.*`): 16 MB aperture, engine-block map; PMC_BOOT_0=0x02A000A3 (matches
    silicon), PBUS PCI mirror, PSTRAPS. Value read at PSTRAPS is discarded by the 2BL.
  - **ACPI/PM** (`pm.*`) behind the LPC I/O BAR at 0x8000: PM1/GPE/GPIO + a **monotonic PM
    timer stand-in** (kernel stall/calibration loops progress; real 3.375 MHz cadence TBD).
- **Value policy for NV2A/PM** (documented in the headers): registers the boot reads are
  modelled with faithful values (hardware capture where available, else xemu); unmodelled
  registers inside a real block return xemu's default (0) **and are logged** (a debt to replace
  with hardware truth via the oracle); aperture holes fault loudly. xemu boots the same retail
  2BL with these defaults, so they are boot-correct witnesses.
- **Verified:** `ctest` still 3/3 (~0.1 s, no regression). `xlle --boot <flash> --run2bl <bldr>
  --max-insns 200000000` reaches kernel HAL init in ~16.24M insns, final eip 0x800357b8,
  fault-stop at `out 0x2e` (SuperIO).
- **Open / next (A2e):** LPC47M157 SuperIO (config-port device), then the functional
  interrupt/timer stack the kernel needs — **i8259 PIC (0x20/0xA0), i8254 PIT (0x40), mc146818
  RTC (0x70)** and interrupt delivery into Unicorn. Also to confirm against the oracle when the
  console is next on: PM BAR = 0x8001 for this v1.1 board (v1.1 also has a PMBASE alias at LPC
  cfg 0x84), and the real PSTRAPS / PM-timer rate.

## 2026-07-16 — FATX HDD image built + verified; xemu still deferred

- **`machine/xbox-hdd.img` (8GB FATX) BUILT and independently VERIFIED** (pyfatx): C: has
  `xboxdash.xbe` + `xboxdashdata` + Audio/Xbox.xtf; E: has TDATA/UDATA. A xemu-loadable HDD
  image with the real dashboard + saves. (Built via a subagent whose text reports were
  unreliable, but the filesystem check confirms the real content.)
- **xemu**: WSL build hangs in meson configure (deferred). Docker route attempted — Docker
  Desktop won't reach "running" without maintainer interaction (first-run prompt/login), so
  the container build is paused on the maintainer. Not critical (xemu boot-validation needs
  MCPX anyway).
- **A1/A2 inputs now in hand**: EEPROM ✅, validated flash ✅, **HDD image ✅**, hardware
  profile ✅. Only real remaining gate: MCPX (deferred; authentic-cold-boot only).

## 2026-07-16 — Flash VALIDATED; HDD pulled; NV2A expanded; xemu blocked

Ran tasks 1-3 (console access serialized). Results:
- **Flash BIOS VALIDATED** (structural, high confidence): offline analysis + local spot-check
  confirm `machine/flash-dump-ff000000.bin` is a genuine, complete, correctly-ordered 256KB
  retail-4983 flash — NV2A-ptr header (0xFF000009, LSB bit), MCPX table @0x70, 2BL @0x39E00,
  FBL @0x3D400, x86 GDT + reset jmp @0x3FFF0, all at documented offsets. **This is our
  `flash.bin`** (only open item: hash cross-check vs a reference DB that 403'd bots).
- **HDD files PULLED**: `C:` + `E:\TDATA` + `E:\UDATA` via FTP → `machine/hdd/` (2895 files,
  1020MB, 0 errors); MS dashboard (`xboxdash.xbe` + `xboxdashdata`) captured. NEXT: pack into
  a FATX HDD image.
- **NV2A expanded snapshot**: PCI `10DE:02A0`, rev A3, ~233MHz core, framebuffer @0x03EB4000,
  engine/PLL/PFIFO registers (`machine/probes/hw-2026-07-16.md`). Agent stayed healthy.
- **xemu build BLOCKED**: needs WSL sudo `apt` deps (meson/ninja/glib/pixman/SDL2/…). Not
  urgent — full boot-validation is gated on MCPX anyway (deferred).
- Agent handed back to MediaCenter (DASH); relaunch `xlle-agent` for future hardware work
  (v0.3 with HDD file-read planned so no MediaCenter/agent swap is needed).

## 2026-07-16 — Agent v0.2 (crash-guarded); flash dumped; NV2A characterized

- **v0.1 hung the console** on a read of the top-of-memory MCPX region (no fault guard);
  required a maintainer power-cycle. **v0.2** guards reads (MmIsAddressValid + MMIO/flash
  range whitelist, top-512B excluded), writes limited to RAM/NV2A. Verified: the fatal
  address now returns ERR, agent survives. Auto-redeployed on reboot; relaunched by maintainer.
- **Flash BIOS dumped by the agent itself** (256KB @0xFF000000 → `machine/flash-dump-ff000000.bin`,
  sha1 02798c98…) — so `flash.bin` won't need to come from the maintainer. **Pending
  validation** vs a known-good retail-4983 image / xemu boot before use (careful posture).
- **NV2A characterized from silicon**: chip rev A3 (`PMC_BOOT_0=0x02A000A3`); GPU core clock
  **≈233 MHz** (NVPLL decode — matches known spec); engine-enable / PFB / PTIMER / PLL
  values captured (`machine/probes/hw-2026-07-16.md`).
- **MCPX secret ROM confirmed not readable** from a running console (masked after boot) —
  stays a known per-revision image, deferred (only needed for authentic cold boot).

## 2026-07-16 — HARDWARE ORACLE LIVE: agent running, real CPU characterized

The "hook Claude to a real Xbox" endgame is working. `xlle-agent` launched on the console;
PC drives it hands-off over tcp/4380. Verified end-to-end:
- `PING`/`INFO` OK; read `kernel 1.0.4983.6` from live memory (matches EEPROM); read the
  agent's own XBE header (`XBEH`) out of guest RAM over the network.
- **Arbitrary code execution proven**: pushed x86 payloads that ran on the real CPU (a
  constant-return, then live `CPUID`).
- **CPU characterized from silicon** (see `machine/hardware.json` `cpu_measured`):
  GenuineIntel, sig **0x068A** = family 6/model 8/stepping 10 (PIII Coppermine); features
  fpu/tsc/msr/cx8/cmov/mmx/fxsr/**sse**; **no SSE2/SSE3** — confirms DESIGN §1 from hardware.
- **Flag-provenance cross-checked vs silicon**: `xor` → defined bits match, AF=0; `mul` →
  CF/OF defined match, undefined ZF/PF/SF/AF captured. The CPU floor's policy is consistent
  with real hardware; the oracle resolves exactly our undefined-flag questions.

This is the independent ground-truth oracle (PRINCIPLES §8) the whole plan leans on — now
operational. Launch is one manual step per session (this build's XBMC HTTP API can't
RunXBE); everything after is hands-off.

## 2026-07-16 — Hardware-control agent built & deployed (ready to launch)

- **nxdk toolchain working** in WSL Ubuntu (clang-18 + `llvm-*-18` symlinked to unversioned
  names, no sudo; build via `../build-nxdk.sh` + `MSYS_NO_PATHCONV=1`). hello sample + agent
  both build clean to `bin/default.xbe`.
- **`xlle-agent` v0.1 built and deployed** to the console at `E:\Apps\xlle-agent\default.xbe`
  (487 KB). TCP control server (tcp/4380): PING/INFO, guest-memory R/W, EXEC arbitrary
  payload, XBE launch, DASH-to-dashboard. Driver: `tools/xbox_agent.py`.
- **Not yet launched** — launching takes over the console screen; awaiting maintainer's go
  (they launch `xlle-agent` from MediaCenter, or I attempt XBMC HTTP `ExecBuiltIn(RunXBE)`).
  Once up, PC connects to `:4380` and drives it hands-off — the hardware-oracle tier is live.

## 2026-07-16 — Console onboarded over FTP; identified; EEPROM secured

- **Console found and reachable:** `192.168.4.81` on the LAN (XBFileZilla / XBMC). FTP
  read/write authorized by maintainer (no deletes).
- **Identified: Xbox v1.1**, kernel **1.0.4983.6**, 64 MB, NTSC / XBE region 01 (NA),
  Seagate ST310014A HDD, Samsung SDG-605B DVD, genuine MS MAC OUI `00-0D-3A`. Softmod
  (MediaCenter via `evoxdash.xbe`) — runs stock **retail** BIOS.
- **Firmware pinned:** v1.1 board → **MCPX v1.0** (matches xemu's recommended profile).
- **Secured into `machine/`** (git-ignored): `eeprom.bin` (256B, sha1
  eb2a1e82…; keys redacted from any output), the ConfigMagic info text, `hardware.json`
  (provenance-tagged), and the original MS dashboard exe `hdd/C/xboxdash.xbe` (Arc 1 target).
- **Still needed for A1/A2:**
  - `mcpx.bin` (standard **MCPX v1.0** — per-revision, not per-console) and a BIOS image
    (retail 4983 and/or an xemu-compatible one like Complex 4627). ConfigMagic can't dump
    BIOS; needs a flash tool or a standard image (maintainer owns the firmware).
  - A bootable HDD image: recommend selective FTP rebuild (`C:` + `E:\TDATA` + `E:\UDATA`)
    into a FATX image, vs. a physical raw dump.
  - nxdk homebrew probe channel (hardware oracle).

## 2026-07-16 — Arc 1 / A0 (Foundations) COMPLETE and verified

The A0 deliverable is met: the host runs and the CPU floor executes IA-32 instruction
fixtures under ring instrumentation, with **no Xbox devices attached**. Builds and runs with
no copyrighted firmware.

- **Repo layout:** `xboxlle/xboxlle` (framework), workspace parent `xboxlle/` holds reference
  clones out of tree.
- **Toolchain:** MSYS2 MinGW64 gcc 15.2 + Ninja + CMake; deps via FetchContent (Unicorn
  x86-only, SDL3). See `docs/BUILDING.md`.
- **CPU floor = Unicorn (confirmed).** The x86-32 fixture spike passes **7/7**
  (`mov`/`add`/`sub`/`xor`/`inc`/two-insn/mem-store), including flag-provenance handling
  (AF correctly excluded after `xor`). The from-scratch interpreter fallback was **not**
  needed — Unicorn is the floor. (`src/cpu/unicorn_floor.*`, `src/spike/*`.)
- **Observability skeleton works end-to-end.** Always-on rings (`include/xlle/ring.hpp`),
  JSONL emitter (`jsonl.hpp`), TCP debug server on 4370 (`src/obs/*`). Verified over the
  socket: `ping→pong`, `rings`, `query retired/mem` return JSONL windows (the `mov_store_mem`
  write showed up as `{addr:0x2000,size:4,value:0xdeadbeef}`).
- **Host shell (SDL3) works.** `xlle --window` opens/closes a window cleanly; SDL_gpu
  (Vulkan/D3D12) backends are available for the future presentation path. SDL3.dll is copied
  next to the exe.
- **Tests:** `ctest` 3/3 (ring, jsonl, spike-acceptance).
- **References cloned out of tree** (`../refs/`): **xemu** (full NV2A source — A4/A5 harvest)
  and **sp00nz/xboxrecomp** (kernel-ABI map — A2/A3 reference).

### Known A0-scope notes (honest, not blockers)
- `seq` is per-CpuFloor; in the spike each fixture uses a fresh floor so `seq` restarts. The
  real machine has one floor, so `seq` will be globally monotonic there.
- Instruction hook counts *dispatch*, not true architectural *retirement* (PRINCIPLES §6);
  precise retirement/restartable-fault boundaries are a later contract validated on hardware.
- x87/MMX/SSE state is not yet in `X86Regs` — added when a trace first needs it
  (PRINCIPLES §5).

## Blocked on (for A1 / A2)

The Tier-1 machine evidence package + the nxdk homebrew probe channel (`docs/INPUTS.md`):
`mcpx.bin` (v1.0), an xemu-bootable P0 `flash.bin`, `eeprom.bin`, `hardware.json`, `hdd.raw`,
and homebrew execution on the console. Everything after A0 needs real hardware/firmware to
have an oracle.

## Next action

A1 — Oracle + hardware harness: stand up the pinned xemu-TCG reference behind a determinism
gate and the nxdk probe agent, all on the one JSONL schema. Begins when the Tier-1 package +
probe channel are available.
