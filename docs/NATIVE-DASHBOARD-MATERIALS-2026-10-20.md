# Dashboard material and transition follow-up

Tracking: `beads-xbox.1.174`. The owner reported a washed-out Memory panel,
an overly bright console model in Credits, and short Home-menu entry hitches.
This follow-up uses the optimized native title path and isolated state/media;
it does not repeat campaign qualification or use physical hardware.

## Memory panel

The original draw selects dark green `TFACTOR` RGB (`0xaf041400`) and
modulates its alpha with an unbound texture. The prepared renderer disabled
the whole stage when either RGB or alpha referenced that empty sampler,
discarding the independent RGB selection. The shared fix preserves RGB when
only alpha samples the empty texture. That sample supplies neutral alpha one.
No Dashboard-specific color substitution is involved.

`pilot_graphics_null_texture_alpha` checks actual D3D11 draw/readback against
independently calculated source-alpha blending. Before the fix the factor
case returned red `(255,0,0)` instead of `(2,16,32)`. Afterward the factor and
diffuse cases return `(2,16,32)` and `(128,0,32)`. The existing later empty-RGB
stage compatibility used by Blinx's movie is also checked and retained. Its
broader precision question remains [NTD-17](NATIVE-TITLE-DEFERRALS.md).

## Credits decoration

The console uses the original gray falloff material, with a captured RGB
coefficient of `229/255`; this is separate from the Memory-panel defect.
The Credits asset mod now requests console opacity `0.45` for readability.
A state capture after the transition settles confirms alpha `0.45` reaches
the original model shader. Credits and Licenses share this decoration;
ordinary System Info restores opacity one. This is a scoped styling choice,
not a claim that the original console material was darker.

## Compiled shader reuse and remaining hitch

Native vertex shader creation now uses the prepared renderer's persistent
compiled-code cache. Its key includes full source, entry, profile and
optimization settings, plus the native D3DCompiler47/VS5/O3 backend tag.
Device shader objects and input layouts retain their existing ownership.
Unavailable, disabled or rejected cached bytecode falls back to compilation.
Other renderer builds retain their existing compiler path.

Three combined GPU checks pass: ordinary graphics, fixed declarations and
the new empty-alpha case. Cached native shader hits occur during the pixel
checks and real Dashboard navigation. The fixed-declaration GPU check also
passes with the cache disabled and with deliberately invalid bytecode in an
isolated cache copy; the latter exercises rejection and recompilation.

The 47.6-second native navigation batch covers cold/warm Memory, Music and
Settings, Credits/Licenses, ordinary System Info restoration, reopening and
Home return. Captured images were inspected; no guard, fault or script error
was reported. Eight authored Credits checks pass. A separate settled-model
capture verifies the decoration's alpha rather than inferring it from a still.
Private evidence is under
`G:/Temp/Codex/xboxlle/dashboard-review-20261020`.

**The menu-entry hitch remains open.** TCP screening observed pauses up to
171 ms in this run, versus up to 93 ms in the corrected baseline. These are
busy-host frame-advance observations, not isolated presentation timings or
evidence of an end-to-end speedup. Cache reuse avoids repeated compilation;
it does not establish that compilation caused every reported hitch.
Owner visual review can proceed without repeating long unattended loops.
No new Blinx/JSRF title run or submodule-pin update is claimed here.
