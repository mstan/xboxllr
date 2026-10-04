# Borrowed phobos D3D11 renderer experiment

Tracking: `beads-xbox.1.57`. Branch: `spike/phobos-backend-20261004`.
Base: `4bcad36`. Configure `XLLE_RENDERER=HLE` and
`XLLE_HLE_BACKEND=PHOBOS_D3D11` in a Windows build. `OPENGL` remains the default
HLE backend; `XLLE_RENDERER=LLE` still selects the separate reference renderer.
There is no runtime renderer switching.

## Substantial borrowed implementation

This imports phobos's real D3D11 RHI: device, resources, shaders, state objects,
draws, clears and ordered GPU readback. It also imports the NV2A vertex decoder,
vertex HLSL generator and register-combiner HLSL generator. Pin:
`f01b20ded38856abd3d37b561521cc7013363725`. The reviewed later `7884426` change
touches a different shader-state file. Provenance and retained licenses are in
[the import manifest](../src/third_party/phobos/README.md).

The local adapter implements the existing whole-renderer contract: guest color
and depth formats, resource aliases and allocation release, real target
contents/readback, feedback texture snapshots, pipeline state, screen/depth
coordinates and original vertex streams. It extends the donor with real
D3D11.1 logic operations. Unsupported shader modes fail explicitly; omitted
bump/clipping/depth-output implementations do not produce placeholder pixels.

Aligned float/UNORM8/SNORM16 attributes use GPU vertex input directly. Packed
11/11/10, integer and unaligned formats use actual CPU conversion within this
renderer. Missing attributes use guest defaults. All indices are checked before
submission. The adapter uses native D3D shader math, not instruction-history
comparison with the reference renderer.

The donor title's native XDK/kernel runtime and swapchain presentation were not
ported. The common NV2A command/resource interface and existing presentation
route remain. This evaluates a borrowed backend at that boundary, not the
donor's entire execution architecture or its game frame rate.

## Applied validation

The full build passes all 80 applicable tests. A new test uses an actual D3D11
hardware device to draw and read back a triangle. The renderer tests cover
fixed/programmed vertices, depth/stencil rejection, XOR logic operations,
resource aliases/release, RGBX and RGB565 storage, aligned/unaligned streams,
default attributes and unsupported draw failure. A missing shader-layout entry
for disabled attributes was caught and fixed by these tests.

Real-dashboard bring-up exposed and resolved:

- An unsupported depth-texture upload: implemented normalized depth texture
  storage and actual shadow sampling.
- The 768 MiB harness memory guard: invalidated plane objects were retained;
  their slots are now reused. The final comparisons use the same 1536 MiB limit
  on both executables. This is not a claim that the backend meets the old limit.
- CPU expansion of every raw stream: the first complete route took 173.8494
  seconds, with an 8.78209-second final 123-vblank window. It drew all 580,540
  draws and completed all 14 inputs. The subsequent native-input path avoids
  that expansion where the host vertex formats support the guest layout.

The first complete run ends with 17 plane slots, 27 texture entries and 28
shader programs. Captures were visually reviewed during bring-up; the final
paired run is the performance evidence below.

## Final paired dashboard result

| Measurement | Retained OpenGL HLE | Borrowed D3D11 HLE |
| --- | ---: | ---: |
| Sum of 18 route frame windows | 149.5616 s | 207.7754 s |
| Last four windows | 29.7147 s | 35.2525 s |
| Final 123-vblank window | 7.49128 s | 9.75986 s |
| Real draws | 580,540 | 580,540 |
| Direct GPU vertex-input draws | Existing raw-stream path | 449,494 |
| CPU-expanded vertex draws | Existing conversion behavior | 131,046 |

Both complete all 14 input events and the real Settings/return route with empty
stderr. All 28 captures differ at the byte level, as expected from different
native rasterizers. Mean absolute RGB channel difference averages 0.07786 on
the 0..255 scale across the 28 capture pairs; the maximum per-capture mean is
0.10846, and the final capture mean is 0.10432. Clock, settled menu, Settings,
return and final images were visually reviewed side by side: reached screens,
text, selections and animated objects agree. These measurements qualify this
route, not every rendering state or arbitrary title.

The candidate ends with 17 plane slots, 27 textures, 28 programs, 1,105 uploads
and 2,350 resolves. Direct GPU input is active for 77.4% of draws, so this is an
executed optimization rather than an unused option. It still does not establish
a speedup. Both legs contain heavy foreign compiler/runner activity and are
marked unusable for quiet-host timing. The observed regression is not an
isolated causal estimate; the result provides no basis for replacing the retained
renderer. The earlier CPU-expanded run is a different busy-host sample and must
not be used to claim a causal gain/loss for native input.

Disposition: parked, not integrated. The backend port is real and reusable;
this route provides no demonstrated performance advantage. Original-speed or
distinct-frame-rate operation is not established. No further renderer rewrite
is selected solely from this result.

Private evidence: main checkout `build/r3-phobos-raw-leg00-A-r1*` and
`build/r3-phobos-raw-leg01-B-r1*`. Earlier probes are `r3-phobos-probe`
(unsupported depth texture), `r3-phobos-depth` (memory guard) and
`r3-phobos-memory` (complete CPU-expanded route). Worktree tests:
`build/ctest-final.log`. Contact sheet: main `build/r3-phobos-raw-review.png`.
Early failed launches are not timing evidence.

## Compatibility limits

This is a dashboard-tested experimental backend, not a general Xbox graphics
compatibility claim. It admits texture modes 0 through 4 and dependent AR/GB;
other modes fail. Fog RGB needs a missing packet field; texture alpha-kill,
additional texture formats and compressed volume textures are not implemented
and reject the draw. Native clipping/filtering/rasterization can differ from
the reference. Additional render-state/title coverage and long-run resource
behavior are unqualified. Readback and command parsing remain possible costs,
not demonstrated bottlenecks selected for another optimization.
