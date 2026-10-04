# Remaining donor opportunities: implementation results

Follow-up to the owner's request to try all five leads in the
[ranked experiment report](RESULTS-RANKED-DONOR-SPIKES-2026-10-04.md).
Every experiment has an independent worktree and remote branch. Base `4bcad36`
contains the retained `54c102a` runtime plus documentation. The direct-service
branch additionally imports the previously parked matrix implementation.

## What was actually tried

| Opportunity | Implemented scope | Result / disposition |
| --- | --- | --- |
| Direct AOT-to-HLE calls | Connected native execution calls the whole matrix service with live CPU state and certified guest RAM; 699,337 of 941,677 calls avoid the Unicorn entry adapter | Mechanism works; no substantial dashboard wall-time gain. Parked. Wider AOT coverage remains open. |
| Complete native XDK graphics services | Both whole vertex-constant setters, including the CPU-readable bank, ordered command writes, real ring allocation and calling convention | 2.12M native calls; flat/slower dashboard screen. Parked. This is two complete APIs, not replacement of the complete graphics subsystem. |
| Substantial donor renderer | phobos D3D11 RHI, NV2A vertex decoder/HLSL and combiner HLSL behind a separate build-selected renderer | Real route completes with 580,540 draws; close visual results, slower observed timing. Parked. |
| Multi-command render packets | NoRain-inspired owned packets, bounded queues, genuine fences/readback and failure propagation | 592,686 commands in 20,225 packets; no substantial gain. Parked. |
| Compiled shader disk cache | phobos-inspired versioned full-key records, atomic writes and actual source compilation on invalid/missing binaries | Warm launch avoids all 33 distinct source compilations; useful startup/hitch candidate, no steady-menu win. Saved separately. |

No runtime changes from these experiments are integrated into the retained
branch. No missing draw/service is replaced with successful placeholder work.
HLE is allowed native math, scheduling and rendering differences; exact
instruction histories and mandatory concurrent LLE comparison are not imposed.

## Parked means available for a relevant title workload

Owner clarification, 2026-10-04: preserve and revisit these branches. The closed
issues describe completed bounded experiments; they do not declare the ideas
useless for every game. Blinx bring-up is now allowed before menu performance
qualification and will supply a second workload when it reaches a repeatable
scene. These are retry conditions, not predicted speed gains:

| Candidate | Revisit when |
| --- | --- |
| Direct native services | A reached, costly Blinx routine has a verified caller contract and native callers; audit the title's own function identity before installing an adapter |
| Whole native XDK APIs | Blinx's SDK version and measured API usage identify substantial host cost; implement the full selected API effects rather than installing dashboard addresses |
| phobos D3D11 backend | A repeatable title scene exercises supported graphics states and offers a different draw/vertex/texture workload; report missing state support explicitly |
| Multi-command packets | A title trace shows useful independent GPU work between genuine completion dependencies; compare ordered draw completion and caller wait time |
| Persistent shader cache | Cold/warm title loading or scene transitions compile new programs; measure compilation hitches separately from warm steady-scene speed |

Keep the dashboard as a regression and attribution workload for every shared
change. Current instruction/call counts have not established its limiting host
cost. Use sampled host time on a reached menu window before selecting another
dashboard optimization; retain sampling coverage, binary identity and host-load
limits. Do not run every parked branch against a title merely because it boots.

## Actual dashboard measurements

Each pair completes the real 27B clock/menu/Settings/return route with all 14
input events. Values below are sums of the 18 recorded frame-window wall times,
not total process startup duration. The final window contains 123 guest
vblanks; it does not measure distinct displayed FPS.

| Experiment | Route wall time: control -> candidate | Final window: control -> candidate |
| --- | ---: | ---: |
| Direct matrix service, versus the old matrix entry adapter | 95.3420 -> 94.7587 s | 4.14354 -> 4.12833 s |
| Native XDK constant APIs, versus retained runtime | 101.5446 -> 101.7939 s | 4.77457 -> 5.01213 s |
| phobos D3D11 backend, versus retained runtime | 149.5616 -> 207.7754 s | 7.49128 -> 9.75986 s |
| Multi-command worker, versus retained runtime | 100.2931 -> 103.1339 s | 4.77275 -> 5.03284 s |
| Shader cache, empty versus warm with the same executable | 103.1356 -> 102.3881 s | 4.87419 -> 5.01288 s |

Foreign compiler/runner processes were present. These are large-gain screens,
not statistically qualified small-percentage comparisons. Do not compare
different pairs to claim an improvement as host load changes.

The shader cache's direct measurement is useful: 1.7746 seconds of source
compilation in the empty-cache launch becomes zero in the warm launch, with
0.0502 seconds in binary lookup/loading. It does not establish a 1.77-second
end-to-end startup improvement, and the final menu window was slower.

## Validation and saved branches

All five candidates pass their 80 applicable checks, including meaningful
caller/resource tests. Direct service, packet worker and shader-cache pairs
match all 28 captures. Native constants match 25/28, including the final menu;
the three changed clock/transition images were visually reviewed. Its small
animation/raster differences and 580,683 versus 580,540 draws are recorded,
rather than hidden behind an instruction-count comparison.

The phobos candidate executes 449,494 draws through direct GPU vertex input and
131,046 through real conversion. All 28 captures differ, but the per-capture
mean absolute RGB error averages 0.07786 on a 0..255 scale. Clock, menu,
Settings, return and final screens were visually reviewed side by side. Both
legs complete all required draws; this is no general game-compatibility claim.
This pair used a 1536 MiB limit for both executables after the donor backend
hit the old 768 MiB guard during bring-up. The branch report records fixes and
remaining memory/compatibility limits.

| Branch / head | Contract and measured evidence |
| --- | --- |
| `spike/aot-hle-direct-20261004` / `476ad8a` | [Direct native service report](https://github.com/mstan/xboxlle/blob/spike/aot-hle-direct-20261004/docs/RESULTS-SPIKE-DIRECT-SERVICES-2026-10-04.md) |
| `spike/xdk-constants-20261004` / `2e3deb5` | [Whole constant API report](https://github.com/mstan/xboxlle/blob/spike/xdk-constants-20261004/docs/RESULTS-SPIKE-XDK-CONSTANTS-2026-10-04.md) |
| `spike/phobos-backend-20261004` / `392515d` | [Borrowed D3D11 backend report](https://github.com/mstan/xboxlle/blob/spike/phobos-backend-20261004/docs/RESULTS-SPIKE-PHOBOS-BACKEND-2026-10-04.md) |
| `spike/command-packets-20261004` / `c26dda5` | [Command packet report](https://github.com/mstan/xboxlle/blob/spike/command-packets-20261004/docs/RESULTS-SPIKE-COMMAND-PACKETS-2026-10-04.md) |
| `spike/shader-disk-cache-20261004` / `89d2908` | [Persistent shader report](https://github.com/mstan/xboxlle/blob/spike/shader-disk-cache-20261004/docs/RESULTS-SPIKE-SHADER-DISK-CACHE-2026-10-04.md) |

Raw logs, captures, private profiles and binaries remain in ignored build
directories. Each branch report names its evidence. Source and donor license
notices are committed; dashboard code/assets are not.

## What this establishes about the matrix result

The prior 30.9% reduction counted native guest instructions, not host CPU time.
This round removes the old Unicorn entry/register-transfer route from 74.3% of
matrix calls. The real elapsed-time result is still essentially flat. We have
now tested that particular explanation; it does not establish the remaining
25.7% of adapter calls, additional math functions, or generic transition removal
as a major opportunity. The wider native-function proposition remains unproven.

## Donor coverage and remaining scope

The supplied compiler variants remain the audit set: sp00nznet `1409a7d`, NoRain
compiler `7adaf21`, NoRain DOAXVB runtime `aa1d3c8`, and phobos `7884426`.
phobos RHI/shader imports are pinned at `f01b20d`; the later delta changes a
different shader-state file. NoRain supplies packet ownership/publishing and
native service direction; phobos supplies actual D3D11/shader code and the
persistent cache design. sp00nznet is the compiler-lineage reference, not a
separate modern backend port in this round.

Full native XDK replacement, wider whole-function AOT, native kernel/audio
services, original-speed dashboard qualification and Blinx remain open. The
two-API slice and matrix dispatch trial must not be counted as completed full
subsystem replacements. Tracking: `beads-xbox.1.55` through `.59`; broader
graphics integration remains under `.45` and the menu gate under `.17`.

All five bounded experiment issues are closed with their measured dispositions;
the broader performance goal is not closed. Central Beads updates are durable
locally. `bd dolt push` still fails with error 1105 because the configured remote
data ref is missing (existing tracker `beads-geh`). Source branches are pushed
successfully; this issue-store synchronization failure is separate.
