# Ranked donor follow-up: implementations tested, no new speed milestone

Working runtime baseline remains `54c102a` on
`perf/measurement-first-2026-09`. Four independent implementations and one
bounded kernel audit were tried in the agreed perceived-value order. None
established a substantial additional dashboard speed gain. Preserve the code
and evidence remotely; do not promote component counters into an FPS claim.

The LLE floor, build-selected HLE policy, caller-visible memory/results/lifetime
contracts, real completion and no-stub rule remain intact. HLE does not have to
preserve guest arithmetic, instruction histories or internal implementation.
The matrix experiment deliberately uses native float arithmetic. No live
HLE/LLE switching was introduced.

## Ranked outcomes

| Rank / experiment | What was actually tried | Result / disposition |
| --- | --- | --- |
| 1. Native XDK routing | Own index payload at the ordered real BEGIN command; remove index packets and repeated refills | Refills 29,814 → 0, but final idle fast-forward disappears and menu slows. Park. This is one operation, not all XDK graphics. |
| 2. Whole-routine math | Byte-pinned native 4x4 matrix multiply with alias-safe inputs and stdcall return | 941,677 calls; final native instructions fall 30.9%. Validated opt-in candidate preserved remotely; no proven menu speed gain, not promoted. |
| 3. Graphics state | Program-local changed constant ranges, raster and sampler state suppression | 5,447,584 uniform updates skipped; all captures match. Timing small/mixed. Park. |
| 4. Render worker | Actual separate render thread, owned inputs, bounded queue, residency metadata and ordered fences | All 580,540 draws execute; captures match. Queue reaches 128, 107,505 backpressure events. Timing mixed; park. |
| 5. Kernel services | Audit donor event/memory helpers against our live guest scheduler and current profile | No supported major hot service identified; event bridge alone lacks our scheduler ownership contract. Audit-only; no speculative port. |

## Real dashboard timing, with limits

Each implementation completed the full 27B interactive Settings/return route,
all 14 inputs, empty stderr. All hosts were contaminated by foreign workloads.
The worker uses the immediately preceding state experiment's unmodified A arm
as its shared baseline; it is not a dedicated worker A/B pair.

| Experiment | Whole-route frame windows, baseline → candidate | Final 123-vblank window, baseline → candidate |
| --- | --- | --- |
| Direct index payloads | See branch evidence; decision follows idle regression | 5.7800 → 6.4961 s |
| Native matrix | 122.1214 → 117.4513 s | 5.8952 → 5.9537 s |
| State cache | 122.2557 → 120.2490 s | 5.8232 → 5.7589 s |
| Worker, shared baseline | 122.2557 → 117.7112 s | 5.8232 → 6.0696 s |

The matrix's final four windows improve from 24.8007 to 21.3450 seconds, but
the final window is flat. State's final four regress 21.6567 → 22.5070 seconds;
worker's regress to 23.2182 seconds. Small route-total changes on this busy host
cannot settle the speed question. Do not select only the favorable intervals.

Main retains the earlier round's roughly **21 guest vblanks/s** steady-menu
screen (5.763 seconds for 123 vblanks in its prior combined run; this round's
unchanged baseline commonly measures about 5.8 seconds). This is guest-clock
throughput, not distinct displayed FPS. Original-speed operation, original
content cadence and the Blinx gate remain open.

## Validation and preserved work

| Branch / head | Validation | Detailed evidence |
| --- | --- | --- |
| `spike/xdk-direct-20261004` / `a70bfaf` | 80/80 checks; real queue ordering/ownership test; full route, changed animation phase | [Direct XDK result](https://github.com/mstan/xboxlle/blob/spike/xdk-direct-20261004/docs/RESULTS-SPIKE-XDK-DIRECT-2026-10-04.md) |
| `spike/native-matrix-20261004` / `2b4c14f` (implementation `2e8c560`) | 80/80; 2,000 original-function matrix/alias comparisons; full route, visually reviewed | [Matrix result](https://github.com/mstan/xboxlle/blob/spike/native-matrix-20261004/docs/RESULTS-SPIKE-MATRIX-HLE-2026-10-04.md) |
| `spike/hle-state-20261004` / `d064562` | 79/79; clear/program/constant transitions; all 28 route captures identical | [State result](https://github.com/mstan/xboxlle/blob/spike/hle-state-20261004/docs/RESULTS-SPIKE-HLE-STATE-2026-10-04.md) |
| `spike/render-worker-20261004` / `177c068` | 80/80; 1,000 ordered draws, owned inputs, backpressure/error tests; all 28 route captures identical | [Worker result](https://github.com/mstan/xboxlle/blob/spike/render-worker-20261004/docs/RESULTS-SPIKE-RENDER-WORKER-2026-10-04.md) |
| `spike/kernel-services-audit-20261004` / `21da04e` | Donor/runtime/profile audit; no implemented replacement or speed claim | [Kernel decision](https://github.com/mstan/xboxlle/blob/spike/kernel-services-audit-20261004/docs/RESULTS-SPIKE-KERNEL-SERVICES-2026-10-04.md) |

All branches are based on `54c102a`, isolated from one another and pushed.
No runtime changes from this round are integrated. The main checkout receives
this report and status only; its previously validated LLE build is unchanged.
Private logs, generated profiles and captures stay in build directories.

## Donor coverage and what this changes about prioritization

Remote heads checked for all three supplied compiler variants, plus the
NoRain title runtime: sp00nznet `1409a7d`, NoRain compiler `7adaf21`, DOAXVB
`aa1d3c8`, phobos `7884426`. The phobos delta from the earlier `f01b20d` pin is
a fog-coordinate fix, not new scheduler/state/worker architecture.

NoRain supplied the queue ownership/native API direction; phobos supplied
state-cache and kernel/event mechanisms. The NDS whole-math routine adapter
provided a useful caller-contract model, applied to a measured Xbox hotspot.
sp00nznet remains the compiler-lineage reference. No donor's unsupported draw
skips or immediate-success services were copied.

The ranking was a hypothesis, not a promise. Narrowing index submission exposed
a scheduling cost; state caching and a worker are insufficient by themselves;
whole-routine math removes real guest execution but still needs wall-time
qualification. Do not start a combination sweep or broad native scheduler port
without evidence of the limiting cost. Existing profiles still show 4.53 million
native/fallback handoffs per final interval, including substantial idle-boundary
traffic; this is an attribution lead, not a count of automatically removable
kernel calls or a demonstrated next speedup.

Tracking: `beads-xbox.1.50` through `.54`, under `beads-xbox.1`. The broader
graphics integration / original-speed dashboard objective remains open.
