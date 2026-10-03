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
wider renderer coverage and full LLE qualification remain open. The owner's
subsequent priority correction puts **sustained original-speed 60 FPS in the
dashboard menu before Blinx**. These measurements do not close that gate.
Partial AOT and compatible enhanced paths remain allowed; full shader coverage
and dynarec removal are separate later goals. See the
[active menu gate](GOAL-AOT-DASHBOARD.md#active-menu-performance-gate).

## Menu60 follow-up: decoded textures and unused stages

The next implementation checkpoint expands the experimental adapter. It does
**not** demonstrate a speedup or close `beads-xbox.1.17`.

- Admit the reference device's owned, decoded DXT1/DXT2-3 image for its bounded
  single-stage PROJECT2D path. Do not reinterpret raw compressed guest memory.
- Reduce TENT mip minification to bilinear only for exactly one declared level.
  Public GPU tests force minification with nearest magnification to distinguish
  the two paths. Multi-level, unspecified-level and BOX mip filters stay excluded.
- Remove unread independent PROJECT2D stages only when they have no alpha-kill,
  color-key or cube effects. Require descriptors for consumed texture inputs.
- Guard initial R0 alpha reads, including MUX: xemu and the current reference
  initialize that value differently. Qualifying the floor is `beads-xbox.1.42`.
  The adapter requires an overwrite before a read; no substitute value is added.

The same final executable (`xlle-dxt-r6.exe`, SHA-256
`091b93b347cfae7a860494077373d2a66bda85ca2f64da775dcd33edae559126`)
completes the 27B route with both `opengl` and `xemu`. The 286-region private
native input, O2 native compilation, Release O3 host, locked COW HDD and input
route are unchanged. Full public validation passes **77/77**; the real donor
GPU test executes, rather than skipping for an unavailable context.

| Generated-shader execution | Previous enhanced | Final candidate |
| --- | ---: | ---: |
| Attempts | 8,426 | 8,426 |
| Donor draws | 1,035 | 5,349 |
| Donor triangles | 806,426 | 1,357,402 |
| Reference texture declines | 7,391 | 3,077 |

Donor draw coverage increases from 12.28% to **63.48% of generated batches**.
This denominator excludes other GPU/device paths. The first remaining texture
decline is raw format `0x08810629` with a linear packet interpretation; its
layout contract remains excluded pending qualification.

All **270 reference checkpoint rows and ten reference route captures** reproduce
the prior reference exactly. Enhanced execution matches all checkpoint columns
except the DMA data hash (210 rows differ). Native/fallback/idle instruction
counts and all printed native execution counters match between the two arms.
The script completes at 24,488,199,989 instructions; both arms reach endpoint2
report 3018 at the final capture. No CPU coverage increase is claimed.

| Final candidate versus reference | Differing pixels | Largest channel delta | Mean channel delta over differing pixels |
| --- | ---: | ---: | ---: |
| Clock before A | 258,270 | 29 | 1.7008 |
| Clock after A | 2 | 1 | 0.3333 |
| Dashboard settled | 130,263 | 25 | 0.6361 |
| Before Down | 219,567 | 19 | 1.1626 |
| Xbox Live selected | 234,156 | 30 | 1.7732 |
| Settings selected | 210,060 | 29 | 1.0772 |
| Settings open | 208,376 | 18 | 1.1882 |
| Before Back | 203,203 | 16 | 1.0952 |
| Dashboard returned | 235,770 | 28 | 1.2121 |
| Final | 252,039 | 28 | 1.8730 |

The six main-menu captures add at most one channel unit relative to the prior
enhanced path. The newly admitted unused-texture draws also move Clock and
Settings-interior rendering to donor shaders, introducing larger differences
there. Inspected Clock, menu/selection, Settings, return and final images retain
their expected objects and selection. These observations do not establish a
whole-scene tolerance or hardware equivalence. The path stays experimental.

Both final observers are healthy (2,170/2,124 samples; maximum gaps
137.507/138.437 ms), but detect competing processes and mark timing unusable.
The final steady window observes **7.554 reference / 6.866 enhanced guest
vblanks per host second**, not qualified displayed FPS or a speedup estimate.
Neither a quiet repeated set nor a sustained 60-second original-speed menu
window has passed. Input-script correctness is separate from interactive latency.

An earlier instrumented diagnostic of the preceding adapter identifies several
costs in the last 25.5B-to-27B window. It also has host contamination, so use the
distribution to choose further experiments, not to predict their gains:

| Exclusive host work | Seconds | Share of diagnostic wall |
| --- | ---: | ---: |
| Native generated body | 3.472 | 18.56% |
| OpenGL submission/work | 2.944 | 15.74% |
| Vertex processing | 2.870 | 15.34% |
| Generic raster preparation/work | 2.410 | 12.88% |
| Slice overhead | 1.277 | 6.83% |
| Native boundary/admission | 1.242 | 6.64% |
| GPU command dispatch | 1.056 | 5.64% |
| Texture setup | 1.001 | 5.35% |
| Dynamic CPU execution | 0.960 | 5.13% |
| OpenGL resolve | 0.485 | 2.59% |

The generic bucket includes CPU preparation for GPU draws; it is not a count of
software-rendered pixels. Native memory/MMX/x87 service costs, vertex work and
host submission are larger next candidates than simply removing remaining shader
declines. Whole-route GPU timestamp totals also show material uploads; overlapping
GPU work cannot be added to this exclusive host table.

The [all-donor review](RENDERER-REUSE-2026-10-02.md) remains the source map for
those experiments: content-checked decode caching from sp00nznet, compiler fixes
from NoRain211, phobos's RHI/state tracking, and DOAXBV's compressed uploads and
presentation worker. The current packet contains decoded owned data; borrowing a
compressed-upload path requires qualifying a real compressed packet contract.
This checkpoint adds no other donor runtime or native/HLE service.

Private artifacts: `build/menu60-20261003/` contains immutable executables,
test logs, image comparisons and `r6-validation.json`; the final arm logs,
argv, process observations, checkpoints and PPM captures are
`build/menu60-dxt-r6-{opengl,xemu}*`. Earlier `r1`/`r2`/`r4`/`r5` artifacts are
diagnostic iterations, not independent confirmation of a performance win.
