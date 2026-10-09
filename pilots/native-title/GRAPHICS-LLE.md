# Bounded graphics LLE qualification

Family `.116` belongs to the shared Blinx LLE burndown `.102`. The maintained
route is PFIFO/0x97 command consumption, physical vertex DMA or inline arrays,
transform, rasterization, framebuffer writes and visibility-report DMA in
`src/machine/nv2a.cpp`. This document records component qualification; full
graphics-family acceptance remains open and the total remains **4/9**.

## Packed colors and persistent fetch - 2026-10-09

The previous session ended with a suspected packed-color mismatch. Independent
draws reproduced it: a `UB_D3D` DWORD `0xffff0000` rendered blue, and
`0x7f123456` rendered `0x7f563412`. Both physical DMA and inline arrays decoded
the bytes as RGBA. `UB_D3D` is normalized BGRA, while `UB_OGL` is normalized
RGBA. The correction separates those formats in both maintained decoders.
`SET_VERTEX_DATA4UB` keeps its independent immediate RGBA packing.

`tests/test_nv2a_vertex_colors.cpp` supplies **140 actual draw cases**:

- 96 cases combine DMA A/B, direct/callback reads, array/indexed/inline
  submissions and both packed formats. Red, blue and two asymmetric colors
  include nontrivial alpha. DMA inputs use padded strides and nonzero indices;
  colors change after binding without replacing or rebinding the storage.
- 40 cases bind linear A8R8G8B8 surface bytes as diffuse vertex data, then
  execute repeated GPU `CLEAR_SURFACE` writes and subsequent array/indexed
  draws. They perform no CPU color upload or attribute rebind between passes.
- Four immediate controls establish that array-format state does not change
  `SET_VERTEX_DATA4UB` ordering.

The fixture checks actual framebuffer pixels, untouched background, normalized
transform output and completed nonzero visibility reports. Expected bytes and
colors are authored independently of either decoder and of the HLE adapter.
Before correction the first 136 cases produced **240 failed assertions**;
the corrected final 140 cases have **zero failures**. Four assertions in the
older `test_nv2a.cpp` also encoded the old permutation. Their unchanged input
DWORDs now require red normalized output, radial pixel `0xff102e51` and
untextured pixel `0x80402010`; updating those expectations removes correlated
agreement with the erroneous decoder.

Matching native HLE checks execute persistent vertex buffers at nonzero first
index with a padded stride, UP submissions and alpha blending on actual D3D11.
Each of three device lifetimes performs eight exact RGB cases plus an alpha
blend case (27 draws). XRGB readback does not promise stored alpha, so blending
observes that lane with a one-LSB tolerance. HLE already satisfies this contract;
no production HLE change or new rendering cost is introduced. Its existing
upload strategy and shader implementation remain usable.

Independent reference evidence:

- MIT [nxdk_pgraph_tests surface-as-vertex procedure](https://github.com/abaire/nxdk_pgraph_tests/blob/706a41e61188d3b08e5b1bb2a32b74ad062a883d/src/tests/surface_as_vertex_array_tests.cpp)
  specifies linear A8R8G8B8 rendered colors reused as `UB_D3D` diffuse streams,
  including successive surface updates. Our test is authored here and uses
  bounded clears rather than copying that implementation.
- Pinned xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
  `hw/xbox/nv2a/pgraph/{gl,vk}/vertex.c`: the GL input declares BGRA for
  `UB_D3D`; Vulkan marks the D3D swizzle separately from `UB_OGL`. This is
  source-level behavioral corroboration, not a new differential xemu run.
  No GPL implementation body is copied or linked by this correction.
- The locally retained CC0 golden repository at
  `06b041d6a50c060b4974d93617405f86a03a0b8f` contains Image_blit captures,
  not a matching SurfaceAsVertexArray golden. No new hardware observation or
  hardware-qualified packed-color capture is claimed.

Reproduction from this worktree (PowerShell):

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build build\visibility-lle --target test_nv2a_vertex_colors test_nv2a test_nv2a_tile_commands test_vsh_triangle test_generic_vertex_scratch xlle_nv2a_reference -j 4
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build\visibility-lle -R '^(nv2a|nv2a_vertex_colors|nv2a_tile_commands|nv2a_tile_layout|nv2a_zeta_compression|vertex_pipeline_guards|generic_vertex_scratch|vsh_triangle|nv2a_fp_timing)$' --output-on-failure
& 'C:\Program Files\CMake\bin\cmake.exe' --build build\blinx\host-calendar-lle --config Release --parallel 4
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build\blinx\host-calendar-lle -C Release --output-on-failure
```

Results: **9/9** related machine/graphics checks (0.95s) and **56/56** native
checks (37.38s). The no-SDL machine fixture explicitly skips its pre-existing
OpenGL-only cases; those skips do not qualify OpenGL behavior. The native
packed-color cases execute on the real D3D11 backend.

The rebuilt native Blinx executable retains SHA-256
`76ff2a4e836246508dac2992a77972aa552cfb8ff8ceb87950196530ff12f730`,
exactly matching `build/blinx/audio-completion-floor-attract/run.aot-private.json`.
Its recorded **190.578s / 18-capture** demo/interruption/title/reentry regression
is reused. The changed vertex decoder is not linked into that native title
route; its own execution evidence is the maintained LLE draw fixture. The
original whole-title LLE startup still has its previously recorded encoder
dependency; no new title-startup qualification is asserted.

Private logs are `build/blinx/graphics-color-{before,lle-build,lle-tests,
native-full-build,native-tests}.log`. None are shipping inputs.

## Remaining offline work

Continue the original vertex-declaration/command boundary and packed signed
formats, persistent shader inputs and texture/state combinations actually used
by Blinx. These color cases do not qualify all vertex slots, arbitrary aliasing,
tiled vertex input, shader arithmetic or the entire graphics family. Retain
existing scene-reference checks; audit shared expectations before accepting
agreement. Console-dependent startup and disputed hardware timing stay at the
back of the burndown. LLE performance is not an acceptance gate; preserve HLE
shortcuts wherever they meet the observable contract.
