# Native/HLE optimization campaign

Tracking: central `beads-55wj`, related to `beads-2wy8.1`
(`campaign:optimization-campaign`). Branch: `perf/native-hle-groups-20261026`.

The owner requested a broader investigation, evaluation of every opportunity
found, and final JSRF/Blinx candidate builds for a visual/play check. Owner
acceptance authorizes the validated merge to main/master and new title releases
pinned to that integrated framework. Until that check, changes remain isolated.

## Acceptance and evidence

Each game can improve or remain neutral. A reproducible regression in one game
cannot be offset by another game's improvement. Small mixed differences within
measured variation are neutral/inconclusive. Identical original/candidate failures
are shared baseline defects, tracked separately. Preserve native saves, supported
features, guest timing, real completion, resource ownership and portable fallbacks.

Use matched built workloads, audio processing, graphics settings, source/binary
identities and normal owner scheduling. Count original game updates or source
swaps separately from interpolated presentations. Alternate A/B order, retain raw
per-game rows and package labels, and measure without diagnostic readback/logging.
Do not claim previous improvements again as campaign gains.

Raw evidence and the resource ledger are in
`G:/Temp/Codex/xboxllr/native-hle-20261026`. Local and NAS hosts are shared with
other console campaigns. Busy-host attribution is diagnostic only; check for a
benchmark window at 30-minute intervals rather than running competing stress jobs.

## Opportunity burndown

`Open` means a concrete evaluation remains. `Candidate` means implemented but
not accepted. `Previously evaluated` reuses existing evidence; it is not a new
performance claim. Every final rejection/deferral must retain its evidence and
reopening condition. Add opportunities when source inspection or profiles expose
them, rather than treating this table as an exhaustive list of hypothetical work.

| ID | Opportunity and boundary | Current evidence / next decision | Status |
| --- | --- | --- | --- |
| G01 | Reuse native draw validation/preparation within an ordered strip group | Blinx 2-1 emits about 20,000 draws/update; existing batching still validates raster/feedback/admission per draw. Reuse only while a prepared queue survives all mutation and observation flushes; geometry-dependent blend profiles retain per-draw qualification. | Candidate; GPU and game A/B pending |
| G02 | Build immutable shader stream jobs at shader creation | `pilot_gfx_shader_streams` rescans 16 declarations and resolves formats on every draw. Cache mask/extents for the owned immutable shader, including fixed/current-input declarations. | Candidate; GPU and game A/B pending |
| G03 | Group frontend preparation above individual guest draw calls | Vertex, texture, deferred-state and raster preparation still run per original draw. Determine remaining material cost after G01/G02 and identify a proved mesh/packet boundary before replacing original work. | Open |
| G04 | Dependency-aware callback overlap | Prior capture found about 5.95 ms/update waiting at global callback barriers. Timestamp-only prototype lacked dependency proof and a reliable gain; no callback may be omitted or completion fabricated. Measure current waits and prove actual read/write consumers before another experiment. | Open; prior prototype rejected |
| G05 | Resource versions, alias invalidation and vertex preparation | Existing texture write tracking already saved 16.3% CPU/update in a bounded 2-1 comparison. All-alias/native-writer tracking must remain; inspect remaining vertex comparisons/uploads and small/untracked texture cost. | Open; existing texture improvement is baseline |
| G06 | Deferred guest-state synchronization | Existing cache saved about 21% in its separate bounded experiment. Inline guest writes still require observation; assess remaining mapping/copy/comparison cost before changing granularity. | Open; existing reuse is baseline |
| G07 | Submission/index-buffer strategy | Existing backend strip merging is baseline. Append/ring index upload passed pixels but measured 39.73 vs 39.70 CPU ms/update and 23.25 vs 23.00 updates/s, establishing no benefit. Reopen only if a current upload hotspot remains after group work. | Previously evaluated; append experiment rejected |
| G08 | Raster preparation and redundant shader-stage binding | Reuse only the unchanged VS0 binding while an admitted prepared strip group retains ownership. Raster/fog bytes and current/constants validation still run; fixed/foreign bindings clear validity and actual mutations end the group. Presentation uses PS7 and restores its state. Startup diagnostic control retains the original rebind. | Candidate; combined GPU and game A/B pending |
| P01 | Interpolation/source capture/presentation ownership | Cooperative presentation already restored about 60 presentations/s in the bounded 2-2 entrance while original updates stayed about 30/s. Measure remaining capture/composition cost and preserve enabled owner features. | Open; cadence correction is baseline |
| P02 | Shader/resource creation and first-use stalls | Existing shader disk cache and resource preparation are baseline. Separate cold loading, shader compilation, actual driver calls and renderer-lock waits; use actual entry evidence rather than steady-state FPS. | Open |
| C01 | Native entry/checkpoint and guest-register bookkeeping | Prior address caches and huge-player-function optimizer did not establish useful whole-title gains. Entry hooks occur 50k-89k times/update; measure caller/barrier cost before reopening a correct inline fast path. Preserve suspension, diagnostics and callback materialization. | Open; previous address-cache/huge-body experiments rejected |
| C02 | Whole-routine math/game logic HLE and CPU lowering | Prior matrix HLE removed 30.9% of counted native guest instructions without establishing substantial wall-time gain. No drop-in replacement was found for measured player/geometry routines. Reopen only on a measured hot routine with a defined caller/state/exception contract. | Previously evaluated; broad rewrite not justified |
| C03 | Host-idle polls and scheduler wakeups | Main Blinx frame-poll host idle is baseline: measured wait CPU fell 99.7%, but total title results were mixed. Review movie/other polls only if a current CPU profile makes them material and each publisher/lock contract is known. | Open; prior wait-resource result is baseline |
| A01 | Audio mixing, ADPCM/resampling, spatial work and DSP advancement | Native XAudio2/real DSP path is already selected. Attribute audio thread CPU, sample/refill continuity and movie decoding before choosing a larger native job or proven component reuse. Keep audio emulation enabled during muted QA. | Open |
| M01 | VM/pool/contiguous allocation and resource loading | Existing task `beads-xbox.1.200` replaced full-VM scans with authoritative allocation ledgers and query fallbacks; JSRF loading improved in its retained before/after samples. Already landed in `866c434`, included in both current delivered native baselines. Preserve and qualify current loading; do not re-credit it. | Previously evaluated and landed; inherited baseline |
| I01 | File cache, media-lock serialization and asynchronous publication | Historical 25-second Blinx stall is not reproduced by later bounded 0.76-3.93-second entries. Sparse file waits have near-zero caller CPU; raw I/O/lock/refill correlation is needed. No speculative pacing or buffer change. | Open; historical defect remains separate |
| I02 | Synchronous informational logs inside allocation/I/O owners | Pool/contiguous creation and file-transfer paths still write text while handling real work; profile 004 contains 2,415 pool-create records including navigation/loading. Correlate their CPU/wait cost before replacing them with bounded buffered observation. Required diagnostics and session capture must remain. | Open |
| X01 | Renderer/dependency replacement or substantial donor reuse | Both games already use static native CPU and Phobos D3D11/HLE; a new interpreter removal is not a candidate. Compare reusable native jobs/components against measured remaining costs before introducing another renderer or CPU architecture. | Open; reuse decision follows current attribution |

Remote refresh found framework `a3afc799` and title heads JSRF `1c56e91` /
Blinx `f546a28`. Final qualification will use that frozen current base with the
retained candidate, rather than replacing those newer features with the older
local checkout. VM/pool accounting and resident-media services are already on
that framework base (`866c434`) and will not be credited as a new renderer gain.
The audio donor explicitly uses `JitEngine`; original effect code is already
compiled by its JIT, so A01 has no demonstrated interpreter-removal shortcut.

## Game qualification and handoff

| Game | Required workloads | New package result |
| --- | --- | --- |
| Blinx NTSC-U | Draw-heavy 2-1, representative gameplay/time control, loading and native save/fresh load; retain owner adaptive aspect and enhancements | Untested |
| JSRF original supported NTSC-U revision | Populated gameplay, area/loading transition and native save/fresh load | Untested |
| JSRF USA standalone | Matching representative gameplay plus revision-specific startup/save smoke | Untested |

Initial baseline profiles 001-003 were excluded: a short network-startup deadline
and wrong-stage navigation did not produce the intended workload. Profile 004
reached actual Blinx stage 4 / Round 2 Stage 1 through original inputs. It records
about 594,000 draws per 30 updates, but host contention and instrumentation prevent
using its elapsed times as an optimization result. The existing independent JSRF
process, owner profiles and native saves were left untouched.

The initial G01/G02 GPU strip fixture passes exact pixels, real visibility
samples, mutations and 64-draw capacity flushes, including 63 prepared reuses in
65 compatible draws. The float-blend fixture also passes. Broad backend and
fixed-declaration fixtures exceeded their wall-time deadlines while restricted
to one below-normal-priority CPU on a busy host; retain those failures and rerun
candidate/control in a suitable window before judging correctness or promotion.

At handoff retain passing builds and useful evidence, remove disposable scratch,
prefill the verified game files, and open normal windows with normal audio. Owner
acceptance clears the merge handoff once; do not request duplicate acceptance.
Release replacements are verified before removing older same-game ZIPs from
`G:/Nextcloud/Xbox`, leaving one current archive per game.
