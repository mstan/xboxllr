# Direct native AOT to HLE calls: parked

Tracking: `beads-xbox.1.55`. Branch: `spike/aot-hle-direct-20261004`.
Base: `4bcad36`, plus the previously parked matrix implementation `2e8c560`.

Connected AOT execution now calls registered native services with its live
integer state. Matrix multiplication reads certified guest RAM, computes the
complete native result and returns directly to the caller. It avoids individual
Unicorn register API calls and emulator stop/restart on this path. Cold RAM or
non-native callers use the existing native matrix entry adapter. No generated
guest instruction count is invented for HLE work. This is whole-function HLE,
not the old register-only exact-history replacement interface.

The donor proposition is native-function execution across service boundaries,
as used by the sp00nznet-derived runtimes. This experiment implements that
dispatch change; it does not add wider AOT coverage or remove dynarec.

## Real dashboard result

One old-adapter/direct-adapter pair completed the 27B settings route. Both
executed all 14 inputs, with empty stderr; all 28 captures are byte-identical.

| Measurement | Old matrix adapter | Direct native services |
| --- | ---: | ---: |
| Sum of route interval wall time | 95.3420 s | 94.7587 s |
| Final 123-vblank interval | 4.14354 s | 4.12833 s |
| Total matrix calls | 941,677 | 941,677 |
| Calls retaining live AOT state | 0 | 699,337 |
| Calls through Unicorn entry adapter | 941,677 | 242,340 |

The mechanism works for 74.3% of calls. The observed 0.6% route difference and
0.4% final-interval difference are not a substantial speed gain. Both legs
contain other projects' compiler/runner processes and are marked unusable for
quiet-host timing. These are directional screens, not statistically qualified
speedups or distinct displayed FPS. This result does not establish transition
overhead as the dominant matrix cost.

All 80 checks pass. New tests exercise direct service return, unchanged guest
retirement accounting, declined calls retaining real execution, and rejected
wrapping memory accesses. The existing 2,000 original-function comparisons
continue to cover arithmetic, aliases and calling convention.

Private evidence: main checkout `build/r3-direct-services-leg00-A-r1*` and
`build/r3-direct-services-leg01-B-r1*`; worktree `build/ctest.log`.

Disposition: parked, not integrated. Broader native-function coverage remains
untried; this result is specifically the matrix service dispatch experiment.
