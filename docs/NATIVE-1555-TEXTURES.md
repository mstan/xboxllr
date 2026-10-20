# Native 1555 textures

Tracking: beads-xbox.1.151. Standard source-bound Xbox texture formats 2 and 3
are A1R5G5B5 and X1R5G5B5. Both use two-byte Morton-ordered texels with tightly
packed mip levels. The original definitions agree with Cxbx-Reloaded
`XbD3D8Types.h` at `585c49a50af1255ab155099e06f24505f9c5a800` and the pinned
Phobos renderer's format declarations and B5G5R5A1 native storage mapping.

The shared guest resource owner retains original format/header/byte layout,
contiguous CPU backing and allocation lifetimes. The native upload copy forces
X1 alpha opaque without changing caller bytes. Its unchanged-input comparison
accounts for the ignored X bit, avoiding repeated uploads of identical XRGB.
A1 retains its actual one-bit alpha. Linear variants, volume/cube textures and
render-target usage are not admitted by this change.

Authored GPU checks cover all four mip levels of an 8x4 texture down to 1x1,
rectangular Morton order, both alpha endpoints and X-bit values, distinct RGB
endpoints/intermediate codes, unchanged and changed uploads, source preservation
and rejected render-target usage. Native storage readback is checked exactly;
actual sampled RGB is compared to independently calculated 5-bit UNORM within
one output byte and alpha exactly. A separate runtime fixture checks original
headers, 86-byte mip storage, offsets 0/64/80/84 and allocation retirement.
Both fixtures run three device lifetimes. Six combined 1555/G8B8/LIN565 and
deferred-state checks pass in 1.81 seconds.

Actual optimized native JSRF run `20261020-053406-jobkqrc6`, executable SHA256
`849c179352b8236f3c1e06793261bbe343a5c83081d65b01485d88281c53e834`,
loads its particle textures and geometry, passes format 3 and reaches creation
of stage sound buffers with flags `0x40110`. Spatial audio is the next explicit
boundary. Full attract completion is not established.
