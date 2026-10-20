# Classic SDK graphics admission

`xdk-d3d8-classic-cache-v1` binds an exact original image to the shared renderer.
The title owns hashes, decoded calling conventions and layout references. The
adapter contains no game address or game-name branch. Original bodies remain
compiled native code, and unsupported reached services stop explicitly.

The initial reviewed SDK 4134 profile covers 131 bodies. Two discovery signatures
needed correction against actual RET instructions: GetTexture2 writes through a
second argument; GetAdapterModeCount consumes only one argument. Tail bodies are
checked too. Compact render-state caches use the shared semantic mapping rather
than overwriting adjacent original globals. Pixel/vertex program caches, raw
transforms and the separate projection/viewport result retain original layouts.

A profile-declared, constant-time service table selects reviewed CPU-only mode
filters, allocation/register thunks, surface descriptions and light/material
cache routines. These retain their
original generated implementations. They borrow the real guest caller stack,
not the small temporary native service frame. This is static native execution;
there is no interpreter fallback. Active fixed-function lighting still requires
an admitted draw consumer. Gamma's original getter reads two source-proved
identity caches; setting a nonidentity ramp remains an explicit reached boundary.

The reusable presentation descriptor also admits two-back-buffer COPY mode.
There are four owned images: raster, two scanouts and depth. COPY exchanges the
scanouts' physical storage and completion owners while preserving logical
reference counts and physical texture aliases. The corresponding original
Data/Lock fields exchange too. RGB565 headers retain their original two-byte
pitch; native rendering continues under the explicit RGBA8 precision policy.
Exact RGB565/dither output and pixel-aspect display calibration are separate
precision work. Extended COPY Reset is guarded before replacing any storage.

Validation: 25 source/ABI/profile tests pass, including owned original classic
and LTCG fixtures, seven negative classic admissions, additive gamma-proof
upgrades, preserved original arguments and drift rejection. Fourteen combined
Windows tests pass in 15.62 seconds, covering real COPY/Gaussian GPU pixels,
physical alias history, unequal surface reference counts, initializer/cache,
presentation, commands, AV, input and worker waits. Portable descriptor and
initializer fixtures pass GCC. Original-image metadata and pixels stay private.

Reached classic resource ABIs include out-pointer texture/surface creation and
range-checked vertex locks. Native L8 mip storage/sampling and linear RGB565
uploads, readback and actual render-target clears pass GPU checks. Optional
XInputPoll retains the automatic report interval and owned-handle lifetime;
old eight-entry profiles retain their exact header/wrapper text.

The source-proved tile cache retains eight six-DWORD descriptors. Native RHI
images hold logical color/depth, abstracting physical tile/Z-tag encodings.
Only reviewed flags and aligned owned contiguous ranges are admitted; other
layouts stop explicitly. Null and null-memory descriptors clear the cache.
Hardware register acknowledgements are never fabricated. The LLE tile model
remains separate, and physical compressed byte identity is not claimed.

Native audio adds public AddRef aliases and reports actual native buffer/page
ownership through the Xbox four-DWORD GetCaps layout. Spatial voices report
zero until their consuming services are implemented. These are native service
capacity reports, not a claim of exact MCPX allocation counters. Unknown device
methods stop by name rather than being mistaken for buffer interfaces.

Validation of this follow-up: 25 source/profile tests and seven combined actual
GPU/audio/input checks pass (1.75 seconds for the Windows batch). Audio caps
follow allocated versus borrowed sample storage without touching canaries.

Actual JSRF now passes device/input/sound setup, resource descriptions, depth-tile
setup and begins real game-data loading. The next required boundary is native
asynchronous file APC delivery, at 2483 entries. No complete title frame, audio,
movie or attract cycle has yet passed. Dashboard/Blinx previews remain frozen.

Numeric API/layout facts were checked against the owned original routines and
the nxdk/Cxbx declarations; no Cxbx implementation was copied. Native rendering
continues to use the already attributed Phobos/sp00nz donor and notices.
