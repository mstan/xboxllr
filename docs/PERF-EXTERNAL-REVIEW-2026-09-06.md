# xboxdashboard / xboxrecomp: source review and next experiments

Owner requested an exploratory stopping point after committing the current work.
This is a source audit, not a benchmark or a port. GPT-5.5 High inspected the
toolkit; the primary independently checked the consequential claims against both
repositories and xlle's actual pinned build dependency. No external executable,
console access or owner play instance was used.

Pinned sources:

- [xboxdashboard `34c2dda`](https://github.com/sp00nznet/xboxdashboard/tree/34c2dda6b444af88dc22d404846db206a630c7fc)
- [xboxrecomp `a781596`](https://github.com/sp00nznet/xboxrecomp/tree/a781596e2181f8d4b15336659897ce07bdd7d56a)
- xlle baseline: published `7cae6c4`; local default-off vertex diagnostic `2d54a1d`.

## What their rendering demonstrates

xboxdashboard's README reports a black clear, no geometry submissions and no
working dashboard UI. This supports saying xlle is further along on the visible
stock-dashboard workload, not a universal ranking of the projects. Its current
source also contradicts the README's no-overrides claim: `sub_00034924` replaces
the XIP integrity check, reports selected diagnostics and returns successfully
instead of following the original reboot path. `main.c` seeds the CRT lock table
and initializes a replacement kernel. These are different execution boundaries
from xlle's firmware/device path.
Sources: [README](https://github.com/sp00nznet/xboxdashboard/blob/34c2dda6b444af88dc22d404846db206a630c7fc/README.md),
[integrity override](https://github.com/sp00nznet/xboxdashboard/blob/34c2dda6b444af88dc22d404846db206a630c7fc/src/recomp/recomp_manual.c#L87),
[startup](https://github.com/sp00nznet/xboxdashboard/blob/34c2dda6b444af88dc22d404846db206a630c7fc/src/main.c#L280).

xboxrecomp has several distinct rendering paths. Its Windows D3D8 replacement
does compile guest vertex microcode into HLSL and issue D3D11 draws. The captured
menu pushbuffer translator instead produces pretransformed vertices and chooses
fixed-function input; it does not execute the uploaded NV2A transform program.
The separate CPU pushbuffer executor skips batches requiring vertex programs.
These paths must not be conflated. The HLSL translators are Windows-only; the
POSIX build selects a different `d3d8_gl.c` implementation.
Sources: [D3D draw path](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/d3d8_device.c#L825),
[CPU executor](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/kernel/nv2a_pb_exec.c#L593),
[platform selection](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/CMakeLists.txt#L5).

The menu translator is especially unsuitable as an NV2A correctness reference:
it redefines clear/color-clear as `0x01D0/0x01D4`, conflicting with its own register
header's `0x1D94/0x1D90`. Its ignored-method range includes the real clear method.
It also forces depth off, alpha blending on and a pretransformed FVF, with
title-specific texture mappings. This is a directly observed limitation of that
file, not a claim that every toolkit rendering path has the same fault.
Sources: [translator definitions and draw state](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/nv2a/nv2a_pgraph_d3d11.c#L78),
[register definitions](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/nv2a/nv2a_regs.h#L1277).

Captured menu replay also selects stored command streams and applies host-driven
animation. It can inform diagnostic workload shapes, but must not replace live
guest PFIFO/PGRAPH execution. Do not import its captured asset headers into our
source tree. [Replay implementation](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/nv2a/nv2a_pb_replay.c#L254).

## Useful techniques, with the local gap checked

| Candidate | External evidence | What xlle actually lacks / next test |
|---|---|---|
| Bulk generated-shader constants | Combiner constants are packed into one mapped D3D11 constant buffer | xlle generated submission still sends individual uniforms, including up to sixteen stage constants. Measure call count, changed bytes and CPU cost; compare vector-array uploads, dirty tracking and a UBO before choosing one. |
| Generated-path upload ring | D3D8 DrawPrimitiveUP uses a 4 MiB dynamic ring with discard/no-overwrite | xlle already has a witnessed SSBO ring, default 4. The generated executor instead refills one SSBO per submit. Measure that specific path's bytes and stalls; retain explicit lifetime/fence ownership if experimenting with persistent mapping. |
| GPU vertex translation | D3D8 shader creation/prepare compiles and caches HLSL | Potentially larger reach, but output currently feeds CPU triangle setup in xlle. A separate GPU pass plus synchronous readback may lose. First map a path that keeps transformed vertices on GPU through the next consumer. |
| State/shader cache tuning | D3D11 caches state objects and translated shaders | xlle already caches GL state and generated programs. Count actual misses, redundant binds and cross-path invalidations before changing anything; the presence of another cache is not evidence of a local gain. |

Source details: [bulk constants](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/d3d8_combiners.c#L1020),
[upload ring](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/d3d8_device.c#L748),
[state application](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/d3d8_states.c#L329).
Local comparison: `src/host/nv2a_opengl_renderer.cpp`, generated submission around
2569-2627, witnessed ring around 6536, state helpers around 6910;
`src/app/main.cpp` sets the witnessed ring default to 4.

The toolkit's texture Lock/Unlock API supplies convenient dirty boundaries.
That does not transfer to arbitrary guest RAM and DMA writes. xlle's accepted
[post-resolve texture conversion cache](PERF-TEXTURE-CACHE-2026-09-06.md) already
exploits exact conversion inputs while retaining coherence. Any further GPU copy
must preserve overlapping color/depth/texture and CPU-read ownership transitions.
[Resource upload implementation](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/d3d8_resources.c#L1444).

## Why the GPU vertex translator cannot be copied as-is

Primary inspection confirmed concrete differences from xlle's pinned CPU floor:

- ARL emits `floor(x)` rather than `floor(x + 0.001)`: values just below an integer
  can address a different constant register.
- RCC uses a positive clamp even for a negative reciprocal. At input `-1`, it
  cannot produce the floor's `-1` result.
- EXP/LOG replicate one scalar across four components instead of the floor's
  distinct exponent/fraction or exponent/mantissa components and final `1`.
- Native multiply/dot expressions omit the floor's finite-input overflow repair.
- Shader-cache lookup accepts a 32-bit FNV match without checking full microcode
  equality; this is insufficient as an exact program identity.

[Translator and cache source](https://github.com/sp00nznet/xboxrecomp/blob/a781596e2181f8d4b15336659897ce07bdd7d56a/src/d3d/d3d8_vsh.c#L642),
[pinned CPU operation bodies](https://github.com/xemu-project/nv2a_vsh_cpu/blob/863fc0f50aa4027af113718d2a63e45ac3be7a42/src/nv2a_vsh_cpu.c#L18).
Agreement with this CPU floor is the immediate differential baseline, not proof
that every floor behavior is hardware-validated; that source itself retains a
hardware-validation TODO for LOG.

## Amended next experiments

1. Finish the pending disabled-diagnostic 3+3 retention check when the host is
   available. Keep the rejected direct-mapped vertex reuse result recorded;
   do not implement it as an optimization.
2. Attribute generated GL submission per product window. Separate packing,
   buffer uploads, uniforms, binds/state, draw/barriers/flush, error checks and
   timer-query overhead. Tag asynchronous GPU samples by their issuance window;
   completion-window deltas alone misattribute queued work. Avoid forced waits.
3. Choose bulk uniforms or generated-buffer upload reuse only if that attribution
   gives a useful ceiling. Alternate generated/witnessed/resolve/clear paths in
   counterexamples so a stale host-state mirror cannot hide a rendering fault.
4. Design a program-specific GPU vertex experiment around the dominant 23-op
   program observed in [the timer census](PERF-TIMERS-2026-09-06.md). Match full
   program bytes/state, not merely its fingerprint; exclude context writes and
   unsupported operations, retain CPU fallback. Compare raw outputs before raster
   clamping, including exceptional floats and relative-address boundaries. Only
   pursue the integrated path if transfer, ordering and CPU-consumer costs fit.
5. Continue generic-family numeric correctness and GPU-to-GPU texture-copy work
   under their existing evidence requirements. Re-profile after each accepted
   change; do not add overlapping savings or assume today's CPU share is fixed.

All candidates remain **unmeasured**. Acceptance remains behavioral faithfulness
and same-session interleaved noperf min/median gates; byte locks are diagnostics,
with explained fidelity changes re-pinned under the existing methodology. Include
the canonical replay, navigation windows and interactive cadence/latency. This
review provides no new evidence for a CPU replacement, no 60 FPS claim, and no
authorization to access physical hardware. The 60 FPS plus headroom objective
and opt-in HLE matching the LLE floor remain unchanged.
