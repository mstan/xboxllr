# Native presentation surface projection

This records the initial projection/reference batch. The subsequent
[guest presentation contract](NATIVE-GUEST-PRESENTATION-2026-10-19.md) adds
Gaussian raster/resolved dimensions and optional depth; its ownership mask
supersedes the three-mandatory-surface assumption below.

This fixes two source-proved gaps in the reviewed LTCG graphics adapter before
the first frame. It adds no renderer methods or GPU algorithms. The retained
original caller supplies public CreateDevice behavior `0x40`; the original
driver separately masks `0x10` for internal state. Those are independent
contracts. Profile data now records both, the binder verifies the original
`AND 0x10`, and the shared native public policy accepts `0x40` as the existing
state-cache adapter does. Unsupported public bits still stop.

The exact-image profile also records presentation and current-target offsets.
The binder checks their operand roles against the original reset-buffer,
device-initializer and SetRenderTarget bodies, alongside their existing exact
body hashes and RET checks:

| Field | Original role | Native projection |
| --- | --- | --- |
| Count | BackBufferCount + 1 | Two owned colors for the existing one-back-buffer backend contract |
| Presentation color array | Index 0 and index 1 color surfaces | Stable native header 0 and header 1 |
| Presentation depth | Separate depth surface | Native header 2 |
| Current color/depth | SetRenderTarget state; NULL color retains current | Actual selected color/depth headers |

The initializer selects presentation color index 0 as its initial current
target. Index 1 is the other presentation color, not depth. This establishes
the required header projection for the observed inline getters. It does not
qualify a standalone LTCG GetBackBuffer2 API: this original method inventory
does not contain one. The exact offsets and title addresses remain in profile
data, with generated macros consumed by the shared runtime.

Creation publishes the owned array, depth and current fields. SetRenderTarget
updates only the current fields, leaving the presentation pointers stable.
Successful Reset republishes the owned presentation fields and default
current targets. Device release clears all published pointers and count
before releasing backend objects or guest header storage. Without the new
`PILOT_D3D_SURFACE_PROJECTION_VERSION` macro, legacy generated headers retain
their existing projection and reference behavior.

## Inline reference ownership

The original caller increments the surface Common low 16 bits directly around
a descriptor query and usually decrements them inline afterward. Replacing a
later named AddRef/Release with host-counter-only bookkeeping can overwrite a
real guest reference or miss an outstanding reference on device release.

`graphics_surfaces.h` is the production portable helper used by the new
profile. At named surface AddRef/Release, render-target ownership transitions,
Reset and device release, it reconciles both increases and decreases between
the live guest count and owned backend references. Each difference invokes the
existing real backend AddRef/Release interface. A named operation then applies
its own one-reference change. The device's underlying base ownership remains
alive even when the guest count is zero. Descriptor queries do not synchronize
temporary inline references; they do not transfer ownership.

The helper first validates the owned header's structural Common bits and
16-bit count bounds. Overflow/underflow stop before backend mutation. When
reconciling all three surfaces, all headers are validated before changing any
backend reference. Backend failure or count disagreement stops immediately.
Device release therefore sees inline-held references and rejects premature
retirement before closing the device. A later inline decrement is reconciled
at the next ownership boundary, so the backend reference is balanced.

The existing backend Reset discards external presentation references and
rewrites headers. The new projected profile explicitly guards Reset while a
guest surface reference is outstanding, rather than silently invalidating it.
Legacy Reset behavior is unchanged. Supporting externally retained presentation
surfaces through replacement is a separate owner/lifetime contract.

## Validation

- Eleven profile tests passed, including the actual original 101-method
  adapter, exact body/RET/layout rejection, distinct driver/public masks,
  presentation/current field drift, and both previously emitted header forms.
- MSVC `/O2 /W4 /WX` passed the no-GPU production-helper fixture:
  `tests/test_native_graphics_surfaces.c`. It tests initial/current/reset/clear
  projection, unchanged neighbors, inline increases and decreases, mixed
  named/inline references, base-owner lifetime, balanced backend calls,
  corruption, overflow, underflow and all-header preflight.
- An isolated copy of the actual retained dashboard generated tree migrated
  successfully. All prior binding facts, original generated bodies and wrapper
  bytes stayed unchanged; only the new policy/layout record and header changed.
  Repeat bind was byte-identical. The source generated tree was unchanged.

The scratch migration is explicitly not launchable. No GPU, title execution,
CPU regeneration, owner binary, settings or generated output was changed by
these checks. First-frame rendering and actual reset/device lifetime remain
integration validation. Private receipts and original evidence are under
`build/native-profile-checks-20261019/` in
`dashboard-firstgraphics-preview.aot-private.json`,
`native-d3d-surfaces-migration.aot-private.json`, and
`native-graphics-surfaces-contract.aot-private.json`.
