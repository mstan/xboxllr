# Renderer and runtime reuse for the dashboard

**All-donor refresh, 2026-10-03:** tracking `beads-xbox.1.41`. The owner requires
continued assessment and useful borrowing from **all three** linked xboxrecomp
repositories, including relevant title runtimes. Earlier coverage named all three
but investigated them unevenly. The fork-specific findings below replace the
generic descriptions of the forks; this remains bounded source/test evidence,
not a benchmark of their games or a completed transplant.

**Owner clarification, 2026-10-03:** the
[current gameplan](GOAL-AOT-DASHBOARD.md) now targets a borrowed enhanced
renderer as an early product path, backed by the runnable LLE reference and
an opt-out. Exact reproduction of our software renderer is not an enhanced-path
acceptance requirement; observable contracts and documented tolerances are.
Native-port internals and kernel HLE are eligible at qualified boundaries.
The pinned source findings below remain useful; their unimplemented shim
behaviors must be replaced with real services, not used to exclude a whole donor.

Owner direction: borrow as much qualified rendering implementation as practical.
This is a source assessment, not a new backend performance result. No new donor
runtime has been integrated by this assessment. Primary goal:
[faithful AOT dashboard](GOAL-AOT-DASHBOARD.md).

## Meaning of host rendering

The guest still programs the NV2A through real memory, MMIO and command streams.
The host backend executes that workload on the PC GPU. This fits LLE: the host
graphics API is an implementation detail while guest-visible results, memory
coherence, synchronization and interrupts retain their hardware contracts.
The existing OpenGL 4.3 backend already renders qualified compute/raster paths
and resident surfaces. A new graphics API is not an acceptance requirement.

## Existing reuse and candidate donors

| Source | Existing value / candidate | Integration decision |
| --- | --- | --- |
| `xboxlle` OpenGL backend | Validated dashboard output, resident color/depth surfaces, resolve/invalidate interfaces, compute/raster paths and presentation | Retain as baseline and extend through qualified general behavior |
| `nv2a_vsh_cpu` at `863fc0f50aa4027af113718d2a63e45ac3be7a42` | Already linked vertex-shader interpreter; Unlicense | Keep reuse and independent checks; host-GPU shader execution remains separately qualified |
| xemu at `482088490c2d5983ed5e1eafbd09906157c1eca0` | Complete host OpenGL/Vulkan backend source, shader translators, texture conversion and surface management | Primary reuse candidate; map actual machine/state dependencies and test before adopting a component or substantial backend |
| sp00nznet at `1409a7d7801d3e931fb1074be6104209ddd9a33e` | Content-checked decoded DXT cache, threaded raster work, combiner setup reuse, clipping/filtering and kernel DPC fixes | Evaluate concrete components at the measured renderer/service boundary; replace any unimplemented dependencies |
| NoRain211 at `7adaf210a89814c5ca4444e4876fccb51b938878` | DOAXBV-derived carry/LOOP fixes and safe recovery of jump-table arm entries | Reuse compiler ideas and regression cases; qualify emitted code against our CPU/state/event contract |
| phobos665 at `f01b20ded38856abd3d37b561521cc7013363725` | Vulkan RHI, redundant-state suppression, disk shader cache, pushbuffer/HLE routing and incremental generation | Evaluate backend/resource/submission code as a first-class candidate; adapt coherence and completion semantics |
| NoRain211/doaxbv-re at `aa1d3c8afcd8d063171d819e27307070b63f056b` | D3D11 compressed-texture upload, buffer rings, render-target retention, asynchronous presentation and native service adapters | Inspect the title runtime alongside its compiler fork; qualify the selected boundary rather than importing the whole runtime's assumptions |

The local references live outside the source repository under `../refs/`.
These pins state what was inspected, not that every graphics path is qualified.

## What the refreshed review actually establishes

Remote HEAD was checked for all three linked repositories on 2026-10-03.
sp00nznet and NoRain211 still match the earlier pins. phobos665 advanced from
`012e356` to `f01b20d`; a separate detached worktree preserves the earlier review.
The associated DOAXBV runtime advanced from our September audit's `df63e09` to
`aa1d3c8` and also has a separate worktree. Do not describe the old DOAXBV review
or its two public tests as a qualification of this newer runtime.

**sp00nznet:** `src/kernel/nv2a_pb_exec.c` caches each decoded DXT block using
its address, format and complete compressed block bytes. Its thread-local caches
and row workers are useful examples for repeated decode/raster work.
`src/kernel/nv2a_combiner.c` moves reusable combiner preparation out of pixel work.
`src/kernel/kernel_bridge.c` implements locked DPC insertion/removal and the
guest `Inserted` flag. These are useful service-contract examples, not evidence
that replacing our real guest kernel with that bridge is already interchangeable.
Our current texture cache already avoids some conversion/upload work, so measure
remaining decode and submission cost before duplicating a donor cache.
[Pinned raster source](https://github.com/sp00nznet/xboxrecomp/blob/1409a7d7801d3e931fb1074be6104209ddd9a33e/src/kernel/nv2a_pb_exec.c).

**NoRain211 compiler:** the fork delta from `32da238` changes 17 files, primarily
the lifter, translator and regressions. Concrete additions include carry
propagation into ADC/SBB across blocks, LOOP-family ECX/flag behavior and refusing
recovered switch-arm entries that depend on unavailable incoming state or code.
Ran its four selected pytest files for LOOP, casino flags, arm entries and
slot-one recovery: **12 tests and 4 subtests passed**. These primarily inspect
generated source; they do not prove native execution, full IA-32 semantics or
our scheduler/exception ABI. Use the cases as adversarial inputs when considering
frontend reuse or expanding our own lowering.
[Pinned translator](https://github.com/NoRain211/xboxrecomp/blob/7adaf210a89814c5ca4444e4876fccb51b938878/tools/recomp/translator.py).

**phobos665:** `src/d3d/rhi_vulkan.c` suppresses unchanged dynamic-state commands,
resetting tracked state at command-buffer recording boundaries. Its RHI and
shader/texture code are more substantial candidates than the earlier generic
fork summary suggested. `rhi_shader_cache.c` stores compiled shaders with a
versioned header and checks complete compile-key text, not only a hash. This
targets compilation hitches; it does not establish a steady-menu FPS gain here.
The latest generation path also avoids rewriting unchanged `recomp_hle.c`.
Ran the selected flag-join/backedge tests: **14 passed**, plus the
write-if-changed test: **1 passed**. These are compiler/tooling checks, not a
Vulkan build, live title run or renderer qualification.
[Pinned Vulkan source](https://github.com/phobos665/xboxrecomp/blob/f01b20ded38856abd3d37b561521cc7013363725/src/d3d/rhi_vulkan.c),
[shader cache](https://github.com/phobos665/xboxrecomp/blob/f01b20ded38856abd3d37b561521cc7013363725/src/d3d/rhi_shader_cache.c).

**NoRain211 title runtime:** the current `recomp-runtime/d3d_presenter_d3d11.cpp`
maps DXT1/3/5 to host BC1/2/3 uploads, uses streaming buffer rings, and retains
render targets separately from ordinary texture eviction. Its capture/presenter
thread split is a candidate for reducing synchronous host submission cost.
Some queued texture pointers remain borrowed from mutable guest RAM; copying or
synchronizing those inputs is necessary for our owned-packet contract. The
texture fingerprint samples 128 words and can miss edits. The worker also counts
some declined draws and continues; importing that behavior would not meet our
unsupported-work policy. Reuse the useful implementation with real ownership,
coherence and error handling. No new D3D11 build or gameplay test was run here.
[Pinned renderer](https://github.com/NoRain211/doaxbv-re/blob/aa1d3c8afcd8d063171d819e27307070b63f056b/recomp-runtime/d3d_presenter_d3d11.cpp),
[capture ownership](https://github.com/NoRain211/doaxbv-re/blob/aa1d3c8afcd8d063171d819e27307070b63f056b/recomp-runtime/d3d_presenter_capture.cpp).

The updated DOAXBV public status reports playable 60 FPS sessions with private
generated inputs, while retaining specific visual/coverage limitations. That is
the donor's reported evidence; we have not reproduced it. Our September note's
description of then-current public status must not stand in for this release.
[Pinned status](https://github.com/NoRain211/doaxbv-re/blob/aa1d3c8afcd8d063171d819e27307070b63f056b/docs/public-status.md).

## Applying the ideas to the active menu spike

1. Qualify the already-decoded DXT2/3 packet on the borrowed shader path and
   measure actual decode/upload/submission costs. Compare sp00nznet's
   content-checked decode cache and DOAXBV's compressed host upload where those
   costs justify it; raw compressed upload requires a real packet/layout contract.
2. If driver submission or transfer dominates, compare phobos's state tracking,
   buffer/resource approach and RHI with DOAXBV's producer/renderer split and
   xemu's backend. A substantial transplant remains an option; backend branding
   alone does not choose the winner.
3. If CPU/service work dominates, use all three lifter/runtime implementations
   and their regressions to choose a bounded native/HLE replacement or compiler
   improvement. Preserve the actual guest memory, timing and fallback contracts.

Some donor optimizations need changes at our boundary. phobos's
`hle_d3d8_texture.c` defaults to sampling texture changes with a full check every
eight frames; a missed small edit can appear late. That is not a substitute for
our coherence contract. Its NV2A acknowledgement thread mirrors PUT to GET and
clears selected status bits under its HLE execution assumptions. We need
completion tied to the actual selected executor's work and visible ordering.
These findings constrain those particular techniques; they do not reject the
fork's shaders, backend, tools or other native services. Frame interpolation is
also separate from our original-speed 60 FPS menu acceptance.

Before implementing a relevant expensive subsystem, check the corresponding
paths and since-last-review changes in **all three** forks and relevant title
runtimes. Record the checked pins, useful differences, what is already inherited
upstream, and the concrete reuse/adaptation decision in the owning issue.
Revisit that decision when a benchmark or new upstream work changes the evidence.
Keep this part of each bounded spike, not an exhaustive research phase before
making progress. We do not need to import code from every donor on every change;
we do need to consider them and borrow the useful qualified parts.

## Concrete compatibility findings

Our `Nv2aRenderer` interface receives owned snapshots and has explicit resident
surface resolve/invalidate contracts. xemu's Vulkan renderer accesses
`NV2AState`/`PGRAPHState` directly through `nv2a_int.h`, its renderer resources,
memory tracking and context lifecycle. It is substantial reusable code, but
connecting it requires real state ownership and synchronization adapters.
Invented memory or IRQ responses would invalidate that integration.

The sp00nz standalone `qemu_shim.h` implements `pci_irq_assert/deassert` as
no-ops and `address_space_map` as a null result. Its `nv2a_core.c` has generic
zero-return/ignored-write handlers for unimplemented blocks. Related handlers
remain in both inspected forks. Those exact implementations are excluded.
The D3D11 translator describes a captured menu profile with five-dword vertices;
that profile is not proof of general dashboard hardware behavior. Captured
title pushbuffer/data headers are not replacements for guest execution.

Source references:

- [xemu Vulkan backend](https://github.com/xemu-project/xemu/tree/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/nv2a/pgraph/vk)
  and [GLSL translation](https://github.com/xemu-project/xemu/tree/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/nv2a/pgraph/glsl).
- [sp00nz standalone shim](https://github.com/sp00nznet/xboxrecomp/blob/1409a7d7801d3e931fb1074be6104209ddd9a33e/src/nv2a/qemu_shim.h)
  and [GPU core](https://github.com/sp00nznet/xboxrecomp/blob/1409a7d7801d3e931fb1074be6104209ddd9a33e/src/nv2a/nv2a_core.c).

Licenses are file-specific: xemu's inspected Vulkan renderer carries LGPL
2-or-later text, while its pixel-shader translator carries GPL 2-or-3 text and
additional author attribution. Preserve those terms; do not relabel a combined
backend under the project's original-source license. Distribution status is
already tracked in [LICENSE-SCOPE](../LICENSE-SCOPE.md).

## Qualification order

1. Pin the dashboard route and compare actual shader, texture, surface and
   synchronization requirements with the existing backend's gaps and measured
   host costs. Startup-loop counts do not select renderer work.
2. Prefer reuse of general shader translation and texture/format behavior where
   it closes an observed gap. Use substantial backend reuse when the real
   dependency adapter is smaller and better supported than incremental changes.
3. Check authored edge cases and the same guest workload: color/depth, blending,
   texture addressing, aliased CPU/GPU memory, resolve timing and IRQ delivery.
   No ignored commands, forced completion or stale surface substitutions.
4. Measure the retained implementation against the previous native baseline;
   qualify guest speed, distinct frames and input latency independently.

Choosing Vulkan alone does not establish any speedup. The largest useful reuse
unit remains an engineering question to resolve with the state/dependency audit
and workload evidence, not an arbitrary limit to tiny code snippets.
