# NV2A producer/consumer seam — design

> Where the CPU thread stops and a render consumer starts, what a job must own, what can
> feed back into guest-visible state, how coherence is detected and fenced, how the toggle-off
> path stays structurally identical, and the ordered staging plan.
>
> Created 2026-09-03. Companions: `docs/PERF-AUDIT-2026-09-02.md` (§3, §5, §8a),
> `docs/reference/testing/PERF-METHODOLOGY.md` (§1 owner decisions, §3 re-baseline rule, §4 threading model,
> §5 order of work). Comparator design:
> `F:\Projects\ndsrecomp\ndsrecomp\docs\device_work_parallelization.md` (the LineJob /
> feedback-table / fence shape this document mirrors) and
> `F:\Projects\ndsrecomp\ndsrecomp\runner\src\emu_profile.h` (exclusive always-on accounting).
>
> Tags: **[verified]** read in source at the cited line during this session, or re-derived
> from an artifact; **[inferred]** reasoned but not measured; `unknown` = not determinable
> with today's instruments.
>
> Owner mandate under which this was written: threading approved on the ndsrecomp model;
> golden hash may be re-baselined under methodology §3; hardware frame capture deferred until
> a hard blocker; CPU-vs-CPU threading out of scope. **This document changes no source.**

---

## 0. The measured situation this design has to answer to

| Fact | Value | Source |
|---|---|---|
| Wall time not attributable to instruction retirement | 68–78 % | audit §8a |
| Witnessed software raster | 114.6 s / 251 s (45.6 %) | audit §2 |
| Generic combiner raster | 18.7 s (7.5 %) | audit §2 |
| `dispatch_method` minus raster | 17.4 s (6.9 %) | audit §2 |
| Fragments through the 15 CPU-only witnessed profiles | 98.2 % of 8.746 G | audit §5 |
| `renderer_resolve` : `renderer_draw` | 3 998 ms : 4.2 ms (940:1) | audit §5 |
| Overdraw | ~41× (12.6 M frags / 307 200 px per presented frame) | audit §5 |
| Witnessed pass rate | 19.6 % (radial: 4.1 G shaded, 727 M written) | audit §5 |
| Host threads today | 3 (emulation, fixture-only debug accept, sample profiler); zero mutex/condvar/queue | audit §3 |

Two structural facts set the shape of the seam:

1. **All device work runs inside a guest store.** `Nv2a::mmio_write` → `process_pushbuffer`
   (`src/machine/nv2a.cpp:604`, `:628`) → `drain_pull_queue` (`:10795`) → `dispatch_method`
   (`:10072`) → `submit_vertex_batch` (`:1209`) → `rasterize_witnessed_batch` (`:2643`) — all
   on the Unicorn MMIO callback stack, before the guest `mov` retires. [verified]
2. **The witnessed region has no renderer call.** `rasterize_witnessed_batch` spans
   `nv2a.cpp:2643-5845` and contains zero `renderer_->submit_*`; the only submit sites are
   `:8323` (resident) and `:8425` (non-resident) inside the generic-combiner path
   (`rasterize_generic_combiner_batch`, `:5942`). [verified — grepped the whole file for
   `renderer_->submit`; only 8323/8425 match]

So the GL path is a sidecar, and 98 % of the work is a single-threaded scalar loop that reaches
guest memory directly. The seam therefore has to be placed inside the *software* path, not at the
renderer interface.

---

## 1. THE SEAM

### 1.1 Where the boundary goes, and why not one level up or down

Three candidate cut points, evaluated:

**(A) PFIFO word level — REJECTED.** `process_pushbuffer` (`nv2a.cpp:10833`) mutates
`ChannelState` (`get`, `put`, `method`, `method_count`, `subchannel`, `non_incrementing`,
`subroutine`, `subroutine_active`) and republishes the consumed offset into **two guest-readable
apertures** at `:10917` (`pfifo_regs_[0x1244] = ch.get`) and, on the USER path, at `:622-628`.
The comment at `:10914-10916` states the contract explicitly: "the synchronous parser has
completed by the time the CPU can read either aperture." Moving the parser off-thread turns a
guest register read into a race. The parser also faults the machine on malformed streams
(`:10847`, `:10875`, `:10894`, `:10900`, `:10906`) and `fault_` is an emulator-level stop.
It stays on the CPU thread.

**(B) Method-semantics level (a "command stream" of decoded methods) — REJECTED as the primary
seam.** `dispatch_method` (`nv2a.cpp:10072-10782`) is 130 top-level `if/else if (method …)` arms
at 4-space indentation, 148 `} else if (` tokens including nested, and **zero** `switch`
[verified by `awk NR>=10082 && NR<=10782 | grep -c`]. It mutates ~90 register mirror words
(`raster_state_`, `nv2a.hpp:1752`, indices enumerated `nv2a.hpp:1060-1125`) plus dozens of scalar
members, and each method's effect is read by the *next* method. There is no useful parallelism in
the decode itself, it is only 17.4 s of 251 s (audit §2), and it carries the recorder
(`methods_.push(push)` at `:10089`, clear-trace capture `:10091-10119`, `method_counts_` at
`:10123`) whose ordering is part of the diagnostic contract. Decode stays on the CPU thread.
(Its 17.4 s is attacked by staging step (a), the jump table, not by threading.)

**(C) Draw-batch level — CHOSEN.** The producer/consumer boundary is placed **inside
`submit_vertex_batch` (`nv2a.cpp:1209`), immediately after vertex transform completes and
`transformed_batch_` (`nv2a.hpp:1772`) is populated, and immediately before
`rasterize_witnessed_batch` is called (`nv2a.cpp:1674`).**

```
guest mov  →  mmio_write  →  process_pushbuffer  →  drain_pull_queue  →  dispatch_method
                                                                              │
                                                          method 0x17FC(0) ───┤
                                                                              ▼
                                                                    submit_vertex_batch
                                                                       │  (CPU thread)
                                                       vertex fetch ───┤  reads guest arrays
                                                       VSH / fixed  ───┤  fills transformed_batch_
                                                                       │
                                        ══════════ LATCH ══════════════╪══ THE SEAM
                                                                       │
                                                       profile match ──┤  (CPU thread: cheap,
                                                       surface resolve─┤   and it faults)
                                                       texture copy ───┤
                                                                       ▼
                                                                  DrawJob (owned)
                                                                       │
                                                        ┌──────────────┴───────────────┐
                                                   inline mode                    threaded mode
                                                   execute_draw_job()             ring → worker
                                                   here, now                      execute_draw_job()
                                                        └──────────────┬───────────────┘
                                                                       ▼
                                                          telemetry merge, in job order,
                                                          on the CPU thread, at drain
```

**Why after transform, not before.** Vertex transform (`nv2a.cpp:1459-1465` timed bucket
`perf::g_ns_nv2a_vertex_transform`) calls `fetch_vertex_attribute`, which reads guest memory
through `mem_direct_` or `mem_read_` (`nv2a.cpp:1005-1017`). Keeping it on the CPU thread means
**guest vertex arrays are never a coherence hazard** — the job carries transformed floats, not
addresses. It also keeps `transform_output_hash_` (`nv2a.hpp:1727`, folded at `nv2a.cpp:1447-1455`)
trivially in order. The cost of that choice is that transform stays serial; it is a small share of
the raster time and can be moved into the job later once the fence census (§6) says vertex-array
pages are not touched mid-frame. **Recommendation: transform stays on the CPU thread through
stage (e); revisit at stage (f).**

**Why after profile matching and surface resolution.** The profile ladder
(`nv2a.cpp:3206-3252`) and the per-profile subset validators (`:3277-3520`) can call `fault_`,
which stops the machine (`:3304` is the documented fault-stop for matched-but-out-of-subset).
Faults must be raised on the CPU thread, in guest order, at the instruction that caused them.
Likewise `resolve_surface` (`nv2a.cpp:3626-3650`) and the DMA-range validators fault. Everything
that can fault runs at latch, on the CPU thread. **The job executor is not allowed to fault**;
it can only fail, and a failure is reported at the next drain as a fault with the latched
job's identity (§1.4, F13).

### 1.2 What a batch job must latch — the DrawJob inventory

This mirrors `device_work_parallelization.md` §4's `LineJob` inventory item for item. Every
member is something the fragment loop reads today and that the CPU thread can change before a
worker runs.

```
struct DrawJob {
    // --- identity / ordering -------------------------------------------------
    uint64_t seq;                    // vertex_batches_ at latch; job order == draw order
    uint8_t  profile;                // Profile enum value (nv2a.cpp:2647-2664) or kGeneric
    uint8_t  primitive_mode;         // primitive_mode_ (5/6/7 witnessed, 8 immediate quad)

    // --- register mirrors ----------------------------------------------------
    std::array<uint32_t, kRasterStateWords> raster_state;   // full copy, nv2a.hpp:1752
    uint32_t surface_format, surface_pitch;                 // nv2a.cpp:3554-3612 inputs
    uint32_t surface_clip_horizontal, surface_clip_vertical;
    uint32_t surface_color_offset, surface_zeta_offset;

    // --- derived surface descriptors (computed at latch, nv2a.cpp:3546-3690) --
    uint32_t color_address, depth_address;   // resolved through dma_object_address
    uint32_t pitch, surface_width, surface_height;
    uint32_t clip_x, clip_y, clip_width, clip_height;
    uint64_t color_bytes, depth_bytes;
    bool     uses_depth, writes_depth, color_profile, inline_depth_test;

    // --- destination ownership -----------------------------------------------
    uint8_t* color_direct;           // mem_direct_(color_address, color_bytes)
    uint8_t* depth_direct;           // mem_direct_(depth_address, depth_bytes)
    // NON-NEGOTIABLE: a threaded job requires BOTH to be non-null. See §1.3.

    // --- vertex data ownership -----------------------------------------------
    std::vector<TransformedVertex> vertices;   // moved out of transformed_batch_

    // --- texture source bytes (already owned copies today) -------------------
    std::vector<uint8_t> texture_data;             // nv2a.cpp:3745
    std::vector<uint8_t> projected_texture_data;   // nv2a.cpp:3804
    std::vector<uint8_t> linear_blit_texture_data; // nv2a.cpp:3859
    std::vector<uint8_t> multi_texture_data;       // nv2a.cpp:3920
    std::vector<uint8_t> single_texture_data;      // nv2a.cpp:3966
    std::vector<uint8_t> pitch_tex5_texture_data;  // nv2a.cpp:4003
    std::vector<uint8_t> inline_texture_data;      // nv2a.cpp:4069
    std::vector<uint8_t> palette_data;             // immediate quad, nv2a.cpp:2218
    uint32_t texture_width, texture_height, projected_texture_width, …;
    // Swizzle tables are pure functions of (width,height) — rebuild per job or
    // share an immutable cache; SwizzleLookup (nv2a.cpp:167-196) is const after
    // construction, so a shared const instance is thread-safe.

    // --- combiner words -------------------------------------------------------
    // Already inside raster_state: kRasterCombinerAlphaIcw0 … kRasterCombinerFinalFactor1
    // (nv2a.hpp:1115-1124). No separate latch needed. Same for
    // kRasterShaderStageProgram / kRasterShaderOtherStageInput / kRasterCombinerControl
    // / kRasterDotRgbMapping (nv2a.hpp:1093-1096).

    // --- telemetry sink -------------------------------------------------------
    DrawJobTelemetry out;            // per-job counters + per-profile FNV, merged in order
};
```

**Inventory notes, item by item:**

| Latched item | Today's source | Why it must be latched |
|---|---|---|
| `raster_state` (≈90 words) | read live per fragment from `raster_state_` (`nv2a.hpp:1752`) at `nv2a.cpp:3206-3252`, `:3277-3520`, and inside the fragment loop (e.g. `kRasterCombinerFactor0_0` at `:5640-5658`) | a later method in the same pushbuffer batch can rewrite any of them before a worker runs |
| `surface_format_/pitch_/clip_*/color_offset/zeta_offset` | scalars read at `nv2a.cpp:3554-3612` | same; SET_SURFACE_* methods are frequent |
| color/depth addresses | `resolve_surface` at `:3626-3650` via `dma_object_address` (`:635`), which reads `pramin_` | PRAMIN is guest-writable (`nv2a.cpp:607-613`); resolution must happen at latch |
| `transformed_batch_` | one shared `std::vector` member (`nv2a.hpp:1772`), `clear()`ed at `:1406` on every batch | the very next batch overwrites it — this is the single most dangerous shared buffer |
| texture bytes | `mem_read_` into function-local vectors at `:3745/3804/3859/3920/3966/4003/4069` | already owned copies; **move** them into the job instead of copying again |
| palette bytes | `PagedRasterSurface palette_surface` at `:2218` | immediate-quad I8/menu/artwork profiles read guest palette memory per fragment |
| destination pointers | `PagedRasterSurface::direct_data()` at `:4094-4095` | see §1.3 |
| `primitive_mode_` | `nv2a.hpp` scalar, cleared at `:10705-10717` | 0x17FC(0) clears it immediately after the draw |
| telemetry | ~40 `uint64_t` counters + 22 FNV accumulators, incremented inline | §4 |

**Function-local mutable statics** (ndsrecomp F16): grepped — the witnessed and generic raster
bodies use no `static` locals; the per-triangle scratch (`vertex_diffuse_clamped` at
`nv2a.cpp:4513`, `BatchTransformCache` at `:1411-1418`) is already stack/`std::optional` local
and per-invocation. **No hoisting needed.** [verified]

### 1.3 The direct-pointer constraint (a hard finding)

`PagedRasterSurface` (`nv2a.cpp:37-165`) takes `mem_read_`/`mem_write_`/`mem_direct_` **by
const reference** (`nv2a.cpp:163-167`) and is non-copyable, non-movable (`:57-60`). Those
callbacks are wired in `machine.cpp:148-159`:

* `mem_read_` → `cpu_->read_mem` → `uc_mem_read`
* `mem_write_` → `cpu_->write_mem` → `uc_mem_write`
* `mem_direct_` → a raw `ram_.data() + addr` for the two RAM aliases

`uc_mem_read` / `uc_mem_write` are Unicorn API calls; they are **not safe to call from another
thread while `uc_emu_start` is running on the emulation thread** (they take the engine's
JIT-state save/restore path, `uc.c` `UC_INIT` / `restore_jit_state`). [verified by reading
`uc.c:1708+` and the `restore_jit_state` calls; the API has no documented concurrency contract]

Therefore:

> **A DrawJob may be executed off-thread only if `mem_direct_` returned non-null for every
> surface and every guest-memory region the job touches.** If any region falls outside the two
> RAM aliases (`machine.cpp:154-158`), the job is executed **inline**, at the latch point, on the
> CPU thread. This is not a fallback for correctness convenience — it is the mechanism that keeps
> the Unicorn API single-threaded.

This is affordable: audit §5 correction records **874 k of 876 k** paged surfaces taking the
zero-copy pointer path. [verified from the audit; re-measure as part of stage (d) — the exact
counter is `perf::record_paged_surface_direct` at `nv2a.cpp:50`]

A second consequence: the threaded executor should not construct a `PagedRasterSurface` at all.
The witnessed loop already has the direct-pointer fast path in the form of `read_depth32` /
`write_depth32` / `write_color32` (`nv2a.cpp:4096-4116`), which branch on
`depth_direct`/`color_direct`. The job executor uses **only** those lambdas, with the paged
fallback compiled out of the threaded path.

### 1.4 FEEDBACK PATHS — every route from render output back to guest-visible state

This is the ndsrecomp §3 table for xboxlle. Every row is a path that must be handled explicitly
or the seam is silently wrong.

| # | Path | Site(s) | Handling |
|---|---|---|---|
| **F1** | **Rasterizer writes unified RAM directly.** The witnessed depth-only profile writes 16-bit depth (`write16` at `nv2a.cpp:4588`); every color profile writes 32-bit color and (mostly) depth (`:4691`, `:4949-4951`, `:5100-5101`, `:5142`, `:5272`, `:5431`, `:5681-5683`). Through `direct_` this is a plain `memcpy` into `ram_` (`nv2a.cpp:126-131`). | `nv2a.cpp:4588`, `4691`, `4949`, `5100`, `5142`, `5272`, `5431`, `5681` | **This is the job's whole purpose.** Guest-visible, but the guest cannot observe it except through F4/F5/F6/F7/F8. Jobs own disjoint destination *surfaces*, not disjoint pixels — two jobs to the same surface **must** execute in job order. See §1.5 (ordering). |
| **F2** | **`PagedRasterSurface::flush()` writeback.** When `direct_` is null the surface buffers 4 KiB pages and pushes them back with `write_` at end of batch (`nv2a.cpp:89-101`, called `:5776`, `:5780`, `:2621`, `:9795`, `:9816`). | `nv2a.cpp:89-101` | **Never threaded** — `write_` is `uc_mem_write` (§1.3). Any job needing this path executes inline. |
| **F3** | **Resident-surface resolve/invalidate.** After a software batch writes guest memory, `invalidate_renderer_color_overlap` / `_depth_overlap` (`nv2a.cpp:5784-5788`, `:265-271`, `:340-346`) tell the GL backend its copy is stale; before a software batch reads, `resolve_renderer_color_overlap` (`:230-265`) pulls the GPU copy back into guest RAM through `mem_write_`. Also `upload_resident_color_rows` (`:278-300`) using `written_begin()/written_end()`. | `nv2a.cpp:230-350`, `4082-4087`, `5784-5788` | **Emulation thread only, in order.** Resolve runs at latch (before the job is queued); invalidate runs at the drain that retires the job. Both touch `renderer_` and `mem_write_`. |
| **F4** | **`CLEAR_SURFACE` reads and writes the same surfaces.** `clear_surface_plane` (`nv2a.cpp:701-798`) does a row-wise read-modify-write through `mem_read_`/`mem_write_` (`:771`, `:788`) and calls `resolve_renderer_*_overlap` at `:761-763` and `invalidate_*` at `:796-797`. | `nv2a.cpp:701-798`, dispatched from `dispatch_method` | **Full drain before, then execute on the CPU thread.** A clear is a write to the same destination range as pending jobs and is ordered against them. Cheap: clears are far rarer than draws. |
| **F5** | **Scanout capture reads guest RAM.** `capture_scanout` (`nv2a.cpp:10950-11021`) resolves resident color (`:10975`), then `mem_read_(info.start, …)` (`:10984`) over the whole visible framebuffer. Fed to `write_frame_ppm` (`main.cpp:286`) and to the scanout RGBA FNV-64 (`main.cpp:3902`). | `nv2a.cpp:10950-11021` | **Full drain first.** This is a guest-anchored gate point (§4) and happens at most once per frame-series checkpoint plus once per flip. |
| **F6** | **Texture reads of a surface rendered in the same frame.** The witnessed texture snapshots (`nv2a.cpp:3745`, `3804`, `3859`, `3920`, `3966`, `4003`, `4069`) and the immediate-quad texture/palette surfaces (`:2213-2225`) read guest RAM at arbitrary addresses — which may be a color surface a pending job is writing. The immediate-quad path already **detects** the aliasing case and refuses the draw (`nv2a.cpp:2203-2210`, comment at `:2198-2202`: "an aliased texture would otherwise observe undefined ordering between their independent page caches"). | `nv2a.cpp:2198-2210`, `3745-4075` | **Range check at latch: if the texture source span intersects any pending job's destination span, drain first, then snapshot.** The check is the same overlap arithmetic already used by `check`-style code at `:3670-3675`. Cost is one linear scan over ≤ N pending jobs. **Frequency is `unknown` — measurement M3, §6.** |
| **F7** | **Guest CPU reads rendered VRAM.** Structurally undetectable today: `UC_HOOK_MEM_READ/WRITE` is installed only on the fixture path (`unicorn_floor.cpp:78-89`), and `uc_mmio_map` (`:186-197`) covers only device windows, not RAM. RAM is `uc_mem_map_ptr` twice (`machine.cpp:36-37`) so TCG reads go straight to `ram_` with no callback. | `unicorn_floor.cpp:78-89`, `186-197`; `machine.cpp:36-37` | **§2. This is the open coherence question.** Shipping policy: conservative anchor drains (§2.4). |
| **F8** | **Guest CPU *writes* memory a pending job reads** (vertex arrays, textures, PRAMIN DMA objects, the surface itself). Same detection gap as F7. | same | Vertex arrays and DMA objects are consumed **at latch** (§1.1), so they are not a hazard. Textures are snapshotted at latch (F6). The surface itself is F7's problem. |
| **F9** | **PFIFO GET/PUT republication.** `pfifo_regs_[0x1244] = ch.get` (`nv2a.cpp:10917`), USER `0x44`/`0x40` (`:620-628`), and `pgraph_surface()` (`:10926-10930`) are guest-readable. | `nv2a.cpp:10917`, `620-628` | Parser stays on the CPU thread (§1.1) — unaffected by the seam. |
| **F10** | **PGRAPH stall flags.** `waiting_for_flip_` (`nv2a.cpp:10243`), `waiting_for_nop_` (`:10224`), `pgraph_fifo_access_` (`:586`) gate `drain_pull_queue` (`:10796`) and `resume_pushbuffers` (`:10785`). `flip_read_`/`flip_write_` are advanced by guest register writes (`:563-566`) and by method 0x0130/0x0134 (`:10228-10243`). | `nv2a.cpp:563-576`, `10224-10243`, `10784-10792` | CPU thread only. But **a flip stall must drain**: the guest is being told the GPU is done. Add a drain at the top of `resume_pushbuffers` and at the point `waiting_for_flip_` clears (`:566`). |
| **F11** | **Semaphore release writes guest memory.** Method `0x1D70` (`nv2a.cpp:10751-10777`) writes a 4-byte completion token through `mem_write_` at `:10768`. `AvSetDisplayMode` uses it as a GPU completion fence (comment `:10750`). | `nv2a.cpp:10751-10777` | **Full drain before the write.** A semaphore release is literally the guest asking "is the GPU finished?"; answering it while jobs are pending is the one unambiguous correctness bug this seam could introduce. |
| **F12** | **Interrupts.** `pcrtc_intr_ |= 1` in `set_frame` (`nv2a.cpp:11048`), `pgraph_intr_`/`pgraph_intr_en_` (`:576-582`), `pmc_intr_status` (`:11026-11031`), `update_irq` → `irq_cb_` → `pic_->raise` (`machine.cpp:163`). Delivery happens at slice boundaries (`machine.cpp:398-410`). | `nv2a.cpp:11026-11052` | CPU thread only, and `set_frame` is a **drain point** (F5/F13). Interrupt *timing* must not move: it is derived from `total_insns_` (`machine.cpp:375-381`) and is part of the byte-lock gate (§4). |
| **F13** | **Faults.** `fault_` from inside the raster body (`nv2a.cpp:4583`, `4589`, `4721`, `4998`, `5167`, `5385`, `5433`, `5684`, …) stops the machine with a message; `main.cpp` treats a fault as a hard stop and the gate requires empty stderr (methodology §2.1). | throughout `2643-5845` | **The executor may not call `fault_`.** It sets a `failed` flag plus a reason string in `DrawJobTelemetry`; the next drain, on the CPU thread, raises `fault_` with that reason. This delays the fault by at most one drain interval and changes *which instruction* it is attributed to. Threaded-vs-inline byte-lock must therefore also compare **fault-free-ness**, and any run that faults is re-run inline to get the true attribution. |
| **F14** | **Per-profile telemetry.** 22 order-dependent FNV-1a accumulators (`nv2a.hpp:1424-1656`, e.g. `raster_output_hash_` folded at `nv2a.cpp:4599-4600`, `radial_color_output_hash_` at `:5698-5701`), ~40 counters incremented in a 15-way ladder (`nv2a.cpp:4536-4560`) and a per-profile batch ladder (`:5807-5845`), plus `last_*_raster_address_` scalars. | `nv2a.cpp:4536-4560`, `4599`, `5650-5745`, `5807-5845` | **§4.3.** Recommendation: per-job accumulator, merged on the CPU thread in job order at drain — bit-identical to today. |
| **F15** | **Debug callbacks.** `surface_write_debug_cb_` (`nv2a.cpp:5789-5813`), `generic_batch_debug_cb_`, `generic_packet_snapshot_cb_` (`nv2a.hpp:1757-1762`), the PPM writers driven from them (`main.cpp:1180-1189`, `:1478-1488`). | `nv2a.cpp:5789-5813` | Fire on the CPU thread at drain, in job order, from latched `SurfaceWriteDebugInfo`. When any is installed, force inline mode (they are diagnostic-only paths and not perf-relevant). |
| **F16** | **Method / batch recorders.** `methods_` ring (`nv2a.cpp:10089`), `method_counts_` (`:10123`), `raster_state_samples_` (`:1234-1253`), `clear_trace_*` (`:10091-10119`), `Ring<uint32_t> scanout_starts_` (`nv2a.hpp:1782`). | `nv2a.cpp:10089-10123`, `1234-1253` | All on the CPU thread at latch. `raster_state_samples_` holds a **raw pointer** `tracked_raster_sample` into a `std::vector` that can reallocate (`nv2a.cpp:1247-1252`) — pre-existing hazard, unchanged by the seam, but worth a note: the job must carry `current_raster_state_sample_index_` by value, never the pointer. |

**Checked and clear** (recorded so it is not re-derived): `SwizzleLookup` (`nv2a.cpp:167-196`) is
immutable after construction and safe to share read-only; the `vertex_diffuse_clamped` and
`BatchTransformCache` scratch are per-invocation locals; there are no function-local `static`s in
the raster bodies; and `perf::` counters are `std::atomic<uint64_t>` already (`perf_accounting.hpp:47+`)
so worker `fetch_add`s do not tear, though they do become genuinely contended (measure the cost;
prefer per-job accumulation and one merged add).

### 1.5 Ordering contract

* Jobs retire in **latch order**, always. Two jobs writing the same surface must not be
  reordered; the witnessed profiles do read-modify-write blending (`nv2a.cpp:5136-5142`,
  `:5251-5272`, `:5397-5431`) and depth-test-against-stored (`:4581`, `:4719`, `:4996`, `:5165`,
  `:5458`), so pixel results depend on the order of preceding draws to the same address.
* With **one worker** this is free. With N > 1 workers, jobs whose destination spans intersect
  must be serialized. Recommendation: **start with one worker** (ndsrecomp shipped one worker and
  measured −15.5 % / −4…6 % with it; more workers were never swept). A second worker requires
  the overlap-partition logic and is a separate bead.
* The emulation thread **executes pending jobs itself while draining** (ndsrecomp §5.2), so a
  pathological drain pattern degrades to single-threaded cost plus ring overhead, never to a
  stall and never to a wrong result.

---

## 2. COHERENCE

### 2.1 What exists today: nothing

* `UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE` is installed **only** by `UnicornFloor::on_mem`
  (`unicorn_floor.cpp:77-89`), which is called only by the fixture path; `main.cpp:4173-4177`
  returns before reaching it under `--boot`. [verified — audit §3, and the comment at
  `unicorn_floor.cpp:80-82` states the intent: "Normal boot replay has no RAM observer."]
* `uc_mmio_map` (`unicorn_floor.cpp:186-197`) is called only for device windows: NV2A at
  `0xFD000000`, APU, ACI, NVNet, etc. (`machine.cpp:166-230`). No RAM range is MMIO.
* Guest RAM is `uc_mem_map_ptr(0, kRamSize, ram_.data())` **and** aliased at `kRamSize`
  (`machine.cpp:36-37`) — both to the same host pointer. TCG loads/stores hit the host page
  directly through the TLB fast path.

So the question "does the guest CPU read rendered VRAM?" is **structurally undetectable**
(audit §5, §8 item 10), and so is "does the guest write a surface a job is about to read".

### 2.2 Candidate mechanisms and their real cost class

#### (a) Unicorn per-access mem hooks bounded to VRAM/framebuffer ranges — **REJECTED for production**

The hope is that `uc_hook_add(…, UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE, cb, this, begin, end)` with
`begin/end` set to the framebuffer range costs only for accesses in that range. **It does not.**
Three verified sites make the cost global:

1. `qemu/tcg/i386/tcg-target.inc.c:1765-1769` —
   ```c
   // Unicorn: fast path if hookmem is not enable
   if (!HOOK_EXISTS(s->uc, UC_HOOK_MEM_READ) && !HOOK_EXISTS(s->uc, UC_HOOK_MEM_WRITE))
       tcg_out_opc(s, OPC_JCC_long + JCC_JNE, 0, 0, 0);
   else
       /* slow_path, so data access will go via load_helper() */
       tcg_out_opc(s, OPC_JMP_long, 0, 0, 0);
   ```
   With any such hook present, the TLB-compare branch is replaced by an **unconditional jump to
   the slow path for every guest load and store in every translated block.**
2. `qemu/target/i386/translate.c:547-548` and `:557-558` — `gen_op_ld_v` / `gen_op_st_v` emit
   `gen_sync_pc` before every load / store when the corresponding hook exists.
3. `qemu/accel/tcg/cputlb.c:1206-1214` — the TLB entry is never returned to the clean/dirty
   fast state while these hooks exist.

`HOOK_EXISTS` is `((uc)->hook[idx##_IDX].head != NULL)` (`include/uc_priv.h:243`) — a *list is
non-empty* test, with **no** range awareness. `HOOK_BOUND_CHECK` (`uc_priv.h:238-241`) is applied
only *inside* `load_helper`/`store_helper`, after the access has already taken the slow path
(`cputlb.c:1597`, `:2111`). [all verified in `build/_deps/unicorn-src`]

Cost class: this is the same class of change as the always-on `UC_HOOK_CODE` that already costs
17–58× (audit §4 measured table). For memory it means every load/store goes through
`load_helper`. **Expected 3–10× slowdown of the CPU tier** [inferred from the structure; the
measured analogue is H3-vs-H2 in the bench table]. Unusable as a production coherence mechanism.

**Still useful as a one-off census instrument** (§6, M1): build a `--vram-access-census` variant
with the hook installed from process start and an always-on `Ring` of `(eip, addr, size, rw)` for
accesses inside the surface set. It is slow but it is *always-on for the whole run*, guest-anchored,
and it answers M1 exactly once. That is a census, not an arm-then-capture window.

#### (b) Map rendered surface pages as MMIO while a job is pending — **REJECTED**

`uc_mmio_map` calls `mem_map_check` → `memory_overlap` and returns `UC_ERR_MAP` if the range is
already mapped (`uc.c:1305-1313`, `:1226-1256`). So the sequence per epoch is: `uc_mem_unmap` the
surface range out of the 64 MB ptr-mapped RAM region, `uc_mmio_map` it, then reverse. `uc_mem_unmap`
of a sub-range drives `split_region` (`uc.c:1438-1530`), which **deletes the whole 64 MB
MemoryRegion and re-creates up to three** — and the RAM is mapped **twice** (`machine.cpp:36-37`),
so every transition does this twice. Each deletion invalidates the TLB and the TBs covering the
region.

Cost class: hundreds of microseconds to milliseconds per transition, plus TB retranslation
[inferred from the code shape; not measured]. At ~17 825 accelerated draws and ~876 k paged
surfaces per run this is catastrophic at draw granularity. It might be tenable at **frame**
granularity if the surface set is stable, but it buys nothing that (c) or (d) do not.
**Rejected.**

#### (c) Conservative drain at guest-anchored NV2A anchors — **SHIP THIS**

Drain the job queue (executing pending jobs on the emulation thread if no worker has taken them)
at every point where the guest could observe rendered state, without needing to detect a raw
memory access:

| Anchor | Site | Rationale |
|---|---|---|
| Semaphore release (0x1D70) | `nv2a.cpp:10751-10777` | explicit GPU-completion fence (F11) |
| `CLEAR_SURFACE` | `nv2a.cpp:701-798` | RMW on the same destination (F4) |
| Scanout capture | `nv2a.cpp:10950` | reads the framebuffer (F5) |
| `set_frame` (vblank) | `nv2a.cpp:11044-11052` | frame boundary; also where PPMs/hashes are taken (F12) |
| Flip stall clear / `resume_pushbuffers` | `nv2a.cpp:563-566`, `:10784` | the guest is being told the GPU finished (F10) |
| Texture snapshot overlapping a pending destination | `nv2a.cpp:3745-4075`, `2203` | render-to-texture within a frame (F6) |
| Any PGRAPH register **read** (`blk->off == 0x400000`) | `Nv2a::mmio_read` | conservative catch-all for status polling |
| Any `resolve_renderer_*_overlap` | `nv2a.cpp:230`, `:315` | GL/software mixing (F3) |
| Any surface descriptor change (SET_SURFACE_COLOR_OFFSET / ZETA_OFFSET / FORMAT / PITCH) | `dispatch_method` | retarget = potential aliasing of a pending destination |
| Frame-series checkpoint, debug-server read, shutdown | `main.cpp:2153+` | gate points (§4) |

**What it does not cover:** a plain guest `mov eax,[framebuffer]` with no NV2A register access
around it. That is exactly measurement M1. Xbox title code that reads back the framebuffer without
touching PGRAPH is possible (software cursor composition, screenshot code, CPU-side blits), and the
stock dashboard's behaviour here is `unknown`.

Determinism: **exact**, provided every anchor drains. Guest-visible state at every anchor is
identical to inline mode by construction. The residual risk is an *unenumerated* anchor, which is
why M1 exists and why the byte-lock gate (§4) is mandatory before the flag is ever defaulted on.

#### (d) Host page-protection guard on `ram_` — **THE RIGHT LONG-TERM MECHANISM**

Because guest RAM is a plain host allocation (`std::vector<uint8_t> ram_`, `machine.cpp:31`)
handed to Unicorn by pointer (`:36-37`), the host can `VirtualProtect` the pages backing a pending
job's destination surface to `PAGE_READONLY` (or `PAGE_NOACCESS`) and service the resulting access
violation in a vectored exception handler: drain, unprotect, resume the faulting instruction.
Precedent exists in this codebase — the 1 GiB TB cache is "lazily committed via a process-wide
VEH" (audit §4) [verified as a claim in the audit; the VEH implementation itself was not re-read
this session — **[inferred]** that the same mechanism generalizes].

Properties:
* **Zero cost when the guest does not touch the pages.** No TCG deoptimization at all; the fast
  path is unchanged because Unicorn knows nothing about it.
* Cost class: one `VirtualProtect` per surface per epoch (a syscall, ~1–5 µs) plus one VEH
  dispatch per first touch per page per epoch (~2–10 µs) [inferred].
* Detects **reads** as well as writes (`PAGE_NOACCESS`), which (c) cannot.
* Determinism: guest-visible behaviour is unchanged — the fault is invisible to the guest, the
  instruction re-executes and sees the drained state. **Exact.**

Complications to design against, in order of danger:
1. Our own host code writes those pages through `mem_write_`/`mem_direct_`. The VEH must handle
   faults from `xlle.exe` frames as well as from JIT frames, or (better) all emulator-side writers
   unprotect first.
2. QEMU's dirty-bitmap machinery writes to `ram_` from `store_helper`'s notdirty path — a fault
   there must be serviceable.
3. Interaction with Unicorn's own VEH for the TB cache: handler ordering matters; register with
   the correct priority and chain.
4. Windows-only. A POSIX port needs `mprotect` + `SIGSEGV`.
5. Page granularity is 4 KiB; a 640×480×4 surface is 300 pages, so protecting/unprotecting is a
   loop over a range, not per page.

**Recommendation:** ship (c) as the default policy at stage (e). Build (d) as its own bead after
M1 says how often the guest actually touches surface pages — if M1 says "never", (c) alone is
provably sufficient and (d) is dead weight; if M1 says "often but only via NV2A-adjacent code",
(c) covers it; if M1 says "often, in code with no NV2A access nearby", (d) becomes mandatory
before the flag can default on.

### 2.3 Comparison table

| Mechanism | Detects | Cost when idle | Cost per event | Determinism | Blocker |
|---|---|---|---|---|---|
| (a) Unicorn mem hooks | reads + writes, everywhere | **3–10× whole-CPU-tier slowdown** | cheap | exact | fatal cost |
| (b) `uc_mmio_map` surface pages | reads + writes, in range | zero | 100 µs–ms + TB flush, ×2 aliases | exact | cost + region churn |
| (c) Anchor drains | nothing directly; covers NV2A-mediated observation | zero | one drain | exact **iff** anchors are complete | completeness is unproven → M1 |
| (d) Host page protection | reads + writes, in range | zero | ~1–10 µs first touch per page | exact | VEH interaction, Windows-only |

### 2.4 What must be measured to choose

See §6. The decision rule, stated in advance so it is not re-litigated:

* **M1 = 0 surface-page touches outside NV2A-adjacent code** → (c) alone; (d) is not built.
* **M1 > 0 but all within an NV2A MMIO access window** → (c) alone, with the specific anchor added.
* **M1 > 0 in unrelated guest code** → (d) is a prerequisite for defaulting the flag on; (c)
  remains the mechanism for the default-off shipping state.

---

## 3. TOGGLE-OFF EQUIVALENCE

The requirement (methodology §4): *toggle-off executes each job inline at the latch point — the
same code in the same order — so the faithful path stays structurally identical, not merely
forceable.*

### 3.1 Shape

```c++
// inside submit_vertex_batch, replacing the direct call at nv2a.cpp:1674
DrawJob job;
if (!latch_draw_job(job, executed)) return false;   // may fault; CPU thread
if (job.kind == DrawJob::kSkip) return true;        // recorder-only families

if (!threaded_ || !job.threadable) {
    execute_draw_job(job, job.out);                 // <-- identical call
    retire_draw_job(job);                           // telemetry merge + F3/F15
} else {
    ring_.publish(std::move(job));                  // worker calls the SAME function
}
```

`execute_draw_job` is the **only** implementation of the pixel math. There is no second copy, no
`#ifdef`, no "software fallback" variant. Inline mode differs from threaded mode in exactly two
things: who calls `execute_draw_job`, and when `retire_draw_job` runs relative to the next guest
instruction. Everything the executor reads it reads out of the job.

### 3.2 How the 15 witnessed profiles become job executors without changing the pixel math

`rasterize_witnessed_batch` (`nv2a.cpp:2643-5845`) is today one function that does four things:

1. **Match** — profile fingerprint ladder (`:2666-3252`).
2. **Validate** — per-profile subset guards that `fault_` (`:3277-3520`).
3. **Set up** — surfaces, DMA resolution, texture snapshots, swizzle tables (`:3540-4125`).
4. **Execute** — triangle setup (`:4400-4530`) + the shared fragment loop (`:4523-5745`) +
   flush/invalidate/telemetry (`:5776-5845`).

The refactor splits it at the (3)/(4) boundary, mechanically:

* Steps 1–3 become `latch_draw_job` and stay on the CPU thread. They keep every `fault_` call
  exactly where it is. Their outputs — the `Profile` value, the surface descriptors, the texture
  vectors, the swizzle tables — become `DrawJob` members instead of stack locals. **No expression
  changes.**
* Step 4 becomes `execute_draw_job(const DrawJob&, DrawJobTelemetry&)`. Every reference to
  `raster_state_[…]` becomes `job.raster_state[…]`; every reference to `pitch`, `clip_x`,
  `color_address` etc. becomes `job.pitch`, `job.clip_x`, `job.color_address`; every reference to
  a counter becomes `out.counter`; every hash fold `x_ ^= word; x_ *= 1099511628211ull;` becomes
  `out.x ^= word; out.x *= 1099511628211ull;` on a per-job accumulator seeded from the FNV basis.
  **No floating-point expression is touched, no operation order inside a triangle or a fragment
  changes, no `double`→`float` or reassociation is introduced.** This is the property that keeps
  the byte-lock meaningful, and it is why step (d) of the staging plan is exactness-neutral and
  must be gated as such.
* The per-profile branch structure inside the fragment loop is untouched at this stage. It is
  attacked separately in stage (b) (per-profile specialization), which is a *different* bead with
  its own gate.

The immediate-quad family (`rasterize_witnessed_immediate_quad`, reached at `nv2a.cpp:2645`,
body from `:2195`) takes the same split. Note it constructs `PagedRasterSurface` **without**
`mem_direct_` (`:2212-2225`) — so under §1.3 it is inline-only until that is changed. Given it
carries 7 of the 22 hashes and is a fullscreen blit family, converting it to the direct path is
worth its own bead but is not on the critical path.

`rasterize_generic_combiner_batch` (`nv2a.cpp:5942+`) becomes a job executor too, but with
`job.threadable = false` whenever it takes the renderer path (`:8253-8330`, `:8382-8455`) — GL
calls must stay on the thread owning the context, and the resident-surface protocol writes guest
memory through `mem_write_` (`:8442-8446`). Its pure-software fallback path is threadable under
the same direct-pointer rule.

### 3.3 What proves the equivalence

The gate is not "we believe the refactor is mechanical". It is:

1. **Stage (d) alone, toggle absent** (job latched and executed inline): byte-lock vs the
   pre-refactor build at every frame-series deadline — all 22 hashes, both framebuffers, IRQ
   counters, XID report count, idle/slice census, final SHA. Any difference means the refactor
   was not mechanical, and the diff is found before threading is even introduced.
2. **Stage (e), toggle on vs toggle off**, same binary, same guest anchors: the same byte-lock.
3. A witness that threading actually ran (`threaded_jobs` > 0, `inline_jobs` census), so a
   trivially-passing gate cannot be mistaken for a passing gate — this is exactly the trap
   ndsrecomp documented (`threaded_lines`=181 969 / `inline_lines`=0 as the witness).

---

## 4. DETERMINISM GATE

### 4.1 Byte-lock points

Guest-anchored, at the existing `--frame-series` deadlines (`main.cpp:833-850`, `:2153-2165`).
Every deadline is an exact retired-instruction count, so both legs stop at identical guest state.
At each deadline, compare threaded vs inline:

| Class | What | Source |
|---|---|---|
| Framebuffer | Full scanout RGBA and its FNV-64; the frame-series PPM bytes | `nv2a.cpp:10950-11021`; `main.cpp:286`, `:3902` |
| Surfaces | Raw bytes of every color and zeta surface addressed in the window (color/zeta DMA-resolved spans), not just the scanned-out one | `nv2a.cpp:3626-3650` gives the spans |
| Profile hashes | All 14 witnessed-profile FNVs + 7 immediate-quad FNVs + scanout FNV = **22**; plus `generic_combiner_output_hash_` and `transform_output_hash_` **when perf-accounting is on** (they are gated on `collect_diagnostics`, methodology §5a) | `nv2a.hpp:1424-1656`, `1572`, `1727`; parser `tools/measure_xbox_replay.py:267-283` |
| Per-profile counters | fragments / alpha passes / depth passes / writes / depth writes / batches per profile, and every `last_*_raster_address_` | `nv2a.cpp:4536-4560`, `5807-5845` |
| IRQ counters | delivered-interrupt count (`injected_`, `machine.cpp:406`), IRQ0/PIT pulses, `pending_boundaries_` (`machine.cpp:352`) | methodology §3 item 4 |
| XID | endpoint-2 report count | `tools/measure_xbox_replay.py:255-257` |
| Census | idle-census slices/insns/idle_eip_slices/early_stops per window | `main.cpp:1612-1640` |
| Flip | write-increments / stalls / read-increments / notifications / surface / waiting | `main.cpp:3892` |
| Seam-specific | `threaded_jobs`, `inline_jobs`, `drains` by cause, `fenced_batches`, `helper_executed_jobs` | new |
| Overall | final-frame SHA-256, exit code 0, **empty stderr** | `tools/measure_xbox_replay.py:795-807` |

**Byte-lock must be run in both directions**: (i) pre-refactor build vs stage-(d) build, both
inline — proves the refactor is mechanical; (ii) stage-(e) build toggle-off vs toggle-on — proves
the threading is exact. ndsrecomp ran both and it is what caught the difference between "the
refactor is fine" and "the mechanism is fine".

Re-baselining is governed by methodology §3: the final SHA may change **only** if items 1–5 there
are all satisfied, and the seam is not expected to change it at all — unlike an execution-tier
change, the seam does not move where interrupts land.

### 4.2 What the gate cannot prove

The software rasterizer has never been compared to hardware (audit §5; `machine/probes/hw-2026-07-16.md`
is MMIO registers only). A byte-lock proves threaded == inline; it does not prove either is right.
PRINCIPLES §8 forbids inferring correctness from two implementations agreeing — that applies here
and must be restated in any bead that closes on this gate. Hardware capture (`beads-xbox.1.14.9`)
remains deferred per owner decision 2.

### 4.3 The per-profile telemetry hashes under threading

The 22 hashes are order-dependent FNV-1a folds over `(address, value)` word pairs, applied
**per write**, inside the fragment loop (`nv2a.cpp:4599-4600` for depth;
`:5698-5701`/`:5710-5713`/`:5722-5726`/`:5734-5738`/`:5744-5747` for the shared color body;
`:5147-5152`, `:5280-5290`, `:4702-4707`, `:5117-5123` for the specialized ones;
`:2584-2604` for the seven immediate-quad variants). They serialize the inner loop by
construction: `x ^= w; x *= prime;` is a two-cycle loop-carried dependency on the hottest path in
the emulator, executed once per *written* word (727 M writes for radial alone).

Three options:

| Option | Exactness | Threading impact | Perf impact |
|---|---|---|---|
| **O1. Per-job accumulator, merged on the CPU thread in job order at drain** | **Bit-identical to today.** FNV is not associative, but within a job the write order is unchanged, and jobs merge in latch order — so the fold sequence is the same sequence. Merging requires folding job B's *sequence* into A's state, which a plain `xor`/`multiply` of two finished states does **not** do. Implementation: each job keeps its own running state seeded from the previous job's final state **only if executed in order** — i.e. the merge is not a combine, it is a *replay of the job's recorded word stream*. Cheaper alternative: each job records its written `(address,value)` word pairs into a per-job vector and the CPU thread folds them at drain. That is exact but costs 8 bytes × 2 per write — 727 M writes × 16 B is 11 GB/frame-equivalent. **Not viable at full rate.** | keeps threading | prohibitive if the stream is materialized |
| **O2. Order-independent hash** (e.g. sum of per-write 64-bit mixes, or XOR of `h(addr,value)`) | **Changes every hash value** → all 22 golden values re-baseline (permitted under methodology §3, but §3 item 2 uses the *previous* hashes as the equality check, so the re-baseline loses its own anchor for one generation). Order-independence also weakens the hash: two draws swapping their writes to the same pixel become indistinguishable, which is exactly the class of bug threading can introduce. **Actively counterproductive for this gate.** | keeps threading | ~1 multiply + 1 add per write; vectorizable |
| **O3. Gate the fold behind a flag; jobs are threadable only when the flag is off; with the flag on, all jobs run inline** | Exact in both modes. With the flag on you get today's hashes and today's (single-threaded) semantics; with it off you get threading and no per-profile hashes. The **byte-lock still works**: run the gate legs with the flag ON (both inline — proves the refactor), then compare framebuffer/surface bytes with the flag OFF threaded vs OFF inline (proves the mechanism). The framebuffer bytes are the stronger statement anyway; the hashes are a cheap proxy for them. | flag-dependent | **removes a serial dependency from the hottest loop in the emulator** — this is also stage (b)'s win |

**Recommendation: O3.** Concretely: a `--nv2a-profile-hashes` flag (default **on** while the gate
matters, default **off** for headline timing runs, mirroring the `--perf-accounting` split in
methodology §2 rule 6). When on, `job.threadable = false` for every profile that folds a hash —
which is all of them — so the run is exactly today's behaviour plus the latch. When off, the fold
is compiled out of the executor by a template/`if constexpr` parameter, not a runtime branch in
the fragment loop.

This composes with stage (b): gating the hash is listed there as an exactness-neutral loop fix,
and it is the same switch. It also resolves the §5a open item cleanly — the hash families become
explicitly an instrumentation artifact with a named flag, rather than an accident of
`--perf-accounting`.

O1 is recorded as **considered and rejected** with its reason (materializing the write stream is
11 GB-class), so it is not re-derived. A narrower O1 variant — fold per job into a per-job state
and then fold the *job states* in order into a run state with a second-level FNV — is exact for
detecting reordering but produces different values from today and so has O2's re-baseline problem
without O2's speed; also recorded as rejected.

---

## 5. STAGING PLAN

Ordered so that every exactness-neutral win lands before any structural change, and so that each
step's measurement is interpretable on its own. Protocol for every step: methodology §2 —
interleaved ABBA, ≥ 3 legs per arm, same session, quiet host, noperf build for headline numbers,
gate on **final-four wall** median AND min, min-of-N and median both reported, plus the correctness
gate first.

### (a) `dispatch_method` jump table

* **Touches:** `nv2a.cpp:10072-10782`. Replace the 130-arm `if/else if (method …)` chain (zero
  `switch`, verified) with a dispatch table indexed by `method >> 2` for `method < 0x2000`
  (the same index `method_counts_` already uses at `:10123`), with the residual arms in a tail.
  The hottest method `0x1800` sits at branch ~#124 (`nv2a.cpp:10718`) and is 76.9 % of 83 M words
  (audit §5).
* **Measured before/after:** `perf::g_ns_nv2a_dispatch_method` / `g_calls_nv2a_dispatch_method`
  (`perf_accounting.hpp:50`) → ns/word; plus final-four wall on the noperf build.
* **Expected effect class:** dispatch is 17.4 s of 251 s at 209 ns/word (audit §5). A table
  removes ~120 predicted-not-taken compares from the hot method's path. Basis: 76.9 % of words
  currently traverse ~124 arms. Plausible 5–12 s off dispatch → **2–5 % of wall** [inferred].
  Note 209 ns/word is far more than 124 compares can explain (~40 ns at 3 compares/ns), so a
  large part of dispatch time is the *bodies* and the recorder (`methods_.push`, `clear_trace_*`,
  `method_counts_`) — the table alone will not deliver the whole bucket, and the measurement must
  distinguish. Adding a second bucket around the recorder block (`:10089-10123`) before the change
  is cheap and makes the result interpretable.
* **Risk:** low. Exactness-neutral by construction (same bodies, same order). Failure mode is a
  transcription error in the table, caught by the byte-lock.

### (b) Per-profile loop specialization + hoist the counter ladder + gate the hash

* **Touches:** `nv2a.cpp:4523-5745`. Three separable changes:
  * **b1.** Hoist the 15-way counter ladder at `:4536-4560` out of the per-fragment path: it
    resolves `profile` — a loop invariant — to one of 15 `uint64_t*` counters. Replace with a
    single `uint64_t* frag_counter` resolved once per batch, incremented unconditionally.
    Same for the depth-pass and write ladders (`:4587`, `:4726-4731`, `:5002`, `:5172-5178`,
    `:5233-5236`, `:5247-5250`, `:5650-5745`).
  * **b2.** Gate the per-write FNV fold (`:4599`, `:5698-5747`, and the ten specialized sites)
    behind the `--nv2a-profile-hashes` switch as an `if constexpr` template parameter (§4.3, O3).
  * **b3.** Specialize the shared 1 221-line body per profile so the 57 `profile ==` compares per
    fragment (audit §5) become compile-time constants. Mechanically: template
    `execute_draw_job<Profile P>` and a 16-way switch at the top; the compiler then folds every
    `profile == Profile::kX` and deletes the dead arms.
* **Measured before/after:** `perf::g_ns_witnessed_profile[i]` / `g_calls_witnessed_profile[i]`
  per profile (already exists, `nv2a.cpp:3266-3271`) → ns/fragment per profile; `g_ns_raster`,
  `g_ns_raster_generic`; final-four wall noperf. Also binary size (b3 will grow `.text` ~16×
  for that body — record it; methodology §2 rule 8).
* **Expected effect class:** the inner loop is "unvectorizable by construction" (audit §5) for
  three named reasons, and b1+b2+b3 remove all three. Witnessed raster is 114.6 s / 45.6 % of
  wall. Even a 25 % improvement in that loop is **11 % of wall**; the branch-and-dependency
  removal plausibly does better [inferred]. This is the single largest exactness-neutral lever
  identified.
* **Risk:** medium. b3 is a large mechanical edit across 1 221 lines and is where a
  float-expression change could sneak in. Mitigation: land b1, b2, b3 as three separate commits
  each with its own byte-lock; b3 profile by profile (16 gates, not one).

### (c) Early-Z / front-to-back where the depth state allows

**Which witnessed profiles have a depth test that could reject early [verified by reading each
fragment path]:**

| Profile | Depth test | Depth write | Early-Z eligible? |
|---|---|---|---|
| `kPitchZ16Depth` | `incoming > stored` → skip (LEQUAL), Z16 (`nv2a.cpp:4581-4586`) | yes (`:4588`) | it *is* the Z pass — this is the producer of the buffer the others test against |
| `kSwizzledRadialColor` | shared body, LEQUAL on `stored>>8` (`:5458-5464`) | yes (`:5681-5683`, `:5694`) | **yes** |
| `kSwizzledTexturedColor` | shared body, same | yes (`:5704`) | **yes** |
| `kPitchTexturedColor` | shared body, same | yes (`:5714`) | **yes** |
| `kSwizzledProjectedColor` | shared body, same | yes (`:5738`) | **yes** |
| `kPitchProjectedColor` | shared body, same | yes (`:5726`) | **yes** |
| `kPitchMultiTextureColor` | LEQUAL (`:4719-4727`) | yes (`:4950-4959`) | **yes** |
| `kPitchThreeTextureColor` | LEQUAL (`:4719-4727`, shared arm) | yes (`:4950-4962`) | **yes** |
| `kPitchTex5ProjectedColor` | LEQUAL (`:4996-5003`) | yes (`:5101-5108`) | **yes** |
| `kPitchTex5DiffuseColor` | LEQUAL (`:4996-5003`) | yes (`:5101-5108`) | **yes** |
| `kPitchSingleTextureColor` | LEQUAL, **read-only depth** (`:5165-5175`, comment "LEQUAL, read-only depth") | no | **yes** (test only) |
| `kTex7FanColor` | LEQUAL, read-only depth (`:5165-5177`) | no | **yes** (test only) |
| `kPitchAdditiveColor` (a.k.a. inline-color) | **conditional**: `inline_depth_test` = `profile == kPitchAdditiveColor && raster_state_[kRasterDepthFunc] == 0x0202` (`:3546-3548`), LESS, read-only (`:5381-5394`) | no | yes when `inline_depth_test`; otherwise no depth at all |
| `kTexfProjectedFanColor` | **none** — the fragment path (`:5126-5153`) never reads or writes depth, even though `uses_depth_surface` is true for it (`:3615-3617`) and a depth surface is constructed and range-validated | no | **no** |
| `kInlineLinearBlitColor` | **none** — `uses_depth_surface` is explicitly false for it (`:3615-3617`); fragment path `:4604-4708` writes color only (`color_surface.write32` at `:4691`) | no | **no** |

So **12 of 15** (13 counting the Z-only producer) test depth and could reject early, and 10 of
those write depth so front-to-back ordering pays twice. Audit §5 gives 41× overdraw and a 19.6 %
witnessed pass rate — i.e. **80 % of shaded fragments are discarded**, and radial alone shades
4.1 G to write 727 M.

* **Touches:** the fragment loop's ordering of work. Today the depth test happens *after* the
  full barycentric/perspective interpolation and (for the shared body) after texture sampling.
  Moving the depth read+compare to immediately after the `depth` value is computed
  (`nv2a.cpp:4563-4571`, before any texture work) is **exactness-preserving** for every profile
  whose depth test does not depend on a value computed later — verified true for all 12: the
  incoming depth is `floor(clamp(depth, …))` of the already-computed interpolated `depth`.
  Careful: `kPitchAdditiveColor` tests **alpha first** (`:5370-5377`, `source_alpha8 < 1` →
  `continue`, and `++inline_color_alpha_passes_` is a *counter*), so reordering the depth test
  ahead of the alpha kill changes `inline_color_alpha_passes_` and the depth-pass counters. That
  is telemetry, not pixels — but it *is* part of the byte-lock, so either keep the order for that
  profile or re-baseline that counter with the change documented.
  Front-to-back submission reordering is a **separate and much bigger** change (it reorders draws,
  which changes blending for any non-opaque profile) — **out of scope here**; only per-fragment
  early rejection is in scope.
* **Measured before/after:** per-profile `*_fragments_` vs `*_depth_passes_` vs `*_writes_`
  (already counted) → the work-avoided ratio directly; `g_ns_witnessed_profile[i]`; final-four wall.
* **Expected effect class:** if 80 % of fragments are discarded and the discard currently happens
  after interpolation+sampling, moving it before eliminates most of that work. Bounded above by
  the fraction of per-fragment cost that sits after the depth compare — measure it. Plausible
  **10–25 % of witnessed raster time** [inferred], i.e. 4–11 % of wall.
* **Risk:** medium-low for pixels (the test is a pure function of already-computed values), higher
  for telemetry counters, which is exactly what the byte-lock will flag. Budget for one
  documented counter re-baseline.

### (d) The seam with the inline executor — zero behavior change

* **Touches:** `nv2a.cpp:1209` (`submit_vertex_batch`), the (1)(2)(3)/(4) split of
  `rasterize_witnessed_batch` (`:2643-5845`), `rasterize_witnessed_immediate_quad` (`:2195+`),
  `rasterize_generic_combiner_batch` (`:5942+`); new `DrawJob` / `DrawJobTelemetry` /
  `latch_draw_job` / `execute_draw_job` / `retire_draw_job`; new `nv2a.hpp` members.
  **No ring, no worker, no atomics.** Every job is executed inline immediately.
* **Measured before/after:** final-four wall noperf, interleaved ≥ 3 — this is the *overhead*
  measurement and it is the whole point of the step. Also `g_ns_nv2a_submit_vertex`, `g_ns_raster`,
  and a new `g_ns_nv2a_latch` bucket; peak RSS (jobs own texture copies that today are stack
  vectors — the move should make this neutral, prove it); binary size.
* **Expected effect class:** **target ≤ +1 %**, ideally 0. ndsrecomp measured its latch at
  +3.8 % of *display* time (+0.06 ms/frame). Here the latch is per *draw*, not per scanline, and
  the texture vectors are **moved** not copied, so the cost is one ~90-word `raster_state` copy
  plus a `std::vector` move per draw. If it exceeds +2 %, the latch is copying something it
  should be moving — find it rather than accepting it.
* **Risk:** medium. This is the step where a mechanical refactor can silently change a float
  expression. Gate: byte-lock vs the pre-(d) build at every frame-series deadline, all 22 hashes,
  both framebuffers, all counters, empty stderr, identical final SHA. **Land this step alone,
  measure it alone, gate it alone.**

### (e) Threaded consumer, default-off behind a flag, with the gate

* **Touches:** a `Ring<DrawJob>`-shaped SPSC job ring (the existing `include/xlle/ring.hpp` is a
  *lossy overwriting* ring — it is the right philosophy but the wrong data structure for a work
  queue, which must not drop; build a bounded ring with a claim index and a completed counter,
  mirroring ndsrecomp §6); **one** worker thread; `std::atomic<uint64_t> jobs_outstanding_`;
  the drain function; the anchor drains of §2.2(c); `--nv2a-threaded` flag default **0**;
  `threaded_jobs`/`inline_jobs`/`drains{by cause}`/`helper_executed_jobs` census.
* **Fence on the hot path:** one relaxed atomic load of `jobs_outstanding_`, taken only at the
  anchors of §2.2(c) — not per guest access. The emulation thread executes pending jobs itself
  while draining.
* **Measured before/after:** final-four wall noperf, toggle on vs off, interleaved ≥ 3, **at
  several core counts** (all / 4 / 2, pinned) — ndsrecomp measured **−15.5 %** on one route,
  **−4…6 %** on another at ≥ 4 cores, and **+14.7 % (a regression)** at 2 cores. Report drain
  counts by cause, `helper_executed_jobs` fraction, `inline_jobs` (the eligibility witness), and
  `g_ns_raster` on the emulation thread.
* **Expected effect class:** this is **latency hiding, not work reduction**. Upper bound is the
  raster time that moves off the emulation thread: up to 133 s of 251 s. Realistically bounded by
  drain frequency, which is `unknown` until M2/M5. If drains happen every draw the win is zero;
  if they happen once per vblank the win approaches the raster share. **This is why M2 and M5 come
  before the code.**
* **Risk:** high, and it is the reason for default-off. Named risks: an unenumerated anchor (F7);
  a drain that deadlocks against `waiting_for_flip_`; the fault-attribution shift (F13); worker
  contention on a low-core host; the `raster_state_samples_` raw pointer (F16).

### (f) GPU-resident consumer using `refs/xemu` shader generators

* **Touches:** a new `Nv2aRenderer` implementation. Harvest `refs/xemu/hw/xbox/nv2a/pgraph/glsl/psh.c`
  (1 791 lines, register-combiner → GLSL) and `vsh-prog.c` (772 lines, vertex program → GLSL),
  plus `pgraph/gl/surface.c`'s `SurfaceBinding` cache and `pgraph/gl/texture.c`. Surfaces stay
  resident; readback only at fence points (§2.2(c) anchors), not per draw.
* **Measured before/after:** `renderer_resolve` bytes and ns (today 5.4 GB / 3 998 ms vs
  `renderer_draw` 4.2 ms — a 940:1 ratio driven by *path mixing*, not by the hash, audit §5);
  fraction of draws reaching GL (today ~0.13 % [inferred]); GL timer queries
  (`beads-xbox.1.14.6`) to separate GPU execution from driver overhead; final-four wall.
* **Expected effect class:** the readback problem is caused by 96.8 % of draws running on the CPU
  and forcing a resolve. Getting the 15 witnessed profiles onto generated shaders removes the
  mixing, which removes the resolves. This is the only step that can plausibly reach the 18.8×
  needed for 60 fps on the device side — but it also **changes pixel output**: real NV2A
  interpolates in float32/fixed point while the software model uses `double` (audit §5), so the
  GL path is *more* likely to match hardware and *certain* not to match the software golden.
  **This step forces the hardware-oracle question** (owner decision 2) and cannot be gated by
  byte-lock against the software rasterizer. Do not start it before (a)–(e) are landed and
  measured; it is where hardware capture becomes a hard blocker.
* **Risk:** highest. Scope is a quarter-class project. Also note the seam is a prerequisite: a
  GPU-resident consumer *is* a consumer, and the latch/fence contract of (d)/(e) is what lets it
  exist without a resolve per draw.

### (g) Presentation via a real swapchain

**2026-09-06 correction and implementation:** SDL already presents the interactive window
through a real DXGI swapchain on Windows. Absence of `SDL_GL_SwapWindow` did not mean absence
of a swapchain. Stage g keeps SDL, removes scalar scanout staging and the fixed 33 ms
throttle, and measures successful host presents separately from guest flip writes and
guest speed. The shared scanout fence/resolve remains mandatory. See
[PERF-PRESENT-2026-09-06.md](https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/PERF-PRESENT-2026-09-06.md) for the scope, driver evidence,
measurement definitions, and validation. The following bullets record the original plan.

* **Touches:** there is **no `SDL_GL_SwapWindow` in the tree** (audit §1); the GL context is a
  hidden 1×1 window (`nv2a_opengl_renderer.cpp:3561`) and output leaves only through
  `glReadPixels` into guest RAM. The interactive path (`sdl_shell.cpp:80`) has a 33 ms throttle
  → ~30 fps ceiling, and a scalar per-pixel `capture_scanout` on the emulation thread
  (`nv2a.cpp:10992-11017`).
* **Measured before/after:** presented flips per second (today 2.75/s, audit §1) — labelled as a
  separate number from `effective_fps` per methodology §2 rule 5; scanout capture ns.
* **Expected effect class:** **zero** effect on the benchmark (which never presents) and on
  `effective_fps`. It converts the emulator from "produces bytes" to "shows a picture" and lifts
  the 30 fps interactive ceiling. Necessary for the product, irrelevant to the 60 fps metric as
  defined. Explicitly last.
* **Risk:** low, and orthogonal.

---

## 6. MEASUREMENTS THIS DESIGN NEEDS FIRST (blind spots)

Each is a number this design assumes and cannot currently produce.

**M1 — Guest framebuffer/surface-page touches per vblank.** The one that decides §2. Today
`unknown`, structurally (F7). Instrument: a `--vram-access-census` build with
`UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE` installed from process start (accepting the 3–10× cost, this
build is never used for timing) writing `(eip, addr, size, is_write, insn_count)` into an always-on
`Ring` for accesses inside the currently-bound color/zeta surface spans and the scanout span.
Report per vblank: touch count, distinct pages, distinct EIPs, and — critically — whether each
touching EIP is within N instructions of an NV2A MMIO access. **Decision rule in §2.4.**

**M2 — Per-vblank job count and bytes.** How many draws, how many fragments, how many destination
bytes per vblank; and the distribution (is it 3 huge draws or 3 000 small ones?). Without this the
ring size, the worker count, and the expected win are all guesses. Substantially covered by
`beads-xbox.1.14.5` (per-vblank ring) — extend it with per-draw destination span and job-eligibility
(direct-pointer available? texture overlap?).

**M3 — Fraction of batches whose texture source lies in a surface rendered in the same frame**
(F6). Determines whether the texture-overlap drain is rare (fine) or per-draw (fatal). Cheap:
at latch, compare each texture span against the set of surfaces written since the last vblank;
count matches per profile. Note the immediate-quad path already detects and *rejects* the aliased
case (`nv2a.cpp:2203-2210`) — the counter for that rejection
(`last_immediate_quad_fullscreen_raster_reject_mask_ |= 1u << 21`) is existing evidence that the
case occurs at least sometimes.

**M4 — Depth-test-enabled fragment share per profile.** Partly derivable today: every profile has
`*_fragments_` and most have `*_depth_passes_` and `*_writes_` (`nv2a.cpp:4536-4560`,
`:4726-4731`, `:5002`, `:5172-5178`, `:5694-5745`). What is missing is the *cost split*: how much
per-fragment work sits after the depth compare. Add a per-profile counter of fragments rejected by
depth and a bucket around the post-depth work in one profile as a calibration. Decides the size of
stage (c).

**M5 — Fence/drain frequency under each coherence policy.** Simulate before building: at each
anchor in §2.2(c), count how many draws would have been pending. Doable in the stage-(d) inline
build at zero risk — the latch exists, jobs are executed immediately, but the counter answers
"how many jobs *would* have been outstanding". If the answer is ≈ 1 everywhere, threading buys
nothing and stage (e) should not be built. **This is the cheapest and most decision-relevant
measurement in the list and it comes free with stage (d).**

**M6 — Direct-pointer eligibility rate.** `perf::record_paged_surface_direct` (`nv2a.cpp:50`)
already counts it; the audit says 874 k / 876 k. Re-confirm per profile, since §1.3 makes
non-direct surfaces inline-only. Also confirm the immediate-quad family's rate (it constructs
surfaces **without** `mem_direct_` at `:2212-2225`, so its rate is 0 by construction today).

**M7 — Latch overhead.** Produced by stage (d) itself; listed so it is not forgotten as a
deliverable.

---

## 7. WHAT xemu DOES (from `refs/xemu` only)

All citations are `F:\Projects\xboxlle\refs\xemu\hw\xbox\nv2a\…`.

### 7.1 Threading topology

xemu runs **one dedicated PFIFO/PGRAPH thread**, created at realize
(`nv2a.c:238-239`, `qemu_thread_create(&d->pfifo.thread, "nv2a.pfifo_thread", pfifo_thread, d, …)`).
`pfifo_thread` (`pfifo.c:452-487`) loops: `pgraph_process_pending(d)` → `pfifo_run_pusher(d)` →
`pgraph_process_pending_reports(d)` → sleep on `fifo_cond` when no kick. So xemu's seam is
**one level higher than the one chosen here**: the *whole* pushbuffer parser and method dispatch
run off the vCPU thread, and the vCPU only writes registers and kicks a condvar.

Two locks: `d->pfifo.lock` and `d->pgraph.lock`. The vCPU acquires both in a strict order via
`nv2a_lock_fifo` (`nv2a.c:264-272`): lock fifo, broadcast, **drop the BQL**, wait on
`fifo_idle_cond`, retake the BQL, lock pgraph. That is a full quiesce-the-GPU-thread primitive,
used for reset, savevm, and surface-scale changes.

**Why xboxlle cannot copy this directly:** the parser republishes GET into two guest-readable
apertures (F9) and faults the machine on malformed streams; xemu can do this off-thread because
QEMU register reads go through `nv2a_lock_fifo`, i.e. the vCPU *blocks* on the GPU thread for
every such read. xboxlle's `Nv2a::mmio_read` runs on the Unicorn callback stack with no
equivalent primitive, and adding one would put a condvar wait inside a guest `mov`. **Harvest the
shape (dedicated consumer thread, explicit quiesce primitive), not the boundary.**

### 7.2 Surface cache and dirty tracking

* `SurfaceBinding` list `r->surfaces` with `vram_addr`/`size`/`pitch`/`width`/`height`/`fmt`,
  plus two dirty bits per binding: `draw_dirty` (GPU has newer data than guest RAM) and
  `buffer_dirty`/`upload_pending` (guest RAM has newer data than the GPU)
  (`pgraph/gl/surface.c:1067`, `:1116`, `:1196-1207`, `:1274-1287`).
* **Overlap eviction:** `invalidate_overlapping_surfaces` (`pgraph/gl/surface.c:496-513`) walks
  every binding, downloads any dirty one that overlaps, and invalidates it. xboxlle has the same
  concept in `resolve_renderer_color_overlap` / `invalidate_renderer_color_overlap`
  (`nv2a.cpp:230-346`) — **the seam is already there**; what is missing is the cache with more
  than the current 1-surface-at-a-time behaviour.
* **Surface → texture:** `pgraph_gl_render_surface_to_texture` (`pgraph/gl/surface.c:312+`) with a
  fast blit path and a `render_surface_to_texture_slow` fallback that downloads to a buffer and
  re-uploads (`:279-306`). This is exactly F6/M3: xemu handles render-to-texture on the GPU
  instead of draining. **This is what stage (f) buys.**
* **Guest CPU coherence — the important one.** `surface_access_callback`
  (`pgraph/gl/surface.c:424-476`): for every binding overlapping the accessed range, if
  `surface->draw_dirty` it sets `download_pending` and the accessor **blocks** on
  `downloads_complete` after kicking the FIFO thread; if the access is a write it sets
  `upload_pending`. It is registered per surface by `register_cpu_access_callback`
  (`pgraph/gl/surface.c:469-479`) — and note:
  ```c
  static void register_cpu_access_callback(NV2AState *d, SurfaceBinding *surface) {
      if (tcg_enabled()) { … mem_access_callback_insert(qemu_get_cpu(0), d->vram,
                                surface->vram_addr, surface->size,
                                &surface_access_callback, d); … }
  }
  ```
  **Under TCG, xemu installs a per-surface, range-bounded CPU memory-access callback.** Under
  KVM/WHPX it instead uses the dirty bitmap: `mem_dirty = !tcg_enabled() &&
  memory_region_test_and_clear_dirty(d->vram, entry.vram_addr, entry.size, DIRTY_MEMORY_NV2A)`
  (`pgraph/gl/surface.c:1112-1114`), with logging enabled at realize
  (`nv2a.c:231-233`, `memory_region_set_log(d->vram, true, DIRTY_MEMORY_NV2A)` and `_TEX`).

  This is the direct answer to §2: **xemu solves F7 with a bounded per-surface access callback**,
  which is candidate (a) done properly — QEMU's `mem_access_callback_insert` is range-scoped in
  the TLB fill, whereas Unicorn's `HOOK_EXISTS` is a global list test that deoptimizes everything
  (§2.2(a), verified). So the mechanism xemu uses is *unavailable in Unicorn as shipped*. The two
  ways to get it: patch Unicorn to make the mem-hook fast-path test range-aware
  (`HOOK_EXISTS_BOUNDED` exists at `uc_priv.h:244-246` and is simply not used by the codegen
  test), or use host page protection (§2.2(d)). **The Unicorn patch is worth costing** — it is a
  small, local change to `tcg-target.inc.c:1765` plus `translate.c:547/557` plus
  `cputlb.c:1211`, and it makes candidate (a) behave the way xemu's does. Record as an option;
  it forks a vendored dependency, which is why (d) is recommended first.

### 7.3 Shader generation

`pgraph/glsl/psh.c` (1 791 lines) generates fragment GLSL from the register-combiner state;
`vsh-prog.c` (772) and `vsh-ff.c` do the vertex program and fixed-function paths;
`geom.c` handles primitive expansion; `shaders.c` binds it together. xboxlle currently has
**4 hand-written GLSL programs against 946 observed combiner fingerprints** (audit §5), and
`nv2a.cpp:6525` already cites `pgraph_glsl_gen_psh` in a comment. **Harvest wholesale.** The
xboxlle-side `raster_state_` layout (`nv2a.hpp:1060-1125`) is already a superset of what psh.c
consumes (it holds the ICW/OCW/factor words per stage), so the adapter is a register-name mapping,
not a redesign.

### 7.4 Harvest-versus-build summary

| Component | xemu has it | Decision |
|---|---|---|
| Fragment shader generation from combiner state | `pgraph/glsl/psh.c` | **Harvest** (stage f) |
| Vertex program → GLSL | `pgraph/glsl/vsh-prog.c`, `vsh-ff.c` | **Harvest** (stage f) |
| Surface binding cache + overlap eviction | `pgraph/gl/surface.c` | **Harvest the model**; xboxlle's resolve/invalidate seam (`nv2a.cpp:230-346`) is the insertion point |
| Surface → texture (render-to-texture) | `pgraph/gl/surface.c:279-390` | **Harvest** (stage f); it is the principled answer to F6/M3 |
| Per-surface CPU access callback (TCG) | `pgraph/gl/surface.c:424-479` | **Cannot harvest** — needs a QEMU/Unicorn primitive Unicorn does not expose (§2.2(a)). Build §2.2(d), or patch Unicorn. |
| Dirty-bitmap coherence (non-TCG) | `pgraph/gl/surface.c:1112` | N/A — xboxlle is TCG-only |
| Dedicated PFIFO/PGRAPH thread + quiesce primitive | `nv2a.c:238`, `pfifo.c:452` | **Harvest the shape, not the boundary** (§7.1) |
| Swizzle / S3TC / texture decode | `pgraph/swizzle.c`, `s3tc.c`, `texture.c` | xboxlle already has equivalents; compare only if a format is missing |
| Vulkan backend | `pgraph/vk/` | Not now. Record that it exists and mirrors the GL structure, so the GL work is not a dead end. |

---

## 8. Open questions this document does not close

1. **M1 is unanswered** and it gates whether §2.2(c) is sufficient. Everything after stage (d)
   is contingent on it.
2. **M5 could kill stage (e) outright.** If the anchor drains fire on nearly every draw, the ring
   is pure overhead. M5 comes free with stage (d), so this is resolved before any threading code
   is written.
3. **Worker count > 1** needs the destination-overlap partition (§1.5). Deliberately deferred.
4. **Moving vertex transform into the job** (§1.1) is deferred to stage (f) and depends on M1
   covering vertex-array pages, not just surface pages.
5. **The software rasterizer is not validated against hardware.** Every byte-lock in §4 proves
   self-consistency only (PRINCIPLES §8). Stage (f) forces this question; owner decision 2 says
   stop and ask at that point.
6. **Patching Unicorn's mem-hook fast-path test to be range-aware** (§7.2) is a real option that
   would make xemu's exact coherence mechanism available. Costed but not chosen; it forks a
   vendored dependency.
