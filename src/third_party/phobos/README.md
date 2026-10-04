# phobos D3D11 backend import

Source: https://github.com/phobos665/xboxrecomp
Pin: f01b20ded38856abd3d37b561521cc7013363725.
License: GPL-3.0, retained in COPYING; upstream MIT notice is retained alongside it.

Imported from `src/d3d/` at that pin:

| Local file | Donor file / portion |
| --- | --- |
| rhi.h, rhi_backend.h, rhi.c, rhi_d3d11.c | Same filenames; real D3D11 backend |
| nv2a_vsh.h, nv2a_vsh.c | `src/kernel/` with the same filenames; NV2A vertex decoder (CPU executor is unused) |
| vsh_hlsl.c | d3d8_vsh.c, string builder and HLSL generation before input-layout management |
| d3d8_combiners.h | Same filename; combiner state and constants |
| combiner_hlsl.c | d3d8_combiners.c, texture-mode enums and HLSL generation |

The later reviewed donor head `7884426` changes `d3d8_shaders.c`; none of these
imported source portions changed. Shader compilation, buffers, images, views,
state objects, drawing and ordered readback are donor implementation. Local
adaptations:

- Remove optional donor title diagnostics and donor configuration-dependent
  disk caching; real `D3DCompile` remains active.
- Extend blend state with real D3D11.1 logic operations required by NV2A.
- Remove title-specific aspect-ratio rewriting and debug shadow color output.
- Exclude incomplete bump, clipping and depth-output texture modes; unsupported
  modes return an error. No placeholder texture results are supplied.

`src/host/nv2a_phobos_renderer.cpp` supplies the caller adapter: screen/depth
coordinates, guest resources and allocation invalidation, real readback,
format conversion, feedback copies, state mapping and native raw vertex input.
Its supported texture-mode subset is narrower than the donor generator.
This experiment selects the entire HLE backend at build time. It does not import
the donor title's native XDK/kernel runtime or its swapchain presentation path.

No missing renderer operation is replaced by a success-returning placeholder.
See `docs/RESULTS-SPIKE-PHOBOS-BACKEND-2026-10-04.md` for validation and limits.
