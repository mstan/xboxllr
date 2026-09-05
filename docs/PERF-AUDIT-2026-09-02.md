# Performance blind-spot audit — 2026-09-02

> Measurement-first assessment of the path from ~3 reported fps to 60. Five serial
> Opus subagents (execution model, measurement stack, NV2A pipeline, CPU tier,
> ndsrecomp history); every load-bearing claim re-verified in source by the
> orchestrator. Scope is blind spots and measurability, not implementation.
>
> Report artifact: https://claude.ai/code/artifact/87a59840-fcf5-4529-8f0e-d47154e91d11
> Tracker: `beads-xbox.1.14` (index) + nine child measurement beads; notes on `beads-xbox.1.13`.
> Companion: `docs/PERF-METHODOLOGY.md` (owner decisions, re-baseline rule, protocol).
> Reference run: `build/codex-cachefix-perf-v1-12b.report.json` (12 B insns, 252 s wall, HEAD 93d006d).

Tags: **[verified]** read in source or re-derived from artifacts; **[inferred]** reasoned, not
measured; `unknown` not determinable today.

---

## 1. Verdict

1. **The headline metric is not a frame rate.** `effective_fps` (`src/app/main.cpp:2086`) is guest
   vblanks per host wall-second, and vblanks derive from retired instruction count
   (`src/machine/machine.cpp:377` → `nv2a.cpp` `set_frame`). So `fps = 60 × realtime ratio`;
   60 fps means retiring 733 M guest insn/s. The canonical run retires ~43 M/s. The benchmark
   configuration (`tools/run_stockdash_replay.ps1`) never presents a frame; there is no
   `SDL_GL_SwapWindow` in the tree. Actual flips in the reference run: 694 / 252 s = 2.75/s. [verified]
2. **The CPU tier is the binding constraint and has never been measured cleanly.** A whole-address-
   space `UC_HOOK_CODE` is always installed; with Unicorn's count hook that forces TCG to kill all
   ~28 guest globals at every instruction. Best window ever measured: 172.7 M insn/s (23.6 % of
   realtime), on the idle loop. Removing *all* GPU work would still land under 8 fps. [verified]
   **Superseded 2026-09-03 (§8a, §8b):** the exclusive partition measures TCG at 36 % and the CPU-only
   witnessed software rasterizer at 46 % of wall. Both tiers must be fixed; the NV2A path is first.
3. **The GPU path is a sidecar.** ~98 % of fragments are shaded by a single-threaded software
   rasterizer with no code path to the OpenGL renderer. The renderer drains/reads back the GPU
   ~940× more than it draws. [verified]
4. **The measurement stack cannot see wins under ~20 %.** Single runs, no interleaving, a gate on
   one noisy window, a default-on instrumentation flag costing 17 % that also changes hot-loop
   code. 28 prior rejections in the 2–12 % band are undetermined, not disproven. [verified]
5. **The exactness gate is pinned to a host scheduling parameter.** Changing only
   `--machine-chunk-insns` changes the golden hash. The gate encodes where interrupts land, not
   whether the emulator is correct, and the software rasterizer it protects has never been compared
   to a hardware frame. [verified]

| Figure | Value |
|---|---|
| Reported "fps", canonical 12 B replay (perf build) | 3.188 |
| Same, noperf build | 3.846 |
| Presented flips per second, same run | 2.75 |
| Mean guest insn/s | ~43 M |
| Required for 60 "fps" as defined | 733 M |
| Best 500 M-insn window ever measured | 172.7 M insn/s |
| Cost of `--perf-accounting` (default on in the replay script) | 17.1 % |

---

## 2. Where the wall time goes

All buckets nest inside the CPU bucket because device work runs inside Unicorn's MMIO callbacks
on the same stack. Buckets overlap; pure TCG time is **not derivable** — the band is 0–100 s of a
251 s run (`src/machine/perf_accounting.hpp:15-21` says so; `tools/compare_xbox_reports.py:145-154`
subtracts anyway).

| Bucket | Seconds | Share of wall | Note |
|---|---:|---:|---|
| Inside `uc_emu_start` (`cpu`) | 251.1 | 99.6 % | everything below nests here |
| Outside any NV2A bucket | 100.4 | 40.0 % | Unicorn + hooks + other devices |
| Witnessed software raster | 114.6 | 45.6 % | CPU only; no GL path exists |
| Generic combiner raster | 18.7 | 7.5 % | GL admitted or software fallback |
| `dispatch_method` minus raster | 17.4 | 6.9 % | 134-branch if-chain |
| OpenGL renderer total | 5.7 | 2.3 % | of which draw calls: 4.2 **ms** |
| `host_poll_devices` | 0.63 | 0.25 % | 71.7 ns/scope — measures its own clock reads |

**Amdahl on the current split** (base 3.188 fps):

| If this were free | Remaining wall | Speedup | "fps" |
|---|---:|---:|---:|
| All rasterization | 117.8 s | 2.13× | 6.8 |
| All NV2A work incl. dispatch | 100.4 s | 2.50× | 8.0 |
| Target 10 fps (north-star milestone) | ≤ 80.1 s | 3.14× | 10 |
| Target 60 fps | ≤ 13.4 s | 18.8× | 60 |

GPU offload alone cannot reach even 10 fps. A CPU-tier campaign is mandatory and must come first.

---

## 3. Execution model (agent 1)

- `main.cpp:2740` calls `mach.run()` exactly once; the real loop is `XboxMachine::run`
  (`machine.cpp:333-413`): 4096-insn `uc_emu_start` slices; device clocks (PIT, vblank, OHCI, APU)
  are pure functions of `total_insns_` at 733 333 333 Hz. Guest time == instruction count.
- **Threads:** main/emulation; debug-server accept loop (started only on the fixture path, which
  `main.cpp:4173-4177` returns before reaching under `--boot`); host sample profiler worker.
  Zero `std::mutex`, `condition_variable`, `async`, or queues in the tree. ~60 `std::atomic`
  counters in `perf_accounting.hpp` are single-threaded `fetch_add`s.
- **Unicorn hooks** (`src/cpu/unicorn_floor.cpp`): `UC_HOOK_CODE` begin=1,end=0 (`:31`; match-all per
  `uc_priv.h:238`); `UC_HOOK_MEM_*_UNMAPPED`; `UC_HOOK_INTR` (manual IDT walk `:236-284`);
  `UC_HOOK_INSN` IN/OUT. MMIO via `uc_mmio_map` (correct). `UC_HOOK_MEM_READ|WRITE` only on the
  fixture path.
- **GPU inside a guest store:** `Nv2a::mmio_write` → `process_pushbuffer` (`nv2a.cpp:604/628`) →
  `drain_pull_queue` → `dispatch_method` → `submit_vertex_batch` → `rasterize_witnessed_batch` →
  software raster **or** `renderer_->submit_*` → `glReadPixels`, all before the `mov` retires.
- **GL context:** hidden 1×1 window (`nv2a_opengl_renderer.cpp:3561`), no swapchain; output only via
  `glReadPixels` into guest RAM; `SDL_GL_MakeCurrent` pair + 4 `steady_clock::now()` per submit,
  **not** gated on the perf flag (`:1614-1628`, `perf_accounting.hpp:648-649`).
- **Present (interactive only):** `sdl_shell.cpp:80` 33 ms throttle → ~30 fps hard ceiling; scalar
  per-pixel `capture_scanout` on the emulation thread.
- **Unconditional diagnostics on hot paths:** `main.cpp:2593-2604` `push_back`+`erase(begin())` per
  I/O and per MMIO access; thread attribution `uc_mem_read` every 64 insns unless
  `--reduced-retire-diag`.
- **Linear scans:** `find_mmio` over 6 devices; `Nv2a::find_block` over 20 blocks with the USER
  doorbell last (`nv2a.cpp:216-227`); `unordered_map` PFIFO regs, 4 lookups per doorbell.
- **Build:** default `RelWithDebInfo`; `-march=native` only on `xlle_machine`/`test_nv2a`; Unicorn
  gets no `-march`; LTO/PGO tried and reverted (replay script refuses LTO baselines).

## 4. CPU tier (agent 4)

**Corrections to earlier beliefs [verified]:**
- `sync_eflags` is conditional on `cc_op` change; `gen_sync_pc` and `check_exit_request` are
  per-instruction (`translate.c:4822-4837`).
- The `irq_pending_` EFLAGS poll is bounded: `pending-boundaries=11692` of 2.94 M slices (0.4 %), < 1 s.
- Interrupt delivery: ~29 API calls × 19 577 deliveries → < 0.2 % of wall.
- Slice overhead < 1 % (chunk 8192 vs 65536: 7.7× fewer slices, 1.4 % *slower*). `get_regs()` per
  slice is ten `uc_reg_read`s incl. an EFLAGS recompute, outside every timed region.
- Sampler `no_module_leaves` = leaves outside `xlle.exe` (ntdll, msvcrt, GL ICD, JIT together);
  the JIT share is separately unmeasurable.

**The real cost:** `qemu/target/i386/helper.h:1` `DEF_HELPER_4(uc_tracecode, …)` carries no
`TCG_CALL_NO_*_GLOBALS` flag, so `tcg.c:2398-2402` runs `la_global_kill` on every hook call →
every guest register is spilled to `env` at every instruction boundary for the entire run. With
`hooks_count==2` (`uc.c:1075` adds the count hook) `tcg-op.h:50-68` also takes the out-of-line
dispatcher. **The count hook alone reproduces the global kill**; removing only the user hook buys
just the dispatcher. TB chaining survives hooks; max 512 insn/TB. `UC_HOOK_BLOCK` exists (per-TB,
before `gen_tb_start`, passes size in bytes); `UC_HOOK_EDGE_GENERATED` exposes per-TB icount, so a
pc→icount map is buildable. `count<=0` deletes the count hook and `tb_flush`es; `timeout=` reads
the host clock (forbidden for determinism).

**TB cache:** 1 GiB, lazily committed via a process-wide VEH; no per-slice flush.
`hooked_regions` (37 380) = distinct `(pc,size)` TB identities ever seen, **not** cache statistics.
`tb_flush_count` exists in source, unexposed.

**Idle loop:** NT idle thread `sti;nop;nop;cli` + DpcListHead check at `0x8001e3ce-0x8001e3ff`; the
canonical run ends at `0x8001e3d5`. Idle fraction at 12 B is `unknown` (attribution disabled by the
benchmark's `--reduced-retire-diag`). `build/a4-ohci-xid-8b`: idle thread 1.687 B / 8 B = **21.1 %**,
and worker totals identical at 6.4 B and 8 B → 100 % idle in 6.4–8 B. Free fix: count EIP-in-idle
at slice boundaries (`machine.cpp:409` already reads EIP).

**Throughput ceiling:** mean 36–53 M insn/s; best window 172.7 M (idle).

**MEASURED 2026-09-03** (`tools/bench/unicorn_hook_bench.cpp`, same g++/-O3 and the same three
Unicorn archives `xlle.exe` links; Ryzen 7 9800X3D; 200 M insns per cell, 5 interleaved reps;
median insn/s; full table in bead `beads-xbox.1.14.2` and `tools/bench/run_200M.log`):

| Workload | H0 no hooks | H1 count only | H2 block hook | H3 xlle (code+count, 4096 slices) | H3→H2 |
|---|---:|---:|---:|---:|---:|
| TIGHT (ALU loop) | 10 552 M | 338 M | 9 163 M | 157 M | **58×** |
| MIXED (mem/call/indirect) | 2 991 M | 379 M | 2 843 M | 165 M | **17×** |
| IDLE (`sti;nop;nop;cli;cmp;je`) | 702 M | 330 M | 550 M | 170 M | **3.2×** |

- H3 IDLE (170 M) reproduces production's best-ever window (172.7 M): the arm is faithful.
- **Slicing is free:** code+count hooks in one call ≈ H3 with 48 829 calls. Raising chunk size
  buys nothing. Disproved: "slice re-entry is a meaningful cost."
- **The per-instruction hook is the whole cost, and it is bimodal:** one hook (inline path) costs
  ~30× on TIGHT; a second hook flips to the out-of-line dispatcher and costs a further 2.1–2.3×.
  Correction: `uc_add_inline_hook` (`tcg.c:669`) also sets `flags=0`, so the inline path *also*
  pays `la_global_kill`; the paths differ only by the dispatcher.
- **Block hook costs 5–22 %.** An always-on block ring is affordable.
- **733 M insn/s with zero hooks:** TIGHT yes (14×), MIXED yes (4×), **IDLE no** (702 M = 96 %;
  550 M with a block hook). The idle loop's `sti` ends a TB and the next TB holds exactly one
  instruction (`HF_INHIBIT_IRQ_MASK`), so TBs are ~2 insns. → **Idle fast-forward is a required
  lever, not optional.**
- Two earlier estimates disproved: hookless TCG is 7–20× faster than the 150–1000 M guess; hook
  removal is 17–58× on real code, not 3–5× (which holds only for the idle loop).
- H3 is an upper bound on the current architecture; production's mean of 43 M is ~3.7× lower
  because of INTR/unmapped hooks, per-slice device clocks, MMIO, and NV2A work inside the slice.

**Determinism model:** guest time = insn count; no host clock in `src/machine/`; RTC frozen; input
scripted on the guest's USB poll count. **But** `rdtsc` → `cpu_get_ticks` → `__rdtsc()`
(`hw/i386/x86.c:33`, `timer.h:886`) — host TSC leaks to the guest; rdtsc count uninstrumented.
**And** the golden SHA depends on slice size: 4096 → `876b58c2…`, 8192 → `1e7a2c47…`,
65536 → `ebf386ad…` (`build/chunk*-v1-12b.report.json`, both `ok:false`). Every execution-tier
change re-baselines the gate by construction.

**Alternatives (assessment only):**

| Option | Throughput | Determinism | Observability | Cost |
|---|---|---|---|---|
| Unicorn, block hooks only, icount via edge hook | 3–5× today [inferred] | preserved in kind; never use `timeout` | block ring cheap enough to be always-on | idle loop's 2-nop window may sit mid-TB |
| QEMU TCG directly | same core; native icount + IDT delivery | better (record/replay) | best of TCG options | rewrite of machine clock plumbing |
| WHPX / Hyper-V | 10–100× | structurally gone (no exact insn count) | VM exits only | mutually exclusive with exactness doctrine |
| Own dynamic x86 JIT | 300 M–1 G [inferred] | you define the clock | rings in generated code, always-on | multi-quarter correctness project |

## 5. NV2A pipeline and GPU offload (agent 3)

- **Witnessed profiles:** 15 exact-equality register-pattern matches captured from one dashboard run
  (`nv2a.cpp:2647-2664`, ladder `:3206-3252`; one carries an 11-entry factor allowlist `:2867`).
  Unmatched combiner state → generic combiner; matched-but-out-of-subset → **fault-stop** (`:3304`).
  The witnessed region (`2643-5845`) contains **no** renderer submit; the only `renderer_->submit_*`
  sites are `8323/8425` inside the generic path (`5942+`). → 114.6 s of 133 s raster (45.6 % of
  wall) is CPU-only regardless of `--nv2a-renderer`.
- **GPU share:** 8.746 G fragments; ≤ 1.82 % could reach GL; the largest generic family (fp89,
  109 M frags) is 100 % rejected (vertex-expansion). Est. actual GL share ~0.13 % [inferred].
  `accelerated=17 825` overstates (3 255 issued no submit). `renderer_draw` 4.2 ms vs
  `renderer_resolve` 3 998 ms.
- **Readback:** 5.4 GB `glReadPixels`, all emulator-internal triggers (software fallback needing the
  surface 3.5 GB, texture read, region re-upload, CPU `CLEAR_SURFACE`, witnessed overlap). Full-
  surface, synchronous, no PBO, ignores tracked `dirty_surface_pixels`. No guest-CPU-read detector
  exists (`UC_HOOK_MEM` never installed on replay) → whether the guest reads rendered VRAM is
  **structurally undetectable**. Readback is forced by *path mixing* (96.8 % of draws on CPU), not
  by the hash.
- **Exactness gate actually enforced:** `measure_xbox_replay.py:795-807` = final-frame SHA +
  `stderr_empty`. The 15 per-profile output hashes and 946 fingerprint hashes are printed and never
  compared. Software model: int 1/16-px edge functions + **double** interpolation. **Never validated
  vs hardware** (`machine/probes/hw-2026-07-16.md` = MMIO regs only; xemu-oracle/ = monitor UI
  shots). Real NV2A interpolates in float32/fixed point → the blocked GL path (STATUS 2026-08-21,
  70 ±1-LSB pixels) is the one more likely to match hardware. PRINCIPLES §8 forbids inferring
  correctness from two implementations agreeing; §9 forbids free-running host threads.
- **Overdraw:** 12.6 M frags per presented frame on 307 200 px = 41×; witnessed pass rate 19.6 %
  (radial: 4.1 G shaded, 727 M written). 58 % of assembled triangles culled after full CPU
  transform. Early-Z / front-to-back is exactness-preserving and unmeasured.
- **Dispatch:** `dispatch_method` = 134 `else if`, 0 `switch` (`10082-10782`); method `0x1800`
  (76.9 % of 83 M words) at branch ~#124 (`:10718`); 17.4 s, 209 ns/word.
- **Inner loop:** 1 221-line shared body, 57 `profile ==` compares per fragment, 15-way counter
  ladder (`4536-4560`) ungated, per-write serial FNV (`4599`) → unvectorizable by construction.
  No SIMD (0 hits). No tile-thread scaffolding in tree.
- **VSH** is a CPU interpreter, program re-parsed per batch (`:1371`); vertex transform never on GPU.
- **Reusable:** PFIFO parser; method semantics; `nv2a_vsh_parse_program` IR; swizzle/format decode;
  `Nv2aRenderer` resolve/invalidate seam. **Gaps:** 4 hand-written GLSL programs vs 946
  fingerprints (no shader generation); 4-slot texture "cache" validated by full scalar FNV per
  draw; no swapchain; no dirty-page tracking. `refs/xemu/hw/xbox/nv2a/pgraph/` holds `psh.c`,
  `vsh-prog.c`, full GL and Vulkan backends — unused (`nv2a.cpp:6525` cites `pgraph_glsl_gen_psh`
  in a comment).
- **Correction:** `direct_surface_bytes` 1.4 TB = logical spans at construction; 874 k of 876 k take
  the zero-copy pointer path; real payload ≈ 153 MB. Not a bandwidth problem.

## 6. Measurement stack (agent 2)

- **Nesting:** `cpu ⊃ nv2a_pushbuffer ⊃ dispatch ⊃ submit_vertex ⊃ {vertex_transform, raster ⊃
  {raster_generic | witnessed[16]}}`; `dispatch (150.7 s) > pushbuffer (145.7 s)` proves ≥ 5 s of
  dispatch runs outside `process_pushbuffer` via `resume_pushbuffers`. `raster_witnessed = raster −
  raster_generic` is the one legitimate subtraction.
- **Instrumentation cost:** ~191 M `steady_clock::now()` per run (83 M in `dispatch_method`
  alone); `record_pfifo_queue_push/pop` 83 M atomic RMWs. `--perf-accounting` is default-on in
  the replay script (`ps1:93-99`), costs 17.1 % (3.846 → 3.188), enables detailed telemetry
  (`main.cpp:1077`) and shifts the syscall-storm share 41 % → 18 %. The one always-on timing site
  (`record_gl_context_site`) is discarded unless perf is on and never parsed by tools.
- **A/B protocol:** `run_stockdash_replay.ps1:114` runs once; `compare_xbox_reports.py:176-215`
  gates one stored baseline vs one candidate on `final_fps` (single 500 M window; adjacent windows
  1.79–11.82). `summarize_xbox_report_set.py` (variance) is called by nothing. Perf gates disarmed
  by default (`MinFinalFpsGainPct=0`). Observed identical-code drift 3.7–4.4 fps (18.9 % p2p).
  **MDE:** 5 %/10 % undetectable, 20 % marginal, 2× yes. Best case with existing tools wired
  properly: ~2 % at n≥3 interleaved on final-four wall.
- **Golden-hash correctness gate is strong and sound** (SHA + empty stderr + build-config guards).
- **Frame-series:** only `vblank-delta` (constant 40/41), `wall-ns`, `effective-fps` are interval-
  scoped; everything else is cumulative and differenced positionally
  (`perf_samples`=25 vs `frame_series`=24 — already misaligned).
- **Rings:** four `Ring<>` instantiations, none timestamped; `g_retired`/`g_mem` + debug server only
  on the fixture path. **0 % doctrine compliance for performance data.**
- **Guest visibility in benchmark:** none (idle fraction, IRQ rate, per-vblank draws/frags,
  thread attribution all absent or disabled).
- **TCG visibility:** no TB flush/cache stats; `pending_boundaries_` is a proxy, printed once,
  unparsed.
- **GPU visibility:** zero timer queries/fences/`glFinish`; all renderer buckets are CPU-side
  driver wall time.
- **Sample profiler:** `kMaxReportEntries=24` (`sample_profiler.hpp:55`) truncates each window;
  `symbolize_host_profile.py:162` uses the truncated sum as denominator → STATUS 248-255
  percentages (39/20/14 %) are really 17.9/8.8/6.2 %. 75 % of blocking-pair samples have
  `caller=0`. The profiled binary (08-21) matches neither on-disk exe (08-22, 08-10) → function
  attributions unverifiable. `Sleep(2)` without `timeBeginPeriod`.
- **PGO:** `build/pgo-use-v1-12b.report.json` `ok=False`, hash `208b0818…` == the rejected X8
  SRC_ALPHA shortcut's hash → reproducible float-reassociation exactness oracle, undocumented.

## 7. Prior rejections — status after this audit

| Rejected change | Claimed | Basis | Status |
|---|---:|---|---|
| Resident alpha-contract shader | −11 % | one run, self-attributed to drift | **undetermined** |
| `std::function`→lambda raster callback | +12.4 % | single run | **undetermined** |
| Immutable SwizzleLookup cache | +12.6 % | single run | **undetermined** |
| DXT cache, FBO attach cache, batch-cache sizes, VSH caches, ~20 others | +3–8 % | single run, perf build | **undetermined** |
| Disabled tile-thread scaffolding; compiled-out VSH census | +4.9 %, +5.7 % | no code executed | noise-floor evidence |
| Resident primary clip, X8 SRC_ALPHA, broad tile-bin compute, ≥256-vertex skip, CPU-clipped mixed-W | hash changed | golden SHA | **stays rejected** |
| Broad primary tile-bin compute (+146 %), resident exact-compute texture (+13 %) | large | measured | stays rejected |
| MakeCurrent as primary blocker | 1.85 s / 240 s | per-site timing | sound; model decision |
| `XLLE_VERTEX_TRANSFORM_CACHE` | 1 849 hits / 453 k misses | hit rate | sound; model decision |

Full list with STATUS/PROGRESS line refs is in bead `beads-xbox.1.14` notes.

## 8. Blind spots, ranked by decision impact

Each is a question the system cannot answer today → bead.

1. **Is this 5 % change real?** → `beads-xbox.1.14.1` (interleaved ABBA, ≥3/arm, final-four wall, noperf).
2. **How fast is bare Unicorn (0 / block / code hook)?** → `.14.2` (standalone benchmark).
3. **How much of the 251 s is translated code vs device callback?** → `.14.3` (exclusive accounting; ndsrecomp `emu_profile.h`).
4. **What fraction of the run is the idle loop?** → `.14.4` (EIP-in-idle at slice boundaries; free).
5. **What did vblank N cost and what did the guest ask for?** → `.14.5` (per-vblank ring).
6. **Is GL time GPU execution or driver overhead?** → `.14.6` (timer queries + fences).
7. **Where is the other 55 % of the profile?** → `.14.7` (full table, true denominator, binary provenance).
8. **How many TBs flush / retranslate?** → part of `.14.2`/`.14.3` (expose `tb_flush_count`).
9. **Which guest code is hot, on which thread?** → `.14.4` (block-hook PC ring).
10. **Does the guest CPU ever read rendered VRAM?** → prerequisite for any GPU-resident design; `unknown`.
11. **Do per-profile hashes drift?** → `.14.8` (wire existing hashes into the harness).
12. **What does the software rasterizer get wrong vs hardware?** → `.14.9` (probe framebuffer capture; owner authorization required).

## 8a. Idle census — MEASURED 2026-09-03 (`beads-xbox.1.14.4`)

Zero-cost slice-boundary census added to `XboxMachine` (`machine.hpp/.cpp`, one range compare plus
one 4-byte KPCR read per 4096-insn slice; printed per frame-series window in `main.cpp`). Canonical
noperf replay: exit 0, empty stderr, final SHA `876b58c2…` unchanged (exactness-neutral). Log:
`build/idle-census-noperf-v1-12b.stdout.log`.

- **Idle = 39.44 % of instructions** (4.733 B of 12 B); idle-thread attribution agrees to 0.115 %.
  Threads: idle 39.5 %, `0xd00086b8` 31.6 %, `0xd0008a98` 26.4 %, rest < 1 % each.
- Per-window idle ranges 0 % (windows 10–18) to 93 % (window 21). **Idle fraction does not predict
  wall time:** window 6 at 73 % idle ran 15.5 M insn/s; window 21 at 93 % idle ran 90.7 M insn/s.
  A naive regression attributing 76 % of wall to idle is disproved by window 21 directly.
- Bounds: idle loop 5.6–11.8 ns/insn → the 4.7 B idle insns cost 27–56 s of 299 s (9–19 %).
  Non-idle guest code runs ≥ 178 M insn/s in device-light windows, so pure TCG retirement for the
  whole run is ≤ ~67 s. **68–78 % of wall is not instruction retirement** — it is the device path
  reached through MMIO (cross-checked against `build/vsh-census-perf-v1-12b`: same slice count to
  4 slices, `nv2a_pushbuffer_ns` 163 s nested in `cpu_ns` 262 s).
- Early stops: 7 724 (0.26 % of slices), all IRQ-armed; PIT ch0 steady at 998.7 Hz (682 pulses per
  window); no fault stops.
- Projections [inferred]: idle fast-forward alone 1.10–1.23×; block-hook CPU tier alone
  1.23–1.34×; both 1.28–1.46×. Neither is the largest lever on today's wall; both are required for
  60 fps once the device path is fixed.
- Caveats: single leg (no interleaved control), instrument cost estimated ~0.1–0.3 % not measured,
  exact-until-full tables dropped 5.7 % of slice pages in the tail, and this run was ~1.8× slower
  per window than a 2026-08-21 noperf run for unexplained reasons (host drift or instrument cost —
  the pending A/A session decides).


## 8b. First measured results of the combined tree — 2026-09-03 (`beads-xbox.1.14`, `.15.1`, `.15.2`)

Combined working tree (idle census, exclusive accounting, dispatch jump table, raster loop
specialization, GL timer queries, profiler fix, harness) compiled with zero errors/warnings;
`.text` +128 KiB (+7.6 %); 15/15 C++ tests and 4/4 guard tests pass. Canonical noperf replay:
exit 0, empty stderr, final SHA `876b58c2…`, all 49 hash fields byte-identical to the census run.

**Exclusive partition (`[perf-excl]`, perf run, wall 227.0 s, unaccounted 0.045 %):**

| bucket | s | % |
|---|---:|---:|
| nv2a_raster_witnessed | 104.3 | **46.0** |
| tcg_exec (retirement + Unicorn internals) | 82.2 | 36.2 |
| nv2a_vertex | 9.3 | 4.1 |
| nv2a_raster_generic_sw | 7.7 | 3.4 |
| nv2a_gl | 5.1 | 2.2 |
| nv2a_dispatch (82.7 M entries, 58.6 ns each) | 4.8 | 2.1 |
| nv2a_gl_resolve | 3.9 | 1.7 |
| nv2a_pfifo | 2.9 | 1.3 |
| everything else (texture, clear, recorder 7.3 ns/word, mmio, slice, host_poll, intr, io) | 6.6 | 2.9 |

The 68–78 % device-path inference (§8a) was right in direction, high in size: **NV2A 60.7 %, TCG
36.2 %, all other overhead 0.5 %.** One bucket, the CPU-only witnessed rasterizer, is 46 % of
the run. The dispatch if-chain premise (209 ns/word) does not reproduce: dispatch + recorder =
66 ns/word = 2.4 % of wall, so the jump table's ceiling is 2.4 %.

**GPU timers (`[perf-gl-gpu]`):** gpu_draw 2.3 s (28,703), gpu_upload 4.5 s, gpu_resolve
146.6 s over 1,307 readbacks; CPU-side drain 1.4 s + readpixels 1.8 s + copy 0.3 s.
**Resolved 2026-09-03 (read-only analysis, `beads-xbox.1.14.6`):** the 146.6 s is a bracket
artifact, not GPU work. The `GL_TIME_ELAPSED` bracket around the synchronous `glReadPixels`
closes with an end marker that the GPU only executes at the *next* flush, and nothing flushes
between resolves (hidden 1×1 window, no swap, `SDL_GL_MakeCurrent` early-outs when already
current at 110–340 ns/call). So it measures GPU idle time until the next command-buffer flush.
Bounds: the synchronous readback cannot cost more than its CPU wall, 1.36 ms each (2.77 GB/s,
normal PCIe). **Corrected GPU utilization: 6.8–8.6 s of 225.5 s = 3.0–3.8 % busy; the GPU is
~96 % idle.** Rule for the timer: no bracket may contain a call that blocks on the GPU; fix via
PBO readback with the bracket around the async read, or `glFlush` after `glEndQuery`; add a
`gpu_resolve_ns <= readpixels_ns` self-check.
**Readback headroom:** `bytes_read == dirty_bytes` because every submit declares a full-surface
clip (not a tracking gap), and 70 % of readback bytes are the software rasterizer fetching a
surface, which has no clip rect at all — scissored readback saves nothing. PBO async saves at
most 3.2 s (1.4 %) and there is nothing to overlap it with. All GL readback is 1.6 % of wall.
Stage (f) of the seam removes ~76 % of readback bytes; stages (d)/(e) remove none. The
940:1 resolve:draw ratio is a path-mixing artifact, confirmed. **The 122 s of software raster
is the problem; GL cost is under 12 s.**

**Perf-flag cost:** 17 % does not reproduce. HEAD instrumented vs noperf +5.1 %; combined tree
+2.9 % (n=1 each). The decoupled telemetry is proven: perf runs now leave both perf-gated hashes
at the FNV basis. Under HEAD, requesting timing silently added ~155 M counter increments and
~155 M FNV folds to the raster inner loop.

**Host noise floor (clean null A/A, 3+3):** cross-arm median −0.36 %, min −0.41 %; single-round
excursions to 2 %. A contaminated leg in the earlier pass read +40.7 % — the justification for
interleaving and quiescence checks.

**Interleaved A/B, HEAD vs combined, 3+3 noperf:** gate metric (final-four wall) median +0.85 %,
min +1.14 % → REJECT at 5 %. Total 12 B wall median **+3.46 %**, min +4.00 %. Per-window: every
one of the eight witnessed-raster windows (0.5–4.0 B) improved **+4.3 % to +6.7 %**; the
generic/GL tail windows were flat to mixed. Controlled attribution (instrumented pair with
telemetry parity): raster_witnessed −11.0 % (66 % of the gain), raster_generic −5.2 %, dispatch
−2.0 % (≈1 % of the gain). **The win is the loop specialization; the jump table is ~1 %.**
Undetermined: the census instrument cost (2+2 legs disagreed in sign).

Owner items raised: retention of `.15.2` given that the final-four gate window is not raster-
dominated; restating methodology rule 6 (17 % → ~5 %/~3 %).


## 8c. Stage (c) early depth rejection — NULL, and the GPU timer corrected (2026-09-04)

- **Disproved premise:** 11 of the 12 depth-testing witnessed profiles already test depth before
  any texture fetch or combiner math. The "80 % of shaded fragments are discarded" figure counts
  fragments after coverage, most of which are already rejected by an early depth test at low
  cost. Only `kPitchAdditiveColor` tested depth late, and reordering it changes two telemetry
  counters, so it was gated behind `!hash_on` — inert in every measured configuration
  (`inline-color` early-reject census = 0). Fresh 3+3 interleaved A/B (`ab-earlyz-v3`): total
  wall median +0.21 %, min −0.04 %, 24/24 hashes identical → the predicted null. An earlier
  partial session (`v2`) showing +6.8 % was host contamination (same binary 231–237 s vs
  208–209 s) and is discarded. Stage (c) code to be reverted as dead complexity.
- **Witnessed raster fraction is stable at 46 % of wall** across three perf runs (0.459, 0.463,
  0.464) while absolute wall drifted 214–237 s — host drift of ~10 % between sessions is real and
  is why only interleaved same-session numbers count.
- **GPU timer bracket fixed:** `glFlush` after `glEndQuery` in the two readback wrappers, plus a
  `resolve_bracket_ok` self-check. `gpu_resolve_ns` 138.5 s → **2.46 s** (1.88 ms/resolve,
  1.39× the CPU-side readpixels time). Final SHA still golden. Real GPU busy for the run:
  draw 2.3 s + upload 4.0 s + resolve 2.5 s ≈ 8.8 s of 214 s = **4 %**.
- Open: `single` profile early-reject census = 0, unexplained; `glFlush` cost bounded only.


## 8d. Stage (d) seam with inline executor — landed, exactness-neutral (2026-09-04)

- `DrawJob` latched at the draw-batch boundary in `submit_vertex_batch`; `execute_draw_job` is the
  sole entry to the pixel math; the fragment code is textually unchanged (aliases bound to job
  fields). Golden SHA and 24/24 hashes identical. 66-assertion guard test parses the anchor list
  out of the design doc so code and doc cannot drift.
- **M5 (coherence census, always-on):** 436,110 of 455,812 draw jobs (95.7 %) are queueable;
  4,551 drains over 981 vblanks → mean queue depth 96, max 423, 4.6 drains per vblank against
  445 jobs per vblank. Binding anchors: semaphore release (64 %) and surface retarget (34 %); five
  anchors never fire. **M3 = 0:** no render-to-texture within a frame in 152,064 texture
  snapshots. Structurally inline-only jobs 4.3 %. **Verdict: stage (e) threading is worth
  building.** Two measurement bugs were caught first (set_frame anchor firing per slice; resolve
  anchor counting empty resolves) — the wrong placement had said queue depth 1.
- A/B 3+3: total wall −0.86 % median / −1.19 % min, final-four +2.96 %; within the design's
  ≤ 1 % overhead target and below the MDE. Retained as a neutral enabler (rule 7); recorded as
  undetermined at the 1 % level.
- Constraint for stage (e): jobs are non-threadable while the per-write profile hashes are on
  (default). Threaded legs must run with `--no-nv2a-profile-hashes` and be byte-locked on the
  framebuffer SHA plus per-checkpoint scanout hashes and counters against an inline leg of the
  same binary.


## 8e. Stage (e) single in-order draw consumer — exact, REJECTED on timing (2026-09-04)

- Implemented behind `--nv2a-draw-threads N` (default 0; max 1). Witnessed batch split into a
  latch half and `execute_witnessed_fragments(DrawJob&)` (fragment code textually unchanged);
  127 counters moved into the job; all ten anchors drain; faults recorded on the worker and
  raised at the fold. Toggle-off is golden-exact; the threads 0 vs 1 byte-lock of the same binary
  shows **0 differences** across 24 checkpoint PPMs, hashes, and every counter. The byte-lock
  caught a real ordering bug first (non-threadable jobs overtaking the queue) — fixed with an
  order fence.
- A/B same binary threads 0 vs 1, 3+3 quiet: **+0.39 % median, +0.34 % min → REJECT.**
- **Why, measured:** worker busy 0.489 of elapsed, emulation thread blocked in drains 0.449 —
  sum 1.04, so the theoretical ceiling was 4 % and the queue never saturated (backpressure 0).
  The guest submits draws in bursts ~10× faster than they rasterize, then hits a semaphore or
  surface-retarget anchor and waits for the whole burst. The other ~55 % of emulation-thread time
  (idle loop, CPU tier) runs with an empty queue and cannot overlap. **Disproved:** "the threading
  win is bounded by drain frequency" — deeper queues and looser anchors cannot move it.
- **Implication:** deferring rasterization does not help; rasterization must get *faster*. The
  levers are (1) within-job parallelism: partition a job's destination spans across N workers
  (exact by construction — fragments in disjoint spans of one draw do not interact; draws stay in
  order), the ndsrecomp threaded-scanline model; (2) GPU-resident execution (stage f).
- Residual risk recorded: F3 staleness window in `resolve_renderer_color_overlap` before the
  drain; not observed; must close before the flag could default on.


## 8f. Stage (e2) within-job scanline-span parallel rasterization — PASS (2026-09-04)

- All N workers process the same DrawJob on interleaved rows (`y_first`, `y += parts`), barrier
  per job; draw order untouched; per-part sub-accumulators folded in part order; per-draw and
  per-triangle effects on part 0 only; destination swizzle table latched once per job. Exact by
  construction and **byte-locked**: threads 0 vs 4, same binary, 24 checkpoint PPMs and every
  counter identical. The byte-lock caught the per-triangle counter ladder running on every part
  (counts exactly 4×) before any timing was taken.
- **A/B same session, gate = total 12 B wall:** threads 0→4 **−26.8 % median / −27.1 % min**;
  threads 4→8 **−11.8 % / −11.6 %**. Chained **1.55×** (218.0 → 140.2 s), 92 % of the 1.68×
  Amdahl ceiling at N=8; barrier cost 1.4 % and flat, so sync is not the limiter; knee not reached
  at 8 threads. Raster-dominated windows −38 to −45 %; the GL/generic tail flat.
- Presented flips per second in the replay: 3.18 → 4.95 (arm medians).
- Accounting note: `[perf-excl] nv2a_raster_witnessed` now measures part 0 + barrier only
  (42.4 s of 164 s); helper time (118 s) must be reported as a sibling off-thread figure, not
  folded into the emulation-thread partition.
- Default remains off (`--nv2a-draw-threads N`) until the F3 staleness window and M1 guest-read
  detection are closed; harness legs may opt in.
- **Where wall goes at N=8 (est. from the partition):** TCG execution ~82 s is now the largest
  share (~55–60 %); raster part-0 ~42 s; rest ~15 s. The CPU tier is next.


## 8g. CPU tier stage 1 (block hooks) — works, but exposed a host-clock leak (2026-09-04)

- Landed behind guards: `UC_HOOK_BLOCK` only on the canonical path; instruction clock from
  per-TB icount via `UC_HOOK_EDGE_GENERATED` (charged = retired = 12,000,000,002 exactly, zero
  misses); event-based deadline slicing; IF checked at block entry (IRQ landing unchanged in
  kind: irq0 16,342 identical, injected 19,538 vs 19,537); always-on 8192-entry block-PC ring;
  retire diag installs the code hook only on request. Never pass `count>0` then `count<=0` to
  `uc_emu_start`: the resulting `tb_flush` memsets the 1 GiB code buffer and crashes under the
  768 MiB cap.
- **Measured, controlled pair:** `tcg_exec` 50.0 s → 28.0 s (1.78×); end-to-end vs the audit's
  82 s ≈ 2.9×. Whole run n=1 chain 204.9 → 155.3 s (1.32×). Why not more: 4.74 G block entries
  for 12 B instructions = **2.53 instructions per block**; the idle loop (41 % of instructions)
  is 2-instruction blocks by construction, so the block hook fires almost per instruction there.
  Idle fast-forward is confirmed mandatory.
- **Host-clock leak, now load-bearing:** guest `rdtsc` returns the host TSC
  (`misc_helper.c:218` → `x86.c:33` → `cpus.c:55`). The dashboard reads it (11,880 reads per
  12 B). So a faster emulator changes the guest workload: the pre-change exe still reproduces
  the golden, but the same scheduling merely 17 % faster diverges from checkpoint 9.5 B, and
  adding a semantically neutral register read per block (slower) restores the match. The golden
  hash was only ever stable because run speeds were similar.
- **Fix shipped, default on:** guest TSC derived from the retired-instruction clock (per-file
  rename of the pinned `cpu_get_tsc`; `--legacy-host-tsc` restores the old behavior). With TSC
  and PIT finally agreeing, 12 B instructions is 16.4 s of dashboard time instead of a guest
  that saw ~98× more TSC time than PIT time: the old run ends on the main menu with MUSIC
  highlighted, the new run is still inside the CLOCK dialog (`build/cmp-final.png`). 12 of 24
  checkpoints and 9 of 22 profile hashes move; input timeline and IRQ0 pulses do not.
- **Escalated as `beads-xbox.1.14.10` (P0, owner decision):** this is a behavior change, not
  interrupt drift, so §3 does not auto-accept. No new golden written; no timing session run.
- ~~Unexplained: `nv2a_vertex/dispatch/pfifo` +36 % in the block arm~~ **DISPROVED 2026-09-04.**
  A matched `--perf-accounting` pair at `--nv2a-draw-threads 0 --no-nv2a-profile-hashes`
  (`build/perf0-A-oldexe` vs `build/perf0-B-blocktier`) shows those buckets going the OTHER way:
  `nv2a_vertex` 8.244 → 6.072 s (−26 %), `nv2a_dispatch` 4.781 → 3.502 s (−27 %), `nv2a_pfifo`
  2.737 → 2.020 s (−26 %), and −25 % per draw after correcting for the 1.4 % draw-count
  difference. The original figure came from unmatched configurations on (as
  `beads-xbox.1.14.11` now shows) different guest trajectories. The icount-table
  cache-pressure hypothesis has no anomaly left to explain.

## 8h. CPU tier stage 2 (idle fast-forward) and the attribution that ends the CPU tier (2026-09-04)

- `--idle-fast-forward` (`beads-xbox.1.4.7`) retires whole kernel-idle-loop iterations without
  executing them, bounded by `next_device_deadline()`. The loop is 8 instructions in 4 blocks at
  `0x8001e3ce`, verified byte for byte from guest memory on every skip; all four block-boundary
  phases are valid skip points (restricting it to the loop head covered only 9.5 % of a window,
  because a slice boundary preserves the phase an idle stretch was entered at). Measured
  4,918,497,264 instructions skipped in 14,801 skips = 40.99 % of the run and 99.36 % of all idle
  instructions, with `tb_insns + tb_skipped_insns == retired` exactly.
- **Byte-lock INCONCLUSIVE, ships default off.** 5 of 6 legs identical on every quantity; the
  sixth landed on the pre-existing second trajectory of §8i, which the unmodified binary also
  produces. Under the owner rule the flag is default off until §8i is closed.
- **Timing (both gates passed):** threads 0, hashes on: 152.593 → 139.571 s median (+8.53 %);
  threads 8, no hashes: 78.129 → 61.302 s median (+21.54 %), −18.6 % / −17.4 % trajectory-matched.
  The win lands exactly on the census's idle windows and nowhere else.
- **Attribution, matched triple at threads 0** (`build/perf0-*`, unaccounted 0.05 %):
  `tcg_exec` 77.277 s (36.7 %) → 27.956 s (17.9 %) → 14.285 s (10.0 %) across old exe → block
  tier → idle fast-forward, i.e. **5.4× on the CPU bucket**. `nv2a_raster_witnessed` is 98.6 s in
  all three and is now **63–69 % of the run and larger than every other bucket combined**. The
  audit's "68–78 % of wall is not instruction retirement" inference is confirmed (measured 63.3 %
  on the old exe). The CPU tier is finished as a lever.

## 8i. RESOLVED 2026-09-05: the replay was mutating its own input disk

- **Root cause.** `IdeController::attach_hdd` opened the HDD image `"r+b"`. The retail boot
  reads LBA 3 (the Xbox `BRFR` refurb-info sector), increments the boot counter in it and writes
  it back within the first 18 M instructions of every run, so no two runs started from the same
  disk. Verified directly: `xxd -s 1536` on the image reads `BRFR 36 04 00 00` before a 30 M
  run and `BRFR 37 04 00 00` after; the file mtime moves on every run.
- **How it was found**, with three new always-on digests (per-slice trajectory, per-device-access
  value, per-DMA-write content) plus whole-RAM images at guest-anchored counts: trajectory
  divergence at 9.164 B -> device-value divergence 2.9 B earlier at 6.2789 B (the NvNet
  `NvRegRandomSeed` write) -> a 258-byte RC4 state differing in a `NETD` pool block -> the first
  divergent guest-visible byte, `ide 000d48c4 512` at insns 17,957,599 -> two bytes at
  `0x000d48c8` / `0x000d4b04`, `0x35` vs `0x36`, right after the ASCII `BRFR`.
- **Fix.** Copy-on-write HDD attach, default on: image opened `"rb"`, guest writes held in an
  LBA-keyed in-memory overlay and served back to reads in the same run, bounded and
  fault-stopping at the cap. `--hdd-writeback` opts back in. `cow_sectors=`/`writeback=` are on
  the IDE census line. Guards: `tests/test_hdd_cow_guards.py`.
- **What this retires.** Every "correlated but not determined by" and "ruled out" line in the old
  §8i was the right conclusion about the wrong variable: speed, draw threads, idle fast-forward,
  the RTC, the device deadlines and the guest TSC are all genuinely irrelevant, because the input
  that changed was the disk. The `--no-nv2a-profile-hashes` contingency (Fisher p = 0.059) was a
  run-order artefact and is withdrawn.

## 9. Not yet explored

- The guest workload itself (VRML scene + JS VM per-frame instruction budget vs idle).
- Softmmu TLB share of CPU time.
- Interactive present path (33 ms throttle, scalar scanout) — irrelevant until faster.
- Minor devices (APU, IDE, OHCI, NVNet) — measured share < 1 %.
- The stock dashboard's real frame rate on hardware (may itself be < 60).

## 10. Corrections recorded during this audit

- Idle-window EFLAGS poll: bounded to 11 692 slices, < 1 s, not a hot cost.
- "33 % of samples in no module" ≠ JIT; it is all leaves outside `xlle.exe`.
- `direct_surface_bytes` 1.4 TB is logical spans; real payload ~153 MB.
- Manual IDT interrupt delivery is architecturally awkward but < 0.2 % of wall.
