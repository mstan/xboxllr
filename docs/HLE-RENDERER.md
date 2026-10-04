# Separate graphics implementations

The build selects the whole draw pipeline with `-DXLLE_RENDERER=LLE` (default)
or `-DXLLE_RENDERER=HLE`. Use separate build directories. The HLE application
always creates `HleRenderer`; it does not switch to the LLE renderer during
execution. Existing renderer CLI arguments do not override this build choice.

The common NV2A device still consumes guest commands, resolves DMA mappings,
and supplies memory, interrupts and display events. At the draw boundary, HLE
receives raw vertex attributes, indices, vertex program words and constants,
combiner/render state, and resource contents. It bypasses the LLE CPU vertex
executor, profile matching, software triangle setup, and software pixel loops.

The HLE renderer owns GPU shaders, indexed primitive submission, blending,
depth/stencil tests, textures, clears and resident render targets. Native
floating point, interpolation, clipping and texture filtering are intentional.
Convolution-filter requests use native bilinear filtering in this HLE build;
the LLE build retains its reference filtering policy.
The device's readback and resource snapshot paths resolve GPU-written bytes;
render-target textures can be sampled directly without a CPU readback. This
integration does not add a general trap for arbitrary guest CPU loads/stores
into resident graphics memory; broader game coverage must validate those
ownership boundaries.
There is no required instruction-history agreement or shadow execution.

Borrowed implementations:

- xemu revision `482088490c2d5983ed5e1eafbd09906157c1eca0`: programmable
  vertex translation, register-combiner GLSL generation, volume texture
  unswizzling and depth-texture comparison. `tools/import_xemu_hle.py` records
  the portable import adaptations. Copyright and GPL/LGPL notices remain.
- NoRain DOAXBV revision `aa1d3c8afcd8d063171d819e27307070b63f056b`:
  rectangular texture layout implementation. Compressed textures use native
  BC1/BC2/BC3 GPU uploads, as in its native presenter.
- The previously imported phobos XDK scanner remains available. This draw
  pipeline consumes NV2A commands; it does not yet replace guest XDK routines.

Unsupported operations fail explicitly. Required draws are never replaced
with success returns or silently omitted. The current scope must be judged
by the stock dashboard boot, menu animation, Settings input route and actual
elapsed time. A renderer unit test alone is not dashboard validation or a
60 FPS claim. The LLE build remains the accuracy-oriented implementation.

Developer checks:

```
cmake -S . -B build-hle-renderer -DXLLE_RENDERER=HLE
cmake --build build-hle-renderer --target xlle test_nv2a_hle_renderer
ctest --test-dir build-hle-renderer -R ^nv2a_hle_renderer$ --output-on-failure
```

Use the repository's normal compiler/dependency settings and private boot
assets when running the dashboard. These are not distributed with the source.

Resource lifetime follows NoRain's native-presenter rule: retire cached GPU
resources when the guest frees the underlying allocation. The guest kernel's
`MmFreeContiguousMemory` and `MmFreeSystemMemory` bodies still execute. Function
entry observers invalidate host resource ownership, with no draw/frame readback
barrier and no fabricated kernel return. Native regions that would bypass an
observed entry use the CPU floor. Other AOT regions remain enabled.

The current allocation-size adapter is verified against kernel 4983, using its
export table and page-table allocation end marker. Another kernel build fails
explicitly until its metadata contract is implemented; the LLE build is the
available choice for it. This is a limitation of this first renderer integration.
The host also supports RGB565, XRGB1555, XRGB8888 and ARGB8888 color targets.

The CPU renderer unit suite runs in the LLE build; the HLE build registers the
native renderer GPU suite instead. Shared CPU/AOT/device tests remain applicable
to both builds. The real dashboard input replay is a separate acceptance check.

Dashboard results: [whole-renderer screen](RESULTS-WHOLE-HLE-RENDERER-2026-10-03.md).
