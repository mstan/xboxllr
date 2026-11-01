# Deferred vertex state relevance

Tracking: beads-xbox.1.149. Inline original setters update the guest render-state
cache before the next native draw. Unsupported state must be checked at its
consumer: an inactive parameter is not an unsupported rendered effect.

The semantic mapping follows the original source-bound cache and the state
definitions in Cxbx-Reloaded `XbD3D8Types.h`, reference revision
`585c49a50af1255ab155099e06f24505f9c5a800`. Lighting parameters 104–111, 113 and
114 feed fixed-function vertex lighting/material selection. Parameters 116 and
118–123 feed point rasterization. The native owner preserves and validates
their actual cache values. State 112 and POINTSIZE_MIN 117 retain their already
qualified initializer ownership. SPECULARENABLE and texture wrapping retain
their active native consumers.

Every lit fixed-function draw still stops before native submission. Every
point/line primitive still stops at the triangle-only primitive guard. Binding
a pixel shader also samples the cache, so it must not claim that a vertex
consumer has run. Native programmable and pretransformed triangle draws do not
consume these vertex lighting or point fields.

The focused native runtime fixture creates a real device, stores 17 nondefault
parameters, submits a pretransformed triangle with LIGHTING enabled, checks its
red pixels against a blue clear, and confirms the cache survives. It runs three
device lifetimes. Four subprocess checks require the exact guards for a lit
fixed-function draw, point primitive, invalid deferred boolean and nonfinite
point coefficient. The broad lifecycle ABI fixture requires entries absent
from the dashboard profile and is not counted as evidence for this change.

The two focused tests pass in 0.91 seconds. Three additional GPU regressions
cover shader rasterization, fixed declarations and initializer consumers.
Actual optimized JSRF run `20261020-052823-0r2cuzue`, executable SHA256
`8e5e859bc907f8f2605d65bd111647293bd8d8100c469a253dc7b1a0985c5976`,
passes these parameters, submits logo draws and loads the subsequent particle
assets. Its next explicit guard is texture format 3. This establishes actual
startup progression, not complete scene or attract acceptance.
