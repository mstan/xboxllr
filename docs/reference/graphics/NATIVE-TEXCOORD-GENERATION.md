# Native texture-coordinate generation

Tracking: `beads-xbox.1.160`. Actual generated-native JSRF passes its SSE
rounding query and reaches Xbox texture state 28, TEXCOORDINDEX `0x10000`,
during scene setup. The previous state adapter admitted only source sets 0–3.

The prepared, attributed Phobos fixed-function shader already consumes
camera-space normal generation: world/view inverse-transpose normal,
normalization and stage texture matrix. The new adapter admits that existing
mode with source-set bits 0–3 on the qualified PHOBOS backend. Other flags,
out-of-range stages/sets and other generation modes remain rejected without
modifying cached state. Programmable vertex shaders retain direct output
coordinates and do not acquire fixed-function coordinate generation.

The independent rotated-camera fixture initially failed: +X became -Y instead
of +Y. The prepared donor uploaded the row-major inverse-transpose directly
into a column-major HLSL matrix, effectively applying another transpose.
The preparation adapter now packs both world and world/view normal matrices
as the untransposed inverse bytes; interpreted column-major, those represent
the required inverse-transpose. This fixes normal direction for the actual
consumer without changing the matrix/state ABI or editing the donor checkout.

The shared state value is retained verbatim. There are no title names,
addresses or fabricated coordinates in the adapter. No reference code was
copied for this change. The behavior was compared against
[JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/blob/36828c4d2d80bf8cd1b4962579a2cdb10f4f05a9/src/d3d/d3d8_shaders.c)
and [Microsoft's API description](https://learn.microsoft.com/en-us/windows/win32/direct3d9/automatically-generated-texture-coordinates).
The existing prepared renderer retains its own upstream notices and license.

The focused GPU fixture samples independently authored four-color texels with
four stages/source selectors, independent camera/world rotation and a
texture-matrix axis swap (192 expected samples).
Projection cancels the camera rotation for geometry, so a changed sampled
color proves that camera normal generation actually runs. Pass-through samples
prove source selection independently. Rejected states must preserve the
previous valid value. These are independent expected pixels, rather than two
entry points comparing their common shader implementation.

Both the fixed-declaration fixture (including all 192 expected samples) and
the baseline graphics fixture pass, 2.79 seconds combined. Actual optimized
JSRF stage `20261020-081611-qfjtkzlw` (SHA256
`7b44697819837fccbd7c6c6cd8ae99a4bdc7e8abf66ae4d8edfea3329594b182`)
passes this state and reaches the separate deferred texture-transform-flags
state 21 value 2. Original startup caches were populated in a fresh temporary
native state directory. Interactive title/menu and complete attract remain open.

Nonuniform normal-transform precision, other generation modes and exact NV2A
edge behavior retain their existing qualification scope. The current change
does not claim them or complete title/attract output.
