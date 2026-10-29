# Native initializer render states

Tracking: `beads-xbox.1.137`. This finite shared state batch addresses the
original dashboard initializer's reached `SET_DITHER_ENABLE` packet and its
immediate following controls. It does not establish a rendered menu or exact
NV2A pixel precision. See the central [deferral ledger](../runtime/NATIVE-TITLE-DEFERRALS.md),
especially NTD-04.

The exact original initializer and already admitted setter bodies are retained
in the private `dashboard-init-state-assessment.aot-private.json` receipt.
Its ten simple packets map to existing states 66, 64, 60, 62, 63, 57, 61, 58,
65 and 59, in that order. Only the dither packet adds a new simple-method
mapping. The named ZBias, NormalizeNormals and VertexBlend bodies were already
proved by the D3D profiles; their runtime cases now reach the shared owner.
No generated body, binding record or title profile changes are required.

| Xbox state | Admitted values | Native effect and guest cache |
|---|---|---|
| 65 DitherEnable | 0, 1 | Retain the request in the owned precision snapshot. Select the explicit `PILOT_COLOR_NATIVE_RGBA8` policy, using real native UNORM8 output rather than forwarding an ignored donor enum. |
| 149 ZBias | 0 | Clear actual D3D11 constant/slope/clamp bias while preserving other raster state. Publish original negative-zero bits `80000000` to cache 77/78 and disable offset enables 79/80/81. |
| 142 NormalizeNormals | 0 | Track the inactive control and its guest cache. |
| 137 VertexBlend | 0 | Track the inactive control and its guest cache. |
| 112 AmbientMaterialSource | 0, 2 | Track the inactive material source; 2 is the initializer's changed value. |
| 117 PointSizeMin | 0, float 1 | Track the inactive point control; float 1 is the initializer's changed value. |

Inactive controls are admitted only with the existing programmed or
pretransformed triangle consumers. Programmable vertices own their normal,
lighting and blend calculations; pretransformed FVF rejects normals and
weighted positions. Points, lines and general fixed-function vertex lighting
remain guarded. Other unchanged deferred controls retain the existing rule:
an unsupported value is accepted only when identical to the exact original
initializer baseline. This batch does not admit arbitrary material, point or
vertex-blend behavior.

Dither uses an explicit native precision approximation for the existing
RGBA8/XRGB8 format classes 6, 7, 12 and 1E. Enabling it on an unsupported target
or binding such a target with it enabled is rejected. Requests and the policy
are observable through `pilot_gfx_initializer_snapshot`; the policy is a seam
for a future source-grounded effect. Exact console dithering matrix and
rounding are unqualified. Local xemu GL delegates dithering to an
implementation-dependent host result and its Vulkan path leaves the effect
unresolved; those sources do not prove a matrix to reproduce. RGB565 and
general other-format consumers are not admitted by this approximation.

Simple setters preserve the original division of work: update native state,
then let their original caller publish the cache. Named setters publish the
cache after native validation. Invalid values fail before owner/cache or
raster mutation. Draw checks use finite vertex/topology tests and the owned
target-format cache, without new per-draw COM queries or register scans. The
real raster-state query occurs only when applying ZBias zero. Successful Reset
clears tracked requests/controls alongside the original guest-array reset;
rejected/failed Reset preserves them. Closing the device retires the snapshot;
recreation starts a new owner.

Validation:

- `tests/test_native_graphics_initializer.c` compiles and runs with MSVC
  `/O2 /W4 /WX`. It includes production `graphics_state.inc` with an authored
  backend and checks the ten original packets, caller-owned cache behavior,
  negative-zero and enable slots, deferred values, rejection without mutation,
  consumer bounds and format classes.
- Existing `pilot_graphics_test --initializer-states` checks real D3D11 raster
  bias clearing, tracked requests, invalid values, RGBA8 pixels with dither
  disabled/enabled under the declared native policy, and existing programmable
  raster/depth/perspective behavior. It also creates the reached Gaussian
  no-depth presentation, renders through its real 2x raster and present resolve,
  checks logical pixels and verifies failed Reset preserves state while
  successful Reset and recreation clear it. This is
  a native-policy test, not an original-versus-native dither comparison.
- Existing `--guest-presentation` and `--visual` GPU component checks pass.
  Actual retained Blinx and dashboard generated headers both pass shared
  runtime syntax checks. This is compatibility preparation, not a Blinx
  gameplay regression or an actual dashboard launch.

Private commands, source/donor identities and fixture hashes are retained in
`build/native-profile-checks-20261019/graphics-initializer-contract.aot-private.json`.
All fixture work uses isolated scratch products; no owner generated output,
game process, save, setting or framework pin is changed.
