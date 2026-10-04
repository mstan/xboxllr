# Four donor-informed spikes: three retained, presentation parked

Each experiment has its own worktree, branch and remote commit, independently
based on `4d2370a` (`spike/hle-baseline-20261003`). The retained changes are
integrated on `perf/measurement-first-2026-09`. No original-speed or full-AOT
completion is claimed.

| Experiment | Remote branch and commit | Decision and evidence |
| --- | --- | --- |
| Raw vertex streams and upload rings | [spike/hle-raw-streams-20261003](https://github.com/mstan/xboxlle/tree/spike/hle-raw-streams-20261003), `7d3df45` | Retain. Final 123-vblank menu interval 10.459 -> 8.861 s; GPU decoding replaces CPU expansion. |
| Native XDK indexed command construction | [spike/hle-xdk-graphics-20261003](https://github.com/mstan/xboxlle/tree/spike/hle-xdk-graphics-20261003), `3212ff5` | Retain as a private-profile opt-in. Final native instructions 144.3M -> 91.9M; 29,814 genuine ring refills over the route. |
| Connected native CPU execution | [spike/aot-native-regions-20261003](https://github.com/mstan/xboxlle/tree/spike/aot-native-regions-20261003), `c7023ac` | Retain. Final menu interval 11.499 -> 9.472 s; avoids 6.58M intermediate integer-state transfers. |
| GPU-resident presentation | [spike/hle-gpu-present-20261003](https://github.com/mstan/xboxlle/tree/spike/hle-gpu-present-20261003), `54b92b4` | Parked, [draft PR #1](https://github.com/mstan/xboxlle/pull/1). Presentation latency 1.430 -> 0.394 ms, but final menu 8.685 -> 8.661 s: no meaningful menu gain established. |

These are busy-host screening results, not isolated causal speedup estimates.
Do not multiply independent improvements together. No required work is omitted
or replaced with a success stub. All four candidates complete the real dashboard
clock/menu/Settings/return route with all 14 inputs and empty stderr. CPU and
presentation captures match the baseline exactly; raw-stream arithmetic differs
by at most one channel level at one pixel in two captures. XDK timing changes
produce small image differences documented in its report; reviewed Settings and
returned-menu screens remain intact.

## What was reused, and what remains outside these spikes

NoRain's DOAXVB presenter supplies the raw-stream/ring and native-surface policies.
The XDK experiment uses phobos/XbSymbolDatabase discovery, verifies the actual
dashboard ABI, and replaces work that NoRain's adapter still executes before
its host draw. The xboxrecomp variants' native function execution motivates
retaining our existing compiled state across connected regions. Existing xemu
shader translation and NoRain texture code remain the whole HLE renderer's basis.
Pins and source locations are in the individual reports and
[the all-donor assessment](RENDERER-REUSE-2026-10-02.md).

The XDK spike replaces indexed command construction, not every graphics API or
the entire guest driver. It retains guest preparation/allocation/completion and
NV2A command consumption. The CPU spike connects existing regions; it does not
add whole-function coverage or remove the dynarec. The presentation spike does
not implement a separate worker queue. These boundaries are deliberate, reviewable
experiments, not claims that the larger opportunities are finished.

The LLE renderer remains the default build. HLE selection remains a whole-build
choice, with no live renderer switch or instruction-history requirement. The
optional XDK adapter requires a locally generated, audited function profile;
leave `XLLE_XDK_DRAW_PROFILE` empty to retain the guest implementation.

## Combined validation

The retained commits are `4b697df` (raw streams), `a91208f` (CPU regions) and
`2270c7a` (XDK) on the integration branch. Both HLE and LLE builds pass all
79 applicable checks after integration: `build/spikes-combined-tests.log`
(63.40 s) and `build/spikes-lle-tests.log` (63.85 s). The combined HLE build
includes the private dashboard XDK profile; the LLE build does not use it.

The combined **interactive** 27B route completes all 14 inputs with empty stderr
and zero presentation failures. The final returned menu was visually reviewed.
Against the standalone XDK route, 27/28 captures are identical; the 15B capture
differs at one pixel by one channel level, consistent with GPU arithmetic.
The final 123-vblank interval takes **5.7629638 s** (21.343 guest vblanks/s).
Total frame-window time is 122.6498678 s. The earlier interactive baseline took
8.6846247 s in the final interval, but it was a separate busy-host run; this is
not a controlled paired estimate. The process census marks timing unusable for
isolated attribution. No original-speed or distinct-frame-rate claim follows.

All three paths are active: 578,619 raw draws over the route, 142,113 native
indexed calls with 29,814 real ring refills, and 6,272,404 connected native
entries in the final interval. Native/fallback instruction totals agree with
the standalone XDK run. The remaining 4,526,619 fallback-to-native handoffs in
that interval were **not** removed; larger native coverage/services remain
necessary candidates. No extra noisy timing repeats were used to promote a win.

Private combined evidence is `build/spikes-combined-r1*`; the preserved prior
baseline executable is `build/spike-baseline-4d2370a.exe`. Reproduce with the
stock replay, the Settings route, native AOT, idle fast-forward, provisioned
HDD security, public register transfers, decoded-DXT cache disabled, and
`--interactive --interactive-present-stats-ms 5000`. Original Xbox speed and
content cadence remain the acceptance target; Blinx remains downstream.

## Detailed evidence

- [Raw streams](RESULTS-SPIKE-RAW-STREAMS-2026-10-03.md)
- [Native XDK command construction](RESULTS-SPIKE-XDK-GRAPHICS-2026-10-04.md)
- [Connected CPU regions](RESULTS-SPIKE-NATIVE-REGIONS-2026-10-03.md)
- [Parked presentation report](https://github.com/mstan/xboxlle/blob/spike/hle-gpu-present-20261003/docs/RESULTS-SPIKE-GPU-PRESENT-2026-10-04.md)

All private firmware, title profiles, generated title code and frame captures
remain outside tracked source. The replay harness records binary identities,
arguments and concurrent-process observations beside each local run.
