# Shared native vertex inputs

Tracking: `beads-xbox.1.137`. The shared renderer now handles both current
vertex attributes and XYZ FVF layouts. These are reusable input owners;
there is no dashboard-specific draw substitution or interpreter path.

## Current attributes

A program input without an enabled array reads the current register value.
The original shader constructor copies its declaration/program without
consuming vertex arrays. It does not remap the dashboard's declared register
6 to the program's register 7. Eager native compilation previously rejected
this valid missing-array class.

The native shader now reads undeclared inputs from a 256-byte GPU constant
buffer. Declared inputs retain their existing typed streams. The provider is
the prepared renderer's existing current-value owner, including changes after
shader creation or binding. Only changed values require an upload; vertex
decoding remains on the GPU. The diagnostic stream-output path uses the real
bound provider/buffer too.

The shared packet owner accepts aligned, non-position 4F, 2F and 4UB attribute
updates. Validation performs no mutation. Unsupported formats and position
setters remain guarded: position writes can emit vertices and need an
immediate-mode owner. Device close releases the buffer; recreated devices
start with the native reference's default policy. Exact original context
initialization is separate precision question NTD-15, rather than a claim
that native defaults prove cold hardware state.

Source basis: Phobos reference `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`,
`src/d3d/d3d8_vsh.c` current-value initialization/get/set and disabled-array
shader generation; xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/xbox/nv2a/pgraph/pgraph.c` NV097 attribute methods. Existing reference
notices and donor preparation remain intact.

## XYZ FVF

The original profile-proved FVF decoder establishes FLOAT3 position and
FLOAT3 normal for ordinary XYZ FVF. Packed CMP normals belong to the
declaration format and remain supported by the same fixed-function core.
Shared layout decoding now admits XYZ with optional normal/colors and up to
four texture coordinates, alongside the existing XYZRHW family. Weighted
positions, reserved bits and unowned combinations remain guarded.

Original queryable descriptors retain their flags, offsets and coordinate
dimensions. XYZ draws use the existing typed fixed-function GPU shader and
original cached matrix owner; XYZRHW retains screen placement. Compiled XYZ
shaders are cached by source FVF and released with the device. Rebinding or
changing matrices does not recompile them. The reached fixed-function
consumer still requires lighting disabled; a required lighting dependency
must be implemented rather than counted as a precision deferral.

Source basis: the selected XBE/profile's complete 314-byte FVF decoder
(`d1bcf57dd943dd14162961d315e41ed8ed290f54cabaa5a0e28218910de903b8`).
Seventeen unmodified decoder calls also executed under Unicorn as private
descriptor evidence. This does not qualify silicon raster precision.

## Acceptance and current boundary

Fifteen combined Windows checks pass. Current-input checks exercise the real
bound GPU buffer, updates after creation/binding, mixed stream/current inputs
versus the maintained LLE vertex reference, guards and two device lifetimes.
XYZ checks cover five layouts, normal/color/texture inputs, two matrix states,
stable cached shader identity and actual pixels versus the direct native
fixed-function reference. Runtime checks cover original descriptors and
cached matrices, plus earlier graphics/audio/scheduling regression.

Actual native dashboard startup passes both former stops. A bounded muted
20-second run captures 17 presentations: the first is black; subsequent
images show the dashboard's green 3D background. No recognizable menu text
or choices are visible yet. The process runs until the diagnostic deadline;
this establishes progression, not responsiveness or audio acceptance.
DB03 remains active. Next trace actual menu/text submission and its state.

That checkpoint is superseded by the [transform-cache correction](NATIVE-TRANSFORM-CACHE.md):
the main menu now renders and DB03 passes. Returning from Music exposes the
next required resource-destruction contract under DB04.

Optional raw draw/frame-input formats do not yet serialize current values or
XYZ fixed-function transforms; explicit guards preserve that boundary under
NTD-13. Ordinary native GPU image capture remains available. The iteration
build now explicitly depends on the shader-reference executable, preventing
a stale binary from supplying apparent validation of changed shader code.
