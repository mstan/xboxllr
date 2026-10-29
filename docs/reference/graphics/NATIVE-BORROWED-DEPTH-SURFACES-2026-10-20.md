# Caller-owned native depth surfaces

Tracking: `beads-xbox.1.183`.

The reached JSRF boost path passes a standalone depth surface to the original
SetRenderTarget boundary. Its original metadata queries succeed, but the native
bridge previously attempted to adopt every unknown target as a texture. The
surface has no native parent texture, so the texture-only layout guard stopped
the call. The original header describes LIN_D24S8, 512x512, 2048-byte pitch,
and caller-owned contiguous storage. Its data does not alias a registered native
texture or a presentation surface.

The shared ownership table now distinguishes a standalone surface from a
texture and from a texture's surface view. A caller-owned LIN_D24S8 surface
retains its original header and physical allocation while the existing native
depth/stencil implementation supplies GPU storage. Initial packed CPU bytes
are imported on the first target handoff. Exposure tracking preserves later
GPU writes across unbind/rebind instead of restoring the stale CPU image.

Target references, physical borrows, GPU serial retirement and caller-header
immutability use the existing resource contracts. A surface cannot be bound
directly as a base texture. This path is shared across titles and contains no
game addresses or title-specific branches.

The admitted layout is a single 2D LIN_D24S8 level with canonical 64-byte-aligned
row pitch and owned physical backing. Other surface formats, noncanonical row
pitch and caller-owned GPU-surface CPU locks remain explicitly guarded; they
need implementation when a real consumer reaches them. No broader surface or
hardware precision qualification is implied.

`pilot_graphics_borrowed_depth_surface` passes across three device lifetimes.
It checks distinct initial depth/stencil values, actual GPU clears, preserved
GPU values across rebind despite stale CPU bytes, target references, caller
header/data retention after teardown and the original memory budget. Together
with the vertex-header-storage and deferred-state pixel regressions, the final
source passes in 1.51 seconds.

Private run028 reaches actual Garage gameplay, then performs two original
boost activations. Each spends ten cans, renders the boost blur/fire trail and
returns to ordinary gameplay without the former stop. The same process then
loads `3003c`, responds to movement, and revisits `20028` through its original
introduction without the earlier resource stops. Resource refills are
explicit QA prerequisites, not pickup passes; the earlier run024 independently
establishes actual collection. Repeated area handovers are checked separately.
Original metadata routines continue through their generated native CPU path.
Exact effect pixels and other supporting oracle/hardware work remain deferred.

Private diagnostics, executables, source symbols and captures remain outside
the repository. No original game code or assets are distributed with this fix.
