# Generic evaluator repair and settled-dashboard workload

Tracker: `beads-xbox.1.15.10`; product windows: `beads-xbox.1.17`.
Branch: `perf/measurement-first-2026-09`.

## Independent texture stages

The former admission rule confused register-combiner count with texture-stage semantics.
It refused the one-combiner programs `0x0C01` and `0x0C61`, although their consumed T0 is
PROJECT2D and their remaining stages are independent NONE/CUBEMAP operations. The fix admits
non-cube-format PROJECT2D without a combiner-count restriction, checks all four texture
modes for unsupported side effects/dependencies, and preserves the earlier all-zero and
cubemap compatibility paths. It does not claim general cube-layout support.

The mode separation follows the [NVIDIA texture-shader specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_shader.txt):
texture-stage outputs feed the later combiner, while some operations can discard a fragment
or change depth independently of their color output. The pinned xemu reference
`482088490c2d5983ed5e1eafbd09906157c1eca0`, `pgraph/psh_regs.h` and `glsl/psh.c`, confirms
the five-bit mode layout and PROJECT2D's separate cube-format remap. This reference is
supporting implementation evidence, not an independent hardware oracle.

Tests force the existing bit-2/bit-5 reroute through a renderer that declines GPU work.
Both families reproduce the specialized CPU path's entire color/depth targets. Additional
cases cover several independent programs, inert border changes under clamp addressing,
an interior texture gradient with non-unit Q, and named shader-program refusal for an
unreferenced CLIPPLANE or newly admitted cube-format PROJECT2D. The read-only GPT-5.5 High
review was checked by the primary agent; no two-program exception was added.

## Validation and limits

* Release CTest: 29/29. The additional gradient and named-refusal cases also pass the
  focused NV2A executable.
* Default canonical replay: 28/28 PPMs byte-identical to the pre-fix candidate from stage g.
  Parsed input/device/determinism/DMA digests, generic admission and fingerprint census,
  profile and seam counters also agree. Only timing and worker scheduling fields differ.
* Reference configuration: 31/31 against `golden-noperf-defaults12-2026-09-05d.json`.
* Optional bit mask 36 (families 2 and 5), generated shader/depth enabled: final SHA unchanged;
  26/28 PPMs exact. At 4 B, 2,094 pixels differ by one channel level (0.6816%, largest connected
  difference 852 pixels); at 4.5 B, one isolated one-level pixel differs. The comparator
  flags the first image for review. Its diff was inspected: the same geometry remains,
  with low-level shading rounding across the boot sphere. This is not byte exactness.
* The global shader-mask-zero diagnostic also changes other generated families, so its
  few later one-pixel differences do not isolate the repaired families. Do not use that
  comparison to attribute errors or timing to bits 2/5.

Artifacts are under `build/generic-fix`: `default`, `bits36-gpu`, `bits36-cpu`, `reference`,
the `diff36-*` reports/PNGs, and `validation.json`. These diagnostic single runs establish
coverage and guide investigation; **they are not performance retention evidence**. No new
GPU reroute default is enabled. Remaining numerical families 3, 4 and 7 still need repair
and the combined path needs the normal same-session 3+3 gate.

## Product workload discovery

The canonical 12 B route reaches the stock dashboard, but its neutral input script has not
finished. The extended discovery run `product-old-16b` uses the preserved stage-g executable,
stock HDD, the existing clock-accept script, 250 M instruction checkpoints and interactive
presentation. The script completes at **14,309,533,328 instructions / endpoint2 report 855**.
The 16 B image visibly contains MEMORY, MUSIC, XBOX LIVE and SETTINGS, with MUSIC selected.

Use windows **14.5 B to 16 B**, whose ending deadlines are 14.75 B through 16 B. The window
ending at 14.5 B contains script completion and must not be included as wholly settled.
The discovery run indicates substantially less capacity in this busy animation than the
boot average; repeated noperf measurements remain required. `product-perf-16b` is the
separate instrumented attribution run for this interval.

`tests/scripts/stockdash-settings.txt` extends the proven clock acceptance with stock-HDD
navigation events and captured states. Its navigation portion is under calibration; older
Down/A evidence used a different HDD/XBMC image and cannot validate this route. Input still
traverses the guest OHCI/XID path, with the existing test-only synthetic hub topology.
That topology is a coverage limitation, not proof of real Controller-S wiring.

The first 19 B calibration (`settings-19b`) missed the second five-report Down hold:
the two selected-state captures both show XBOX LIVE. The later captures show SELECT ACCOUNT
and then a return to XBOX LIVE. It validates neither Settings entry nor the intended route.
The next calibration lengthens holds to 20 reports and neutral gaps to 125 reports.

### Settled-window attribution, not a retention result

The primary independently reproduced the GPT-5.5 High helper's interval aggregation for
`product-perf-16b`: six `scope=interval` records ending at 14.75 B through 16 B total
13.786 s, with 99.975% exclusive coverage. The cumulative final record is excluded.

| Exclusive region | Seconds | Wall share |
|---|---:|---:|
| Generic combiner body, excluding nested regions | 4.557 | 33.1% |
| GL | 3.044 | 22.1% |
| Vertex | 2.757 | 20.0% |
| Texture | 0.974 | 7.1% |
| Dispatch | 0.924 | 6.7% |
| TCG execution | 0.704 | 5.1% |
| GL resolve | 0.471 | 3.4% |

The first row's existing telemetry name, `nv2a_raster_generic_sw`, is misleading here.
Secondary counters show **zero software pixel-loop calls**. The generic fallback setup
also constructs generated-GPU packets: 861 calls cost 2.842 s. Resident submissions number
13,284, with 123 nonresident submissions. These secondary cumulative counters must be
differenced at the endpoints; their times overlap exclusive regions and must not be added
to the table. The driver error-check subregion costs 0.513 s, but this alone does not prove
that error queries are the cause rather than where queued work is charged.

The post-script ranking differs sharply from boot: TCG falls from 27.6% over 12 B to 5.1%,
and witnessed raster from 17.5% to 0.024%. Prior boot-based CPU ceilings therefore do not
bound steady dashboard capacity. Audit generic preparation, submission and vertex work
first for this window. These are single instrumented discovery measurements, not a claim
of sustained FPS or an accepted optimization.
