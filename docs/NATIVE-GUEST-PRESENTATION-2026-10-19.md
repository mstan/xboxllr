# Native guest presentation and optional depth

The reached original 5849 caller requests Gaussian supersampling `0x2222`,
one back buffer, no automatic depth, and interlaced flag `0x20`. Admitting
these values over the previous equally sized color surfaces and mandatory
depth would produce incorrect resources. The shared presentation descriptor
now owns this allocation/description contract separately from elective visual
enhancements. Existing `0x11` rendering with automatic depth remains supported.
Legacy generated headers retain their original `0x11`, automatic-depth and
zero-flag admission. Only the new proved profile enables the added combinations.
Elective internal upscaling or extra host supersampling combined with guest
`0x2222` is guarded: retaining enhanced output detail needs a separate enhanced
resolve owner. This does not affect Blinx's existing `0x11` visual mods.

## Original evidence and admitted contract

The exact-image profile retains all 101 original method bodies and wrappers.
It additionally proves the reset/resolve selector, depth branch, descriptor
Data comparison, and AA state/cache roles against original instructions.
The new resolve helper has an exact original body hash. Addresses and device
offsets remain profile data; shared runtime code contains no title address.

For logical 640×480 and `0x2222`, original ResetBuffers allocates a 1280×960
raster image at presentation array index 0 and a separate 640×480 resolved
image at index 1. Original Get2DSurfaceDesc reports sample type `0x2222` only
when Data matches index 0; index 1 reports `0x11`. Two executions of that
unchanged original routine with authored linear headers verified those
descriptions and unchanged output-tail canaries. This is a software component
check, not original GPU allocation qualification.

The native adapter now allocates both actual images and publishes corresponding
guest Size, pitch and sample descriptions. With automatic depth disabled,
there is no depth allocation, projected depth pointer, guest VA or reference
owner. Independent later depth targets remain subject to existing ownership
checks. Reset stages adapter resources before changing the donor presentation
owner, republishes headers, and releases previous offscreen binding ownership.
Failure during validation/staging preserves the previous presentation owner.
Teardown clears published fields and retires only present resources.

The immediately used AA enable value 1 and full mask `FFFFFFFF` are admitted:
the owned supersample raster covers all texels and its Gaussian resolve is
active. Disabling AA, partial masks, other sample modes and mode transitions
remain guarded. These operations are not advertised as host MSAA. The original
cache words use profile-proved generated macros; legacy generated headers do
not acquire new dispatch cases accidentally.

Flag `0x20` means interlaced; `0x40` means progressive. The new profile reports
requested display metadata. Host windows still use the existing progressive
presentation and nominal cadence owner; physical interlaced scanout/field
timing is not qualified. Legacy headers retain their existing enhanced display
metadata policy.

## Resolve, state and readback

At guest Swap the native renderer resolves the actual raster into owned image
1 before presenting that image to the window. Guest front-image readback and
persistence observe that same owned image. Raw surface-0 readback retains the
larger raster dimensions; generic logical diagnostic readback uses the most
recent resolved presentation. It does not force an early guest Swap.

The Gaussian reference is the local xemu NV2A Gaussian3 convolution: a 3×3
kernel with weights `outer([1,2,1],[1,2,1])/16`, linear clamp sampling at the
projected center and texel offsets. A dedicated RHI shader performs the pass;
the window box filter and elective FXAA are not substituted. Exact silicon
sample-center behavior, interpolation precision and final UNORM rounding remain
unverified. The authored pixel fixture permits one byte of conversion tolerance.

The pass saves/restores pipeline state and guest texture slots, including an
alias of the resolved image that D3D11 would otherwise unbind when selected as
an output. Both source-read and destination-write completion owners advance.
Default logical viewports map to the 2× raster once. Offscreen targets retain
their own scale and viewport; guest viewport constants distinguish those cases.
Elective internal scaling remains a separate factor; Gaussian kernel offsets
remain in guest raster texels. Elective enhancements do not change guest
surface descriptions or replace the guest resolve.

## Focused validation

### Programmable vertex sample coordinates, 2026-10-20

The original SDK viewport constants and programmable shader outputs use guest
sample-space pixels. In Gaussian mode that scale is two in each dimension.
The native clip conversion previously divided those outputs by logical extent
alone, enlarging and offsetting 3D menu geometry while fixed-function text
remained correctly placed. The shared draw consumer now divides by the original
pixel scale and logical viewport together; host enhancement scaling remains
owned by the backend. Scale-one Blinx behavior is algebraically unchanged.

A real GPU fixture submits an authored original NV2A MOV program using sample
coordinates and checks an interior white pixel and an outside black pixel.
It runs through two legacy device lifetimes and a separate actual Gaussian
lifetime, with resolved readback after presentation and teardown accounting.
Eighteen combined Windows checks pass in 9.17 seconds. Actual native home-menu
captures show corrected artwork/label alignment against the earlier original
execution reference. The owner-reported alignment error is resolved for those
sampled scenes; this does not qualify exact silicon sample-center precision.

### Earlier component validation

- Twelve D3D profile/ABI/migration tests pass, including actual original bodies,
  helper/field/sample drift rejection and exact header upgrades.
- MSVC `/O2 /W4 /WX` portable fixture passes parameter rejection, raster/logical
  extents, absent-depth projection, reference preflight and legacy depth.
- Isolated MSVC `/O2` Phobos preparation/backend build passes. The GPU fixture
  `pilot_graphics_test --guest-presentation` passes constant, impulse, edge and
  multichannel Gaussian reference pixels, raw/resolved readback, texture-alias
  state preservation, absent depth, offscreen viewport, reset to legacy depth,
  reset back to Gaussian and recreation. The existing `--visual` fixture also
  passes for legacy elective scale/AA/interpolation and front-buffer behavior.
- Both actual retained legacy Blinx headers and upgraded dashboard headers pass
  shared runtime MSVC syntax checks, using scratch include selection only.
- A partial scratch upgrade of the actual retained 101-method record preserves
  wrapper bytes, call records and layout and is byte-idempotent. It is marked
  NOT-LAUNCHABLE; source generated files and receipts remain unchanged.

These checks launch no title and establish no first dashboard frame or menu.
Root owns actual candidate integration. Private commands, source hashes,
original proofs and logs are retained under
`build/native-profile-checks-20261019/guest-presentation-*.aot-private.*` and the
recorded isolated Z scratch root. Owner binaries, saves and settings were not
changed. The preliminary graphics-only fixture read an existing donor default
configuration without changing it; final fixtures use an explicit scratch file.
