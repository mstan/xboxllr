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

**AOT benchmark clarification (owner, 2026-09-07):** faithful portable static/native
execution is a required deliverable, optimized as far as we can. Outperforming
Unicorn is not its acceptance criterion. Native optimization claims compare a
candidate against the preceding native implementation at equivalent guest work;
the interleaved minimum/median rules below still apply. Unicorn is a correctness
reference and fallback, with relative speed available as a diagnostic. Keep
native capability acceptance distinct from a particular speedup claim and from
the broader 60 FPS/headroom product target.

Derived from `F:\Projects\ndsrecomp\ndsrecomp\docs\host_optimization_strategy.md` ("The contract")
and `ISSUES.md`; adapted to xboxlle's replay harness.

1. **Correctness before timing.** A run must be fault-free with empty stderr and pass the
   per-family hash checks of §3 before its timing is looked at.
2. **Guest-anchored windows.** Compare legs at equal retired-instruction counts (the existing
   `--frame-series` deadlines). Host seconds are the *output*, never the input.
3. **Interleaved, repeated, same session.** Minimum 3 legs per arm, order A1 B1 A2 B2 A3 B3 (or
   B/A/B), on a quiet host (no compiler or other runner processes before/after each leg). Never a
   single run against a stored baseline of another date.
   `-RetryContaminatedPairs` may collect replacement **whole pairs within that same harness
   invocation**. Every attempted leg is retained and `*-pair-selection.json` records exclusions
   based solely on before/after process censuses, never timing or correctness outcomes.
   Correctness failures still stop the run. A bounded attempt count stops without a gate if
   three clean pairs cannot be collected; it never relaxes the quiet-host requirement.
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

| `--nv2a-vertex-program-cache` | **on** as of 2026-09-05 phase 5 (`--no-…` restores the per-batch parse) | `nv2a_vsh_parse_program` mallocs ~21 kB and disassembles all 136 slots once per vertex batch; 289 617 of 309 159 mode-2 batches (93.7 %) re-derive a byte-identical result. Exact because the parse is a pure function of the program words and the start slot and both are invalidated at their only writers. Measured 202 → 18 ms. See §3i. |
| `--nv2a-vertex-fast-fetch` | **on** as of 2026-09-05 phase 5 (`--no-…` restores the sixteen-slot loop and the zero-fill) | two pure removals in the per-vertex loop: iterate only attribute slots with a non-zero count (`fetch_vertex_attribute` returns immediately for the others), and drop the 256-byte zero-fill of an input register file that all sixteen slots overwrite before the interpreter runs. Measured 2.682 → 1.300 s of attribute fetch. |
| `--nv2a-vsh-compile` | **on** as of 2026-09-05 `beads-xbox.1.15.11` (`--no-…` runs the vendored `nv2a_vsh_emu_execute`) | the pre-decoded vertex-shader step form: 32-byte records off the parse cache, plus a read-before-write analysis that deletes most of the 416-byte per-vertex register zero-fill. Bit-exact by construction (it calls the vendored implementations) and by evidence (7 822 822 of 7 822 822 verified vertices, 0 mismatches). `vsh_ns` 4.823 → 4.237 s; gate **+2.11 % median / +1.49 % min, FAILS the 5 % bar** and is retained under §3 item 3. See §3k. |
| `--nv2a-gpu-generic-shader` | **on** as of 2026-09-05 phase 6 (`--no-…` restores the software fallback) | the GENERATED register-combiner executor. Four 3+3 interleaved sets on three binaries, all passing: on the shipping binary `3a612b6d`, total 12 B wall median 60.802 → 57.308 s (**+5.75 %**), min 60.460 → 56.825 s (**+6.01 %**), 3 of 3 rounds; 12 of 12 rounds across the four sets. Removes all 704 transferring `software-generic-color-surface` resolves and 1 339 software batches from plane `0x03AA4000`. See §3j. |
| `--nv2a-gpu-generic-shader-image-blend` | **on** (`--no-…` restores GL fixed-function blending) | the blend, the colour mask and the X8 alpha rule inside the generated program, against an image binding under a fragment-shader interlock. NOT an optimisation: GL blending quantises its inputs to 8 bits and the software path does not, measured at exactly 1 LSB over ~90 % of the plane per batch and 38 LSB end to end. §3j(4). |
| `--nv2a-gpu-generic-shader-fp64` | **on** (`--no-…` is single precision) | the generated programs' perspective interpolation in double, matching the software path. Measured to be worth ZERO pixels on this workload (§3j(3)); on by default because it is what the software does, not because it fixed anything. |
| `--nv2a-gpu-generic-shader-wide N` | **4096** guest pixels (0 = every triangle) | a triangle with a vertex further than N pixels from the origin is drawn as its own scanline bounding box, so the fixed-function rasterizer can never under-cover it. 0 is the exactness control and was used to disprove coverage as a cause. |
| `--nv2a-gpu-generic-shader-mask N` | all ones | bit *k* enables the *k*-th distinct generated program in first-attempt order. The bisection tool that isolated the wrong program to one of six. |
| `--nv2a-gpu-generic-shader-depth` | **off** as of 2026-09-05 phase 7 (`--nv2a-gpu-generic-shader-depth` opts in) | the generated executor attaches the resident zeta plane and the ROP performs the guest's depth comparison against `gl_FragDepth`; `uses-depth` declines go 512 to 0. EXACT (10 of 10 checkpoints byte-identical to 5.0 G) and a MEASURED REGRESSION on its own: plane `0x03628000` still has 3 181 witnessed CPU draws, so the 527 batches this moves become GPU producers the next CPU consumer breaks again -- 3 G window, colour resolves 566 to 761 and zeta 180 to 375. A prerequisite for the row below, not a win. See section 3l(1). |
| `--nv2a-gpu-generic-shader-witnessed N` | **0** (all eight bits available) | bit *k* sends the *k*-th of the eight witnessed families still CPU-rasterized on plane `0x03628000` down the strict generic fallback so the generated executor can take it. With all eight the plane census reaches `cpu_draws=0` on both planes and one leg is 54.542 to 52.374 s -- but five of the eight move the image (bit 2 alone: 286 151 px at max delta 255), and the cause is in the GENERIC EVALUATOR, not the executor. BISECTION only. See section 3l(2). |
| `--nv2a-gpu-generic-shader-bgra-upload` | **on** as of 2026-09-05 phase 7 (`--no-...` restores the per-pixel shuffle) | the generated executor uploads guest A8R8G8B8 words as `GL_BGRA` -- no staging buffer at all for a texture, a row copy for a colour surface -- deleting a per-pixel byte shuffle over 2.3 GB per run. Exactness-neutral by construction and by evidence (28 of 28 checkpoints byte-identical between the arms); gate **+1.51 % median / +1.49 % min, FAILS the 5 % bar** and is retained under section 3 item 3. See section 3l(3). |
| `--nv2a-gpu-generic-shader-verify N` | **0**, no `--no-` spelling | DIAGNOSTIC, very slow, changes residency: runs BOTH paths for the first N submitted batches from the same starting image and reports the per-pixel difference. This is the instrument that found the blend, after three wrong theories. |
| `--nv2a-gpu-generic-shader-nodraw` | **off**, no `--no-` spelling | DIAGNOSTIC, KNOWN INCORRECT when on: everything but the draw call, so the frame is missing exactly this executor's output. The control that sized what the executor contributes. |
| `--nv2a-vertex-census` | **off**, no `--no-` spelling | attribution only: two clock reads per VERTEX (91 M pairs per 12 B run). It is what produced the split in §3i and must never ride in a timing leg. |

Since 2026-09-05 phase 5 `perf_excl::init()` is called **unconditionally** — a fixed ~4 ms of
calibration per process, 0.007 % of a replay leg. `perf_excl::g_enabled` is still gated on
`--perf-accounting`, so no `Scope` becomes active; the calibration is there because the always-on
`[host] gpu-resolve-cost` and `[host] nv2a-vertex-census` lines report nanoseconds, and without it
they would print raw TSC ticks labelled `ns` in every noperf run.

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

### 3i. Phase 5 (2026-09-05, `beads-xbox.1.15.10`): the vertex pipeline, and two numbers this file had wrong

Phase 5 was asked for three things in the order the phase-4 evidence supported. Measuring them
first **inverted that order**, and the inversion is the main result.

**(1) The "148 317 no-op residency queries, about 0.9 s" of §3h is WRONG by two orders of
magnitude. Measured directly: 16 ms.** §3h derived it as (a ceiling probe worth −1.3 %) minus (a
byte-rate model of the 230 queries that transfer). Both halves of that subtraction were replaced
by a direct always-on timer around every entry to `resolve_renderer_color_overlap` /
`_depth_overlap`, credited to `noop` or `moved` by whether the call actually wrote guest memory
(`[host] gpu-resolve-cost`, 12 B noperf, exe `65c4aad0`, both arms of the phase-5 A/B agreeing to
0.5 %):

| queries | n | total | per call |
|---|---:|---:|---:|
| colour, moved nothing | 161 015 | **10.0 ms** | 62 ns |
| zeta, moved nothing | 151 064 | **6.1 ms** | 40 ns |
| — of which the witnessed texture snapshot (§3h's 148 317) | 148 317 | **8.2 ms** | 55 ns |
| **colour, MOVED bytes** | **1 954** | **5.70 s** | **2.92 ms** |
| **zeta, MOVED bytes** | **304** | **0.673 s** | **2.21 ms** |
| — of which the witnessed texture snapshot (§3h's 230) | 230 | 0.841 s | 3.66 ms |

So the whole no-op query population is **0.028 % of the run** and a per-draw fast path for it is
**declined on measurement, not built**. The `n` cross-checks against an independent instrument:
`[perf-gl-gpu] gpu_resolve_n=2258` = 1 954 + 304 exactly. What §3h's ceiling probe actually
removed was the 230 **transferring** snapshots at 3.66 ms each — 0.84 s — which is the same
number by a different route, and the resolve rate that produced the 0.39 s estimate (0.741 s/GB)
is wrong because a resolve's cost is a pipeline stall, not bytes: the colour half splits
**4.80 s backend / 0.90 s guest writeback of 6 109 MB**.

**A residency resolve costs 2.2–2.9 ms and there are 2 258 of them: 6.37 s, 11 % of the 12 B
wall. That is the largest single identified item in the run.**

**(2) The per-PLANE consumer census (`[host] gpu-residency-plane`, always on) says exactly which
families gate it, and it is not the family the fragment count points at.** Phase 4's rule — a
break is attributed to the FIRST CPU consumer, so only removing the LAST one removes the
resolves — makes the per-profile break table unusable as a work order. The plane table is a
direct count at the GPU/CPU decision point. On the canonical 12 B run only **two** planes are
mixed:

| plane | bytes | GPU draws | CPU draws | CPU families that must ALL move |
|---|---:|---:|---:|---|
| colour `0x03628000` | 2 457 600 | 131 632 | 3 181 | pitch_three_texture 2304, pitch_single_texture 121, pitch_multi_texture 120, pitch_tex5_projected 126, pitch_tex5_diffuse 42, tex7_fan 304, texf_projected_fan 51, pitch_additive 113 |
| zeta `0x033d0000` | 2 457 600 | 131 632 | 3 110 | the same eight, with pitch_additive 42 |

Every other plane is pure-GPU (`0x02c05000`, `0x02e05000`, zeta `0x02a05000` — already free of
round trips) or pure-CPU (the six 256 kB swizzled planes, `0x032a4000`, and the two z16 zeta
planes) and therefore never resident and never resolved. **`kPitchZ16Depth` is 142 842 CPU draws
and 476 M fragments on two planes with ZERO GPU draws: moving it to the GPU removes no resolve at
all**, confirming phase 2 (b) from the other side. The prize is `witnessed-color-surface` 417 +
`zeta-witnessed-surface` 304 resolves ≈ **1.9 s**, plus those eight families' 2.49 s of
emulation-thread raster — about **4.4 s (7.5 %)** — and it is **all or nothing across eight
shaders**, which is the generator route, not eight hand-written programs. NOT BUILT in phase 5.

**(3) The vertex split, and the answer to "does the parse cache fix it": no.** `nv2a_vertex` is
8.536 s of a 64.214 s instrumented run. `--nv2a-vertex-census` (default off, two clock reads per
vertex, never a shipping path) splits it, on the canonical 12 B run:

| part | n | before | after | per item after |
|---|---:|---:|---:|---:|
| `nv2a_vsh_parse_program` | 309 159 → 19 542 calls | 202 ms | **18 ms** | 905 ns/parse |
| attribute fetch | 52 539 501 vertices | 2.682 s | **1.300 s** | 25 ns |
| **VSH interpretation** | 38 337 230 vertices | 4.520 s | **4.542 s** | **118 ns** |
| batch transform-cache hits (no work) | 75 499 247 of 128 040 827 vertices (59.0 %) | — | — | — |
| fixed-function transform | 14 204 350 vertices | — | — | — |

**The interpreter dominates (4.54 s of the 7.38 s bucket, 62 %) and the cache does not touch it**
— the cache is worth 184 ms. Per the phase-5 task's own condition, that is the statement with
numbers: the next vertex lever is the interpreter (a compiled step form, or the transform on the
GPU), not the parse. `vsh_context_vertices = 0`, so the 3.7 kB full-execution-state path is never
taken on this workload and is not the target.

**The phase-5 A/B.** One binary, one session, 3+3 interleaved A1 B1 A2 B2 A3 B3. Arm A restores
the previous behaviour on the SAME binary with
`--no-nv2a-vertex-program-cache --no-nv2a-vertex-fast-fetch`; arm B is the new defaults.

| set | arm A median / min | arm B median / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|
| `p5-vgate` (exe `65c4aad0`) | 59.143 / 59.129 s | 57.488 / 56.977 s | **−2.80 %** | **−3.64 %** | 3/3 | **FAIL the 5 % bar** |
| **`p5-vgate2` (shipping exe `8df76ed3`)** | **59.134 / 58.936 s** | **57.585 / 57.511 s** | **−2.62 %** | **−2.42 %** | **3/3** | **FAIL the 5 % bar** |

Two independent 3+3 sets on two binaries that differ only by a rebuild, and unlike §3d's four sets
they AGREE: median −2.6 to −2.8 %, min −2.4 to −3.6 %, 6 of 6 paired rounds favouring B
(−3.66, −2.70, −3.27, −2.42, −2.94, −2.42 %). Arm cv 0.17–0.54 % in both sets. Final-four
indicator −2.33 % median / −1.82 % min on the shipping set (not gated). The decision is taken on
`p5-vgate2`, the set run on the binary that ships. The matched instrumented pair puts the win
exactly where the census says it is and nowhere else — `nv2a_vertex` **8.536 → 7.383 s
(−1.153 s)** against a wall of 64.214 → 62.496 s, with `tcg_exec` −0.068, `nv2a_raster_witnessed`
+0.061, `nv2a_gl_resolve` +0.004 and `span_jobs` identical at 149 106 in both arms.

**Behaviour: exact.** All 28 checkpoint PPMs are **byte-identical between arm A and arm B**
(0 differing pixels). Against `build/golden-ref-v2-*.ppm` arm B reproduces the shipping default's
signature from §3a unchanged — 24 byte-identical and the same four checkpoints at 5 / 1 / 56 / 15
pixels, largest connected component 1 px, worst 0.018 % — i.e. **zero new pixels**. Both results
reproduce on both binaries. Final frame SHA `36e87a97…`, scanout FNV `b7e78a1ac17ac5e8`, irq0
16342, XID endpoint2 461, retired 12 000 000 001, exit 0, empty stderr on every leg of both arms;
all 24 profile hashes identical between arms (`differ: []`). The determinism digests are
identical between the arms too, INCLUDING `dma_digest` — `det_digest ed86cf6a1fe11c4f`,
`dev_digest bbadac889020716f`, `dev_events 17588661`, `dma_digest 9ce3cac041285a44`,
`dma_writes 1237977`, `tb_entries 2 302 057 484`, `tb_insns 7 301 344 897` — which is a stronger
byte-lock than phase 4 achieved (there `dma_digest` moved with the differing pixels). Diff PNGs
read by eye (`build/diff-p5/`, reproduced in `build/diff-p5b/`): 3.5 B is isolated 1-LSB
specks on a black boot frame with no shape, 4.0 B is isolated specks on a dark gradient with no
structure, and the final dashboard diff is **entirely untinted** with MEMORY / MUSIC / XBOX LIVE /
SETTINGS legible. `goldens/golden-noperf-defaults12-2026-09-05d.json` is reproduced and is
**NOT re-pinned**.

**Retention.** The gate is failed, so this is a §3 item 3 question rather than a gate pass.
`--nv2a-vertex-fast-fetch` is a **pure simplification** — it removes a loop over twelve attribute
slots whose fetch is `if (!attr.count) return true;` and a 256-byte zero-fill of a register file
that is fully overwritten before it is read; it cannot change output, and it is 3/3 rounds and
0.97 s of the 1.15 s. `--nv2a-vertex-program-cache` is **added state** (one cached parse, two
invalidation hooks) for 0.18 s and is the owner call; `--no-nv2a-vertex-program-cache` restores
the previous behaviour on the same binary, so either decision is a one-flag change.

### 3j. Phase 6 (2026-09-05, `beads-xbox.1.15.10`): the generated register-combiner → GLSL executor

Phase 5 named the generator as the ordered next lever and sized it from the resolve census.
Phase 6 built it, and the first thing it did was **census the generic combiner by the words a
generated shader is a function of**, which moved the target.

**(1) The new always-on census, and the plane it found.** `[host] generic-shader` /
`-words` / `-dest` / `-total` key on exactly the state the generated SOURCE depends on --
CombinerControl, the per-stage ICW/OCW quadruples for the stages that run, the final combiner
words, the texture modes/formats/address modes, the alpha/blend/depth/colour-mask subset, the
colour format and the destination layout -- and on nothing else. The per-stage constants, the
final factors, the texture dimensions and the alpha reference are UNIFORMS and are deliberately
excluded, because folding them would split one program across many rows. Cost: one FNV over ~50
words plus a linear scan of at most 128 rows, once per generic BATCH (19 702 per canonical run).
Each row also carries the DESTINATION PLANE of its batches and the transferring resolves those
batches caused, taken from the same `seam_resolve_transfers_` counter `[host] gpu-resolve-cost`'s
`moved` column comes from, so the two instruments cannot disagree.

Canonical 12 B run, `build/p6-census3`: **29 rows, 0 overflow. Colour resolves 704 (matches
`software-generic-color-surface:n704` exactly); texture-read resolves 602 (matches
`generic-texture-read:n602` exactly).** The per-plane roll-up is the result:

| plane | generic batches | software | fixture-GL | colour resolves | tex-read resolves | software triangles | software scanline box |
|---|---:|---:|---:|---:|---:|---:|---:|
| **`0x03AA4000`** (1280x960 X8) | 18 663 | **1 339** | 17 324 | **704** | 208 | 158 538 | 506.5 Mpx |
| `0x03628000` (1280x480) | 819 | 527 | 292 | 0 | 292 | 489 463 | 24.8 Mpx |
| `0x03978000` (scanout) | 208 | 0 | 208 | 0 | 102 | 0 | 0 |
| `0x02453000` | 11 | 11 | 0 | 0 | 0 | 930 | 10.5 Mpx |

**Every one of the 704 is on ONE plane the phase-5 plane table never mentioned**, from EIGHT
shader states: three declined `combiner-words` (424 + 198 + 92 batches, 536 resolves), one
declined `not-observed` because CombinerControl is 3 so the resident predicate never looked at it
(106 batches, 71 resolves), and four declined `vertex-expansion` (519 batches, 97 resolves) whose
combiner the existing fixture already implements and which it refuses only because their geometry
crosses the w = 0 plane. That plane needs no witnessed work at all.

**Two corrections to phase 5 that outlive this phase.** (a) The 602 `generic-texture-read`
resolves are NOT generic software fallbacks: they are ALREADY-GL-ACCELERATED batches whose texture
source overlaps a GPU-resident plane, so the generator does not touch them and phase 5's "the
generator serves both" is wrong for that half. (b) Plane `0x03628000` has 527 generic SOFTWARE
batches on it that the phase-5 plane table cannot see, because that table counts witnessed draws
only. Moving the eight witnessed families off it therefore does NOT free it -- those 527 would
inherit the 417 + 304 breaks by the same first-consumer rule phase 4 recorded. The witnessed item
is eight families **plus four generic shader states**, not eight.

**(2) The generator.** `src/host/nv2a_combiner_glsl.{hpp,cpp}`. Input is a POD state struct; output
is a fragment program and a fingerprint over exactly the fields the source reads. The SEMANTICS are
harvested from `refs/xemu/hw/xbox/nv2a/pgraph/glsl/psh.c` -- the input mappings, the AB/CD/MUX/SUM
structure, the output scale/bias, the final `D + A*B + (1-A)*C` with its EF product and V1R0_SUM,
the per-stage constant sourcing -- but none of its code is used: xemu emits GLSL against its own
driver-rasterized pipeline, and this emits against the phase-1 executor's contract. The
EXPRESSIONS are the software rasterizer's own fragment body transcribed statement for statement,
with psh.c as the cross-check that the software body decodes the hardware correctly.

Phase-1 exactness rules carried forward: coverage recomputed in the fragment program from the same
int64 edge functions and the same top-left rule at the same 1/16-pixel sample points; a triangle
the fixed-function rasterizer might under-cover drawn as the software path's own scanline box
(bound is a runtime value, `--nv2a-gpu-generic-shader-wide N`, 0 = always); the perspective
reciprocals carried from the device; explicit bilinear against `texelFetch` in the software
sampler's two-stage order; swizzled and A8 textures unswizzled and expanded by the DEVICE with an
FNV-64 so an unchanged re-upload is skipped; depth range-tested in the shader even when the zeta
surface is unusable, because the software path tests it there too.

**(3) Three theories the measurements disproved, in order.** Each cost a run and each is recorded
so it is not re-derived.

* **FP32 barycentric interpolation is NOT the error.** A whole-screen difference (max 38 LSB over
  79 % of the final frame) survived switching the generated programs' interpolation to DOUBLE
  unchanged: 253 072 differing pixels vs 253 071. `--no-nv2a-gpu-generic-shader-fp64` keeps the
  single-precision arm on the same binary; the double form is the default only because it is what
  the software path does and it costs nothing measurable here.
* **Fixed-function-rasterizer coverage is NOT the error.** Forcing EVERY triangle onto the
  scanline-bounding-box path (`--nv2a-gpu-generic-shader-wide 0`), which makes the candidate set a
  guaranteed superset, reproduced the same 244 069 differing pixels.
* **The resident-surface aliasing hazard is NOT the error**, though the guard for it was missing
  and has been added: the witnessed executor's overlap guard now exists on the generated path too,
  and adding it changed the image by zero pixels.

**(4) What the error WAS, measured by a differential verifier rather than reasoned about.**
`--nv2a-gpu-generic-shader-verify N` (diagnostic, default off, very slow, changes residency) runs
BOTH paths for the first N submitted batches from the SAME starting image and the same collected
geometry, and reports the per-pixel difference. Twenty consecutive batches of the primary-colour
program, every one of them:

```
differing=290053 maxdelta=1 ... (4,0 sw=ff041700 gpu=ff041600)
differing=272386 maxdelta=1 ... (0,0 sw=ff031500 gpu=ff041500)
```

**Exactly 1 LSB, on about 90 % of the plane, in both directions, per batch.** That is not a tie
rule and not interpolation: it is GL fixed-function blending quantising the source colour and the
blend factor to the framebuffer's 8 bits before evaluating `Cs*Sf + Cd*Df`, where the software path
evaluates it on the unquantised floats and only rounds the RESULT. One LSB per layer is invisible;
the dashboard composites about five hundred blended layers onto `0x03AA4000`, and the same
measurement end to end was 38 LSB over 79 % of the frame.

**The fix is programmable blending.** The blend, the per-channel colour mask and the X8R8G8B8
alpha rule all moved INTO the generated program, against an `rgba8` image binding under
`GL_ARB_fragment_shader_interlock` with `layout(pixel_interlock_ordered)`, using the software
path's own expressions and its own `floor(x*255+0.5)` pack; the ROP is told to write nothing
(blending off, every channel masked) so there is exactly one writer, and a `glMemoryBarrier`
orders the stores against the next draw, the resolve's readback and any texture fetch of the
plane. `imageLoad` on an `rgba8` image returns exactly `k/255`, which is what `unpack_argb8`
produces from the guest word, so the two sides agree on the destination bit for bit. With that,
the same verifier reports **0 to 2 differing pixels per batch, all at 1 LSB**.
`--no-nv2a-gpu-generic-shader-image-blend` restores GL blending on the same binary, which is the
arm the difference was measured against.

**(5) What moved, on the canonical 12 B run (arm A = `--no-nv2a-gpu-generic-shader`, arm B =
defaults, same binary `3a612b6d`).**

| | arm A | arm B |
|---|---:|---:|
| generic software-fallback batches on `0x03AA4000` | 1 339 | **0** |
| transferring COLOUR resolves (`gpu-resolve-cost`) | 1 954 | **1 356** |
| their cost | 5.861 s | **3.603 s** |
| transferring ZETA resolves | 304 | 304 |
| `seam_census.resolve_transfers` | 2 258 | **1 660** |
| resident-surface uploads / target-switch resolves | 395 / 395 | **294 / 294** |
| generated executor | off | 792 submits, 548 empty, 151 355 triangles, **6 programs**, 0 compile failures, 2 126 cache hits |
| declines | -- | `uses-depth` 512, `swizzled-destination` 11 (nothing else) |

Matched instrumented pair (`--perf-accounting`, one binary, one session,
`build/p6-perf-leg00-A-r1` / `-leg01-B-r1`), wall 63.731 → 60.612 s:

| bucket | A | B | delta |
|---|---:|---:|---:|
| `nv2a_gl_resolve` | 5.387 | 3.717 | **-1.670** |
| `nv2a_raster_generic_sw` | 7.508 | 6.144 | **-1.364** |
| `nv2a_gl` | 5.448 | 5.761 | +0.313 |
| `tcg_exec` | 14.939 | 14.687 | -0.252 |
| `nv2a_raster_witnessed` | 9.189 | 9.120 | -0.069 |
| `nv2a_vertex` | 8.166 | 8.224 | +0.058 |

The win is exactly where the census said it would be and nowhere else.

**(6) The decision A/B. FOUR 3+3 interleaved sets on three binaries, and they agree.** One binary
and one session per set, A1 B1 A2 B2 A3 B3; arm A restores the previous behaviour on the SAME
binary with `--no-nv2a-gpu-generic-shader`. Gate = total 12 B wall, median AND min, 5 %.

| set | exe | arm A median / min | arm B median / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|---|
| `p6-gate` | `f6d31f25` | 61.026 / 60.751 s | 56.977 / 56.827 s | **+6.63 %** | **+6.46 %** | 3/3 | PASS |
| `p6-gate2` | `f6d31f25` | 61.079 / 60.963 s | 56.922 / 56.826 s | **+6.81 %** | **+6.79 %** | 3/3 | PASS |
| `p6-gship` | `4e0c2191` | 61.355 / 60.118 s | 56.836 / 56.562 s | **+7.37 %** | **+5.92 %** | 3/3 | PASS |
| **`p6-gfinal`** | **`3a612b6d`** | **60.802 / 60.460 s** | **57.308 / 56.825 s** | **+5.75 %** | **+6.01 %** | 3/3 | **PASS** |

12 of 12 paired rounds favour B (-6.52, -6.63, -6.46, -6.96, -6.98, -5.84, -6.01, -7.37, -8.52,
-5.75, -7.42, -5.75 %). Arm cv 0.19-1.45 %. Final-four indicator +18.4 to +21.4 % (not gated). The
decision is taken on `p6-gfinal`, the set run on the binary that ships. Every leg is flagged
`contaminated` by the same 40 idle `beads-mcp` python processes present in every leg of every arm,
which is a constant and not a differential.

**(7) Behaviour.** Arm A vs arm B on the shipping binary, all 28 checkpoints: **25
byte-identical**; 11.000733342 G differs on 2 pixels (1 and 5 LSB), 11.500133329 G on 1 (1 LSB),
`settle-1` on 1 (1 LSB). **Largest connected component 1 px in every case; worst frame 0.0007 %.**
The final frame is byte-identical, and so are `det_digest ed86cf6a1fe11c4f`,
`dev_digest bbadac889020716f`, `dev_events 17 588 661`, `tb_entries 2 302 057 484`,
`tb_insns 7 301 344 897`, `irq0 16342`, retired `12 000 000 001` at `0x8001e3e3`, the scanout FNV
`b7e78a1ac17ac5e8` and all 24 profile hashes. Only `dma_digest` and `dma_writes` move
(1 237 977 → 1 237 379), which is the write CONTENT of the three differing pixels and the 792
batches that no longer write guest memory at all. Diff PNGs read by eye (`build/diff-p6/`):
11.0 G is a near-black boot frame with two isolated single-pixel specks and nothing else; the
final dashboard diff is **entirely untinted** with MEMORY / MUSIC / XBOX LIVE / SETTINGS legible.

Against `build/golden-ref-v2-*.ppm` the two arms are, on the same shipping binary and the same
session: arm A the §3a signature unchanged (2.5 G 5 px, 3.0 G 1, 3.5 G 56, 4.0 G 15, all 1 LSB
except one at 2, largest component 1 px); arm B **that signature plus exactly four pixels across
three tail checkpoints** (11.0 G 2 px, 11.5 G 1 px, `settle-1` 1 px), largest component 1 px.
Four new pixels is the whole behavioural cost of the change.

**Golden: `goldens/golden-noperf-defaults12-2026-09-05d.json` is reproduced EXACTLY and is NOT
re-pinned.** The reference-configuration leg (`--nv2a-draw-threads 0 --nv2a-profile-hashes`,
`build/p6-ref`) passes **31 of 31** golden checks -- final SHA `36e87a97…`, scanout FNV, all 24
frame-series checkpoint hashes, all 24 profile hashes, retired -- on the shipping binary. Note
that unlike the witnessed executor, the generated one DOES run in the reference configuration
(nothing about it is gated on the profile-hash folds), so this is a real test of it and not a
run in which it takes zero jobs. Against `build/golden-ref-v2-*.ppm` that leg is byte-identical on
25 of 28 checkpoints and differs on the same three by the same four pixels.

**(8) What is NOT done, with the number each is worth.**

* **`uses-depth`: 512 declined batches.** The generated executor is colour-only; the resident
  zeta plane is not attached. All 512 draw to `0x03628000` or the small swizzled planes, and by
  the first-consumer rule they are worth **zero resolves today** -- the eight witnessed families
  break `0x03628000` first. Adding depth is a prerequisite for freeing that plane, not a win on
  its own.
* **The eight witnessed families on `0x03628000` (417 colour + 304 zeta resolves, ~1.9 s).**
  Unchanged by this phase, and the phase-5 statement of what they need is now known to be
  incomplete: four generic shader states draw to the same plane and must move with them.
* **The 602 `generic-texture-read` resolves (~1.8 s).** Already-GL-accelerated batches sampling a
  GPU-resident plane. The generator cannot help; this needs the resident surface sampled as a
  texture (the lever §3h declined at a wrong number).
* **1 831.6 MB of texture upload and 479.3 MB of surface upload per run** on the generated path,
  through a per-pixel scalar staging loop. It is already inside a passing gate, but it is the
  obvious next reduction: `GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV` would delete the loop.

### 3k. The compiled VSH step form (2026-09-05, `beads-xbox.1.15.11`) -- **FAILED the 5 % bar, and the interesting result is WHY**

§3i(3) measured the vendored VSH interpreter at **4.54 s, 38 337 230 vertices at 118 ns**, 62 % of
the `nv2a_vertex` bucket, untouched by either cache, and named it the next vertex lever. The
working assumption -- stated in the bead and in the task that opened it -- was that a pre-decoded
op array would be worth the usual **3-5×** an interpreter gets from removing per-op decode. **It is
worth 12 %.** The decode was never the cost, and that is now measured rather than assumed.

**(1) The distinct-program census, always on.** `[host] nv2a-vsh-program-census` /
`-program` keys one row on exactly the two inputs the parse is a function of -- the program words
and the start slot -- folded once per PARSE (19 542 per canonical run, not once per batch) and
carried on the parse cache so a hit costs a stored row pointer and two adds. Canonical 12 B run:
**20 distinct programs, 0 overflow, 20 of 20 compiled, 0 declines**, and the vertices are
concentrated:

| program | steps / ops / paired | batches | vertices | share |
|---|---|---:|---:|---:|
| `a83505a475320999` | 21 / 23 / 2 | 13 440 | **18 211 764** | 47.5 % |
| `7545f5d888770122` | 18 / 21 / 3 | 138 012 | **8 725 576** | 22.8 % |
| `cf4ed0fcd680199d` | 28 / 32 / 4 | 123 690 | **6 187 540** | 16.1 % |
| `3c02d8b087b89d04` | 28 / 33 / 5 | 10 540 | 2 204 640 | 5.7 % |
| `476754744910d47f` | 18 / 21 / 3 | 17 360 | 1 185 280 | 3.1 % |
| the other 15 | 2-24 steps | 152 770 | 1 822 430 | 4.8 % |

**Four programs carry 92.1 % of the vertices and a program averages 22 ops**, so a per-program
compile is amortised over a million vertices each and the compile cost cannot be the problem: the
whole pre-decode is **12.4 ms per run, 0.02 %**, measured, not assumed.

**(2) What landed.** `src/machine/nv2a_vsh_compiled.hpp`. Each program compiles once, off the
phase-5 parse cache, into 32-byte records with the operand base pointer, element offset, packed
swizzle, negate and relative bits, the destination base/offset/writemask and a `paired` bit,
replacing a walk over the disassembler's ~168-byte `Nv2aVshStep`. The step walk, the `is_final`
test, the NOP tests, the three-way operand-type switch and the five-way destination-type switch
are all gone. Exactness is structural, not argued: the record holds the **vendored
`nv2a_vsh_cpu_*` function pointer**, so the arithmetic is the same object code; the operand fetch
keeps `mult * in[swizzle[i]]` including the multiply by +1.0f, which is how the bead's **sNaN
question is answered without depending on the answer** (a copy would not quiet a signalling NaN;
the multiply does, and so does the interpreter); the MAC/ILU pairing is preserved so an ILU
reading what its own step's MAC writes still sees the OLD value; and anything outside the range
the vendored code only guards with an assert -- r13-r15, o13+, c192+, a program with no final step
-- makes the WHOLE program decline, after which the interpreter runs it unchanged.

A second, independent removal rides with it. The interpreter path zero-fills 13 output registers,
12 temporaries and a0 -- **416 bytes on every one of 38 337 230 vertices, 16 GB of stores** --
because any of them can be read before the program writes it. Straight-line code makes that
decidable exactly, so the compile walks the ops in issue order (respecting the pairing) and marks
only the registers a lane actually reads before any write, plus the seven the DEVICE reads back
afterwards (position, diffuse, specular, four texture coordinates) unless the program writes all
four of their lanes. On the two dominant programs the plan is **5 outputs + 4 temps** and
**4 outputs + 3 temps**: 144 and 112 bytes instead of 416.

**(3) Exactness is EVIDENCE, not an argument.** `--nv2a-vsh-verify N` (diagnostic, default off)
runs the vendored interpreter on a scratch state from the SAME inputs and compares the seven
registers the device reads back **bit for bit** -- which covers the fetch, the operations, the
stores and the zero plan together. On a 2 B window with the budget set above the vertex count:
**7 822 822 of 7 822 822 compiled vertices checked, 0 mismatched, 0 differing bits.**

**(4) The matched instrumented pair (one binary, one session, `--perf-accounting
--nv2a-vertex-census`, `build/v11-perf-leg00-A-r1` vs `-leg01-B-r1`).** The census denominators are
IDENTICAL between the arms -- batches 455 812, vertices 128 040 827, reuse_hits 75 499 247,
vsh_n 38 337 230, fixed_function 14 204 350 -- so the two arms did exactly the same work:

| | arm A (interpreter) | arm B (compiled) | delta |
|---|---:|---:|---:|
| `vsh_ns` | 4.823 s | **4.237 s** | **−0.586 s (−12.1 %)** |
| per vertex | 125.8 ns | **110.5 ns** | −15.3 ns |
| attribute fetch | 1.302 s | 1.326 s | +0.024 s (noise) |
| parse | 21.8 ms | 22.3 ms | — |
| pre-decode itself | — | **12.4 ms** over 19 542 compiles | 633 ns each |
| `nv2a_vertex` (exclusive) | 7.736 s | **7.198 s** | **−0.538 s** |
| compiled / interpreted vertices | 0 / 38 337 230 | 38 337 230 / 0 | declines 0 |

(Both legs are census-armed, which adds two `rdtsc` per vertex to BOTH arms; the shipping cost is
lower and the gate below is the number that decides.)

**(5) Two variants BUILT, MEASURED and DISPROVED. Recorded so they are not re-derived.** Both were
measured on the canonical 2 B window with `--nv2a-vertex-census`, same session:

| form | `vsh_ns`, 2 B window | verdict |
|---|---:|---|
| vendored interpreter (`--no-nv2a-vsh-compile`, shipping binary) | 0.993 s | baseline |
| pre-decoded, vendored `fn` pointer (shipping binary) | **0.906 / 0.909 s** | kept |
| pre-decoded, the twenty bodies TRANSCRIBED into an inlined switch | 1.540 s | **+70 %, rejected** |
| the same switch forced out of line (`noinline`) | 1.346 s | **+49 %, rejected** |

*Caveat stated because it bounds what these rows can carry:* a 2 B single leg is not a gate. Arm A
measured 0.993, 1.021, 1.107, 1.109, 1.157 and 1.219 s across six different builds of code that
never changed on that path, so the build-to-build and host noise here is about ±10 %. The two
rejected rows are +49 % and +70 %, five to seven times that band, which is why they are called
rejected; the 8-12 % kept row is confirmed by the matched instrumented pair in (4) and by the gate
in (7), not by this table.

Transcribing the vendored bodies into the executor was the obvious next step and it is a large
REGRESSION. The vendored functions are small, hot in the instruction cache, and reached through
one indirect branch whose target sequence is identical on every vertex, which an indirect
predictor learns; a nineteen-way switch is a second indirect branch on top of that, and inlining
nineteen bodies into three call sites blows out the loop. **Do not retry the inlined-switch form.**
A third one is recorded for the same reason: carrying the ceiling probe's branch inside the
executor loop cost 0.906 → 1.071 s, so the probe is a separate function (rule 9 -- an instrument
that changes what it measures is not an instrument).

**(6) The ceiling probe, which is why the lever is small.** `--nv2a-vsh-unsafe-probe`
(knowingly incorrect when armed, default 0, no `--no-` spelling) runs the operand fetch and
optionally the destination stores without the operation. Five legs on ONE binary (the shipping
`5be3655e`), one session, the 2 B window, identical `vsh_n = 7 822 822` in every leg:

| leg | `vsh_ns` | per vertex | what it contains |
|---|---:|---:|---|
| `--no-nv2a-vsh-compile` | 0.993 s | 127.0 ns | the vendored interpreter |
| compiled (×2) | 0.906 / 0.909 s | **116.0 ns** | everything |
| `--nv2a-vsh-unsafe-probe 2` (×2) | 0.349 / 0.358 s | **45.2 ns** | the zero plan, the input copy, the operand fetch and the output extraction -- no operation, no destination store |

So the operand traffic plus the fixed per-vertex cost is **39 %** and the OPERATIONS AND THEIR
DESTINATION STORES are **61 %**, and those operations are the vendored `fix_inf_mult` /
`nv2a_less_than` special cases the NV2A semantics require. **The decode was never the majority of
the cost**, which is why removing all of it is worth 12 % and not 3-5×.
**The ceiling for this whole lever is `vsh_ns` going to zero: 4.24 s of a ~54 s wall, 7.8 %.**

**(7) The decision A/B. 3+3 interleaved, one binary (`5be3655e`), one session, arm A =
`--no-nv2a-vsh-compile` on the SAME binary. Gate = total 12 B wall, median AND min, 5 %.**

| set | arm A median / min | arm B median / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|
| `v11-gate` | 54.750 / 54.015 s | 53.597 / 53.210 s | **+2.11 %** | **+1.49 %** | 3/3 | **FAIL the 5 % bar** |

Paired per-round deltas −2.81, −2.15, −0.77 %; 3 of 3 favour B. Arm cv 1.11 % (A) and 0.77 % (B),
median-to-min spread 1.36 % and 0.73 %. Final-four indicator +2.91 % median / +3.09 % min (not
gated). Every leg is flagged `contaminated` by the same 40 idle `beads-mcp` python processes
present in both arms, which is a constant and not a differential.

**(8) Behaviour: EXACT, and it adds zero pixels.** Arm A vs arm B on the shipping binary, all
**28 checkpoint PPMs byte-identical (0 differing pixels)**, and every determinism digest identical
INCLUDING `dma_digest`: `det_digest ed86cf6a1fe11c4f`, `dev_digest bbadac889020716f`,
`dev_events 17 588 661`, `dma_digest f5c98df2a12072bb`, `dma_writes 1 237 379`,
`tb_entries 2 302 057 484`, `tb_insns 7 301 344 897`, `irq0 16342`, scanout FNV
`b7e78a1ac17ac5e8`, exit 0 and empty stderr on every leg of both arms. Against
`build/golden-ref-v2-*.ppm` arm B reproduces the §3j(7) signature UNCHANGED -- 21 byte-identical
and the same seven checkpoints (2.5 G 5 px, 3.0 G 1, 3.5 G 56, 4.0 G 15, 11.0 G 2, 11.5 G 1,
`settle-1` 1), largest connected component 1 px, worst frame 0.0182 % -- i.e. **zero new pixels**.

**Golden: `goldens/golden-noperf-defaults12-2026-09-05d.json` is reproduced and NOT re-pinned.**
The reference-configuration leg (`--nv2a-draw-threads 0 --nv2a-profile-hashes`, `build/v11-ref`)
passes **31 of 31** checks -- final SHA `36e87a97…`, scanout FNV, all 24 profile hashes, retired --
and the compiled form REALLY RUNS in it (`compiled_vertices=38 337 230, interpreted 0,
declines 0`), so it is a test of the change and not a run in which it takes zero jobs. That leg is
byte-identical to `golden-ref-v2` on 25 of 28 checkpoints and differs on the same three phase-6
frames by the same four pixels.

**(9) Retention.** The gate is failed, so this is a §3 item 3 judgement, and it is recorded as one
rather than dressed up. What is retained, default ON with `--no-nv2a-vsh-compile` restoring the
previous behaviour on the same binary: a change that is **bit-exact on 7 822 822 verified vertices
and on all 28 checkpoints**, wins **+2.11 % median / +1.49 % min with 3 of 3 rounds favouring it**,
costs 12.4 ms of compile per run, and brings with it the three instruments that turned "the next
vertex lever is worth 3-5×" into a measured 12 % with a 7.8 % ceiling. The alternative reading is
that 2 % does not buy a compiled representation and an analysis pass; that call is the owner's and
it is one flag either way.

**(10) What is NOT done, with the number each is worth.**

* **The operations and their destination stores, 61 % of `vsh_ns` (~2.6 s).** `fix_inf_mult` is four scalar
  multiplies each with up to three `isinf` tests, called four times per MUL/MAD/DP*/DST. A
  branch-free SSE form (one `mulps`, three compares against +inf, one blend) would collapse ~32
  scalar instructions into ~10 SIMD ones. It is a REIMPLEMENTATION of NV2A-specific semantics, so
  it needs `--nv2a-vsh-verify` as its gate, and its ceiling is ~2 % of the wall.
* **The operand fetch, ~4 loads + 4 multiplies + 4 stores per operand.** A 256-entry `pshufb`
  control table makes it a load, a shuffle, a `mulps` and one store. Bit-exact (a permutation plus
  the same multiply) and worth perhaps 1 %.
* **The transform off the emulation thread.** The output feeds the CPU rasterizer, so this is a
  fork-join per batch, not a pipeline; 455 812 batches at a measured 5.8 µs barrier is 2.6 s of
  barrier against 4.2 s of work, which is why it is not obviously a win and needs measuring rather
  than assuming.

### 3l. Phase 7 (2026-09-05, `beads-xbox.1.15.10`): depth in the generated executor, the witnessed-family reroute, and the upload staging

Phase 7 was asked for the four items section 3j(8) left, in the order the plane census orders
them. The first two are ONE lever, not two, and that lever is measured, complete, and **DEFAULT
OFF** -- for a reason that is a measurement rather than caution. The third is exact and small.
The fourth was declined on measurement.

**(1) Depth in the generated executor: BUILT, EXACT, and a REGRESSION on its own.** The executor
now attaches the resident ZETA plane -- the same slot and the same `(address, size)` key the
witnessed executor's `find_witnessed_depth_slot` uses, so a plane either of them makes resident
is found by the other -- and the ROP performs the guest's depth comparison against the value the
fragment program writes to `gl_FragDepth`. The one design decision worth recording is what a
depth batch must NOT do: **it does not take the image-blend path**. Writing `gl_FragDepth` forces
late-Z, and the in-shader `imageStore` of section 3j(4) happens INSIDE the fragment shader, i.e.
BEFORE a late depth test -- so a fragment that loses the depth test would already have written
its colour, where the software path's `continue` skips both. With the ROP owning the colour
write, the hardware orders it after the depth test. `image_blend` is part of the shader-state
identity, so the depth and non-depth forms of one combiner are simply two programs.

`uses-depth` declines go **512 to 0** and the batches execute on the GPU (`depth_draws=195` on
the 3 G window, 3 211 on the full run). It is EXACT: with the reroute below off, all 10
checkpoints to 5.0 G are **byte-identical** to the shipping defaults. **And it is a regression.**
Plane `0x03628000` still has 3 181 witnessed CPU draws on it, residency breaks at its FIRST CPU
consumer, and turning the 527 generic depth batches from CPU consumers into GPU producers makes
the plane dirty again after each one, so the next witnessed CPU draw breaks it again. Measured on
the canonical 3 G window, one binary, one session:

| | arm A (shipping defaults) | arm B (depth on, reroute off) |
|---|---:|---:|
| transferring COLOUR resolves | 566 | **761** |
| their cost | 1.160 s | **2.023 s** |
| transferring ZETA resolves | 180 | **375** |
| their cost | 0.378 s | **0.850 s** |
| generated colour-surface uploads | 0 | 195 of 195 submits |

`--nv2a-gpu-generic-shader-depth` therefore defaults **off**. It is a PREREQUISITE for (2), not a
win, and it must not be turned on without it.

**(2) The witnessed-family reroute: BUILT, and it reaches the goal exactly -- but the IMAGE
MOVES, and the cause is in the generic evaluator.** `--nv2a-gpu-generic-shader-witnessed MASK`
sends a matched witnessed profile down the ladder's own strict generic fallback instead of its
hand-derived specialization, so the generated executor takes it. It is ONE mask over the eight
families the phase-5 plane census names, not eight hand-written GPU fixtures, and it is gated on
the generated executor AND on its depth support so either negative spelling restores the ladder
exactly.

With all eight rerouted (`witnessed_reroutes=3181`), the canonical 12 G run reaches the phase-7
goal on the census:

| | shipping defaults | all eight rerouted |
|---|---:|---:|
| plane `0x03628000` CPU draws | 3 181 | **0** |
| plane `0x033d0000` CPU draws | 3 110 | **0** |
| transferring ZETA resolves | 304 (0.636 s) | **0 (0.000 s)** |
| transferring COLOUR resolves | 1 356 (3.206 s) | 1 244 (3.302 s) |
| transferring texture-source resolves | 230 (0.700 s) | 484 (1.233 s) |
| guest-memory surface uploads (`gpu-upload-total`) | 391 / 915.4 MB | **4 / 8.3 MB** |
| GPU clears accepted / declined-missing (colour) | 691 / 318 | **995 / 14** |
| GPU clears accepted / declined-missing (zeta) | 757 / 495 | **1 240 / 12** |
| `uses-depth` declines | 512 | **0** |
| generated submits / draws | 792 | 4 003 |
| total 12 G wall, one leg each | 54.542 s | **52.374 s** |

**And 12 of 28 checkpoints differ, structurally.** 4.5 G differs on 99.79 % of its pixels at max
delta 255: the Xbox boot logo is simply absent. Single-bit legs to 5.1 G, all ten checkpoints
compared with `tools/compare_ppm_series.py` against the shipping defaults:

| bit | family | CPU draws | differing pixels | max delta | verdict |
|---|---|---:|---:|---:|---|
| 0 | `pitch_multi_texture` | 120 | **0** | 0 | EXACT |
| 1 | `pitch_three_texture` | 2 304 | **0** | 0 | EXACT |
| 6 | `texf_projected_fan` | 51 | **0** | 0 | EXACT |
| 2 | `pitch_tex5_projected` | 126 | 286 151 | 255 | differs |
| 3 | `pitch_tex5_diffuse` | 42 | 20 875 | 122 | differs |
| 4 | `pitch_single_texture` | 121 | 18 883 | 202 | differs |
| 5 | `tex7_fan` | 304 | 14 560 | 98 | differs |
| 7 | `pitch_additive` | 113 | 204 691 | 116 | differs |

**The cause is the GENERIC EVALUATOR, not the GPU executor, and that is measured rather than
argued.** The same bit-2 leg run with the generated executor masked off
(`--nv2a-gpu-generic-shader-mask 0`, so the reroute lands in SOFTWARE) reproduces the **same
286 151 pixels**. For bits 2 and 5 the evaluator does not implement the batch's texture-shader
program at all and skips it: `[2bl] NV2A generic-combiner skip census:
unsupported-texture-shader-program=126` and `=304`, exactly those draw counts, so those draws
vanish. And it is not an oversight -- `src/machine/nv2a.cpp:10503-10520` states the restriction
and its reason in the source: a single-stage combiner with a non-{0,1} `SHADER_STAGE_PROGRAM` is
refused because "letting this evaluator's simpler per-ICW-literal formula loose on that same
narrow state produced a wrong (not hardware-matching) answer for tex7_fan". Bits 3, 4 and 7 are
skipped by nothing and still differ, which is a numeric divergence between the generic decode and
the pinned specialization.

Three of eight is worth nothing under the first-consumer rule, so the mask exists for BISECTION
and defaults to **0**. **DO NOT RETRY WITHOUT**: the generic evaluator modelling the
texture-shader stage chain for `SHADER_STAGE_PROGRAM` 0x0C01 and tex7_fan's 0x0C61 (bits 2, 5),
and an explanation for the three that diverge numerically (bits 3, 4, 7) -- or, alternatively, a
route that hands the GENERATOR the witnessed specialization's own expression instead of the
generic decode's.

**(3) The upload staging: BUILT, EXACT, retained at flat cost.** The generated executor's
1 831.6 MB of texture and 479.3 MB of colour-surface upload per run went through
`guest_words_to_rgba*`, a per-pixel byte shuffle. A guest A8R8G8B8 word is little-endian, so its
bytes in memory are already B,G,R,A -- exactly `GL_BGRA`/`GL_UNSIGNED_BYTE`. A texture therefore
needs **no staging buffer at all** (the device already hands over a tightly packed image), and a
colour surface needs only its vertical flip as a row copy; an unchanged geometry re-uploads
through `glTexSubImage2D` instead of re-specifying storage. This is the transform the WITNESSED
executor's upload path already used. `--no-nv2a-gpu-generic-shader-bgra-upload` restores the
shuffle on the same binary.

**(4) Batching: NOT BUILT, and bounded INDIRECTLY rather than by an A/B -- so it is recorded as
UNDETERMINED under section 2 rule 8, not as disproven.** Each generated batch is still one
`glDrawArrays` with its own SSBO upload, 792 per canonical run. Two facts bound it. The reroute
leg above ran **4 003** of them -- five times as many -- and was 2.2 s FASTER overall, which is
not a controlled comparison (that leg changed the whole plane) but does put a ceiling on what
3 211 extra generated draws can cost. And the instrumented partition puts the whole of `nv2a_gl`
at 5.319 s over 304 888 entries, of which the witnessed executor's 20 816 already-batched draw
calls plus its per-batch state are the bulk; 792 of 304 888 entries is 0.26 % of that bucket.
Both are far below the ~1 % MDE of the 3+3 method, so no gate could resolve the lever today.
**DO NOT RETRY WITHOUT** a run in which the generated executor's own draw count is the dominant
term in `nv2a_gl` -- which is exactly what the reroute would create if it ever ships.

**(5) The decision A/B. 3+3 interleaved, one binary (`a3e98dc1`), one session, arm A =
`--no-nv2a-gpu-generic-shader-bgra-upload` on the SAME binary. Gate = total 12 B wall, median AND
min, 5 %.** Depth and the reroute are default-off, so they take zero batches in either arm and
this set measures the upload staging alone.

| set | arm A median / min | arm B median / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|
| `p7-gate` | 54.539 / 54.456 s | 53.718 / 53.643 s | **+1.51 %** | **+1.49 %** | 3/3 | **FAIL the 5 % bar** |

Paired per-round deltas -2.14, -0.63, -1.51 %; 3 of 3 favour B. Arm cv 0.28 % (A) and 0.38 % (B).
Final-four indicator +3.01 % median / +2.48 % min (not gated). Every leg is flagged
`contaminated` by the same four idle `beads-mcp` python processes present in both arms, which is
a constant and not a differential. Retained under section 3 item 3 as a pure simplification: it
deletes a per-pixel shuffle over 2.3 GB and cannot change a texel.

**(6) Behaviour: EXACT, and it adds zero pixels.** Arm A vs arm B, all **28 checkpoint PPMs
byte-identical (0 differing pixels)**, and every determinism digest identical INCLUDING
`dma_digest`: `det_digest ed86cf6a1fe11c4f`, `dev_digest bbadac889020716f`,
`dev_events 17 588 661`, `dma_digest f5c98df2a12072bb`, `dma_writes 1 237 379`, `irq0 16342`,
final frame SHA `36e87a97...`, scanout FNV `b7e78a1ac17ac5e8`, all 24 profile hashes
(`0 differ`), exit 0 and empty stderr on every leg of both arms. Against
`build/golden-ref-v2-*.ppm` arm B reproduces the section 3k(8) signature UNCHANGED -- 21
byte-identical and the same seven checkpoints (2.5 G 5 px, 3.0 G 1, 3.5 G 56, 4.0 G 15, 11.0 G 2,
11.5 G 1, `settle-1` 1), largest connected component 1 px, worst frame 0.0182 %. Diff PNGs read
by eye (`build/diff-p7/`): the three boot frames are isolated 1-LSB specks with no shape and the
final dashboard diff is entirely untinted.

**Golden: `goldens/golden-noperf-defaults12-2026-09-05d.json` is reproduced and NOT re-pinned.**
The reference-configuration leg (`--nv2a-draw-threads 0 --nv2a-profile-hashes`, `build/p7-ref`)
passes **31 of 31** checks and is byte-identical to `golden-ref-v2` on 25 of 28 checkpoints,
differing on the same three phase-6 frames by the same four pixels.

**(7) The emulation-thread partition after phase 7** (`build/p7-perf`, `--perf-accounting`, wall
58.444 s, unaccounted 0.161 %). This is the floor estimate the owner was promised, and what
remains in it is CPU emulation, command dispatch and PFIFO, and driver overhead -- not raster:

| bucket | s | % of wall |
|---|---:|---:|
| `tcg_exec` | 14.690 | 25.14 |
| `nv2a_raster_witnessed` | 9.162 | 15.68 |
| `nv2a_vertex` | 6.739 | 11.53 |
| `nv2a_raster_generic_sw` | 5.806 | 9.93 |
| `nv2a_gl` | 5.319 | 9.10 |
| `nv2a_dispatch` | 4.774 | 8.17 |
| `nv2a_gl_resolve` | 3.791 | 6.49 |
| `nv2a_pfifo` | 2.811 | 4.81 |
| `nv2a_texture` | 1.760 | 3.01 |
| `nv2a_clear` | 1.528 | 2.61 |
| `mmio_dispatch` | 0.673 | 1.15 |
| `nv2a_recorder` | 0.597 | 1.02 |
| everything else (slice overhead, checkpoints, io, poll, scanout, interrupts) | 1.280 | 2.19 |

**(8) What is NOT done, with the number each is worth.**

* **The five diverging witnessed families (bits 2, 3, 4, 5, 7; 706 of 3 181 CPU draws).** They
  are the whole of what stands between here and a free `0x03628000`, which the reroute leg
  measured at **-2.17 s (-3.98 %) on one leg**, with all 304 zeta resolves and 907 MB of upload
  removed. Two need the generic evaluator to model a texture-shader stage chain; three need an
  explanation for a numeric divergence.
* **The 484 transferring texture-source resolves (1.233 s with the reroute on; 230 / 0.700 s
  without).** Already-GL-accelerated batches sampling a GPU-resident plane. The lever is a
  GPU-to-GPU copy (`glBlitFramebuffer` with the Y flip, or `glCopyImageSubData`) from the
  resident plane into the texture slot instead of a readback plus a re-upload; its ceiling is
  1.3 % of the wall without the reroute and 2.3 % with it.
* **`nv2a_dispatch` 4.774 s + `nv2a_pfifo` 2.811 s = 7.585 s, 13.0 % of the wall**, on 82.7 M
  dispatch entries at 57.7 ns each. Nothing in the GPU arc touches it; it is the pushbuffer parse
  and the method ladder, and it is now the third-largest bucket in the run.

### 3m. The pushbuffer run path (2026-09-05, `beads-xbox.1.15.1` reopened) -- **FAILED the 5 % bar at +4.29 %, and the bucket pair is gone**

Section 5b row 6 was the last untouched item in the run: `nv2a_dispatch` 4.774 s +
`nv2a_pfifo` 2.811 s = 7.585 s, 13.0 % of the phase-7 instrumented wall, on 82.7 M method words
that the whole GPU arc never went near. This session measured where those nanoseconds went,
removed the per-word overhead, and gated the result. The change is **byte-exact** -- 28 of 28
checkpoints identical, every determinism digest identical, all 24 profile hashes identical -- and
it **fails the 5 % retention bar by 0.71 pp**.

**(1) The per-word breakdown, measured before anything changed.** `src/machine/nv2a_dispatch_micro.hpp`
is a compile-time-optional (`-DXLLE_DISPATCH_MICRO=ON`, default OFF) micro-partition of one
pushbuffer word into eleven sibling slots, sampled 1 word in 64. It is NOT perf_excl: the slots
never nest, so sampling is sound here in a way perf_excl documents it is not for a hot child of a
hot parent. Two findings had to be fixed before the numbers meant anything, and both are worth
carrying forward:

* **A bare `rdtsc` cannot time a 40 ns interval.** The first chained version summed twelve deltas
  to 12.2 ns against a 41.4 ns envelope -- 36 % of the whole -- because unfenced reads float to
  the head of the reorder window and collapse toward each other while the real work retires
  behind them. `lfence` on both sides of the read fixed it. `perf_excl::tick_now()` is still an
  unfenced `rdtsc`; that is tolerable for its own regions and would not have been tolerable here.
* **The instrument's own cost is SOLVED, not assumed.** Sampled words alternate between an
  envelope mode (2 reads, nothing inside) and a breakdown mode (12 chained reads). The deltas
  telescope, so `span_breakdown = W + 12R` and `span_envelope = W + R` give R -- the marginal cost
  of one fenced read where it actually sits -- from two equations. Measured R = 16.38 ns. The
  obvious estimator, a `null` slot with no work in it, is NOT R: it also carries the sampler's own
  bookkeeping (2.1 ns, mostly two 64-bit divisions by the runtime period) and using it
  over-subtracts from every slot. With R solved, the parts sum to 99.5 % of the whole.

One 12 B noperf leg, `XLLE_DISPATCH_MICRO_METHOD=all`, period 64, 650 697 sampled words of
83 289 212 data words (`build/micro/all1`):

| slot | ns/word | s over the run | what it is |
|---|---:|---:|---|
| `dm_handler` | 354.51 | 29.527 | the method's own semantics, i.e. the DRAW. Not overhead |
| `pf_queue` | 22.37 | 1.863 | the PushMethod aggregate + `pull_queue_.push_back` |
| `pf_fetch` | 18.73 | 1.560 | `mem_read_` -- the std::function into `uc_mem_read` -- for four bytes |
| `drain_enter` | 14.89 | 1.240 | the drain funnel: stall predicate, perf scope, `front()` / `pop_front()` |
| `rec_ring` | 3.93 | 0.328 | `methods_.push` into the always-on ring |
| `dm_prologue` | 2.29 | 0.191 | dispatch_method's SET_OBJECT test and object-class read |
| `pf_dma` | 1.42 | 0.118 | `dma_object_address` |
| `dm_class` | 0.65 | 0.054 | `class_method_counts_` + the class2d slot search + three class compares |
| `drain_exit` | 0.43 | 0.036 | loop re-test and scope destructors |
| `rec_trace`, `rec_counts` | ~0 | 0.000 | at the floor: the clear-trace deques and `method_counts_` cost nothing |
| header words | 28.36 | 0.163 | 5 757 919 jump / call / return / method-header words |
| **per-word overhead** | **64.71** | **5.390** | everything above except `dm_handler` |

So the answer to "where do 57.7 ns/word go" is: **they are not in the method ladder and they are
not in the recorder.** `pf_queue + pf_fetch + drain_enter` alone are 56 ns/word and 4.66 s -- the
CACHE1 deque round trip and the `uc_mem_read` hop, both of which exist only because the pusher was
written one word at a time. The jump table (`beads-xbox.1.15.1` stage (a)) and the recorder split
(`kNv2aRecorder`) were measuring the wrong half of the problem, and this table is the retraction.

A second leg filtered to ARRAY_ELEMENT16 (`build/micro/e16b`, 64 011 925 words = 76.86 % of the
run) prices the whole word at **43.0 ns**, of which the handler -- two vector appends -- is 4.46.

**(2) The method census, and why three arms cover 77 % of the words.** The micro build prints
`[2bl] NV2A method census` from the always-on `method_counts_` histogram at shutdown. 454 distinct
methods, 83 288 956 words: `0x1800` ARRAY_ELEMENT16 **64 011 925 (76.86 %)**, `0x17FC` 911 624,
`0x1EA4` 882 697, the transform-constant range `0x0B80..0x0BBC` 8 552 000 across twelve methods in
runs of four, the transform-matrix range `0x0480..0x04BC` 1 418 400 in runs of sixteen, `0x1808`
9 672, `0x1818` 5 820, `0x1810` 1 306.

**(3) What was built.** Three stages, all default ON, each with a negative spelling on the same
binary.

* **(c) `Nv2a::drain_method_run`** -- a run of method DATA words is consumed in one pass instead
  of one CACHE1 round trip per word. It is the SECOND funnel that opens `kNv2aDispatch`, and it is
  entered only when `pull_queue_` is empty, the puller is not stalled, and the DMA object maps the
  whole run contiguously (proved by translating the first and last word and requiring the host
  addresses to differ by exactly `4 * (n - 1)`, which also catches a wrap of the 0x07FFFFFF mask).
  It re-tests the stall predicate on EVERY word, exactly as `drain_pull_queue`'s loop does, and
  advances `ch.get` / `ch.method` / `ch.method_count` per word, so an observer sees the same
  channel state at the same instant. `--no-nv2a-pfifo-run-batching`.
* **(d) `Nv2a::pushbuffer_host_bytes`** -- one resolved host window per kick instead of a
  `uc_mem_read` per word. XboxMachine maps guest RAM into Unicorn with
  `uc_mem_map_ptr(ram_.data())` and hands the same pointer back through `mem_direct_`, so the bytes
  are identical by construction rather than by agreement. The window is reset at the top of every
  `process_pushbuffer`, which bounds a cached host pointer to one invocation.
  `--no-nv2a-pfifo-direct-fetch`.
* **(d-bulk) the bulk append** for `0x1800` / `0x1808` / `0x1818` -- a non-incrementing class-0x97
  run of one of those goes straight into `vertex_indices_` / `inline_array_words_` without
  entering `dispatch_method` at all. `methods_.push` still runs PER WORD with the same `seq`, so
  the always-on ring and `push_methods()` are unchanged; `method_counts_` and
  `class_method_counts_` take `+= n`, which is legal precisely because no handler reads either;
  each handler's own safety bound is hoisted so a run that would have faulted is declined to the
  generic form and still faults at the same word through the same code; and a clear trace in
  progress declines to the generic form so the capture still sees every word.
  `--no-nv2a-pfifo-run-bulk`.

**Run census on the shipping default, 12 B:** `runs=5 575 015 words=82 709 636 bulk-runs=788 162
bulk-words=63 931 139 per-word=579 576`. **99.30 % of method words are batched**, 76.76 % go
through the bulk append (99.87 % of all ARRAY_ELEMENT16 words), and the 0.70 % that still take the
strict per-word pusher are attributable: `queue=577 314` (words behind an entry a stalled drain
left in CACHE1) and `stall=2 262`. Zero DMA declines, zero direct-window declines. The per-word
path is therefore reachable in every default run and is not dead code.

**(4) The AFTER number for the dominant word class.** The same micro build, run-path envelope,
12 315 sampled runs covering 1 017 009 ARRAY_ELEMENT16 words: **5.34 ns/word**, against 43.0 ns on
the per-word path. Eight times cheaper for 76.9 % of the run's method words.

**(5) The decision A/B. 3+3 interleaved, ONE binary (`a48a1e83`), one session, arm A =
`--no-nv2a-pfifo-run-batching --no-nv2a-pfifo-direct-fetch` (the exact pre-change pusher: run
census `runs=0 per-word=83 289 212`), arm B = the shipping default. Gate = total 12 B wall, median
AND min, 5 %.**

| set | arm A median / min | arm B median / min | median | min | rounds | verdict |
|---|---|---|---|---|---|---|
| `rb-gate3` (both stages) | 53.515 / 53.467 s | 51.222 / 51.129 s | **+4.29 %** | **+4.37 %** | 3/3 | **FAIL the 5 % bar** |
| `rb-gate2` (run batching alone, direct window on in BOTH arms) | 52.282 / 52.158 s | 50.949 / 50.879 s | +2.55 % | +2.45 % | 3/3 | attribution |
| `rb-gate` (run batching alone, direct window in NEITHER arm) | 53.479 / 53.387 s | 51.102 / 51.010 s | +4.44 % | +4.45 % | 3/3 | superseded, see below |

Final-four indicator on the decision set: **+8.07 % median / +8.00 % min** (not gated). Arm cv
0.059 % (A) and 0.275 % (B); median-to-minimum spread 0.090 % and 0.180 %. Every leg is flagged
`contaminated` by the same six idle `beads-mcp` python processes present in both arms -- a
constant, not a differential, exactly as in section 3l(5).

**Per-window medians of the decision set, and the one place the change COSTS time.** The win is
where the pushbuffer traffic is, and there is a small consistent regression where there is none:

| window | arm A median | arm B median | delta | window | arm A median | arm B median | delta |
|---|---:|---:|---:|---|---:|---:|---:|
| 0.5 B | 2.285 s | 2.244 s | +1.78 % | 6.5 B | 1.021 s | 1.047 s | **-2.50 %** |
| 1.0 B | 2.170 s | 2.039 s | +6.01 % | 7.0 B | 0.672 s | 0.701 s | **-4.41 %** |
| 1.5 B | 3.179 s | 3.041 s | +4.33 % | 7.5 B | 0.680 s | 0.693 s | **-1.94 %** |
| 2.0 B | 3.292 s | 3.168 s | +3.75 % | 8.0 B | 0.673 s | 0.680 s | **-1.09 %** |
| 2.5 B | 3.394 s | 3.260 s | +3.93 % | 8.5 B | 0.667 s | 0.674 s | **-1.10 %** |
| 3.0 B | 3.519 s | 3.395 s | +3.53 % | 9.0 B | 0.666 s | 0.675 s | **-1.30 %** |
| 3.5 B | 3.526 s | 3.386 s | +3.97 % | 9.5 B | 0.616 s | 0.629 s | **-2.02 %** |
| 4.0 B | 3.668 s | 3.523 s | +3.96 % | 10.0 B | 1.496 s | 1.414 s | +5.46 % |
| 4.5 B | 3.303 s | 3.262 s | +1.23 % | 10.5 B | 2.226 s | 2.077 s | +6.66 % |
| 5.0 B | 1.359 s | 1.350 s | +0.64 % | 11.0 B | 4.084 s | 3.738 s | +8.46 % |
| 5.5 B | 0.847 s | 0.847 s | +0.11 % | 11.5 B | 4.652 s | 4.234 s | +9.00 % |
| 6.0 B | 0.857 s | 0.855 s | +0.25 % | 12.0 B | 4.653 s | 4.294 s | +7.73 % |

The 6.5 B .. 9.5 B block is idle-fast-forward: ~0.6-1.0 s windows at ~60 effective fps with almost
no pushbuffer traffic. Arm B is slower in **seven of those nine windows**, by 102 ms in total --
0.19 % of the run, small but too consistent in sign to call noise. The mechanism is the run path's
FIXED cost showing where there are no words to amortise it over: the window reset at the top of
every `process_pushbuffer`, the batching preconditions per data word, and two `dma_object_address`
calls plus one `mem_direct_` hop per run. A minimum-run-length threshold would remove it and was
deliberately NOT added -- at 14.8 words per run on the whole workload the threshold would decline
runs worth ~56 ns/word to save ~10 ns of setup, and 102 ms against a 2 293 ms gain is not worth a
tuning knob that has to be re-tuned per title. Recorded so the next session finds it measured
rather than rediscovering it as a mystery.

**The `rb-gate` row is the methodological finding of this session and is why it is superseded.**
That set was run before stage (d) existed and reported +4.44 %. When stage (d) landed, the same
A/B reported **+2.55 %** -- because `--no-nv2a-pfifo-run-batching` gated only the run path, so the
direct window was helping ARM A TOO. The per-word arm moved 53.479 -> 52.282 s (**+2.30 % for the
direct window on its own**) and the apparent win of the change nearly halved. A flag that names
one stage of a two-stage lever measures half the lever and reports it as the whole. Arm A must
restore EVERY stage; `tests/test_dispatch_table_guards.py` now pins that with the numbers in the
failure message.

**(6) Behaviour: EXACT.** Arm A vs arm B of the decision set, `tools/compare_ppm_series.py`:
**28 of 28 checkpoints byte-identical, 0 differing pixels**. A separate 12 B pair
(`build/rb/f12A` vs `build/rb/f12B`) additionally shows every `[2bl]` census line identical, all
24 frame-series checkpoint hashes identical, and every determinism digest identical at all 24
checkpoints -- `det_digest`, `dev_digest`, `dev_events` (17 588 661), `dma_digest`, `dma_writes`
(1 237 379), `irq0` (16 342) -- with final frame SHA `36e87a97...` on both arms. All 24 profile
hashes agree in the gate set (`0 differ`). This is not "behaviourally faithful"; it is identical.

**Golden: `goldens/golden-noperf-defaults12-2026-09-05d.json` is reproduced and NOT re-pinned.**
The reference-configuration leg (`--nv2a-draw-threads 0 --nv2a-profile-hashes`, `build/rb-ref`)
exits 0 with empty stderr and passes every check: final frame SHA `36e87a97...`, all 24 profile
hashes equal, chunk_insns and engine equal, with only `exe_sha256` and `git_head` SKIPped as
expected. Against `build/golden-ref-v2-*.ppm` it is byte-identical on 25 of 28 checkpoints and
differs on the same three phase-6 frames by the same four pixels section 3l(6) records (11.0 B
2 px, 11.5 B 1 px, `settle-1` 1 px, largest connected component 1 px). No new drift.

**(7) The bucket pair after the change.** Two instrumented legs on the same binary, arm A =
both negatives, arm B = default (`build/rb/q12A`, `build/rb/q12B`):

| bucket | arm A s | arm A n | arm B s | arm B n | delta |
|---|---:|---:|---:|---:|---:|
| `nv2a_dispatch` | 4.841 | 82 712 961 | **2.317** | **5 578 340** | −2.524 |
| `nv2a_pfifo` | 2.846 | 20 191 | **0.154** | 20 191 | −2.692 |
| `nv2a_recorder` | 0.598 | 83 288 956 | **0.139** | 19 357 817 | −0.459 |
| the pair | **7.687** | | **2.471** | | **−5.216** |
| the pair + recorder | 8.285 | | 2.610 | | −5.675 |

The dispatch region is now entered once per RUN (5.58 M) instead of once per WORD (82.71 M), and
`nv2a_pfifo` -- the pusher's own self time -- falls by 94.6 %. **The instrumented delta must not be
quoted as the win:** removing 82.7 M region entries also removes 282 M of the instrument's own
`rdtsc` reads (`clock_reads` 377 887 349 -> 95 755 829), so −5.2 s instrumented corresponds to
−2.3 s noperf. The trustworthy number is the gate: 2.293 s of a 53.5 s wall.

**(8) The emulation-thread partition after this change** (`build/rb/q12B2`, `--perf-accounting`,
wall 52.501 s, unaccounted 0.065 %; a second arm-B leg, because the first agreed to 0.001 s on
every bucket of this lever but carried 1.0 s of host noise in `tcg_exec`):

| bucket | s | % of wall |
|---|---:|---:|
| `tcg_exec` | 14.509 | 27.64 |
| `nv2a_raster_witnessed` | 9.172 | 17.47 |
| `nv2a_vertex` | 6.799 | 12.95 |
| `nv2a_raster_generic_sw` | 5.892 | 11.22 |
| `nv2a_gl` | 5.189 | 9.88 |
| `nv2a_gl_resolve` | 3.757 | 7.16 |
| `nv2a_dispatch` | 2.316 | 4.41 |
| `nv2a_texture` | 1.755 | 3.34 |
| `nv2a_clear` | 1.531 | 2.92 |
| `mmio_dispatch` | 0.677 | 1.29 |
| `nv2a_pfifo` | 0.158 | 0.30 |
| `nv2a_recorder` | 0.139 | 0.26 |
| everything else (slice overhead, checkpoints, io, poll, scanout, interrupts) | 0.573 | 1.09 |

**(9) Retention.** The gate says REJECT and the numbers say the lever is finished, so both belong
in the record. What remains in `nv2a_dispatch` after the change is 2.32 s of method SEMANTICS --
the handler bodies of the 19.4 M words that are not a bulk vertex-index append -- not overhead:
the entire per-word overhead this bead was about is now 0.30 % of the wall. **Recommended for
retention under section 3 item 3** on the same reading section 3l(3)/(5) used for the BGRA upload
staging (retained at +1.51 %): it is byte-exact on every surface the project measures, it deletes
82.7 M `uc_mem_read` hops, 82.7 M deque round trips and 82.7 M region entries, and it is 2.8x the
win that precedent retained. It ships DEFAULT ON with three negative spellings, so reversing it is
a flag rather than a revert. **This is a judgement call the owner can overrule with
`--no-nv2a-pfifo-run-batching --no-nv2a-pfifo-direct-fetch`.**

**(10) What is NOT done, with the number each is worth.** All three are below the ~1 % MDE of the
3+3 method and are recorded as **UNDETERMINED** under section 2 rule 8, not disproven:

* **A bulk arm for the incrementing transform-constant range `0x0B80..0x0BBC`** (8 552 000 words,
  10.3 % of the run). It would remove `dm_prologue + rec_ring + dm_class` plus the call, ~7 ns/word
  measured, ~0.06 s deflated -- **0.11 % of the wall**. DO NOT RETRY WITHOUT a reason to touch that
  handler anyway: its per-word `transform_constant_load_` advance and bounds test have to be
  reproduced exactly and it feeds the VSH parsed-program cache.
* **Hoisting `ch.get` / `ch.put` and the ring head out of the bulk loop** (63.9 M words at 5.34 ns
  measured, ~2.6 ns deflated). Worth ~0.1 s, **0.2 %**, and the ring-head part would weaken
  PRINCIPLES section 7's always-on visibility mid-run for that. DO NOT RETRY WITHOUT a workload
  where the bulk loop is the dominant term.
* **Skipping `methods_.push` when no debug consumer is attached** (the shape this session's brief
  suggested). 83.3 M ring pushes are 2.67 GB of stores, measured at 3.93 ns/word, ~0.17 s, **0.3 %**
  -- and it would change `push_methods()`, the `[2bl] NV2A PFIFO: methods=` census and the
  `recent_methods` dump, i.e. it trades an exact result for 0.3 %. DECLINED on that trade, not on
  the number.

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

### 5b. The measured lever order after phase 7 (2026-09-05)

Stage g is complete as of 2026-09-06; its separate interactive measurements and explicit
retention exception are in `PERF-PRESENT-2026-09-06.md`. The forward plan is now
`PERF-CAMPAIGN-2026-09-06.md`, targeting faithful LLE 60 FPS with headroom. This inventory
preserves earlier configurations and bounds; it does not establish global optimality.

Every row is a direct measurement on the canonical 12 B replay, not an inference. The
emulation-thread partition it is read against is the section-3m instrumented leg
(`build/rb/q12B2`, wall 52.501 s, unaccounted 0.065 %): `tcg_exec` 14.509,
`nv2a_raster_witnessed` 9.172, `nv2a_vertex` 6.799, `nv2a_raster_generic_sw` 5.892, `nv2a_gl`
5.189, `nv2a_gl_resolve` 3.757, `nv2a_dispatch` 2.316, `nv2a_texture` 1.755, `nv2a_clear`
1.531, `mmio_dispatch` 0.677, `nv2a_pfifo` 0.158, `nv2a_recorder` 0.139. (The phase-7 leg it
replaces was `build/p7-perf`, wall 58.444 s; the phase-5 leg before that was
`build/p5-perf-leg01-B-r1`, wall 62.496 s.)

| # | lever | measured size | what it costs to build |
|---|---|---:|---|
| 0 | ~~the 704 generic software-fallback resolves on plane `0x03AA4000`~~ | **DONE 2026-09-05 phase 6**: 704 → 0, `nv2a_gl_resolve` −1.670 s and `nv2a_raster_generic_sw` −1.364 s, gate +5.75 / +6.01 % | the generated combiner executor, §3j |
| 1 | **The remaining transferring residency resolves**: 1 356 colour, 304 zeta after phase 6 | **4.32 s** | three routes, below |
| 1a | — free plane `0x03628000`: the eight witnessed families (§3i(2)) **AND the eight generic shader states that also draw to it** | **MEASURED END TO END 2026-09-05 phase 7 and BLOCKED**, §3l(2). Depth in the generated executor is BUILT and exact (declines 512 → 0), the reroute is BUILT, and together they take both planes to `cpu_draws=0`, all 304 zeta resolves to 0, guest-memory surface uploads 391/915.4 MB → 4/8.3 MB and one 12 G leg 54.542 → 52.374 s (**−3.98 %**). But **five of the eight families move the image** and the cause is the GENERIC EVALUATOR: bits 2 and 5 hit a texture-shader program it deliberately refuses (`unsupported-texture-shader-program` = 126 and 304, exactly their draw counts), bits 3, 4 and 7 diverge numerically. Both flags ship DEFAULT OFF | the generic evaluator must model the texture-shader stage chain for `SHADER_STAGE_PROGRAM` 0x0C01 and 0x0C61, and the three numeric divergences must be explained |
| 1c | — the transferring texture-source resolves: ALREADY-GL-accelerated batches sampling a GPU-resident plane, which the generator cannot touch. Measured directly by `[host] gpu-resolve-cost`: **230 at 0.700 s** on the shipping defaults, 484 at 1.233 s once the reroute frees `0x03628000` | 1.3 % of the wall today, 2.3 % after 1a | a GPU-to-GPU copy (`glBlitFramebuffer` with the Y flip, or `glCopyImageSubData`) from the resident plane into the texture slot, replacing the readback plus re-upload. NOT the shader-side resident sampler §3h costed |
| 1b | — make a resolve cheaper: 4.80 s backend (glReadPixels 2.53 s GPU-side + stall) + 0.90 s writeback of 6 109 MB, and `dirty=100.0 %` on every resolve | up to ~2 s | row-scope the resolve+upload against a real dirty rect, or resolve straight into guest RAM via `mem_direct_` |
| 2 | ~~**The VSH interpreter**: 38 337 230 vertices at 118 ns (§3i(3))~~ | **MEASURED OUT 2026-09-05, §3k.** The pre-decoded step form is worth **12 %** of it, not the 3-5× an interpreter usually gives, because the ceiling probe says the DECODE was never the majority: operand traffic and fixed per-vertex cost 53 %, the NV2A-specific operation bodies 47 %. `vsh_ns` 4.823 → 4.237 s, gate **+2.11 % / +1.49 %, FAIL**. The remaining ceiling for the whole lever is `vsh_ns` → 0, i.e. **7.8 %** of the wall, and the two routes to any of it (an SSE `fix_inf_mult`, a `pshufb` operand fetch) are worth ~2 % and ~1 % | done and recorded; §3k(10) has what is left |
| 3 | ~~The 1 878 generic-combiner software fallbacks~~ | **DONE**, and the phase-5 framing of this row was wrong twice: the 602 texture reads are NOT software fallbacks (row 1c), and the 704 fallback resolves were all on a plane §3i(2) never named | — |
| 4 | ~~1 831.6 MB of texture upload + 479.3 MB of surface upload per run on the generated path, through a per-pixel scalar staging loop~~ | **DONE 2026-09-05 phase 7**, §3l(3): `GL_BGRA` deletes the loop entirely for a texture and reduces a colour surface to a row copy. Gate **+1.51 % median / +1.49 % min, FAILS the 5 % bar**, 3 of 3 rounds, 28 of 28 checkpoints byte-identical; retained under §3 item 3 as a pure simplification | done |
| 5 | batch the generated draws (792 per run, one SSBO each) | **UNDETERMINED, bounded well below the method's MDE, 2026-09-05 phase 7**, §3l(4). 792 of `nv2a_gl`'s 304 888 entries is 0.26 % of a 5.319 s bucket, and the reroute leg ran 4 003 of them while being 2.2 s faster. Neither is a controlled A/B; both are ceilings | not built — needs a workload where the generated draw count dominates `nv2a_gl` |
| 6 | ~~**`nv2a_dispatch` 4.774 s + `nv2a_pfifo` 2.811 s = 7.585 s, 13.0 % of the wall**, on 82.7 M dispatch entries at 57.7 ns each~~ | **DONE 2026-09-05, section 3m, and the 57.7 ns/word was never the method ladder.** The per-word micro-partition puts 56 of the 65 ns/word of overhead in the CACHE1 deque round trip and the `uc_mem_read` hop, not in dispatch. Consuming a METHOD RUN in one pass and reading the pushbuffer through the host pointer Unicorn already maps takes the pair 7.687 -> 2.471 s instrumented and `nv2a_pfifo` self time down 94.6 %; 99.30 % of method words are batched and 76.8 % skip `dispatch_method` entirely. Gate **+4.29 % median / +4.37 % min, FAILS the 5 % bar**, 3 of 3 rounds, 28 of 28 checkpoints byte-identical and every determinism digest identical; recommended for retention under section 3 item 3 on the section 3l(3) reading | done; section 3m(10) has the three remaining items, all below the method's MDE |
| — | ~~the 148 317 no-op per-draw residency queries~~ | **16 ms — DECLINED ON MEASUREMENT** | — |
| — | ~~`kPitchZ16Depth` on the GPU (142 842 draws, 476 M fragments)~~ | **zero resolves removed**; its two zeta planes have no GPU draws at all | one shader, for CPU raster removal only |

**Row 7 / stage g - DONE, 2026-09-06:** native scanout upload and guest-vblank pacing;
same-binary interactive 3+3 improves total wall +1.07% median / +1.02% min (**5% REJECT**),
capture 453.2 -> 8.8 us and successful SDL calls 16.50 -> 18.76/s (medians). Old/new
headless is 0.30% median / 0.36% min slower. Explicit presentation/measurement capability
exception, not a 5% claim. Reference golden 31/31 and every checkpoint comparison 28/28
exact. Host capture can move a resident writeback earlier, so the DMA history hash differs
across modes even though the traced payload and all image/input/CPU/device checks agree.
`PERF-PRESENT-2026-09-06.md` carries the complete evidence and metric denominators.

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

Update 2026-09-06 (`beads-xbox.1.14.3`): small-region timing now uses
`lfence; rdtsc; lfence` with compiler barriers, including calibration. Earlier
bare-RDTSC costs are historical observations, not reliable absolute costs for
short bodies. The old `compensated_ns = clock_reads * overhead` overstates actual
subtraction when a read closes no region, the delta is rejected, or it is shorter
than the floor. Reports now sum compensation actually applied in `accrue()`.
`unaccounted_ns` remains raw wall minus compensated buckets; the added
`residual_ns/pct` also removes the actual compensation. Inspect both, along with
discarded deltas. Probe bookkeeping and fence-induced scheduling changes still
perturb an instrumented run, so noperf interleaved sets remain the retention gate.
Direct vertex parse/compile/fetch/execute census deltas subtract the same measured
floor and expose a separate discard count. Detailed texture FNV tracing requires
`--nv2a-gpu-generic-texture-trace`; ordinary profiling no longer restores the
hashing eliminated by the accepted texture cache.
