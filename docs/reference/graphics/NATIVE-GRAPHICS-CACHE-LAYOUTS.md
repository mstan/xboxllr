# Original SDK render-state caches

The native renderer uses 166 semantic state numbers. An earlier SDK can expose
a smaller original cache; translated inline setters must keep writing that
original layout. `graphics_state_cache.inc` maps between those layouts at the
service boundary, without moving or enlarging the original storage.

The reviewed 4134 layout contains 146 states. Original SetRenderStateNotInline
branches at 82 and 117; its complex dispatch table identifies states 117–145.
Relative to the renderer interface it lacks states 82–91, 127–135 and 158
(SAMPLEALPHA). Pixel shader states 0–56 retain their contiguous identity layout.
The original initializer consumes 89 defaults, skipping original slots 116 and
135. These are original-image facts, not permission to bind another revision.

Profiles must supply an injective map, its inverse, cache extent and initializer
skips. Initialization validates the map and identity prefix. Unavailable states
have no original storage; reads report an inactive capability and writes guard.
Diagnostics normalize to the renderer's existing 166-state capture format.

Validation: both identity and compact cache fixtures pass strict MSVC and GCC
builds, including inline writes, inverse lookup, adjacent-global preservation,
invalid-map/write guards and captures. The combined nine-test Windows batch
also covers graphics backend, command execution/guards, presentation, input and
worker waits. This establishes the reusable cache owner; actual 4134 rendering
and complete JSRF attract acceptance remain separate open checks.
