# Renderer reuse for the AOT dashboard

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
| sp00nznet at `1409a7d7801d3e931fb1074be6104209ddd9a33e` | D3D/OpenGL translation, NV2A combiner/VSH work, standalone GPU extraction | Evaluate reusable shader/format/backend algorithms; standalone machine wrapper fails our no-stubs requirement |
| NoRain211 at `7adaf210a89814c5ca4444e4876fccb51b938878` | Related D3D/OpenGL and standalone NV2A code | Same selective qualification; no whole-wrapper transplant |
| phobos665 at `012e35616034ba1a3a310c4958988467996fb212` | Related graphics translation and standalone NV2A code | Same selective qualification; no whole-wrapper transplant |

The local references live outside the source repository under `../refs/`.
These pins state what was inspected, not that every graphics path is qualified.

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
