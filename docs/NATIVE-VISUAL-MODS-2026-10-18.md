# Native title visual mods

Elective, statically linked visual enhancements are selected by data-only
packages and a resolved v2 plan. Disabled features preserve original rendering
and timing. This work does not reopen the independent LLE qualification floor
or change generated guest CPU register layout.

## Implemented scope

| Feature | Implementation and practical limit |
| --- | --- |
| Internal resolution | Original, 1080p, 1440p, 1800p and 2160p. Height is independent of window size. Width follows the initial mod aspect, up to 32:9. |
| Supersampling | Original, 2×2 or 3×3 relative to internal resolution; resolve to the output window. Reject dimensions above 16384 before allocation. |
| Antialiasing | Independently written fast approximate presentation AA. Filters the composed HUD too, so fine text can soften. |
| Texture filtering | Optional 2×/4×/8×/16× anisotropy through the donor's real sampler policy. Ordinary texture sizes are unchanged. |
| Interpolated presentation | GPU frame blending at a fixed 60/120 Hz host target. Original simulation, input, audio, vblank callbacks and guest swap/completion counts remain authoritative. Adds one source frame of presentation latency and can show double edges. It is not optical flow or extra game updates. |
| Actual higher-rate updates | A distinct generic mode and plan operation, mutually exclusive with interpolation. Not implemented or advertised as available for Blinx. Requires a qualified title timing adapter. Requests fail explicitly. |

The initial internal canvas remains fixed for the device lifetime. Adaptive
widescreen still follows the live output aspect for projection and HUD
placement. Resizing the window changes presentation, not every live scene
allocation. An ultrawide 2160p image contains more pixels than 3840×2160;
supersampling multiplies that cost again. No automatic quality switching occurs.

The AA algorithm uses local contrast, edge orientation/search and subpixel
filtering; its algorithm reference is the
[NVIDIA FXAA white paper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf).
The shader is independently written and claims no official preset or bit identity.

## Ownership and correctness boundaries

Guest surface descriptions, viewports, pitches, CPU uploads, readbacks and
front-buffer ownership retain their original logical sizes. Native scene color
and depth allocations use the chosen raster scale. CPU access crosses explicit
resampling boundaries; ordinary draws do not copy the framebuffer to the CPU.
Pixel-center sampling preserves logical CPU roundtrips at noninteger scales.

Scale the presentation-aspect render-target family, including half-sized scene
targets. Square lookup/data/shadow targets retain their authored sampling grid;
render-target usage alone is insufficient evidence to rescale them. This policy
is separate from allocation and can be extended with future title evidence.
Depthless passes can borrow a differently scaled depth allocation. An actual
depth/stencil-tested draw with incompatible grids stops explicitly rather than
silently testing unrelated pixels. A future reached case needs an explicit
resource-family classification or depth bridge, not a global scale override.

AA and interpolation own their output images and shaders. Their passes restore
the native outputs, viewports, shaders, layout, topology, constants, textures,
sampler, blend, depth and raster state they touch. The GPU tests continue an
already-bound guest draw directly after AA, without a donor rebind. Reset,
device recreation, output resize, menu/pause and stage changes clear appropriate
presentation history. Guest history/depth textures never contain synthetic
frames. Blinx supplies only phase/pause/stage scope keys; the worker never calls
the title plugin or executes guest code.

The presentation worker uses a waitable timer and the existing graphics lock.
It skips a deadline while the graphics owner is busy, instead of entering
guest scheduling or sharing the immediate context concurrently. Thus 60/120
are targets, not guaranteed measured rates. Synthetic draws add no guest
submission serial, swap, callback, clock tick or fabricated completion. Its
two history images are bounded and reused. Closing signals and joins the
worker before releasing the device; it never blocks acquiring the owner lock.

A correctness repair also resets cached logical guest dimensions on fresh
device creation: dimensions left by an earlier Reset must not shrink a later
XYZRHW draw. This repair applies with all enhancements disabled.

## Launcher and visibility

`pilots/native-launcher` is a separate generic recomp-ui host. The game resolver
still validates its exact identity, trusted catalogs and complete plan. A
bounded data-only handoff carries staged choices; malformed results cannot
partially replace the current selection. Packages cannot supply executable code.

The UI is pinned to `ccc186e097b362555292b157ffd303abcea26fbf`, the previously
reviewed Xbox black/green theme. `tools/native_launcher_ui.py` records the clean
source revision, archive hash and prepared-file inventory. Its explicit host
profile adapter shows the consumed audio-volume/mod controls, hides unsupported
controller/video controls and omits the shared default SNES brand. It does not
offer package installation when the provider has no installer. It does not
modify the source UI checkout. Configuration verifies the prepared inventory.
The launcher stages retained dependency/asset notices; Windows uses SDL2's
official SDK in the current validated build.

Each feature defaults off. Saved selections live outside bundled catalogs.
`mods/ui-visibility.json` may hide a feature or individual choices, independently
of native implementation and capability filtering. For example:

```json
{
  "version": 1,
  "hidden_features": [],
  "hidden_choices": {
    "xboxlle.enhancement.visuals/resolution/height": ["2160"],
    "xboxlle.enhancement.frame-rate/frame-rate/mode": ["actual"]
  }
}
```

An active choice that becomes hidden is reset and its feature disabled. It
cannot remain active invisibly. Capability filtering separately keeps
unimplemented/unqualified modes out of the picker. Frame rate is one mode
choice, never two independently active switches. The strict plan parser also
rejects duplicate frame-rate operations before any effects.

## Validation and bounded deferrals

Focused checks cover real 1080p/2160p/SSAA rasterization (not just upscaling),
logical resource APIs, full scaled color/depth CPU roundtrips, aliases, Reset,
the existing exact float-blend oracle at 1080p, AA edge changes/flat-region and
guest-buffer preservation, native state continuation, GPU blended midpoints,
stalls, scope cuts and reset at 60/120 targets. Presentation samples leave guest
front-buffer bytes and submission counts unchanged. Policy/provider tests cover
independent defaults, dimensions, parser failure atomicity, capability filtering,
visibility, mutually exclusive modes and the compiled launcher's data roundtrip.

Private actual-game evidence is under ignored
`build/blinx/visual-mods-20261018` and
`build/blinx/gameplay-coverage-20261017/visual-*`. The 1080p/AA/8× filtering build
loaded an authentic copied save, ran Stage 1 and reached original Results/save
using explicit HP/position assists. The interpolation build also reached normal
gameplay with original counters; follow-up time-control and save/reload results
are recorded with the final build milestone. No original game data is committed.

Remaining work is deliberately bounded:

- **Actual Blinx higher FPS:** the original main loop chooses a simulation step
  and also waits against it (`1DBD88`, `1DBD8C`, `AB801`). The step setter also
  reaches `37A2A8`; the fade path consumes the step. Removing only the wait does
  not establish physics, input, audio or ability timing. A future timing adapter
  must pass matched movement/timer tests, pause and all five time controls,
  plus authentic Results/save/reload. Keep this mode unavailable until then.
- **More interpolation quality:** optical flow/motion-vector interpolation,
  lower latency and per-object/HUD treatment are follow-ups if owner playtesting
  finds blending objectionable. They are not needed for simulation correctness.
- **More render-target families:** investigate only a reached unsupported or
  visibly incorrect case; preserve authored lookup/data grids and shared depth
  ownership. Do not globally scale every texture to hide a missing classification.
- **Performance presets:** owner will test 1080p through 2160p and SSAA on the
  actual workload. Hide expensive choices through policy after that feedback.
  No unattended timing sample establishes a whole-title performance improvement.

No Xbox hardware probing is needed for these host presentation enhancements.
