# Multi-command render packets: parked

Tracking: `beads-xbox.1.58`. Branch: `spike/command-packets-20261004`.
Base `4bcad36`, with parked worker `177c068` imported as `1af9de2`.

NoRain's `d3d_presenter_thread.cpp` publishes sealed multi-command packets,
instead of handing off each draw separately. This implementation transfers
owned commands in packets of up to 128 commands / 8 MiB, with two published
packets including the running packet. Readback, clear and explicit GPU kick
publish the preceding packet; readback and clear wait for actual ordered work.
The final partial packet drains on destruction. Oversized individual draws
remain accepted with backpressure. No required draw failure becomes success.

The worker publishes 20,225 packets for 592,686 commands, including all 580,540
dashboard draws. It records 9,208 fences, 1,175 backpressure waits, 17.887 s of
worker execution and 12.702 s of caller fence wait. Publication amortization
works; it does not eliminate actual resource/completion dependencies.

| Measurement | Synchronous retained HLE | Packet worker |
| --- | ---: | ---: |
| Full route interval sum | 100.2931 s | 103.1339 s |
| Last four intervals | 18.1971 s | 19.1871 s |
| Final 123-vblank interval | 4.77275 s | 5.03284 s |

Both legs completed all 14 inputs with empty stderr and all 28 captures
byte-identical. All 80 checks pass, including the worker's 1,000 ordered draws,
caller-buffer mutation, actual GPU readback and sticky-error tests. Production
selection is `XLLE_RENDERER=HLE`, `XLLE_HLE_WORKER=ON`; no CLI flag or live switch.

Private evidence: `build/r3-packets-on-leg00-A-r1*` and
`build/r3-packets-on-leg01-B-r1*` in the main checkout; this worktree's
`build/ctest.log`. Earlier `r3-command-packets*` launch attempts incorrectly used
a nonexistent CLI option and are excluded. The table uses only completed runs
with the worker enabled by CMake.

Foreign compiler/runner processes invalidate quiet-host timing. The screen
shows no substantial gain and a 2.8% route / 5.4% final-interval regression;
those percentages are observations, not causal estimates. This branch is
parked and not integrated. Guest vblank throughput is not distinct displayed FPS.
