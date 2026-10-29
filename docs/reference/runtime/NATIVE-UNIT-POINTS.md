# Native unit-point raster

Tracking: `beads-xbox.1.162`. Actual generated-native JSRF
`20261020-082921-t_tj4dop` submits one buffered POINTLIST vertex from original
`0x155d30` through `D3DDevice_DrawVertices`. Its original FVF is `0x112`;
size is 1, minimum 0, maximum 64, sprites and distance scaling disabled.

The shared draw owner admits this fixed-function buffered case, retaining the
same vertex resource/range, transform, texture, depth, blend and state owners.
It validates active size/minimum/maximum, disabled sprite/scale/edge-AA and
the absence of programmable point-size output. Indexed, user-pointer and
other active point behaviors remain explicit boundaries. Inactive attenuation
coefficients retain their original cache. No title name/address is used to
admit a draw and no sprite or particle is silently dropped.

The backend submits the existing native vertex buffer as D3D11 POINTLIST.
For flat shading it removes the triangle color-broadcast geometry shader:
one point already has one color. Original triangle/quad behavior is unchanged.

Independent GPU checks compare every pixel of twelve complete images: one or
several points, three first-vertex offsets, and both flat/Gouraud states.
Expected point positions and colors are authored independently. The baseline
graphics and fixed-declaration/FVF suites, including the 384 texture-generation
samples, pass (2.11 seconds combined). Program/pretransformed initializer
admission remains rejected for this point scope.

Actual optimized JSRF stage `20261020-083517-djuzc85a`, SHA256
`b95fefbf420d74e16134fe76ebacda92816ab1069345c541ff0cae486b3d86fb`,
passes that original point draw and reaches a separate immediate-mode
`D3DDevice_Begin(6)` call. Captures still show Now Loading. Complete title,
movie/demo progression and repeating attract remain open.

Larger/sprite points, attenuation, programmable size, other source forms and
enhanced-resolution point scaling require their own qualified raster owner.
This bounded implementation preserves that interface for later extension.
