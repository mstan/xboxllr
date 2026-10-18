# Reached native triangle fans

Original Momentopolis entry (UI Round 9, stage index 36) reached the native
primitive 7 guard in `campaign-019` after ordinary skip/confirm input. This is
the Xbox triangle-fan primitive. The draw adapter previously accepted only
triangle lists, strips and independent quads.

The native backend now expands topology into an immutable GPU index buffer.
Triangle `i` uses `(i+1, i+2, 0)`: cyclic rotation preserves winding and selects
the leading rim vertex under the original XDK FIRST provoking convention.
The existing flat-color geometry shader can therefore remain unchanged except
for accepting the fan primitive. The anchor does not supply every flat color.
Smooth varyings retain ordinary interpolation. Persistent streams and existing
user-pointer ring uploads both use this draw path; live vertices are not copied
or expanded to emulate topology. The index capacity grows geometrically,
persists with its device and is released/reset at teardown. Previous native
index binding, format and offset are restored after each draw.

References: [Microsoft's fan topology](https://learn.microsoft.com/en-us/windows/win32/direct3d9/triangle-fans),
local xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`
`hw/xbox/nv2a/pgraph/glsl/geom.c` / `gl/gpuprops.c` for provoking convention,
and the maintained floor's `Nv2a::run_primitives` fan branch. No source is copied
from the emulator into this adapter.

`pilot_graphics_fan` compares complete real GPU targets against independently
listed triangles, including all cull choices, flat/smooth colors, exact
nonzero-first handling, cache growth then reuse, previous R16 index binding
with nonzero offset, malformed/overflow ranges and three device lifetimes.
Explicit interior samples distinguish each rim color from the anchor. PHOBOS
also exercises the actual user-pointer upload route. Existing backend/G8B8/bump
checks pass alongside it, 4/4 in 3.34 seconds; guest graphics ABI passes in
1.88 seconds. Secondary backend and actual title results are tracked with
`beads-xbox.1.135` and the game campaign ledger. Other primitives remain guarded.
