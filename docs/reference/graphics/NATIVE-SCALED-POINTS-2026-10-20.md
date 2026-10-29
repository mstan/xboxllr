# Native scaled point sprites

Tracking: `beads-xbox.1.182`. JSRF's late-game sequence `0811 -> 0812 ->
0880 -> 0890` reaches buffered XYZ+diffuse points with size 0.3, minimum 0,
maximum 64, sprite and distance scaling enabled, A=B=0 and C bits `404b63fa`.
The former one-pixel-only owner correctly stopped here.

The shared renderer now expands each point after the real fixed-function
vertex stage using a cached GPU geometry shader. Eye-space distance and the
original logical viewport height determine size; logical pixel offsets also
follow host resolution scaling without changing guest geometry. Colors, fog,
depth and W retain their vertex values. Sprite UVs replace Xbox texture set 3;
sets 0-2 retain their existing values. Screen-space inputs bypass attenuation.
Point clipping uses the center. Triangle fill/culling cannot remove or outline
the square, and the original raster state is restored after the draw.
Ordinary unit points retain their direct POINTLIST path.

The existing image-bound profile identifies original `D3D_LazySetPointParams`
at `0x195100`, 357 bytes, SHA256
`a50c80b51825812583765aa26962f427bd62215ed9bc799e3eca624de16eb329`.
Its parameter conversion establishes the viewport-height/attenuation algebra.
The [Microsoft point-sprite contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/point-sprites)
describes the API-level sizing and constant vertex attributes. Local xemu
reference `482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/xbox/nv2a/pgraph/glsl/vsh-ff.c` and `psh.c`, supplies comparison facts for
the 63.875 hardware cap and Xbox texture-set3 replacement. This adapter is an
independent implementation; reference behavior is not hardware qualification.

Targeted GPU tests compare full images for independent A/B/C attenuation,
minimum/maximum clamps, changed viewport height, color/fog, four texture
quadrants, screen-size bypass, unit-point reentry and device recreation.
Existing fixed-declaration and pretransformed-fog tests remain required.
JSRF run014 renders the formerly failing introduction and naturally returns
to gameplay state15. Forced scene entry leaves the player outside the arena;
boss spawning, mechanics and completion are not passed by this rendering fix.

Per-vertex/program point sizes, indexed/UP point source paths and edge AA
remain explicitly guarded. Exact NV2A point-size quantization, subpixel edge
rules and reciprocal precision remain supporting precision work after content
coverage; do not use that deferral to hide missing sprite output.
