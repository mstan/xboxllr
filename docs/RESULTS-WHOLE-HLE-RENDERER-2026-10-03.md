# Whole HLE renderer: dashboard route and performance screen

Tracker: `beads-xbox.1.45`. Branch: `perf/measurement-first-2026-09`.
The implementation is in the working tree. Build selection is described in
[HLE-RENDERER.md](HLE-RENDERER.md).

## Implemented boundary

`XLLE_RENDERER=LLE` retains the reference renderer. `XLLE_RENDERER=HLE`
selects a separate native renderer for the whole draw pipeline. The common
NV2A device supplies commands, DMA mappings, interrupts and display events.
HLE receives original indices/attributes, vertex instructions/constants,
textures and render state; native GPU shaders perform vertex processing,
interpolation, texturing, blending, and depth/stencil work. CPU vertex execution
and software triangle setup/rasterization are bypassed.

Actual imports: xemu programmable vertex and register-combiner translators,
xemu volume-texture layout, and NoRain rectangular texture layout. Native BC
uploads follow the donor presenter approach. NoRain's allocation-release policy
also informs the resource lifetime adapter. Its real guest kernel free bodies
continue executing; cached GPU copies retire before the allocation is reused.
The allocation-size metadata adapter currently supports kernel 4983. Phobos's
XDK scanner remains available, but graphics XDK function bodies are not replaced.
See the donor READMEs for exact revisions and licenses.

## Applied validation

Both final renderer builds passed all 79 applicable CTest checks. The LLE build
runs the CPU renderer tests; HLE substitutes its native GPU tests. Shared CPU,
AOT, device and integration tests apply to both. Native GPU coverage includes
fixed/programmed draws, depth and stencil rejection, logic operations, color
storage formats, surface alias retirement and explicit unsupported-operation
errors. The entry-observer test proves the original guest body executes and
an AOT region cannot bypass its ownership notification.

The final serial screen completed 27 billion instructions and all 14 real guest
USB input events on both builds: clock acceptance, menu navigation, Settings,
and return to the menu. Stderr was empty. Private logs, command lines, process
censuses and captures use the prefix `build/whole-renderer-screen-r1-`.
Immutable binaries/configurations are in `build/whole-renderer-screen-r1-binaries`.

The live loop found and fixed concrete failures:

- A depth-only draw incorrectly acquired color memory ownership.
- A freed boot surface at physical `0x03628000` retained 2,457,600 stale bytes;
  its later readback corrupted the heap while opening Settings. Real allocator
  notifications now retire that resource without a GPU readback barrier.
- Stencil/logic enable bits were mapped incorrectly.
- Color storage lacked XRGB alpha semantics and 16-bit target handling. The
  missing clock model, panel and hour digit are now visible.

The HLE background grid remains more visible than the reference, and texture
filtering differs. Convolution requests currently use native bilinear filtering.
These are recorded visual differences, not evidence of full hardware accuracy.
No synthetic dashboard, successful draw placeholder, live renderer switching,
or requirement for instruction-history/bitwise rendering agreement was added.

## Timing screen

Final settled-menu window, each advancing 123 guest vblanks:

| Build | Wall time | Relative throughput |
| --- | ---: | ---: |
| LLE | 18.121 s | 1.00 |
| Initial whole HLE | 10.707 s | 1.69 |
| Whole HLE after overhead fixes | 9.694 s | 1.87 |

The full replay windows summed to 250.024 s and 201.993 s respectively.
Both process observers were healthy and reported foreign workloads. LLE saw
16 compiler/linker process identities; the HLE leg saw none. Consequently the
ratio is a promising shared-host screen, not an isolated causal speedup or a
measurement of fresh displayed frames. It does not establish 60 FPS.

The follow-up removes per-vertex heap nodes for ELEMENT16 index remapping and
retains the GL context when no competing GL context needs restoration. Its
focused GPU check and full 27B replay pass; all 14 inputs complete, stderr is
empty, and all 28 captures match the initial HLE build. This is a regression
check of those two implementation changes, not a requirement that HLE match
LLE pixels. The final menu window takes 9.694 s, and all replay windows total
158.015 s. This observer also reports foreign processes (24 identities), so
neither the 1.87x LLE ratio nor the additional 1.10x menu ratio is isolated.
Evidence: `build/whole-hle-overhead-r1.summary.json` and its logs/captures.
At 123 vblanks / 9.694 s, the screen advances about 12.7 simulated frames/s;
a 60 Hz interval of the same size would need about 2.05 s. Displayed fresh-frame
60 FPS is not established.

## Remaining work

The 60 FPS gate remains open. A specific further donor opportunity is NoRain's
raw vertex-stream upload in `d3d_presenter_d3d11.cpp` (`uploadRing` and
`DrawIndexed`): this HLE adapter still expands attributes on the CPU before GPU
upload. The completed route processed roughly 175 million unique input vertices.
Replacing that staging with native input layouts/decoding is a larger remaining
opportunity than more CPU vertex arithmetic work. Guest XDK execution is also
still present. Blinx remains downstream of the dashboard gate.
