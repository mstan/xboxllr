# Native pretransformed fog

Tracking: `beads-xbox.1.179`. JSRF mission `0x20028` reaches FVF `0x1c4`
with fog enabled, linear mode3, start0, end4000, density0.3 and range fog off.
The previous shared renderer stopped before drawing this case.

`pilot_gfx_fvf_fog` wraps the existing native XYZRHW/color/texture decoder.
It publishes a small device-owned constant buffer at VS b5 and supplies real
per-vertex fog factors to the existing fixed/programmed pixel consumers.
It uses the current original projection cache: an affine projection selects
projected Z; a perspective projection selects clip W recovered from RHW.
Table mode NONE uses supplied specular alpha (or the retained attribute when
that array is absent). Linear, exponential and squared exponential modes use
their original range/density values. Diffuse alpha is preserved independently.
Cached shaders read changed fog constants without compiling a new layout.
All device-owned buffers and shaders are released with their existing owner.

References consulted for this implementation:

- [Cxbx-Reloaded depth selection](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/Direct3D9/Direct3D9.cpp),
  with fixed-function vertex/pixel shaders at the same revision.
- [xemu vertex fog](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/nv2a/pgraph/glsl/vsh.c),
  including per-vertex factor evaluation and infinite-distance/NaN behavior.
- [Microsoft fog formulas](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bb324452(v=vs.85)),
  including manually supplied specular-alpha fog for transformed vertices.

Existing renderer attribution and licensing remain in
`THIRD_PARTY_ATTRIBUTION.md`. This does not add original title data or code.

`pilot_graphics_pretransformed_fog` checks actual GPU pixels against independent
mathematical expectations for supplied alpha, exp/exp2/linear clip-W distance,
affine Z, fog-disabled transitions, zero RHW, nonlinear factors interpolated
under unequal clip W, immediate floating-point attributes and preserved diffuse alpha. Ten cases run across three
device lifetimes. Fixed-declaration and lighting regressions also pass.

Typed streams and immediate host-expanded XYZRHW batches use separate cached
input layouts with the same fog consumer; float colors are not packed into bytes.
Range fog remains an explicit unsupported boundary. Exact NV2A intermediate precision and a hardware/oracle comparison
of pretransformed projection/depth policy remain supporting validation after
game coverage, or sooner if a reached visual mismatch calls for them. These
component checks do not establish whole-area or whole-game correctness.
