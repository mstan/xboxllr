# Performance methodology and owner decisions

> Rules for how performance work on xboxlle is measured, gated, and accepted, plus the owner
> decisions that scope it. Created 2026-09-03 from the blind-spot audit
> (`docs/PERF-AUDIT-2026-09-02.md`, tracker `beads-xbox.1.14`). Where this file and older
> STATUS/PROGRESS entries disagree, this file wins; update it on any pivot.

## 1. Owner decisions (2026-09-03)

| # | Question | Decision |
|---|---|---|
| 1 | Must execution-tier changes preserve golden SHA `876b58c2…`? | **No.** The hash encodes where interrupts land under 4096-insn slicing, not correctness. Re-baselining is permitted under the rule in §3. |
| 2 | May sessions drive the real console via `xboxlle-probe` to capture framebuffers? | **Yes, but not now.** Stop and ask when hardware frames become a hard blocker and a prerequisite for the rest. Until then the software rasterizer remains the working oracle, with its unvalidated status stated wherever it gates a decision. |
| 3 | Keep PRINCIPLES §9 "no free-running host threads"? | **Amended.** Single-threaded emulation is untenable for the goal. Threading is permitted on the ndsrecomp model (§4): latch/execute jobs, fence on guest access, toggle-off runs the identical code inline, byte-lock gate at guest-anchored stops. CPU-vs-CPU shared-memory threading stays out of scope. |
| 4 | Measure-first A/B before every experiment? | **Run things immediately to assess.** Prototype-and-measure is acceptable; the protocol in §2 applies before anything is *retained* or *claimed*. |
| 6 | Retain stage (a) jump table and stage (b) loop specialization? | **Yes (2026-09-03).** (b) is exactness-neutral, wins 4–7 % on the raster-dominated 70 % of the run, and removes the serial constructs later stages need gone; (a) retained as a flat-cost simplification (ceiling 2.4 %). |
| 7 | Gate metric | **Total 12 B wall, median AND min, 5 %**; final-four reported alongside (2026-09-03). |
| 8 | Deterministic guest TSC re-baseline (2026-09-04) | **Accepted.** Guest `rdtsc` now derives from the retired-instruction clock; the golden moves to the new run. Standing rule: a change that makes the emulator MORE faithful or MORE deterministic is an automatic re-baseline under §3 — do not ask; record the evidence and proceed. Ask only when hashes move for an unexplained reason. |
| 9 | Acceptance standard (2026-09-05) | **Behavioral faithfulness, not byte-exactness.** Owner: stop fixating on bit-identical determinism; a change is acceptable when the guest sees the same behavior (same screens reached, same input/interrupt timeline within tolerance, no visible artifacts, no faults) and it is faster. Byte-locks and hashes remain the cheapest *diagnostic* for "did I change anything by accident" and should still be run when cheap, but a small explained pixel or hash drift is not a blocker, and no work is to be held hostage to reproducing a golden. Hardware truth outranks any golden. |
| 10 | Commit policy (2026-09-05) | Owner: commit as appropriate. Land work in logical commits (measurement infra, CPU tier, seam/threads, GPU executor, docs) once the active agent finishes; never commit a tree mid-edit or with failing guards/ctest. |
| 11 | CPU tier replacement (2026-09-05) | Owner: **if Unicorn must be replaced, replace it.** Once the GPU path reaches its floor, the next arc evaluates and builds the replacement: WHPX/Hyper-V hardware virtualization (near-native, MMIO via EPT exits, TSC offsetting for guest time) versus a custom dynamic x86 recompiler. Behavioral faithfulness is the standard, so host-virtualization's loss of instruction-exact determinism is acceptable if the guest's behavior is faithful. |
| 5 | Subagent parallelism | **Owner-set ceiling, currently two.** Exactly one may own the host CPU (timing runs, builds); any other must be read-only or defer builds. Subagents must not spawn children. Ceiling has moved 1→2→3→1→2 across 2026-09-03 sessions; check the latest owner instruction. |

## 2. Measurement protocol (before any retention or claim)

Derived from `F:\Projects\ndsrecomp\ndsrecomp\docs\host_optimization_strategy.md` ("The contract")
and `ISSUES.md`; adapted to xboxlle's replay harness.

1. **Correctness before timing.** A run must be fault-free with empty stderr and pass the
   per-family hash checks of §3 before its timing is looked at.
2. **Guest-anchored windows.** Compare legs at equal retired-instruction counts (the existing
   `--frame-series` deadlines). Host seconds are the *output*, never the input.
3. **Interleaved, repeated, same session.** Minimum 3 legs per arm, order A1 B1 A2 B2 A3 B3 (or
   B/A/B), on a quiet host (no compiler or other runner processes before/after each leg). Never a
   single run against a stored baseline of another date.
4. **Report min-of-N and median, both.** A large median-to-minimum spread is a contamination
   detector, not a result. Physically impossible outcomes (faster with fewer cores, a control
   that changes identically) indict the host or the harness, not the change.
5. **Gate metric is total 12 B replay wall** (sum of all frame-series windows), median AND min
   across legs, both required to clear the threshold. Final-four wall is reported alongside as the
   tail-phase indicator but does not decide. `effective_fps` is 60 × guest speed and may be
   *reported* but never gated on alone. Presented frames per second is a separate number and must
   be labelled as such. (Changed 2026-09-03 by owner decision: the final-four windows are
   generic-combiner/GL-tail dominated and hid a 4–7 % witnessed-raster win.)
6. **Headline numbers come from the noperf build.** Historically `--perf-accounting` also enabled
   detailed telemetry (~155 M extra counter increments and FNV folds in the raster loop); the two
   are decoupled as of 2026-09-03 and the flag's measured cost is ~5 % before / ~3 % after (n=1;
   the earlier 17 % figure did not reproduce). Instrumented runs give attribution, not claims.
   The replay script defaults to noperf; instrumented is opt-in.
7. **Retention gate:** ≥ 5 % quiet interleaved win for added complexity or ABI surface. Pure
   simplifications and neutral generalizations may be retained at flat cost with numbers recorded.
   Any override is labelled as an override with its reason.
8. **Rejections are findable.** Every rejection records numbers, artifact paths, binary-size delta,
   and a "do not retry without X" clause. Rejections below the MDE of the method that produced
   them are recorded as **undetermined**, not disproven.
9. **Instruments carry their own denominators and cost.** A sampler exports its sample count and
   coverage; a timer exports its read count; an always-on surface has a measured overhead (target
   < 1 %). A profile is archived with the exact binary SHA and symbol map it came from.
10. **No arm-then-capture.** Performance data comes from always-on rings and accumulators queried
    after the fact (PRINCIPLES §7). Pause/step is never a synchronization method.

### 2a. Tooling that implements the protocol

| Rule | Tool | How |
|---|---|---|
| 6 (noperf default) | `tools/run_stockdash_replay.ps1` | noperf is the default; `-PerfAccounting` opts in. `-NoPerfAccounting` is accepted as a no-op for older call sites. |
| 3 (interleaved, same session) | `tools/run_stockdash_replay.ps1 -Candidate <exe> -Repetitions N` | One invocation runs `A1 B1 A2 B2 …`. Each leg writes `<label>-leg<NN>-<arm>-r<rep>.report.json` plus `.argv.json` and `.quiescence.json`. Without `-Candidate` and without an explicit `-Repetitions`, the original single-run behavior and file naming are preserved. |
| 3 (quiet host) | same | Before and after every leg the harness lists processes matching `xlle\|g++\|cc1plus\|ld\|collect2\|cmake\|ninja\|python`, records them in `provenance.host_quiescence`, and flags the leg `contaminated`. `-RequireQuiescence` turns that into a rejection. |
| 4 (min and median) | `tools/summarize_xbox_report_set.py --arm-a … --arm-b …` | min / median / mean / stdev / CV for the **total 12 B wall** (the gate metric), the final-four wall (indicator) and every frame-series window, per arm; the median-to-minimum spread; paired per-round deltas — decided on the total wall, with the final-four delta printed in the same row — and the fraction of rounds favouring B; a contamination summary. `--min-improvement-pct` applies the same gate here; `--json` writes the machine-readable form (`two_arm_summary.v2`: `improvement` = total wall, `final_four_improvement` = indicator). |
| 5, 7 (gate metric, 5 %) | `tools/compare_xbox_reports.py --set-a … --set-b …` | Gates on **total 12 B wall** (the sum of every frame-series window `wall_ns`) **median AND min**, both required to improve by `--min-improvement-pct` (default 5). Final-four wall is still computed and printed as a tail-phase **indicator** and is never gated (`set_gate.v2` JSON: `gate` = total wall, `indicator` = final four). `--override` requires `--override-reason`, which is printed and written into the JSON alongside the failures it overrode. |
| 9 (identity) | `provenance` in every report | Exact argv, `chunk_insns`, `engine`, `exe_sha256`, `git_head`, `perf_accounting`, leg index/arm/repetition/session. |
| — | `tools/make_golden_record.py` | Builds a golden record (section 3 item 5) from an existing report plus its stdout log. |

Two structural corrections landed with this tooling:

* Counter samples are keyed on the frame-series **deadline**, never on the list
  index. xlle emits one counter block per checkpoint plus one at shutdown, and
  some families (`perf-resident`, `perf-texture-modulate`) skip the shutdown
  block while others are absent from a noperf log entirely. A family that is
  absent now reports `n/a`; a family that is present but unalignable raises.
* Differences between overlapping perf buckets are relabelled
  `INVALID_ESTIMATE_*` (and `!`-prefixed in `summarize_xbox_report.py`).
  `src/machine/perf_accounting.hpp:11-29` states the buckets are not a
  partition. `raster_witnessed_est_ns` is the one legitimate subtraction because
  `g_ns_raster_generic` is a documented strict subset of `g_ns_raster`.

Two per-profile hashes are **gated on `--perf-accounting`** and are therefore not
comparable across arms that differ in it: `generic_combiner`
(`nv2a.cpp:5948` sets `collect_diagnostics`, `nv2a.cpp:9730` gates the fold) and
`transform_position_fnv64` (`nv2a.cpp:1234` / `1458` / `1451`). In a noperf run
both stay at the FNV-1a offset basis `14650fb0739d0383`. The tools skip them with
the reason recorded rather than reporting a spurious mismatch — or a spurious pass.

## 3. Acceptance rule (rewritten 2026-09-05 by owner decision; supersedes the re-baseline rule)

A performance change is accepted when:

1. It is fault-free with empty stderr at the same instruction budget.
2. **Behavior is faithful:** the replay reaches the same dashboard states (checkpoint images equal
   or differ only in ways a human cannot see or that are explained by timing), the input timeline
   (XID report count at each checkpoint) and interrupt counters agree within a small tolerance,
   and nothing the guest does depends on host speed or host state.
3. It clears the timing gate (§2 rule 5, total 12 B wall median AND min, 5 %) or is retained as a
   simplification/enabler at flat cost.

Byte-locks (final SHA, 24 checkpoint PPMs, counters) stay in the toolbox as the cheapest
regression detector and are still run when cheap; an unexpected difference is a *question to
answer*, not an automatic rejection. Instrument defaults (profile hashes, telemetry) are chosen
for speed; their identity is recorded in the golden record but never blocks a change. Goldens
are re-pinned whenever a faithful change moves them; no owner sign-off needed.

### 3a. The current golden and the shipping defaults (2026-09-05, revised twice)

**`build/golden-noperf-defaults12-2026-09-05d.json`** -- name `golden-defaults12-v4`, exe SHA-256
`054f6585...`, git HEAD `93d006d` + the uncommitted tree, source run
`build/golden-ref-v6.report.json`. Re-pinned 2026-09-05 under `beads-xbox.1.15.10` **phase 4**,
which put `kPitchProjectedColor` on the GPU and made the witnessed resident cache select the
colour and zeta planes into slots INDEPENDENTLY. The record's SUBSTANCE is unchanged from
`...-05c.json` / `golden-defaults12-v3`: final frame SHA-256 `36e87a97...`, final scanout FNV-64
`b7e78a1ac17ac5e8`, all 24 frame-series checkpoint hashes and all 24 profile hashes are identical,
and the reference leg's 28 checkpoint PPMs are byte-identical to `build/golden-ref-v2-*.ppm`.
Only the identity fields moved (exe SHA, name, note, source paths). That is expected: the
reference configuration is `--nv2a-draw-threads 0 --nv2a-profile-hashes`, in which the executor
takes ZERO jobs (`gpu-executor ... jobs=0`, all 436 110 rejected `reason=hash_on`), so no phase-4
code runs in the leg the golden is taken from.

**Behavioural acceptance of the phase-4 default configuration (section 3, behavioural
faithfulness).** Measured against `build/golden-ref-v2-*.ppm`, reproducibly, on THREE independent
arm-B legs of two gate sets (`a1-proj-12b`, `p4-gate-leg05-B-r3`, `p4-gate2-leg05-B-r3`), which
agree pixel for pixel:

| compared | prior default vs phase-4 default (projected profile on the GPU) |
|---|---|
| final frame SHA-256 | identical (`36e87a97...`) on all 12 arm-B legs measured |
| final scanout FNV-64 | identical (`b7e78a1ac17ac5e8`) |
| 28 checkpoint PPMs | **24 byte-identical**; 4 differ -- 2.5 B 5 px (all 1 LSB), 3.0 B 1 px (1), 3.5 B 56 px (all 1), 4.0 B 15 px (14 at 1 + 1 at 2). Largest connected component **1 px** in every case; worst frame 0.018 % of pixels |
| XID endpoint2 / irq0 / retired | 461 / 16342 / 12,000,000,001 at `0x8001e3e3`, identical between arms |
| `det_digest` / `dev_digest` / `dev_events` / `dma_writes` | identical between arms (`dma_digest` differs, which is the write CONTENT of the differing pixels) |
| diff PNGs read by eye (`build/diff-p4/`) | 2.5 B and 3.5 B are isolated faint specks in otherwise black boot-animation frames; 4.0 B is isolated specks in a dark gradient with no structure; the final dashboard has MEMORY / MUSIC / XBOX LIVE / SETTINGS legible and ZERO tinted pixels |
| 24 profile hashes | identical between arms in all four gate sets (`0 differ`) |

The mechanism of the four differing checkpoints is measured, not assumed. Three legs at 4.5 B on
one binary: `--nv2a-gpu-profile-mask 3` (projected on the CPU) reproduces the shipping default's
**exact four single pixels**; mask 7 with `--no-nv2a-gpu-fp32-shader` gives 5 differing pixels
across the same eight checkpoints; mask 7 with FP32 gives 77. So the scatter is FP32 barycentric
interpolation in the projected fragment shader -- the same precision trade section 3b already
accepted for the other two profiles -- amplified by that profile's two dot products and its
fourth-power term. It is not a structural difference and no connected component exceeds one pixel.

**Superseded:** `build/golden-noperf-defaults12-2026-09-05c.json` — name `golden-defaults12-v3`, engine
`unicorn-2.1.1`, exe SHA-256 `db326068…`, git HEAD `93d006d` + the uncommitted tree,
`--machine-chunk-insns 4096`, noperf, opengl, HDD attached copy-on-write, idle fast-forward on.
Final frame SHA-256 `36e87a97…`, final scanout FNV-64 `b7e78a1ac17ac5e8`, 24 frame-series
checkpoints, 24 profile hashes, retired 12,000,000,001. Source run
`build/golden-ref-v5-leg00-A-r1.report.json` (exit 0, stderr 0 bytes). `…-05b.json`
(`golden-defaults12-v2`, exe `3770d6dc`) is an intermediate of the same session and carries
identical hashes; v3 exists only to name the shipping binary.

Re-pinned 2026-09-05 under `beads-xbox.1.15.10` phase 2 session 2, when
`--nv2a-gpu-profiles` and `--nv2a-gpu-fp32-shader` became defaults. **The framebuffer chain is
unchanged**: final SHA, scanout FNV and 23 of the 24 profile hashes are identical to
`golden-defaults12-v1`. The 24th, `inline_blit`, moved `2e0a68cd8b855ccd` → `950d033f4fddfdcd`,
and the cause is measured rather than assumed. Three binaries, reference configuration, 3 B,
identical `batches/tri/frags/writes = 234/234/71884800/71884800`:

| binary | `inline_blit` fold at 3 B |
|---|---|
| `12bfc477` (pre-phase-2, the one `golden-defaults12-v1` was taken from) | `fa7501da3ce94a0d` |
| `545eb5b9` (phase 2 session 1) | `6d6ce6811dfdc90d` |
| `3770d6dc` (this session) | `6d6ce6811dfdc90d` |

So the drift entered with **phase 2 session 1**, which did not re-pin the record, and this session
is fold-neutral. It cannot be a GPU-executor effect either way: the reference configuration
reports `gpu-executor … jobs=0` with all 436,110 jobs rejected `reason=hash_on`, so no GPU code
runs in the leg the golden is taken from.

**Behavioral acceptance of the new default (§3, behavioral faithfulness).** The default
configuration no longer reproduces the reference leg byte for byte, and under §3 it does not have
to. Measured against `build/golden-ref-v2-*.ppm`, reproducibly, on two different legs of two
different sessions:

| compared | reference (threads 0, hashes on) vs default (threads 12, hashes off, GPU executor ON) |
|---|---|
| final frame SHA-256 | identical (`36e87a97…`), on all 11 default-configuration legs measured |
| final scanout FNV-64 | identical (`b7e78a1ac17ac5e8`) |
| 28 checkpoint PPMs | **24 byte-identical**; 4 differ by exactly ONE pixel each — 2.5 B (1 LSB), 3.0 B (1), 3.5 B (4), 4.0 B (2); largest connected component 1 px in every case |
| XID endpoint2 / irq0 / retired | 461 / 16342 / 12,000,000,001 @ `0x8001e3e3`, identical |
| diff PNGs read by eye | 3.5 B and 4.0 B are single faint specks in dark frames; the final dashboard diff is entirely untinted |

**Superseded:** `build/golden-noperf-defaults12-2026-09-05.json` — name `golden-defaults12-v1`, engine
`unicorn-2.1.1`, exe SHA-256 `12bfc477…`, git HEAD `93d006d` + the uncommitted tree,
`--machine-chunk-insns 4096`, noperf, opengl, HDD attached copy-on-write, idle fast-forward on.
Final frame SHA-256 `36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`, final
scanout FNV-64 `b7e78a1ac17ac5e8`, 24 frame-series checkpoints, 24 profile hashes, retired
12,000,000,001. Source run `build/golden-ref-v2.report.json` (exit 0, stderr 0 bytes).

The framebuffer chain is UNCHANGED from `build/golden-noperf-hddcow-92e7397e.json`; only the
record's identity moved. It is re-pinned because the identity now has to name the new instrument
defaults, and because the binary that produced it is not the one that produced the old record.

**The golden is taken in the REFERENCE configuration, not the default one.** The reference leg
is `--nv2a-draw-threads 0 --nv2a-profile-hashes`: inline raster with the per-write FNV folds on,
so the record carries all 24 per-profile hashes. The DEFAULT configuration
(`--nv2a-draw-threads 12`, folds off) then has to reproduce that leg's *output*, and does:

| compared | reference (threads 0, hashes on) vs default (threads 12, hashes off) |
|---|---|
| final frame SHA-256 | identical (`36e87a97…`) |
| all 28 checkpoint PPMs, **by bytes** | identical, 0 differences |
| scanout hash chain, 24 deadlines | identical |
| retired / eip at the stop | 12,000,000,001 @ `0x8001e3e3`, identical |
| parsed scalar report fields | 345 compared, 2 differ, both wall-time derived (`flips_per_second`, `wall_ns_total`) |
| the 24 per-profile hash lines | 11 identical (unexercised profiles), 13 differ — that is the instrument being off, not the guest |

### 3b. Instrument defaults (2026-09-05)

| flag | default | why |
|---|---|---|
| `--nv2a-draw-threads` | **auto** = `min(12, hardware_concurrency - 2)`, and 0 below two logical processors | span-parallel raster is work division, not latency hiding; the knee sweep below chose 12. `--nv2a-draw-threads 0` remains the inline fallback and runs the identical executor with `parts == 1`. |
| `--nv2a-profile-hashes` | **off** (`--nv2a-profile-hashes` opts in) | the folds are a two-cycle loop-carried dependency on the hottest loop in the emulator, executed once per written word, and — decisively — `hash_on` forces every draw INLINE (design §4.3 option O3), so leaving them on would silently disable the default raster path. The byte-lock surface is the framebuffer SHA, the 28 checkpoint PPMs, the scanout chain and the counters; the hashes are a cheap proxy for those, not the thing itself. |
| `--perf-accounting` | off | unchanged (§2 rule 6). Measured cost at `--nv2a-draw-threads 8`, 12 B: 81.039 s instrumented vs 72.992 s noperf median in the same session, **+11.0 %**. |
| `--nv2a-gpu-profiles` | **on** as of 2026-09-05 (`--no-nv2a-gpu-profiles` is the software fallback) | the witnessed-profile GPU executor. Flipped on two passing 5+5 interleaved gates; on the shipping binary `db326068`, total 12 B wall median 71.591 → 63.974 s (**−10.64 %**), min 67.365 → 63.418 s (**−5.86 %**), 5 of 5 rounds. See §3d — the honest conservative figure is the **min**, because arm A's first three legs ran slow in that set. |
| `--nv2a-gpu-fp32-shader` | **on** with the executor (`--no-nv2a-gpu-fp32-shader` restores FP64) | FP64 in the witnessed fragment shaders is 1/64 rate on GeForce and was the dominant GPU cost: matched instrumented 12 B pair, `gpu_draw_ns` 10.746 → 2.364 s and `drain_ns` 3.715 → 0.889 s. The SSBO record is byte-identical between the variants (the FP32 body reads float copies of the same four reciprocals out of the `wv` slot), so this is a same-binary switch. Coverage and depth stay INT64 in both. |
| `--nv2a-gpu-flush-triangles` | **256** (0 = the unbounded batch) | bound on the pending witnessed batch. Chosen from a 2 B sweep, two rounds, same session: 32 → 15.01 s, 64 → 15.07, 128 → 14.98, **256 → 15.09**, 1024 → 15.72, 4096 → 16.24, unbounded → 17.82. 256 is the largest bound still on the floor. |
| `--nv2a-gpu-ssbo-ring` | **4** | SSBO names round-robined per issued batch. Needed only once the batch is submitted early: with one name, a refill lands on a buffer the in-flight draw is still reading. |
| `--nv2a-gpu-kick-flush` / `--nv2a-gpu-vblank-kick` | **on** | submit each issued batch to the driver, and issue a partial batch at the frame boundary. Both have `--no-` spellings so the overlap can be bisected without a rebuild. |

`tools/run_stockdash_replay.ps1`'s `-ExpectedFinalSha` default was re-pinned from the
pre-copy-on-write `876b58c2…` (unreachable since `beads-xbox.1.14.11`) to `36e87a97…`. It is
unchanged by the 2026-09-05 default flip: every leg of every arm still reproduces `36e87a97…`.

### 3d. GPU-executor decision A/B (2026-09-05, `beads-xbox.1.15.10` phase 2 session 2)

One binary per set, one session per set, interleaved A1 B1 A2 B2 …; arm A =
`--no-nv2a-gpu-profiles`, arm B = the shipping defaults. Gate = total 12 B wall, median AND min,
5 %. Four sets were taken and **they do not all agree**, which §2 rule 4 says to report rather
than to average away:

| set | n | arm A median / min | arm B median / min | median | min | rounds favouring B | verdict |
|---|---|---|---|---|---|---|---|
| `gate-p2s5` (explicit flags, exe `2f6d5793`) | 3+3 | 68.095 / 67.777 s | 63.935 / 63.814 s | **−6.11 %** | **−5.85 %** | 3/3 | PASS |
| `gate-final` (shipping defaults) | 3+3 | 67.486 / 66.221 s | 66.439 / 66.380 s | +1.55 % | −0.24 % | 2/3 | FAIL |
| `gate-final2` (shipping defaults, exe `3770d6dc`) | 5+5 | 67.626 / 67.216 s | 64.129 / 63.574 s | −5.17 % | −5.42 % | 5/5 | PASS |
| **`gate-ship` (shipping defaults, shipping exe `db326068`)** | **5+5** | **71.591 / 67.365 s** | **63.974 / 63.418 s** | **−10.64 %** | **−5.86 %** | **5/5** | **PASS** |

`gate-final` is the outlier and carries the contamination signature §2 rule 4 describes: its arm-A
cv is 2.87 % against 0.66 % and 1.10 % in the other two sets, its slowest A leg is 70.042 s, and
its arm-B *final-four* wall is 20.7–22.1 s where every other set puts arm B at 18.8–20.7 s — a
tail inflation that appears in none of the other thirteen B legs of the same configuration. The
executor census is byte-identical across all four sets (`jobs=276668 triangles=5628312
draw_calls=18080 surface_uploads=1336`), so the arms executed the same work in every set.
Pooled over all 16 paired rounds the deltas are −6.38, −6.11, −5.85, −1.64, −4.36, +0.33, −5.65,
−6.18, −4.00, −6.03, −5.17, −10.81, −10.91, −9.95, −6.62, −2.73 %: median −5.98 %, 15 of 16
favouring B.

**The decision is taken on `gate-ship`**, the only 5+5 set run on the binary that actually ships,
and it is read on the **min** (−5.86 %) rather than the median (−10.64 %): arm A's first three
legs in that set came in at 71.6–72.2 s and its last two at 67.4–67.9 s, a warm-up trend that
inflates the median improvement. `gate-final2`, on a binary differing only in a printf format
string and an eligibility predicate that declines nothing on this workload, independently gives
−5.17 % / −5.42 % with arm A at cv 0.66 %. Both clear the bar on both statistics; the pair of
them is the evidence, not either alone. All 16 arm-B legs across the four sets reproduce the
golden final SHA.

### 3f. Transfer-volume A/B (2026-09-05, `beads-xbox.1.15.10` phase 3) — **FAILED**

Same-binary A/B: arm A restores the pre-change behaviour with
`--no-nv2a-gpu-clear --no-nv2a-gpu-skip-empty-packets`, arm B is the new
defaults. Two 3+3 interleaved sets, one binary and one session each.

| set | exe | arm A median / min | arm B median / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|---|
| `p3-gate` | `072ead24` | 63.563 / 63.238 s | 62.220 / 62.091 s | −2.11 % | −1.81 % | 3/3 | FAIL |
| `p3-gate2` | `e20f3d09` | 64.212 / 63.036 s | 62.667 / 62.272 s | −2.41 % | −1.21 % | 3/3 | FAIL |

6 of 6 paired rounds favour B (−1.97, −2.88, −1.61, −3.25, −2.27, −0.58 %); arm B
cv 0.18 % and 0.41 %. The bar is 5 % on **median AND min**, so this does not
clear it. Behaviour is byte-identical to the golden: final SHA `36e87a97…`,
scanout FNV `b7e78a1ac17ac5e8`, all 24 profile hashes equal between arms, 24 of
28 checkpoints byte-identical and the other four the same single pixels
§3a already records. `build/golden-noperf-defaults12-2026-09-05c.json` is
reproduced exactly and is **not** re-pinned.

The win is real but localized: per-window it is −2.3 % to −9.0 % across the nine
windows from 0.5 to 4.5 G where the GPU executor runs, and neutral over the
20.4 s generic-combiner tail (`nv2a_raster_witnessed` is 1 ms there), which is
what dilutes the 12 B total. Transfer volume per 12 B run: device surface
snapshots 1336 / 6016.4 MB → 647 / 2113.0 MB, GL plane uploads 2857.0 →
2113.0 MB, resolves 3282 / 7996.5 MB → 2909 / 7250.5 MB.

**A census correction that outlives this result.** The phase-2 residency census
is a *simulation* and it was wrong about its largest bucket: it reported
`cause=clear_surface resolves=0` where the backend's own counters show
CLEAR_SURFACE causing 535 colour and 398 zeta plane re-uploads (1941.6 MB) and
373 resolves (746.0 MB) per 12 B run — because it modelled a `glClear` the device
never asked the backend for (`clear_resident_surface` was declared in phase 1
and had no caller). Separately, `[perf-renderer-resolve-context]` recorded **no
depth resolves at all**, so it was reporting on roughly half the readback.
Both are fixed; the transfer numbers now come from the side that performs the
transfer. Rule: a census that models a capability must be checked against the
code path that would perform it.

### 3g. Phase-4 A/B (2026-09-05, `beads-xbox.1.15.10` phase 4) -- **FAILED the 5 % bar**

One binary (`054f6585`), one session per set, interleaved A1 B1 A2 B2 A3 B3, 3+3, gate = total
12 B wall, median AND min, 5 %. Arm A restores the prior behaviour with runtime flags on the SAME
binary; arm B is the new defaults.

| set | arm A | arm B | A med / min | B med / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|---|---|
| `p4-gate` | `--no-nv2a-gpu-plane-split --nv2a-gpu-profile-mask 3` | defaults | 61.218 / 61.212 s | 58.979 / 58.513 s | **-3.66 %** | **-4.41 %** | 3/3 | FAIL |
| `p4-gate2` | same | defaults | 61.594 / 61.103 s | 59.250 / 59.198 s | **-3.81 %** | **-3.12 %** | 3/3 | FAIL |
| `p4-gateA` (lever A alone) | `--nv2a-gpu-profile-mask 3` | `--nv2a-gpu-profile-mask 7` | 61.028 / 60.981 s | 59.108 / 59.077 s | -3.15 % | -3.12 % | 3/3 | FAIL |
| `p4-gateC` (lever C alone) | `--no-nv2a-gpu-plane-split --nv2a-gpu-profile-mask 3` | `--nv2a-gpu-profile-mask 3` | 61.266 / 61.250 s | 61.037 / 61.030 s | -0.37 % | -0.36 % | 3/3 | FAIL |

The two combined sets AGREE (unlike section 3d's four sets), 6 of 6 combined rounds favour B, and
the isolated levers add up: 3.15 + 0.37 = 3.52 % against 3.66 / 3.81 % combined. The final-four
indicator is +0.8 to +1.4 % (neutral to slightly favourable) and is not gated. All 24 profile
hashes are identical between arms in every set. The bar is 5 % on median AND min, so neither the
combination nor either lever clears it.

**The retention question is therefore two separate ones, and only one is an owner call.**
* **Lever C (per-plane resident-slot selection)** is a DEFECT FIX, not a performance feature. The
  pre-phase-4 selector fell back to the zeta key on a colour miss and wrote the packet's colour
  over a still-valid DIFFERENT colour surface -- 342 of 647 colour re-uploads (686.0 MB) per
  canonical 12 B run -- and it did so WITHOUT resolving the plane it overwrote, which is silent
  loss of guest-visible output the moment that plane is dirty (`upload_over_dirty_color`, always
  on, is 0 today only by luck of ordering). It costs +0.37 %, removes 682.0 MB of transfer and 342
  evictions, and doubles the GPU clear's acceptance (`accept_color` 349 to 690, `missing_color`
  660 to 319). Retained under section 3 item 3 as a correctness fix at flat cost.
* **Lever A (`kPitchProjectedColor` on the GPU)** is added complexity -- one fragment program, one
  extra texture unit, one packet field triple -- for -3.15 %. That is an OWNER CALL under the 5 %
  rule. It is default-on with `--nv2a-gpu-profile-mask 3` restoring the previous set, so either
  decision is a one-flag change.

### 3h. Lever B (sample a rendered surface from the resident texture): MEASURED AND DECLINED

Not built. Its ceiling was measured instead, with a diagnostic flag that is default-off, has no
`--no-` spelling and is named `--nv2a-gpu-unsafe-skip-texture-resolve` because it is KNOWN
INCORRECT when on (it lets a witnessed texture snapshot read a stale image). ABAB, 12 B, one
binary, one session: off 59.300 / 59.289 s, on 57.995 / 58.749 s -- median -1.56 %, min -2.18 %.

That is an upper bound on the WHOLE seam, and the always-on census
(`[host] gpu-texture-source`) says the seam is 148 547 calls of which only **230** move bytes:
the other 148 317 are `resolve_renderer_color_overlap` queries that return nothing. Lever B as
specified attacks only the 230, worth 521.5 MB; at this run's own measured resolve rate
(`nv2a_gl_resolve` 5.633 s over 7.603 GB of `[perf-renderer-resolve-context]` bytes = 0.741 s/GB)
that is **0.39 s, 0.6 % of the 12 B wall** -- roughly a tenth of the bar, against a shader-side
resident sampler that would have to invert the destination surface's Morton or pitch layout to
address a sub-rectangle of it. DO NOT RETRY WITHOUT: a workload where
`[host] gpu-texture-source resolves=` is dominated by snapshots that actually hit a resident
surface, or a row-scoped resolve that makes each of the 230 cost rows instead of a whole plane.

**What the probe DID find, and it is the larger number:** the ~1.3 s it removes is mostly the
148 317 no-op residency queries, not the 230 resolves. Each one runs `draw_renderer_query_fence`,
the residency census hook and a backend query, per witnessed sampled-profile draw. Hoisting that
query out of the per-draw path (it can only matter when some surface is GPU-authoritative and its
range overlaps) is a next lever with a measured size of about 0.9 s and no exactness argument
attached.

### 3e. Draw-thread knee sweep (2026-09-05)

One binary, one session, quiet host, `--no-nv2a-profile-hashes`, gate = total 12 B wall, median
AND min, 5 %. Interleaved 3+3 for the two gated steps; the N = 2 and N = 4 rows are standalone
legs and are context, not a gate.

| N | total 12 B wall median | min | n | part 0 busy | helper busy (each) | barrier share | barrier µs/draw |
|---|---|---|---|---|---|---|---|
| 0 | 154.475 s | 152.035 s | 3 | — | — | — | — |
| 2 | 99.088 s | 99.088 s | 2 | 0.510 | 0.509 | 0.87 % | 1.99 |
| 4 | 77.599 s | 76.389 s | 4 | 0.344 | 0.344 | 2.17 % | 3.82 |
| 8 | 72.992 s | 72.601 s | 3 | 0.286 | 0.287 | 3.04 % | 5.09 |
| **12** | **67.519 s** | **67.131 s** | 3 | 0.217 | 0.219 | 3.72 % | 5.77 |
| 16 | 83.768 s | 75.434 s | 3 | 0.148 | 0.145 | **22.51 %** | **43.30** |

* **8 → 12: PASS** — median −6.28 %, min −6.14 %, 3 of 3 rounds favour 12.
* **12 → 16: FAIL, and it is a regression** — median +24.07 %, min +12.37 %, and arm 16's cv is
  7.27 % against 0.33 % at 12. The mechanism is in the census, not inferred: the barrier goes from
  3.7 % of elapsed to 22.5 % and from 5.8 µs to 43.3 µs per draw, because the emulation thread plus
  15 helpers occupy all 16 logical processors and leave nothing for the GL driver, the host poll
  and the OS.
* Chosen default **N = 12** by the standing rule (largest N that clears 5 % over the previous
  step, else the previous step).
* Default-on vs the inline fallback, same binary, 3+3 interleaved: 154.475 s → 71.336 s median,
  **−53.8 % (2.17×)**; min 152.035 s → 69.081 s, −54.6 %.

## 4. Threading model (approved)

Adopt the shape in `F:\Projects\ndsrecomp\ndsrecomp\docs\device_work_parallelization.md`:

- The emulation thread **latches** a fully self-contained job (all register state the job reads,
  owned copies of inputs, destination buffer) and enqueues it; workers **execute** and never touch
  emulator state.
- Any guest access to memory a pending job may read or write **drains the queue first**; the fence
  is one relaxed atomic load on the hot path, and the emulation thread executes pending jobs
  itself while draining (degrades to single-threaded cost, never to a wrong result).
- Guest-visible side effects (writes into unified RAM the guest can read, texture-generation bumps)
  are applied **on the emulation thread only, in order**.
- Toggle-off executes each job inline at the latch point: **the same code in the same order**, so
  the faithful path stays structurally identical, not merely forceable.
- Gate: byte-lock at guest-anchored stops (framebuffers, register files, event counters) threaded
  vs inline, plus the §3 hash checks. Ships default-off until re-measured on the target core
  count; ndsrecomp saw a regression at 2 cores.
- Out of scope: running guest CPU work on more than one host thread.

For xboxlle the natural seam is the pushbuffer: the CPU thread parses PFIFO words into a command
queue; a render thread consumes it. Coherence points are guest CPU reads of rendered VRAM (today
undetectable — see audit §5) and scanout.

## 5. Order of work (re-ranked 2026-09-03 after measurements)

Measured facts driving the order: hookless Unicorn runs real code at 3–10 G insn/s but the idle
loop caps at ~700 M even hookless (`beads-xbox.1.14.2`); the canonical run is 39.4 % idle
instructions, yet idle windows are *not* the slow windows — bounding the idle loop at
5.6–11.8 ns/insn and non-idle guest code at ≥ 5.6 ns/insn leaves **68–78 % of wall time not
attributable to instruction retirement** (`beads-xbox.1.14.4`). Device emulation reached through
guest MMIO — the NV2A path — is where the run goes. Hook removal and idle skip are each worth only
~1.1–1.35× on today's wall, but both are mandatory for 60 fps once the device path is fixed.

**Update 2026-09-04 — the 68–78 % inference is now a measurement, and it was right.** §3e's
exclusive accounting gives, at `--nv2a-draw-threads 0`: `tcg_exec` 36.7 % of wall before the CPU
tier, 17.9 % after the block hook, 10.0 % after the idle fast-forward; `nv2a_raster_witnessed`
46.8 % → 63.0 % → 69.2 %. The estimate of "idle fast-forward alone worth 1.10–1.23×" in
`beads-xbox.1.14.4` was made **before** the block tier and is superseded: the block tier had
already taken most of it, and what remained was 13.7 s of `tcg_exec` (1.09× at threads 0,
1.27× at threads 8 where the denominator is smaller).

1. **DONE** bare-Unicorn benchmark `beads-xbox.1.14.2`; idle census `beads-xbox.1.14.4` (instrument
   cost still to be measured by an interleaved A/A); harness protocol + hash gates
   `beads-xbox.1.14.1/.8` (tooling landed; live interleaved session pending).
2. Live interleaved A/A (null) and A/B (census instrument) session with the new harness — the
   first real variance number and the acceptance of `.14.1`.
3. Exclusive-time accounting `beads-xbox.1.14.3` — pins the TCG / NV2A split that is currently a
   68–78 % inference.
4. NV2A path — epic `beads-xbox.1.15` (status 2026-09-04: (a) retained, (b) retained, (c) NULL and
   reverted; (d) landed, neutral; (e) single in-order consumer exact but +0.4 % → REJECTED: producer bursts
   then blocks on semaphore/retarget anchors, worker idle 51 %; (e2) within-job span partitioning PASSED: 1.55× at 8 threads, byte-locked, default-off;
   next is the CPU tier (block hooks + re-baseline), then (f)), design `docs/DESIGN-NV2A-SEAM.md`: (a) dispatch jump
   table `.15.1`; (b) per-profile loop specialization, counter-ladder hoist, per-write hash gate
   `.15.2`; (c) early depth rejection (12 of 15 witnessed profiles test depth) `.15.3`; (d) the
   latch/execute seam at the draw-batch boundary in `submit_vertex_batch` with an inline executor,
   zero behavior change `.15.4`; (e) threaded consumer default-off with the byte-lock gate `.15.5`,
   blocked by the coherence measurement `.15.6`; (f) GPU-resident consumer using the xemu
   `psh.c`/`vsh-prog.c` generators `.15.7`; (g) swapchain present `.15.8`. Coherence: conservative
   drains at ten NV2A anchors first; host page protection later. Rejected on verified mechanism:
   Unicorn per-access mem hooks (any hook forces every load/store to the slow path,
   `tcg-target.inc.c:1765`) and `uc_mmio_map` page swapping.
5. **DONE 2026-09-04** — block-hook CPU tier (`beads-xbox.1.4.6`) and idle fast-forward
   (`beads-xbox.1.4.7`), re-baselined under §3. Together they took `tcg_exec` from 77.3 s to
   14.3 s (5.4×) and the whole 12 B noperf run at `--nv2a-draw-threads 0` from ~205 s to
   139.1–139.6 s. See §3e.
6. **NEW #1 (2026-09-04): `nv2a_raster_witnessed`.** §3e measures it at 98.6 s — 63–69 % of the
   run — and it did not move by more than 45 ms across either CPU-tier stage. It is now larger
   than every other bucket combined and is the only thing between here and 60 fps. Everything
   below it is a rounding error by comparison.
7. ~~**NEW #2 (2026-09-04, P0 `beads-xbox.1.14.11`): close the run-to-run non-determinism.**~~
   **CLOSED 2026-09-05.** The replay was opening its HDD image `"r+b"` and the retail boot
   rewrites the LBA-3 `BRFR` boot counter on every run, so no two runs had the same input.
   Copy-on-write attach is now the default (§3b). Byte-locks pass again: §3c (span raster) and
   §3d (idle fast-forward, now default-on).
8. Per-vblank ring, GL timer queries, profiler fix. `beads-xbox.1.14.5-7`.
9. Hardware framebuffer capture when it becomes a blocker. `beads-xbox.1.14.9` (ask first).

### 5a. Open owner decision (raised 2026-09-03)

`generic_combiner` and `transform_position_fnv64` hashes and the fingerprint census exist only
when `--perf-accounting` is on (`nv2a.cpp:1234/5948`). §3 item 2 as written therefore cannot be
satisfied by a noperf golden. **Recommendation (applied unless overruled):** goldens are noperf
runs; the 22 witnessed-profile / scanout hashes are the gate; the perf-gated families are
reported when present but declared out of scope for goldens, because they are instrumentation
artifacts rather than guest-visible output.

Update 2026-09-03 (exclusive accounting, `beads-xbox.1.14.3`): `--perf-accounting` no longer
enables detailed telemetry; those two hashes now require `--nv2a-detailed-telemetry`. The new
`[perf-excl]` line family is an exclusive partition (18 buckets, rdtsc with compensated overhead,
residual printed as `unaccounted_ns`); legacy `[perf]` lines are kept for tool compatibility but
`host_poll_devices` now spans a narrower region and is not comparable with earlier reports.
