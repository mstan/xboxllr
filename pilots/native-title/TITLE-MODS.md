# Trusted title graphics plugins

`PILOT_TITLE_MOD_SOURCE` supplies a reviewed, statically linked title registry.
It is empty in framework reference builds. The current ABI requires native
D3D11 rendering; configuration rejects another renderer. Generated original
functions and the supplied XBE remain unchanged.

The registry implements `pilot_title_mod_open` from `title_mod.h`, checks its
resolved data-only plan and exact title identity, and returns NULL when disabled.
It can compose callbacks for multiple trusted plugins. Packages cannot name a
DLL, executable or script to load. The current Blinx repository owns its bundled
widescreen manifest, resolver and plugin; this is not a general package installer
or a second launcher UI. The shared recomp-ui provider integration remains
separate work.

Callbacks run at existing HLE boundaries under the graphics owner lock:

- Before SetTransform copies its input, allowing a title to adjust guarded
  matrix data used by both its CPU consumers and the renderer.
- Before drawing, with borrowed CPU position input, the original API caller,
  format and depth state. An optional horizontal affine mapping changes raster
  placement without modifying vertex resources or original shader programs.
- Before presentation, allowing a title to choose the frame's display shape.

There is no instruction-wide or generated-function-wide dispatch. Disabled
registries leave the ordinary graphics behavior selected. Plugins must not retain
draw pointers, call back into guest graphics, or change buffers during submission.
Guest reads/writes are bounded to committed low RAM, at most 64 KiB per call;
writes require writable, non-executable data pages. Title/revision and expected
data guards belong in the title plugin, before effects.

Presentation accepts ratios from 4:3 through 32:9. This changes the host window
and the final resolve's aspect fit, preserving guest render-target dimensions,
viewport/resource queries, depth storage and feedback ownership. A title can
keep menus/movies at 4:3 while widening gameplay. This is anamorphic rendering
at the original internal resolution; it does not claim a higher-resolution
renderer. `pilot_gfx_fvf_placement` and the programmable raster mapping apply
the same screen-X affine transform after the original pixel snap.

The plugin is responsible for identifying UI separately from projected world
effects, keeping full-screen history passes intact and testing its chosen scope.
Build-time linkage does not enable a feature by default. Plan fingerprints and
active feature options must join any future implemented compatibility identity;
no cross-build/netplay/savestate compatibility is asserted by this ABI alone.
