# Native texture color operations

Tracking: `beads-xbox.1.165`. The PHOBOS native renderer now admits Xbox
fixed-function COLOROP 8 through 14 through the existing shared texture-state
owner. There is no title-specific dispatch or alternate draw pipeline.

For each RGB component, with normalized arguments A/B, these operations are:

| Xbox value | Operation | Component result |
|---|---|---|
| 8 | ADDSIGNED | clamp(A + B - 0.5) |
| 9 | ADDSIGNED2X | clamp(2 * (A + B - 0.5)) |
| 10 | SUBTRACT | clamp(A - B) |
| 11 | ADDSMOOTH | clamp(A + B - A * B) |
| 12 | BLENDDIFFUSEALPHA | B + (A - B) * diffuse alpha |
| 13 | BLENDTEXTUREALPHA | B + (A - B) * sampled texture alpha |
| 14 | BLENDFACTORALPHA | B + (A - B) * texture factor alpha |

The pinned prepared renderer already consumes these operations. Admission
extends only its checked color path; ALPHAOP keeps its earlier 1..7 scope.
Later Xbox/desktop enum values diverge and remain separate qualification work.
An unsupported value must not silently fall through to backend multiplication.

The independent GPU fixture exercises all seven operations on all four stages,
including saturation, complemented texture and replicated texture alpha.
Diffuse, texture, factor and prefix CURRENT alpha differ, so choosing the wrong
blend source changes the expected pixels. It checks output alpha independently,
recreates the device, and checks rejection/cache preservation for later color
operations and ALPHAOP14. Run `pilot_graphics_test --texture-color-ops`.

This establishes the native component equations and shared state behavior.
Exact NV2A intermediate fixed-point rounding remains a retained LLE/physical
precision question; this fixture allows one final UNORM byte of rounding.
Actual title progression is recorded separately in its native attract burndown.
