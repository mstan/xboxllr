# xemu pixel-shader integration checkpoint — 2026-10-03

`--nv2a-renderer xemu` executes a borrowed production pixel-shader generator on
live guest draws. The complete 27B dashboard/Settings/return route exercised
**1,035 donor draws and 806,426 triangles**. The option remains experimental:
the first timing pair was slower, and whole-menu numerical differences remain
unqualified for a shipping default. Reference opt-out remains `opengl` or
`software`. No stubs or donor machine/device shims were added.

Tracking: `beads-xbox.1.15.7`. Contract, admitted subset, fallback behavior and
source provenance: [RENDERER-XEMU-PSH](RENDERER-XEMU-PSH.md).

## Retained experiment

Private evidence is under `build/renderer-20261003/`:

- Executable: `xlle-xemu-o2-r2.exe`, SHA-256
  `8905a673e7e3d96f0cc5d1dc645c54cd37968be0631bcb64ec23a1a863be0635`.
- CPU preparation: the existing 286-region O2 memory-integer AOT artifact,
  input SHA-256 `e664910fa235669d314dfd9b44a618d58997e7bd4f3036cf34199f2902c0e276`.
- Runs: `xemu-r2` followed by `reference-r2`, same binary and inputs, differing
  only in renderer selection and output filenames. Instrumentation: 100M
  deterministic checkpoints, 1.5B frame-series windows, no perf-accounting or
  CPU block census. Retained JSON: `r2-comparison.json` and both `*-time.json`.
- Route: `tests/scripts/stockdash-settings-exact.txt`, native AOT enabled,
  idle fast-forward, provisioned locked HDD with COW, disconnected network,
  synthetic controller hub, 4096-instruction machine chunks, 768 MiB process
  cap, 27,000,000,000 guest instructions.

Both runs complete all 14 input events, endpoint2=3018, aggregate=3019. The
enhanced backend attempts 8,426 generated batches: 1,035 use donor shaders and
7,391 use reference shaders because their texture contract is outside the
admitted subset. This is **12.28% of generated batches**, not of all GPU draws.
There are no generated-shader compilation failures.

An earlier `xemu-r1` run completed but executed zero donor draws because an
overly broad guard rejected inactive texture-routing selectors. It is retained
as diagnostic evidence and does not count toward integration or speed results.

## Observable behavior

The reference run reproduces all 270 prior checkpoint rows and all ten prior
route captures byte-for-byte. Comparing enhanced with reference:

- All 270 rows match instruction deadlines, slice counts, CPU digest, device
  digest/event count, EIP, and DMA-write count. The DMA **data** digest differs
  in 210 rows, beginning at 6.1B, as rendered bytes enter guest memory. These
  differences are recorded rather than hidden by changing the hash.
- Native execution, fallback and idle counts match exactly: 1,964,141,698;
  7,043,192,926; 17,992,665,376. IRQ0=36796, injected=46341,
  pending_boundaries=28481, guest TSC reads=14342, device events=18059827,
  DMA writes=2476411. This is renderer work; no CPU AOT progress is claimed.
- All 18 frame-series windows match instruction endpoint, USB report count
  and guest-vblank count. The ten route captures include the clock, main menu,
  selected items, Settings interior and return. Visual inspection of retained
  main-menu and final captures shows the expected objects and selected items.
  This is bounded source/reference evidence, not hardware capture evidence.

| Capture | Differing pixels (of 307,200) | Largest channel delta | Mean absolute channel delta over differing pixels |
| --- | ---: | ---: | ---: |
| Clock before/after A | 0 | 0 | 0 |
| Dashboard settled | 125,054 | 25 | 0.6463 |
| Before Down | 214,570 | 19 | 1.1805 |
| Xbox Live selected | 229,788 | 30 | 1.7987 |
| Settings selected | 205,400 | 29 | 1.0924 |
| Settings open / before Back | 0 | 0 | 0 |
| Dashboard returned | 231,734 | 28 | 1.2256 |
| Final | 248,013 | 28 | 1.8962 |

Driver interpolation/filtering/blending can change numerical output, and
repeated blending can accumulate differences. The table measures the combined
change; it does not establish a single cause or an accepted whole-scene
tolerance. The option stays experimental. Constant/texture identity and
discard/fallback unit cases remain exact; arithmetic fixtures have explicit
one-channel-unit margins.

## Timing

| Measurement | Reference | Experimental xemu |
| --- | ---: | ---: |
| Whole 27B route wall time | 245.645 s | 311.280 s |
| Last 25.5B–27B window wall time | 17.778 s | 18.989 s |
| Guest vblanks per host second in that window | 6.919 | 6.478 |

The candidate is 26.7% slower overall in this **single sequential pair**. Host
timing already differs before the first donor draw, so this does not isolate
backend cost or qualify a regression magnitude. No ABBA repetition set, clean
process-observer qualification, GPU-time attribution, distinct-present-rate or
input-latency result is claimed. The evidence provides no basis to promote this
backend as a performance win or claim 60 FPS. Subsequent performance work needs
isolated repeated measurements and coverage/resolve attribution.

## Validation and next work

The full suite passes **77/77**, including real OpenGL tests for donor constants,
perspective interpolation, nearest/bilinear/rectangle texture sampling, X8
blending, alpha discard, unsupported-state refusal and real reference fallback.
The pure import is reproducible from pinned Git objects. Original licenses and
author notices are retained.

The Sol/high audit and root review caught texture-border/layout and finite/Q
geometry gaps, all guarded before effects. Root inspection also identified
missing reference semantics for texture alpha-kill and divergent shared
constants; those cases remain excluded instead of using HLE to cover an absent
floor contract.

This meets the **live borrowed-component checkpoint**. Performance promotion,
wider renderer coverage and full LLE qualification remain open. Under the
owner's delivery order, the next independent milestone is the real Blinx
media/loader path and first title execution or precise failure, using partial
AOT; it does not wait for full shader coverage or dynarec removal.
