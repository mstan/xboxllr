# Native edge antialiasing

Tracking: `beads-xbox.1.137`. This shared raster owner handles the reached
edge-smoothing request without title-specific rendering rules.

The original dashboard's text path enables edge antialiasing, then requests
multisample disable before drawing. Those setters must be handled together.
The original render-target setup distinguishes multisampling from ordinary
and supersampled rasters: it clears device bit `0x8000` for the latter,
including the admitted Gaussian `0x2222` mode. Either requested multisample
enable value leaves that supersampled raster in place. Partial masks and
additional guest sample types remain guarded.

Local xemu's GL renderer enables line/polygon smoothing only when actual
multisampling is disabled. Its Vulkan renderer records the missing direct
polygon-smoothing equivalent. D3D11 has no direct polygon-alpha-coverage
equivalent either. The native precision policy uses real four-sample coverage
for admitted triangle submissions, followed by a color resolve into the
original target. This precedes the guest Gaussian resolve and is independent
of the optional presentation AA enhancement.

The color destination is copied and replicated to every sample before the
draw. Existing shaders, interpolation, blending, color masks, viewport,
scissors and culling still produce the submitted geometry. The original
target remains the resource observed by subsequent draws and readbacks.
There is no CPU image readback or vertex expansion. Scratch images are reused
for matching target dimensions/format and released on reset/teardown.

This is an explicit native coverage approximation, **not qualified NV2A edge
alpha precision**. The exact coverage kernel, interaction with alpha test,
shared triangle edges and per-fragment alpha are deferred as NTD-14. The
current consumer requires a single-level RGBA8/BGRA8 color image with no bound
depth target. Active visibility counters, feedback sampling, the specialized explicit-blend path and the
donor's internal DrawPrimitiveUP path remain guarded while smoothing is
enabled. A reached required use becomes implementation work; these guards
are not coverage claims.

The two reached swath-width requests (`15`, then `4`) now retain their
original state under an explicit host-automatic raster scheduling policy.
The original caller publishes render state 76; nxdk names method `0x9f8`
SWATHWIDTH, and the reference recompiler maps that method to the same state.
Native GPU tiling owns this performance choice. Other requested values remain
guarded; exact NV2A work scheduling belongs to the existing NTD-12 timing
deferral, not to menu-pixel acceptance.

Focused GPU checks exercise fractional triangle edges, indexed and bound
submissions, destination/blending preservation, disable restoration, ordinary
and Gaussian storage and device recreation. Actual startup passes both AA
setters and the swath requests, opens controller port zero and submits native
edge-covered draws. It presents one black frame, then stops at
`D3D_BlockOnResource` (`1496e0`). That frame is not a recognizable menu.
Actual menu rendering and steady-state performance remain required.
