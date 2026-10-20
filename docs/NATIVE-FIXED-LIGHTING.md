# Native fixed-function lighting

Tracking: beads-xbox.1.167. The original generated CPU setters retain ownership
of light allocation, default creation, enabled-list ordering and material writes.
The shared draw owner samples that cache by value; no title name or original
address is built into the native lighting backend.

Classic profiles opt in with exact original helper/body, ABI, cache-reference,
selector and static zero-fill evidence. The first eight enabled records are
packed in original list order. Logical light indices may exceed seven. Dirty
flags are not enable flags. Malformed pools, links, undefined records and cycles
within the consumed list fail before snapshot publication. Failed publications
preserve the previous snapshot; device close and reset invalidate its admission.

The typed fixed shader retains the prepared renderer's transforms and texture
generation. A separate native constant buffer supplies view-space lighting:
ambient/emissive, directional/point/spot lights, attenuation and range, normal
handling, local/infinite viewer, specular enable and per-component material
sources. Absent vertex colors fall back to the uniform material. Original
semantic LOCALVIEWER is 104, COLORVERTEX is 105; front diffuse/specular/ambient/
emissive selectors are 111/110/112/113. These differ from desktop enum order.

Two-sided materials, active vertex blending and lit XYZ input without an
explicit normal retain guards. A title
reaching either makes its consumer required work. Degenerate or unsupported
light parameters likewise fail explicitly. Exact NV2A intermediate rounding,
denormal behavior and specular approximation remain precision deferrals; native
GPU equations do not claim hardware bit identity.
Unused directional-light position/attenuation/cone fields and disabled specular
power are ignored, including nonfinite contents. Nonfinite uniform material
channels remain conservatively guarded even when a vertex source could override
them; this unreached edge needs source-presence-aware admission if encountered.

The source-bound original bootstrap has zeroed front/back materials and no
lights on a fresh image load. This does not establish reset/destructor behavior
or authorize overwriting later application cache values. Native GPU tests and
actual title advancement are separate acceptance evidence.

The original JSRF consumer now renders its title/Press Start screen and 3D city
background after the qualified lighting draw. Its next observed stop is an
undiscovered native CPU entry, tracked separately as beads-xbox.2.4. Analytic
GPU samples cover ambient/emissive, directional and normal control, point
attenuation/range, spot cones, material sources, view rotation, local/infinite
viewer, specular enable and inert fields. Shared fixed/texgen, indexed,
initializer, immediate and beta-input regressions also pass. This is not a
complete attract acceptance or a hardware pixel-identity claim.

This path uses the same GPL-dependent Phobos reference renderer as the other
fixed-function consumers; see [attribution](../THIRD_PARTY_ATTRIBUTION.md).
