# STATUS

> The single source of "what is true right now." No competing status lives elsewhere.
> Newest entry first. Dates are absolute.

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
