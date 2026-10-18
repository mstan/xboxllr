# Reached native bump-environment sampling

Forge of Hours Stage 1 (UI Round 8, original stage index 32) first stopped at
the native pixel-program guard for object `03e30000`. The retained diagnostic
run `campaign-018` captured its original 240-byte definition: texture modes
`0xc1`, PROJECT2D at stage 0 followed by BUMPENVMAP at stage 1. The prepared
renderer previously sampled the destination without a bump offset, and the
adapter correctly refused that incomplete route.

The independent preparation adapter now emits a dependent, nonprojective
lookup. It interprets unsigned source blue/green as signed byte offsets,
multiplies by the destination stage's live matrix, adds the destination XY
coordinates, then applies the linear texture's texel-to-normalized scale.
The existing Xbox-to-host state mapper exchanges the final two matrix slots.
Stage 1 reads stage 0; later stages use the original PSInputTexture selection.
Invalid stage-zero and forward/self-dependent lookups are rejected.

Contract references are [NV_texture_shader's offset texture operations]
(https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_shader.txt),
[Microsoft's bump matrix formulas]
(https://learn.microsoft.com/en-us/windows/win32/direct3d9/bump-mapping-formulas),
and local xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/xbox/nv2a/pgraph/glsl/psh.c`: BUMPENVMAP, sign3 and bump matrix uniforms.
No emulator implementation is copied. Prepared donor code stays in ignored
private build output with its original notices and source/prepared hashes.

Matrices use the existing per-draw constant upload. They are outside the
structural shader key: there is no CPU pixel loop, shader compilation or GPU
readback in the runtime conversion path. GPU readback is used only in tests.

`pilot_graphics_pixel_bump` exercises three complete native device lifetimes,
all matrix coefficients at stages 1–3, source-stage selection, signed byte
endpoints 0/127/128/255, real G8B8 inputs, distinct ARGB blue/red channels,
swizzled and linear destinations, non-unit Q without projection, output alpha,
live matrix changes and stable compiled shader identity. The existing complete
backend and G8B8 tests also pass: 3/3 in 2.51 seconds on this host.

Signed texture storage, luminance mode, other rejected texture programs and
silicon-exact filtering remain outside this new contract. This unit evidence
does not establish a campaign clear; actual title retry is tracked separately
in the game campaign ledger and `beads-xbox.1.7.7` / `beads-xbox.1.134`.
