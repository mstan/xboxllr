# Stage g: interactive presentation

Tracker: `beads-xbox.1.15.8`. Branch: `perf/measurement-first-2026-09`.

The interactive presenter now uploads the guest scanout in its native pixel format,
paces on guest vblanks, and reports successful SDL present calls independently of guest
speed and guest flip writes. `--interactive-present-legacy` restores the old 33 ms
throttle and scalar RGBA capture on the same executable for comparison.

## Contract and scope

`Nv2a::scanout_direct_bytes` shares geometry validation, the scanout seam fence, and the
resident-color resolve with `capture_scanout`. It returns a borrowed RAM view, consumed by
SDL before guest execution resumes. It removes the scanout staging allocation, RAM copy,
and scalar conversion. It does **not** remove the SDL upload or GPU-to-RAM resolve when a
dirty resident surface overlaps scanout. Noncontiguous RAM falls back to scalar capture.
Canonical `--frame-series` and `--frame-dump` still use the existing RGBA capture.

Little-endian XRGB8888, RGB565, and XRGB1555 map directly to SDL formats. The presenter
disables texture blending so the guest alpha/X bits cannot make scanout transparent.
SDL 3.2's 16-bit expansion differs from the scalar capture by at most one channel level;
the test exhausts every 16-bit value in both formats. Frame-series goldens keep their
original conversion and are unaffected by this host-only rounding.

`frame_due` offers at most one presentation per guest vblank and services keyboard events
on every existing guest-millisecond host poll. Its display-derived pacing guard avoids
uploading multiple frames inside 0.75 display periods; it never sleeps. The legacy arm
uses the original wall-clock throttle. Scripted input may own the gamepad while the
interactive window pumps events and presents, allowing the same deterministic input route
in both arms. Host timestamps never feed the guest clock or scripted input timeline.

The old design's claim that there was no real swapchain was incorrect. On this machine
SDL selects `direct3d11`: the vendored SDL 3.2 source's `D3D11_RenderPresent` calls
`IDXGISwapChain1_Present1`, and `D3D11_SetVSync(0)` selects `syncInterval=0` and
`DXGI_PRESENT_DO_NOT_WAIT`. The startup log records renderer name, the success of disabling
VSync, and read-back VSync state. No shared NV2A GL presentation context was added.
Eliminating the remaining GPU/RAM/SDL transfer is separate work, to be justified by its
measured cost. Other SDL drivers need their own timing before claiming nonblocking swaps.

## What the measurements mean

`[host] present scope=interval|cumulative` exports its wall-time denominator, present and
capture counts, guest vblank and flip-write counts, input-poll count, and clock-read count.
The reporter keeps these rates separate:

* `presented_fps`: successful `SDL_RenderPresent` calls per interactive host second.
  This includes repeated images. SDL/DXGI can discard a submission; it is **not** a count
  of physical display scans or unique completed guest frames.
* `guest_speed`: guest instruction-clock seconds divided by interactive wall seconds.
* `guest_flips_per_s`: NV2A flip write increments divided by that same wall interval.
* `capture_to_present_us_avg`: time from scanout capture start to SDL present return.
  This is **not** input-to-visible or photon latency. Maxima explicitly carry `_max_total`
  because they are cumulative even on an interval line.

The replay parser adds `host_present` and `host_present_samples`; headless logs report
host presentation as unknown. Its older `presented_frames.flips_per_second` JSON key is
retained for compatibility but describes **guest flip writes**; all printed summaries now
use that name. The old headless figure of roughly 12 flips/s was never host presented FPS.

## Final measurements (2026-09-06 UTC)

Each comparison collected three clean A/B pairs in one invocation, noperf, using the
final candidate below. Neither set needed a replacement pair. `final-headless` compares
the preserved pre-stage-g binary with the candidate; `final-interactive` compares the
candidate's legacy throttle/scalar arm with its default native/paced arm. Earlier `probe`,
`interactive`, `quiet`, `headless`, `headless-clean`, and `interactive-clean` artifacts are
diagnostics or superseded builds, not these headline results. Sets with foreign compiler
processes were excluded; a new bounded whole-pair retry mode records census-only selection.

| Noperf metric | A min / median | B min / median | Improvement min / median |
|---|---:|---:|---:|
| Headless total 12 B wall, s | 50.9754 / 51.0887 | 51.1606 / 51.2394 | -0.36% / -0.30% |
| Headless final-four wall, s | 14.1209 / 14.1832 | 14.2037 / 14.2291 | -0.59% / -0.32% |
| Interactive total 12 B wall, s | 51.9063 / 51.9966 | 51.3791 / 51.4428 | +1.02% / +1.07% |
| Interactive final-four wall, s | 14.4188 / 14.4991 | 14.2245 / 14.2820 | +1.35% / +1.50% |

Total-wall CV is 0.24% / 0.27% for headless A/B and 0.23% / 0.12% for interactive A/B.
The headless shift is small but is a measured slowdown, not proof of zero regression.
**Both formal 5% total-wall gates REJECT.**

| Interactive host metric | Legacy min / median | Native min / median |
|---|---:|---:|
| Successful SDL presents/s | 16.50 / 16.50 | 18.61 / 18.76 |
| Capture time per capture, us | 452.4 / 453.2 | 7.6 / 8.8 |
| Capture start to present return, us | 1328.6 / 1345.4 | 735.9 / 769.3 |
| SDL upload per successful present, us | 177.6 / 181.6 | 95.0 / 95.9 |
| SDL present call per successful present, us | 696.3 / 707.2 | 631.3 / 662.2 |
| Guest speed (interactive wall denominator) | 0.313 / 0.314 | 0.311 / 0.314 |

These are min/median **across three run averages**, not per-frame percentiles. No SDL
present failures occurred. Native capture removes about 98% of that stage's median cost;
capture-to-present return falls about 43%, while SDL upload/present remains. Median guest
speed does not improve at the reported precision. Interactive wall covers the full
`mach.run` interval; the retention gate sums frame-series windows, a different denominator.
The median per-run capture-to-present maximum is 3.976 ms / 3.860 ms (A/B); neither this
maximum nor the average measures input-to-visible latency or a p99 tail.

**Retention exception, not a 5% win:** retain this stage as the requested presentation and
measurement capability: it removes scalar staging and fixed 33 ms pacing, exposes honest
host metrics, and has a runtime legacy arm. The small headless cost is recorded above.
The additional API and instrumentation make this an explicit capability exception to the
usual complexity gate, not a claim that every part is a pure simplification. Revisit shared
GL presentation only after a product-window measurement attributes enough cost to the
remaining transfer. The current roughly 19 SDL calls/s and 31% guest speed do **not** meet
the owner's faithful 60 FPS target, with or without headroom.

## Validation and the DMA-history difference

The Release build passes CTest **29/29**, including native capture bounds, a fake resident
surface resolve, single-entry exclusive-accounting assertions, and the SDL software/dummy
presentation test. `tests/test_present_report.py` passes 2/2, and
`tests/test_replay_pair_retry.ps1` covers whole-pair exclusion, retry order, audit records,
attempt exhaustion, correctness-failure propagation, and ordinary interleaving.

The final reference run passes **31/31** checks against
`goldens/golden-noperf-defaults12-2026-09-05d.json`; no re-pin is needed. Every selected A/B
pair has **28/28 byte-identical PPM checkpoints**, as does the headless/interactive candidate
cross-check. All twelve timed legs have matching frame hashes, input reports, and the
`det_digest`, `dev_digest`, `dev_events`, `dma_writes`, and `irq0` checkpoint timelines.
Final frame SHA is `36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`.

**DMA history is not identical across presentation modes.** All six headless legs agree
(`dma_digest=f5c98df2a12072bb`). Native interactive agrees across its three legs
(`9a00622ea56e2653`), while wall-paced legacy interactive varies. The first changed checkpoint
is 9.5 B. The existing `--det-dma-dump` ring established the cause with two diagnostic runs
(`build/present-g/trace-{headless,interactive}.txt`, threshold 9.5 B; not timing evidence).
Among 262,144 entries, exactly one write moved: NV2A address `0x03978000`, size 1,228,800,
payload hash `455739f516d50325`, occurs at 9,325,800,297 instructions with native capture
instead of 9,427,651,634 headless. The other 262,143 entries match including timestamps;
the 1,675 intervening writes do not overlap that framebuffer. The DMA payload multiset
is identical. `dma-trace-comparison.json` records this comparison.

Scanout can force a resident-color resolve, which writes through `dev_dma_write` and
therefore changes an order-sensitive diagnostic history. This is earlier RAM visibility,
not a purely observational read. The trace explains the first divergence; it is not a
proof for every workload or CPU framebuffer-read pattern. Existing scanout coherence
tests and all guest-visible regression checks pass. Keep this distinction when defining
future deterministic product-window metrics; do not weaken the headless golden checks.

Complete local analysis is `build/present-g/validation-final.json`; each timing set also
retains `*-pair-selection.json`, `*-abba-summary.json`, and `*-abba-gate.json`. Native
one-second interval rates reached 57.52-63.48 SDL calls/s across the three runs, confirming
that the fixed 30 Hz ceiling is gone; these short intervals do not establish sustained
60 distinct displayed frames/s.

## Reproduction

From the repository in PowerShell, after the Release build and tests have completed:

```powershell
./tools/run_stockdash_replay.ps1 -Label present-g/final-interactive `
  -Candidate build-release/xlle.exe -Repetitions 3 `
  -RetryContaminatedPairs -MaxPairAttempts 8 `
  -ExtraXlleArgs '--interactive' `
  -ExtraXlleArgsA '--interactive-present-legacy' `
  -RequireQuiescence `
  -QuiescencePattern '^(xlle|g\+\+|cc1plus|ld|collect2|cmake|ninja)([-._0-9]|$)'
```

The output directory `build/present-g` must exist. Idle Python service residents are
excluded by the explicit pattern; no other tests, compilers, or emulator runs execute
during a leg. Reports retain the exact filter and process census. This does not prove
absence of unrelated desktop/GPU load.

The normal 5% total-wall gate is retained and reported even though this stage targets
presentation overhead. A lower capture cost does not imply a 5% whole-emulator speedup.
Do not use the initial one-pair `probe` run as retention evidence; it was diagnostic only.

The separate headless comparison uses `-Label present-g/final-headless`,
`-BaselineExe build-release/xlle-pre-present.exe`, and the same candidate, repetition and
quiescence options, omitting both interactive argument options. The reference correctness
run uses `-Label present-g/final-reference`,
`-GoldenRecord goldens/golden-noperf-defaults12-2026-09-05d.json`, and
`-ExtraXlleArgs @('--nv2a-draw-threads','0','--nv2a-profile-hashes')`.

The preserved pre-stage-g executable is 3,609,941 bytes, SHA-256
`a48a1e831209fcd2ba0074e05a56156c86324d85b19b4da73ac3c2d099765e13`.
The tested candidate is 3,621,039 bytes (+11,098), SHA-256
`3353f557b8df635bdb8eaca623a277dd2bbcafe82c82ad500677327a9c2380a8`.
These are content hashes, independent of the report's build-identity field.
