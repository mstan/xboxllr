# Borrowed enhanced pixel shaders

Tracking: `beads-xbox.1.15.7`. This is the first integration checkpoint under the
[enhanced execution gameplan](GOAL-AOT-DASHBOARD.md), not a complete xemu renderer
transplant or a qualified shipping default.

## Selection and scope

Use `--nv2a-renderer xemu` with the usual machine and CPU options. Use
`--nv2a-renderer opengl` to opt out at startup, or `software` for the software
renderer. CPU AOT and dynamic fallback controls remain independent. No live
backend switch is claimed.

The enhanced path actually runs xemu's register-combiner/final pixel-shader
generator at `482088490c2d5983ed5e1eafbd09906157c1eca0`. Driver triangle coverage,
perspective interpolation, texture filtering and blending replace the reference
fragment shader's software-style edge/interpolation/filter/blend loops. The
existing device parser, vertex processing, clipping, owned texture snapshots,
resident surfaces and resolve/invalidate paths remain responsible for their
contracts. Source and modifications are recorded in
[the import notice](../src/third_party/xemu/README.md).

This deliberately reuses a production donor component without its QEMU machine
wrapper. It contains no fake IRQ, memory-map or completion implementation.
Upstream placeholder shader bodies are removed; unsupported operations are
rejected, not converted to a constant color or an ignored command.

## Boundary contract

`Nv2aGeneratedShaderPacket` owns geometry, textures, initial surface bytes,
uniform values and raw guest controls. The adapter consumes those values;
it does not retain pointers into mutable device state. Shader creation and
final geometry acceptance occur before destination effects. A declined donor
draw uses the existing generated reference shader; if that renderer declines,
the device resolves outstanding surfaces and runs the software path. Submission
errors after effects remain errors, not opportunities to silently replay.

Initial admission covers smooth postclip triangles with finite positive W,
valid projected coordinates, a final combiner, D24 depth representation, and
NONE/PROJECT2D texture modes. Textures must have owned data with supported real
linear or converted swizzled formats and nearest/bilinear nonmip filtering.
Texture border storage, color keys, signed/depth textures, texture Q poles,
wide proxy triangles, polygon bias, W buffering, fog and unsupported registers
retain the reference path. Alpha-kill and divergent shared constants are also
excluded: the current general reference evaluator does not yet implement those
contracts (tracked in `beads-xbox.1.15.16`). Donor support alone cannot qualify
a replacement for a missing floor.

X8 color surfaces preserve the stored FF padding byte while blending with the
actual source alpha through dual-source output. The raw combiner control word
is retained so unique/shared constants cannot silently acquire different
semantics. Inactive routing selectors do not reject independent texture modes.

## Qualification and observability

`test_nv2a_xemu_psh` compiles and executes real shaders against synthetic owned
packets. It checks constant selection, perspective interpolation against an
independent barycentric result, nearest/bilinear/rectangle textures, X8 blending,
alpha discard, unsupported state/geometry refusal and actual reference fallback.
It reports a skip when no OpenGL context exists, rather than claiming GPU checks
passed. These are authored contract checks, not hardware capture evidence.

Numerical GPU checks allow one 8-bit channel unit around independently computed
interpolation/filter/blend values; constant colors, texture identity, discarded
fragments and fallback pixels have exact expectations. This does **not** define
a broad whole-dashboard tolerance: repeated blends can accumulate larger changes.
Keep this path experimental until full-route visual differences, guest behavior
and performance are characterized. Never accept missing menu objects, control
flow changes or stale guest memory as a numerical tolerance.

Shutdown prints `xemu-psh attempts/draws/triangles` and reference-decline counts.
The first occurrence of each decision prints its raw texture controls. Existing
generated-shader upload/resolve/draw counters and frame-series windows remain
available. Attempts and eligibility do not establish execution; actual donor
draws must be nonzero. Guest vblanks, host presents, distinct images and guest
execution speed are separate measurements. A partial donor integration and an
AOT instruction percentage cannot establish a 60 FPS result.

Live-run evidence belongs in the dated results note and STATUS. The next early
game checkpoint remains a real Blinx launch with partial AOT; full shader
coverage and full dashboard AOT do not gate that work.
