# Bounded graphics LLE qualification

Family `.116` belongs to the shared Blinx LLE burndown `.102`. The maintained
route is PFIFO/0x97 command consumption, physical vertex DMA or inline arrays,
transform, rasterization, framebuffer writes and visibility-report DMA in
`src/machine/nv2a.cpp`. This document records component qualification; full
graphics-family acceptance remains open and the total remains **4/9**.

## Packed colors and persistent fetch - 2026-10-09

The previous session ended with a suspected packed-color mismatch. Independent
draws reproduced it: a `UB_D3D` DWORD `0xffff0000` rendered blue, and
`0x7f123456` rendered `0x7f563412`. Both physical DMA and inline arrays decoded
the bytes as RGBA. `UB_D3D` is normalized BGRA, while `UB_OGL` is normalized
RGBA. The correction separates those formats in both maintained decoders.
`SET_VERTEX_DATA4UB` keeps its independent immediate RGBA packing.

`tests/test_nv2a_vertex_colors.cpp` supplies **140 actual draw cases**:

- 96 cases combine DMA A/B, direct/callback reads, array/indexed/inline
  submissions and both packed formats. Red, blue and two asymmetric colors
  include nontrivial alpha. DMA inputs use padded strides and nonzero indices;
  colors change after binding without replacing or rebinding the storage.
- 40 cases bind linear A8R8G8B8 surface bytes as diffuse vertex data, then
  execute repeated GPU `CLEAR_SURFACE` writes and subsequent array/indexed
  draws. They perform no CPU color upload or attribute rebind between passes.
- Four immediate controls establish that array-format state does not change
  `SET_VERTEX_DATA4UB` ordering.

The fixture checks actual framebuffer pixels, untouched background, normalized
transform output and completed nonzero visibility reports. Expected bytes and
colors are authored independently of either decoder and of the HLE adapter.
Before correction the first 136 cases produced **240 failed assertions**;
the corrected final 140 cases have **zero failures**. Four assertions in the
older `test_nv2a.cpp` also encoded the old permutation. Their unchanged input
DWORDs now require red normalized output, radial pixel `0xff102e51` and
untextured pixel `0x80402010`; updating those expectations removes correlated
agreement with the erroneous decoder.

Matching native HLE checks execute persistent vertex buffers at nonzero first
index with a padded stride, UP submissions and alpha blending on actual D3D11.
Each of three device lifetimes performs eight exact RGB cases plus an alpha
blend case (27 draws). XRGB readback does not promise stored alpha, so blending
observes that lane with a one-LSB tolerance. HLE already satisfies this contract;
no production HLE change or new rendering cost is introduced. Its existing
upload strategy and shader implementation remain usable.

Independent reference evidence:

- MIT [nxdk_pgraph_tests surface-as-vertex procedure](https://github.com/abaire/nxdk_pgraph_tests/blob/706a41e61188d3b08e5b1bb2a32b74ad062a883d/src/tests/surface_as_vertex_array_tests.cpp)
  specifies linear A8R8G8B8 rendered colors reused as `UB_D3D` diffuse streams,
  including successive surface updates. Our test is authored here and uses
  bounded clears rather than copying that implementation.
- Pinned xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
  `hw/xbox/nv2a/pgraph/{gl,vk}/vertex.c`: the GL input declares BGRA for
  `UB_D3D`; Vulkan marks the D3D swizzle separately from `UB_OGL`. This is
  source-level behavioral corroboration, not a new differential xemu run.
  No GPL implementation body is copied or linked by this correction.
- The locally retained CC0 golden repository at
  `06b041d6a50c060b4974d93617405f86a03a0b8f` contains Image_blit captures,
  not a matching SurfaceAsVertexArray golden. No new hardware observation or
  hardware-qualified packed-color capture is claimed.

Reproduction from this worktree (PowerShell):

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build build\visibility-lle --target test_nv2a_vertex_colors test_nv2a test_nv2a_tile_commands test_vsh_triangle test_generic_vertex_scratch xlle_nv2a_reference -j 4
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build\visibility-lle -R '^(nv2a|nv2a_vertex_colors|nv2a_tile_commands|nv2a_tile_layout|nv2a_zeta_compression|vertex_pipeline_guards|generic_vertex_scratch|vsh_triangle|nv2a_fp_timing)$' --output-on-failure
& 'C:\Program Files\CMake\bin\cmake.exe' --build build\blinx\host-calendar-lle --config Release --parallel 4
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build\blinx\host-calendar-lle -C Release --output-on-failure
```

Results: **9/9** related machine/graphics checks (0.95s) and **56/56** native
checks (37.38s). The no-SDL machine fixture explicitly skips its pre-existing
OpenGL-only cases; those skips do not qualify OpenGL behavior. The native
packed-color cases execute on the real D3D11 backend.

The rebuilt native Blinx executable retains SHA-256
`76ff2a4e836246508dac2992a77972aa552cfb8ff8ceb87950196530ff12f730`,
exactly matching `build/blinx/audio-completion-floor-attract/run.aot-private.json`.
Its recorded **190.578s / 18-capture** demo/interruption/title/reentry regression
is reused. The changed vertex decoder is not linked into that native title
route; its own execution evidence is the maintained LLE draw fixture. The
original whole-title LLE startup still has its previously recorded encoder
dependency; no new title-startup qualification is asserted.

Private logs are `build/blinx/graphics-color-{before,lle-build,lle-tests,
native-full-build,native-tests}.log`. None are shipping inputs.

## Original declarations, fetch and stream ownership - 2026-10-09

`tools/native_vertex_fetch_reference.py` connects complete original NTSC
`SetStreamSource`, `LoadVertexShader`, `SelectVertexShader` and `DrawVertices`
to the maintained PFIFO/NV2A route. It loads the owned XBE with SHA-256
`e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249` into
Unicorn and shares its actual RAM with NV2A. The original functions and their
callees execute unmodified, including declaration format/address emission and
draw submission. The fixture checks callee-cleaned stack and saved registers.
There are no instruction hooks, substituted return values or replacement XDK
command emitters.

This is a preowned-resource component fixture: shader descriptors, resource
headers, initialized viewport and a bounded pushbuffer are supplied. Only the
relevant declaration/stream dirty bits are set for submission. Original device
creation, shader/resource allocation, pushbuffer exhaustion, whole-title startup
and unrelated render-state updates are outside this qualification.

**720 real LLE draws** cover 48 independently authored payloads across input
slots 1/2/3/4/15 and streams 1/7/15. The twelve formats are FLOAT1/2/3/4,
UB_D3D, CMP, SHORT1/2/4 and normalized SHORT1/2/4. The five formats seen in
the retained 32 Blinx scene captures are included. Cases cover signed endpoints,
asymmetric values, missing-lane defaults, normalized clamping, nontrivial alpha,
padded offset, strides 32/48 and nonzero first indices 1/2.

Every format group binds buffer A, changes its bytes without binding, replaces
it with B, then changes B without binding. Inactive storage and unsubmitted
vertices are poisoned. **360 persistent writes without rebinding** establish
that current storage is fetched, rather than a cached earlier value. **545
original stream transitions** check binding counts, fence side effects, data
pointers, same-header rebinding and final unbinding. All seven preowned resource
headers return to their initial ownership count. The emitted NV2A array formats,
addresses and draw range are checked before actual device consumption.

Expected lane values are authored directly from float/byte/short inputs and
signed 11/11/10 normalization math. They are not derived from our LLE decoder,
the HLE shader translator or shared CPU shader helpers. A diagnostic-only bridge
observes the actual maintained shader output; each draw also checks framebuffer
RGBA after clamping, within one LSB.

The generated private corpus carries those same raw inputs and mathematical
expectations, not measured LLE output. Native `pilot_shader_test
--vertex-fetch-reference` independently passes all **720 cases / 2,880 raw
shader lanes**, with relative tolerance `2e-6 * max(1, abs(expected))`, on actual
D3D11. It additionally renders **720 matching persistent-buffer draws** through
the shipping native draw backend, replacing buffers and changing bytes without
rebinding. RGB pixels agree within one LSB and the untouched background is exact.
Native XRGB readback does not qualify stored alpha; raw shader output covers the
alpha lane, and the preceding packed-color batch retains actual blending checks.
This batch needs no production HLE correction and adds no title rendering cost.

Independent format provenance is the published
[XboxDevWiki vertex-attribute description](https://xboxdevwiki.net/NV2A/Vertex_attributes)
and pinned xemu `482088490c2d5983ed5e1eafbd09906157c1eca0` vertex-format
conversion. Source inspection corroborates the normalization contract; neither
source inspection nor matching native output constitutes a new silicon golden.
The harness and expected-value calculations are authored here. Owned original
instructions and all generated corpus/evidence files remain private.

Reproduce after building the reference DLL and native shader test:

```powershell
$env:PYTHONPATH = 'F:\Projects\xboxlle\wt-native-title-pilot\build\reference-python'
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_vertex_fetch_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --output build\blinx\vertex-fetch-original.aot-private.json --native-cases build\blinx\vertex-fetch-native-cases.bin
& .\build\blinx\host-calendar-lle\Release\pilot_shader_test.exe --vertex-fetch-reference build\blinx\vertex-fetch-native-cases.bin
```

The combined commands from the preceding section pass **9/9 LLE checks (0.99s)**
and **56/56 native checks (39.16s)**. Logs are
`build/blinx/vertex-fetch-{original,native,lle-build,lle-tests,native-build,
native-full-build,native-tests}.log`; private original evidence includes function
addresses/hashes and every draw/stream transition.

The rebuilt title retains the exact executable hash recorded above. Reuse its
190.578s / 18-capture attract/interruption/reentry regression: this batch changes
only diagnostic tools/tests. No fresh title run or original whole-title LLE
integration is claimed. Family `.116` remains partial and the total is **4/9**.

## Original constant transport and native coalescing - 2026-10-09

`tools/native_shader_constants_reference.py` reuses that original submission
fixture to execute complete original `SetVertexShaderConstant1`,
`SetVertexShaderConstant4`, `SetVertexShaderConstantNotInline`,
`SetVertexShaderConstantNotInlineFast` and `GetVertexShaderConstant` routines.
The source XBE identity and preowned-resource limits are unchanged. Their actual
constant-upload packets reach maintained NV2A constant memory and programmable
transform execution; no constant packet is supplied in place of an original
setter. Authored `MOV oD0,c[index]` programs observe the uploaded values through
actual shader output and framebuffer RGBA.

**1,116 cases / 3,348 original setter calls / 1,116 LLE draws** cover all
192 constant slots, one/four-register setters and generic ranges of
1/2/3/4/5/8/17/32 registers. The generic original argument is a DWORD count;
the fixture supplies four DWORDs per register. Ranges include starts 0/1/58/96
and the last valid span, exercising both short and bulk packet paths. Each case
queues preliminary data, overwrites it, repeats the final data and then draws.

The entire CPU constant cache is independently checked after every call.
Fixed-size setters record even when the generic recorder is disabled. The
generic setter records only when enabled; the fast setter leaves the CPU cache
unchanged while still updating actual GPU constants. Both recorder states and
repeated payload revisions pass. Source bytes and surrounding canaries survive;
the original ABI preserves saved registers and cleans its stack. **2,232
original query calls** check the complete 192-slot cache and individual slots
in its signed [-96,95] namespace, including endpoints and output canaries.
Query values can deliberately differ from current GPU values in the fast or
recorder-disabled cases; these are distinct caller-visible effects.

The private corpus again contains authored inputs/expected values, never
measured LLE output. Native `pilot_shader_test --shader-constant-reference`
passes **1,116 cases / 4,464 raw output lanes / 1,116 GPU draws**. Its raw probe
uses the actually bound production constant buffer, layout and shader rather
than a supplied diagnostic constant copy. RGB pixels agree within one LSB;
raw values use the preceding relative tolerance. The existing native caller
fixture retains its original-cache/query/recorder comparison and exercises these
policies through the native XDK interface.

The native backend coalesces each case's three writes into **one GPU upload**;
the subsequent draw reuses it. Upload counters verify no eager upload and no
second draw upload. This preserves the existing performant strategy without
reproducing the original packet traffic. No production HLE correction, added
title cost or new external dependency is needed.

Reproduce with the same dependency setup as the preceding section:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_shader_constants_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --output build\blinx\shader-constants-original.aot-private.json --native-cases build\blinx\shader-constants-native-cases.bin
& .\build\blinx\host-calendar-lle\Release\pilot_shader_test.exe --shader-constant-reference build\blinx\shader-constants-native-cases.bin
```

The preceding 720 original fetch and native raw/pixel cases still pass after
sharing the program-binding helper. Related LLE code is unchanged from its
9/9 combined pass. The native suite passes 56/56 again. Logs are
`build/blinx/shader-constants-{original,native,native-build,native-tests,
fetch-original,fetch-native}.log`. The title executable hash remains identical,
so its matching 190.578s / 18-capture regression is retained. This qualifies
bounded constant transport and software recorder semantics, not general shader
arithmetic, exceptional floats or original title startup. Family `.116` stays
partial; hardware questions remain at the end.

## Independent finite shader arithmetic - 2026-10-09

`tools/native_shader_arithmetic_reference.py` supplies independently encoded
programs and algebraic expectations, then uses the same complete original
load/select/constant-set/draw route to execute them on maintained NV2A. It uses
no shared shader decoder/emulator to produce expected results. Both observed
color outputs are explicitly initialized, so matching assumptions about
unwritten registers cannot establish agreement.

The retained 32 scene captures use MAC MOV/MUL/ADD/MAD/DP3/DPH/MIN/MAX/ARL and
ILU MOV/RCP/RCC/RSQ. Their six paired combinations are MUL+MOV, DP3+MOV,
MUL+RSQ, MUL+RCC, DP3+RSQ and MUL+RCP. The new corpus covers each, plus DP4:

| Slice | Actual original-submission/LLE draws | Independent expectation |
| --- | ---: | --- |
| Vector arithmetic | 720 | Nine MAC operations; finite dyadic inputs, source swizzles/negation, full and individual output-lane masks |
| Scalar/vector ILU | 320 | MOV and reciprocal/reciprocal-square-root results; RCC inputs within the ordinary unclamped range |
| Paired issue | 288 | All six captured combinations consume the old temporary; a subsequent MOV separately observes the MAC's new temporary value |
| Relative addressing | 16 | Positive/negative fractional ARL inputs away from integer boundaries select independently authored constant markers |
| Total across four LLE device lifetimes | **1,344** | Eight explicitly written diffuse/specular output lanes and actual framebuffer RGBA |

Four operand sets and four independent swizzle/negation combinations include
asymmetric signs, scalar replication, permuted lanes and masked destination
preservation. Persistent input storage changes without rebinding. Dyadic vector
inputs avoid making exact hardware rounding a prerequisite for these algebraic
cases. Ordinary finite reciprocal/RSQ inputs avoid zero, exceptional floats and
clamp boundaries. ARL inputs deliberately avoid the unresolved near-integer
rounding question.

The primary arithmetic contracts are NVIDIA's
[NV_vertex_program](https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program.txt)
and [NV_vertex_program1_1](https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program1_1.txt).
[XboxDevWiki instruction fields](https://xboxdevwiki.net/NV2A/Vertex_Shader)
supply hardware encoding facts. The pinned MIT nxdk_pgraph_tests
`vertex_shader_independence_tests.cpp` documents the old-input paired-issue
procedure. The test and encoder are authored here; no external implementation
body or hardware capture is imported. Existing local Image_blit goldens do not
qualify these shader cases, and no new silicon result is claimed.

The native corpus contains authored words, raw inputs, constants and expectations,
never LLE measurements. Native `pilot_shader_test --shader-arithmetic-reference`
matches **1,344 cases / 10,752 raw shader lanes / 1,344 actual GPU draws** through
the compiled production shader and bound constant buffer. The relative raw
tolerance remains `2e-6 * max(1, abs(expected))`; framebuffer RGB agrees within
one LSB and untouched background bytes are exact. The HLE already meets these
contracts; no production code, upload policy or title rendering cost changes.

Reproduction uses the preceding dependency setup:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_shader_arithmetic_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --output build\blinx\shader-arithmetic-original.aot-private.json --native-cases build\blinx\shader-arithmetic-native-cases.bin
& .\build\blinx\host-calendar-lle\Release\pilot_shader_test.exe --shader-arithmetic-reference build\blinx\shader-arithmetic-native-cases.bin
```

Native regression passes 56/56; logs are
`build/blinx/shader-arithmetic-{original,native,native-build,native-tests}.log`.
LLE production code is unchanged from the 9/9 combined pass. The unchanged
title hash retains the exact matching attract/interruption/reentry evidence.
This does not qualify every shader instruction, exceptional numerical behavior,
state programs, original allocation/startup or the entire graphics family.

## Captured scene composition and full-draw audit - 2026-10-09

`tools/native_scene_reference.py` now replays complete captured programs through
the original LoadVertexShader, SelectVertexShader, constant setter,
SetStreamSource and DrawVertices routines and their original callees. It observes
actual maintained transformed vertices through the private reference DLL. A
separate destination-field decoder excludes unwritten lanes; no shared shader
implementation supplies the expected outputs.
Observation covers raster position, diffuse/specular and four texture vectors;
fog, back colors and point size are outside this output comparison.

| Corpus | Actual original-submission/LLE draws | Vertices | Compared written lanes |
| --- | ---: | ---: | ---: |
| Retained `scene-viewport`, including 99-instruction programs | 32 | 184 | 3,304 |
| Captured first-world draw range, 49-instruction programs | 32 | 304 | 5,472 |
| Final bounded scene capture, 11-instruction programs | 32 | 2,520 | 20,160 |
| Total | **96** | **3,008** | **28,936** |

Long programs required a correction to the preowned shader fixture: repeat the
32-DWORD hardware upload-method array, rather than extending one packet into
the constant-upload methods. The original routines remain unmodified. Captured
nonzero first-vertex ranges now determine bounded stream allocation, including
bound streams not read by the current declaration. Streams are unbound through
the original setter and their ownership counts return to the initial values.

The expected XY values independently apply four-fractional-bit truncation to
the native raw output before comparison, since observation occurs after LLE
raster preparation. This replaces the earlier guessed half-unit allowance.
The relative tolerance is `2e-4 * max(1, abs(expected))`; maximum scaled errors
in the three corpora are approximately `9.35e-7`, `2.39e-7` and `4.96e-7`.
This checks composition against retained native execution, supported by the
preceding independent format/algebra cases. It is not a new silicon golden or
complete pixel proof.

The old `draw-N.bin` records contain declarations, constants, streams, state and
raw vertex output but omit bound textures and initial/final targets. The bounded
opt-in draw diagnostic now also writes `scene-N.bin` companions with:

- Original color/depth resource metadata, viewport depth scale, pixel definition
  and packed constant cache.
- Actual before/after GPU color and D24/stencil bytes, with explicit DXGI format
  metadata; target readback admits one 2D image with one mip and one sample.
- Each bound texture's original metadata, accessible owned CPU bytes and actual
  GPU contents for every mip, retaining compressed blocks and explicit layout
  information. Aliased GPU-only storage never receives an invented CPU mirror.

The raw-target readback test checks exact RGBA and depth/stencil clears over
three native device lifetimes. Capture-only GPU copies and allocations occur
only when the existing diagnostic is enabled; ordinary rendering retains its
cached disabled branch and native deferred uploads. The maintained reference
DLL adds observation only, and is not linked into the title.

Private evidence is in `build/blinx/scene-inputs-firstworld-20261009/`. Its 32
companions include DXT1 resources with seven/eight mip levels. The owned CPU
blocks equal the live GPU readback across all levels. Five captured draws change
the real color/depth targets; unchanged targets alone cannot establish execution.
Capturing was armed after observed present 9: the earlier replay-write log
trigger belongs to the native-files backend and never fires on the current
original-files selection.

`tools/native_scene_draw_reference.py` imports the captured initial targets,
relocates preowned resources, then executes actual original texture, pixel,
render-state, texture-stage, viewport and draw routines. Disabled auxiliary modes
154/163/164/165 remain explicit initialized fixture state; their separate
software-control setters are not claimed. The audit services the real graphics
notification with the complete original graphics/software-method routine.
It never acknowledges the device in the transport or substitutes a function
return.

The initial 32 audited captures stopped at **`NV2A unsupported RDI write bank`**:
the original DXT1-noise setter emits software method `0x28`; its original service
attempts RDI index `0x00e00050`, value `1`, followed by `0x00df0008` in the same
original handler. That initial missing write prevented the service from returning
and left the queued draw unqualified. The follow-up below implements this control
transport; enabled noise pixels remain guarded. The report records actual MMIO,
dispatched commands, guards and color/depth differences. `--require-qualified`
returns failure for this result, including draws whose after-image happens to
match because they touched no pixels. No native/LLE no-op agreement is counted.

Reproduction, using the dependency setup above:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --captures build\blinx\scene-viewport --output build\blinx\scene-vertex-original.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_draw_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --captures build\blinx\scene-inputs-firstworld-20261009 --output build\blinx\scene-firstworld-draw-audit.aot-private.json --require-qualified
```

The second command intentionally fails qualification. It is the retained concrete
offline work item, not evidence that every remaining graphics question needs a
console. Pinned envytools and xemu RDI sources were inspected; their unknown or
ignored controls are not an independent pixel oracle. The implemented control
route and remaining silicon noise question are recorded below.

The combined qualification retains all 720 fetch, 1,116 constant and 1,344
arithmetic cases on LLE and native paths. LLE tests pass 9/9; native tests pass
56/56. Logs are `build/blinx/scene-composition-{lle,native}-{build,tests}.log`
and `scene-composition-{fetch,constants,arithmetic}-{original,native}.log`.
The final native regression passes **56/56 in 36.50s**, recorded in
`build/blinx/scene-composition-native-final-tests.log`. The capture manifest is
`build/blinx/scene-composition-final-attract-20261009/run.aot-private.json`;
it runs 190.140s / 18 presents / 32 complete draw companions with no fault/stop,
and repeats the same RDI guard in full-draw replay.

The ordinary production-path attract check omits per-draw capture:
`build/blinx/scene-composition-production-attract-20261009/run.aot-private.json`
records **210.140s / 20 presents** with no fault/stop. Its contact sheet shows
demo playback at present 9, the input interruption/pause and title return at
10Ã¢â‚¬â€œ13, then demo re-entry at 18 and continued playback at 20. Both final runs
use title SHA-256
`02b7917222f2f1725954512a241d9942db4e8d6d2c9062d4e48e01c6fa7352d4`.
The harness intentionally terminates at the requested duration; its exit code
1 is not a title fault. This preserves the observed attract/input behavior;
it is not a new frame-rate or full A/V qualification. Graphics `.116` remains
partial and the family count remains **4/9**.

## Original DXT1-noise control transport - 2026-10-09

`tools/native_noise_control_reference.py` executes the original generic setter,
its complete DXT1-noise callee and complete graph/software-method service against
maintained NV2A. Preowned device, target metadata and pushbuffer are explicit
fixture conditions; no original callee or return is replaced. The public
[nxdk PB_SETNOISE procedure](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/pbkit.c)
independently corroborates the two RDI addresses and copied parameter.

**128 setter calls / 40 complete graphics-service returns across eight device
lifetimes pass.** The requested cache value is checked separately from effective
device state: LIN_A8R8G8B8 permits enable; LIN_R5G6B5 suppresses it. Unchanged
effective state emits no packet. Changes emit the actual WAIT_IDLE/software NOP,
then stop at the real device notification. Before the original service, both
device controls retain their preceding values; afterward both hold the requested
effective value, the original service has acknowledged the real trap and FIFO
has retired. ABI return/stack/canaries are checked by the shared original runner.
This is graphics-service delivery, not a new kernel ISR/DPC qualification.

The maintained device implements only boolean words at RDI `E0:50` and `DF:08`,
with separate bank state, normal cursor advancement and the admitted pristine
PGRAPH reset. Other addresses/modes stay unsupported. Actual 3D submission
rejects disagreeing banks or enabled noise before geometry/pixels/successful
visibility completion; storing controls never stands in for their visual effect.
Sixteen real array/indexed/inline/immediate guard cases check untouched pixels
and pending reports. Two actual disabled-after-enable draws check independent
color output. The prior 140 color draws are retained.

The first-world full-draw audit now consumes both original RDI writes and queued
state/draw methods through submission, then deliberately fails at
**`NV2A DXT1 noise pixel behavior remains unqualified`** on all 32 draws.
`--require-qualified` still exits 1; no unchanged target is accepted as a draw.
All 96 captured shader draws / 28,936 written lanes pass again with the controls
disabled. LLE combined tests pass **9/9 in 0.78s**. Logs and private results are
`build/blinx/noise-control-{lle-build,lle-tests,original,scene-draw-audit}.log`,
`noise-control-original.aot-private.json` and
`noise-control-{scene,firstworld,final}-vertex-original.aot-private.json`.

HLE now accepts both boolean setter requests and retains the caller-readable
requested cache. Four real native draws check setter/cache/deferred-state behavior
and independently expected color; nonboolean requests remain rejected. Native
compressed sampling retains the existing pilot approximation, with no per-pixel
CPU noise generation or LLE dual execution. The enabled-noise pixel error bound
is still open; this cache/control contract does not qualify that visual bound.
Native tests pass **56/56 in 37.24s**, recorded in
`build/blinx/noise-control-native-tests.log`.

The final ordinary-path run is
`build/blinx/noise-control-production-attract-20261009/run.aot-private.json`:
**210.172s / 20 presents**, title SHA-256
`ac2aa89f9507a1022f94d6c7b9347be76c7e8805be1ef0876e0008d27d0f6708`.
All five real input transitions are delivered. The inspected contact sheet shows
demo playback at 9, input interruption/pause at 10, title return at 13 and demo
re-entry at 18Ã¢â‚¬â€œ20. The retained title log has no pilot/audio stop or fault.
As above, the harness intentionally terminates at its time bound; this confirms
observed attract/input behavior and does not establish a new speed/A-V bound.

Reproduction, using the dependency setup above:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_noise_control_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --output build\blinx\noise-control-original.aot-private.json
```

Deferred hardware question: determine the actual enabled DXT1-noise pixel effect
and its error relative to the native compressed sampler, including target-format
conditioning. Public driver control evidence does not establish that algorithm
or an acceptable pixel bound. Keep this specific question at the end while
qualifying independent fog/texture/raster routes offline.

## Programmable fog: original state, scalar output and real pixels

The maintained programmable route now carries `oFog` through interpreted and
compiled shader execution, owned transform-cache/draw snapshots and clipping.
All 15 nonzero masks select the first masked source lane for the scalar X
destination. The compiled opcode remains 32 bytes. Original raw fog coordinates
and derived raster factors have separate fields, so a raster-state change cannot
corrupt a cached shader result or diagnostic observation.

NV097 fog enable/mode/generation/color/three coefficient methods are latched.
Finite LINEAR, EXP, EXP2 and their ABS mode variants evaluate a factor at each
original vertex; clipping and perspective interpolation carry that factor
without clamping it. The actual combiner reads register 3 as ABGR fog RGB plus
the interpolated factor clamped to [0,1]. Fog color's stored alpha is ignored.
Disabled fog supplies factor 1. Unestablished mode/coefficient/nonfinite cases
fault before pixel writes or successful reports. Fixed-function fog remains an
explicitly unsupported vertex route. Optional LLE GPU executors retain their
existing no-fog ABI and decline fog; the maintained software route executes it.

The independently authored original-XDK fixture passes **196 cases / 392 real
draws / 442,176 interior pixel comparisons**. It executes unmodified
SetRenderState, SetPixelShader, Load/SelectVertexShader, constant/stream setters
and DrawVertices/callees over preowned objects/resources/target state. It checks
all 15 MAC and ILU masks, LINEAR/EXP/EXP2, enabled/disabled and zero-density
states, unequal homogeneous W, unclamped-factor interpolation and repeated
draws with persistent resources. Expected ISA inputs, scalar values and
mathematical RGBA are authored independently of both shader implementations.
Every non-edge interior pixel is compared within one byte per channel; exact
diagonal coverage belongs to the separate raster gate. Original XDK logarithm
constants are approximate: retained emitted coefficients agree with the public
formulas within 2e-6, alongside the independent pixel bound. These are finite
component assertions, not a silicon precision golden or full-title GPU startup.

Maintained C++ checks cover **196 additional fog draws** over direct/callback
memory and interpreted/compiled shaders, including clipped geometry, persistent
payload mutation and all six coefficient modes. The existing 142 color draws
remain, for 338 total asserted draws. Three extra actual fog guard submissions
leave all color pixels and pending reports untouched. The 96 retained scene
shader draws now compare **29,424 written lanes**, including 488 previously
excluded fog coordinates; back colors and point size remain outside that
observation. Enabled-noise full-draw audits retain their explicit pixel guard.

HLE now translates scalar fog masks correctly and evaluates the factor in the
native vertex shader, using the existing cached raster-uniform buffer extended
from 32 to 48 bytes. Native combiner and fixed-pixel paths consume the
interpolated factor with late clamping. No CPU pixel loop, shadow LLE execution,
per-state shader compilation or new vertex-upload policy is introduced.
The native fixture matches all **196 cases / 588 raw scalar lanes / 294 real
GPU draws / 331,632 interior pixel comparisons**, including both pixel paths.
The correction fixes lost Y/Z/W writes, per-pixel formula evaluation and
extrapolation from factors outside [0,1]. The native state interface admits
the exercised finite modes 1..3; enabled NONE/degenerate linear ranges remain
guarded. More Xbox state modes remain a separate qualification scope.

Combined validation passes **9/9 LLE in 0.73s** and **56/56 native in 36.12s**.
The dispatch-table guard inventory now includes the six fog methods and earlier
visibility/control additions, accepting 804 distinct methods without losing
any original arm. Logs/results are `build/blinx/fog-{lle-build,lle-tests,
native-build,native-tests,original,native,dispatch-guards}.log`,
`fog-original.aot-private.json`, `fog-native.aot-private.bin` and
`fog-{scene,firstworld,final}-vertex-original.aot-private.json`.

Actual shipping regression:
`build/blinx/fog-production-attract-20261009/run.aot-private.json`,
**210.157s / 20 presents**, executable SHA-256
`1fded5e542385d47dfa933761e61afe04a51f0f08051de2bd13e2c431b9b34d7`.
All five real replay input transitions arrived. The inspected contact sheet
shows demo at 9, pause at 10, title return at 13 and reentry/playback at 18-20;
there is no pilot/audio stop or fault. The bounded harness intentionally
terminates the process. This retains attract/input behavior; it does not
establish a new FPS or A/V timing bound.

Reproduction, after the dependency setup above:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_fog_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --output build\blinx\fog-original.aot-private.json --native-cases build\blinx\fog-native.aot-private.bin
& .\build\blinx\host-calendar-lle\Release\pilot_shader_test.exe --fog-reference build\blinx\fog-native.aot-private.bin
```

Procedure sources: pinned MIT `nxdk_pgraph_tests` `fog_tests.cpp` for scalar masks,
ABGR and NV097 coefficients; Microsoft's [finite fog formulas](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas)
for independent expected factors/blending; existing pinned xemu vertex/pixel
code as a semantic cross-check. No new hardware output or GPL implementation
copy establishes these assertions. Exact silicon exponential/interpolation
precision remains deferred alongside the other hardware questions.

## Finite normalized 2D texture sampling - 2026-10-09

Retained first-world draws use DXT1 128-to-1 eight-level chains; retained final
draws also use swizzled ARGB8 chains. Their texture states request independent
min/mag linear filters, trilinear mip filtering and a -1.2 bias. The maintained
generic route previously read only the base image and always sampled bilinearly.

The LLE route now owns the full finite 2D mip chain, decodes each compressed
level with complete blocks below 4x4, and samples normalized PROJECT2D according
to the actual filter registers. It implements point/bilinear minification and
magnification, nearest/trilinear mip selection, signed Q8 bias, unsigned 12-bit
Q8 LOD clamps, repeat/edge/border addressing and rational projected derivatives.
Format chooses normalized versus pitch coordinates even for a single stage;
the earlier generated-layout fixture now explicitly binds LIN_ARGB8 for its
linear pass. New sampler effects execute on the CPU floor; optional executors
keep their previously supported base-bilinear case. Unsupported filter modes,
signed-channel sampling, anisotropy and inverted clamps reject before color
writes or successful reports.

`tools/native_texture_reference.py` executes the unmodified original texture,
texture-state, pixel-shader, stream, shader and draw callees with preowned
resources and texture DMA contexts. Authored Morton/block bytes and independent
image/filter equations supply expectations; neither texture decoder generates
expected pixels. Explicit Q/W values avoid assuming unwritten shader outputs.
The corpus contains **258 cases / 516 original draws / 444,276 non-edge interior
pixels**. It covers all six basic min-filter encodings, both mag filters,
constant and nonuniform images, XRGB forced alpha despite arbitrary X bytes,
8x8/four-level and 128x128/eight-level chains,
the captured -1.2 bias, all retained levels, minimum LOD clamps, mixed address
axes and persistent draws. Non-unit Q and unequal W cases have independently
authored affine projected footprints. RGBA differs from the ideal mathematical
oracle by at most one byte per channel in every asserted pixel.

The real native D3D11 backend passes the same **258 cases / 516 GPU draws /
444,276 interior pixels** within one byte per channel. These exposed an HLE
defect: clamping a multi-level sampler to a single LOD also changed min/mag
filter selection. A cached one-level native resource view now enforces the
original LOD0 filter while preserving the sampler's bias/clamps and min/mag
choice. The view shares texture storage, allocates once when needed, rebinds
only on a view/binding change and releases with the owning texture. The sampler,
pixel shader and uploads remain native/cached; no CPU pixel loop, production
LLE shadow or per-state shader compilation is added. Both donor preparation
boundaries carry the correction; this pixel qualification uses pinned Phobos.

The maintained C++ suite adds **88 texture draws** across direct/callback
memory and interpreted/compiled shaders, including persistent byte mutation and fractional maximum LOD clamps;
the previous 338 color/fog draws remain (**426 total**). Four actual rejected
sampler submissions preserve every color pixel and pending report. Final related
LLE checks pass **9/9 in 2.52s** and native checks **56/56 in 38.88s**. An earlier
full pass exposed an obsolete sampler-description expectation and a transient
process-wide vblank handle-count failure; the sampler assertion was updated,
the isolated vblank test passed, and the final complete suite passed. Audio
checks are retained from that complete pass; further graphics work uses focused
checks unless a change requires another audio regression.

Private evidence: `build/blinx/texture-original.aot-private.json`,
`texture-native.aot-private.bin`, `texture-{original,native,lle-tests,native-tests}.log`.
The two independently observed routes check **888,552 interior pixels** in total.
Actual shipping SHA-256
`c71f4c896da56dc433daf8740e586198b474c459c4b98f9a215925e6d6e1d452`
passes `build/blinx/texture-production-reentry-20261009/run.aot-private.json`:
**250.422s / 23 presents**, all five real input transitions, demo at 10-11,
title return at 13 and reentry/playback at 20-23. The inspected contact sheet
retains actual frame observations; no pilot/audio stop or fault occurred. The
preceding 210.265s/19-present run also showed demo, interruption/pause and title
return, but ended before reentry; it is retained without claiming that outcome.
Both harnesses intentionally terminate at their bounds. This is a rendering/input
regression, not a new original-speed/FPS or A/V timing qualification.
Reproduction uses the existing dependency setup:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_texture_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --output build\blinx\texture-original.aot-private.json --native-cases build\blinx\texture-native.aot-private.bin
& .\build\blinx\host-calendar-lle\Release\pilot_shader_test.exe --texture-reference build\blinx\texture-native.aot-private.bin
```

This qualifies the bounded authored texture component. It does not qualify
complete retained scene pixels: enabled DXT1 noise still has its explicit
hardware guard, complete captured combiner/raster composition remains open,
and additional perspective fields, broader texture layouts and complete
combiner/raster composition need their own gates. Exact silicon LOD/rounding,
filter crossover rules and enabled-noise error bounds stay deferred last.

## Documented programmable-output defaults

The maintained programmable routes now initialize result registers to
`(0,0,0,1)`, following NVIDIA's
[NV_vertex_program result-register specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program.txt).
The pinned xemu `pgraph/glsl/vsh.c` independently uses this initialization for
position, colors and texture coordinates. This establishes the maintained
software contract; it is not a new Xbox hardware capture.

LLE previously initialized every output lane to zero. Native HLE initialized
raw outputs to zero but patched unwritten position W and texture Q at its
raster interface. Both raw probes and rasterization now retain the documented
defaults through masked writes, including unwritten color alpha. LLE's
compiled read-before-write plan, interpreter, context-writing execution,
verification reference and optional GPU program templates use the same
initialization. Temporary/address registers remain zero. The native path
continues using cached GPU shaders and deferred uploads; no LLE execution is
added to title rendering.

The optional `--defaults-only` corpus in
`tools/native_shader_arithmetic_reference.py` independently authors all 16
diffuse write masks over four input vectors. Its 64 complete original-XDK/LLE
draws and 64 native GPU draws check color/specular defaults, position W,
partially written texture coordinates and all four color channels in pixels.
Native probes compare 1,536 color/texture lanes in addition to position checks.
`tools/native_texture_reference.py --implicit-q` adds eight ARGB/XRGB/DXT1
sampling cases with poisoned input W/Q=31 and unwritten output W/Q. Both
routes execute 16 draws and compare 13,776 interior pixels apiece against
authored sampling expectations. This tests the effect of the defaults on real
texture samples rather than accepting agreement between shader implementations.

Permanent C++ coverage adds 64 masked-output draws across direct/callback
memory and compiled/interpreted execution, for 490 total vertex-color cases.
The obsolete immediate-quad diagnostic expectation is corrected from all-zero
outputs to `(0,0,0,1)`. Focused LLE checks pass 9/9 and native graphics checks
pass 6/6; the audio suite is not repeated. Retained native arithmetic (1,344
cases / 10,752 lanes), textures (258 cases / 444,276 interior pixels), and all
96 original-submission scene shader draws / 29,424 written lanes also pass.
Private evidence uses the `build/blinx/output-defaults-*` prefix.

The shipping executable SHA-256
`df7caa88118ffb3972809c54e771af5af9e59d6eed807997637a65aed0cb94c6`
passes one 250.281s Blinx run with 23 captured presents and all five real input
transitions. Inspection confirms first demo frames 9–11, title frame 13, and
reentry/playback frames 19–23. No STOP/FAULT occurs; return code 1 is the
harness's intentional bounded termination. Evidence is
`output-defaults-production-reentry-20261009/run.aot-private.json` and its
retained log/contact sheet. This checks the native title regression; it does
not establish original timing, FPS, A/V or full scene-pixel agreement.

Reproduce the new original corpora with the existing arithmetic and texture
commands above, adding `--defaults-only` or `--implicit-q` respectively and
fresh `--output` / `--native-cases` paths. Execute those binary expectations
with `pilot_shader_test --shader-arithmetic-reference` and
`pilot_shader_test --texture-reference`. Captured unwritten native lanes from
older runs remain historical observations, not independent hardware goldens.
Full scene pixels and the enabled-noise hardware gate remain open.

## Variable projected texture footprints

`tools/native_texture_reference.py --projected-only` independently derives
texture coordinates and their quotient derivatives from affine homogeneous
planes. Four positive W/Q fields exercise ARGB, normalized XRGB and DXT1,
nearest/linear MIN and MAG, disabled/nearest/linear mip selection, LOD bias,
base-level clamps and mixed-axis repeat/edge/border addressing. Deliberately
offset texel coordinates and nearest-mip bias avoid exact rounding ties.
The oracle does not read either renderer's results to construct expectations.

Both routes pass **432 cases / 864 draws / 743,904 interior pixels** each.
Original owned XDK execution submits complete draws to maintained LLE, which
meets the existing one-channel-step bound without a production sampler change.
Native Phobos GPU results meet a two-channel-step bound for this variable
corpus. The retained 258-case affine corpus still meets its one-step bound;
eight default-Q cases remain exact. Repeated complete frame readbacks must
agree, including edges, and background pixels remain exact. These are finite
corpus bounds, not universal NV2A precision guarantees.

The initial 72-case native subset differed by up to 137 channel steps. The
native implicit LOD calculation uses coarse differences and an orthogonalized
footprint, as described in the
[D3D11.3 functional specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
The authored adapter reconstructs homogeneous planes using `tc / SV_POSITION.w`
under the documented
[DirectX position-W convention](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst),
differentiates the quotient at the fragment, and supplies orthogonal
`SampleGrad` gradients with the maintained isotropic footprint. Native GPU
filtering, bias, clamps, resources and shader caching remain in use. Single-level
views with equal MIN/MAG retain ordinary `Sample`. One-level views with different
MIN/MAG preserve an ideal positive-LOD MIN choice across native LOD quantization
by raising only that footprint to at least 1.0078125; their view cannot select a
different mip. Existing constant-buffer lanes carry the requested bias and the
MIN/MAG flag without an additional upload or shader-cache key.

XRGB border cases expose a separate native storage mismatch. The pinned
[nxdk border-color procedure](https://github.com/abaire/nxdk_pgraph_tests/blob/706a41e61188d3b08e5b1bb2a32b74ad062a883d/src/tests/texture_border_color_tests.cpp)
documents NV2A border RGBA independently of texture format. Native BGRX storage
forces alpha to one even for a border sample. Normalized XRGB now uses BGRA
native storage with opaque texels filled in the existing unswizzle scratch on
dirty upload. Original guest format, queries and raw shadow bytes remain XRGB;
unchanged payloads still skip preparation/upload. No additional allocation or
copy is introduced, and scratch allocation failure stops explicitly. Linear or
renderable XRGB is outside this correction's qualified scope.

Permanent C++ coverage adds 32 projected mip/bias/clamp draws across direct and
callback memory, compiled and interpreted shaders, and ARGB8/DXT1 textures:
**522 vertex-color draws / zero failures**. Focused native graphics checks pass
**6/6**. Retained native affine/default-Q references pass, as do 196 fog cases
with 294 draws / 331,632 interior pixels. The audio suite is not repeated. Private evidence uses `build/blinx/projected-texture-*`, including
`projected-texture-lle.aot-private.json`, the native binary expectations and
`projected-texture-qualified-native.log`. Reproduce with the texture command
above plus `--projected-only` and fresh output paths, then execute
`pilot_shader_test --texture-reference` against that binary.

The HLE changes retain cached native rendering and dirty-upload coalescing;
they do not invoke LLE or CPU pixel rendering during title execution. This
structural check does not establish an FPS bound. Exact silicon LOD precision,
texel/mip ties, filter crossover and enabled-noise pixels remain deferred last.
Broader formats/layouts, other projected fields and complete scene
combiner/raster composition still need qualification. Graphics remains partial,
with **4/9 service families qualified**.

For silent graphics regressions, `XLLE_PILOT_AUDIO_OUTPUT_MUTE=1` sets only the
XAudio2 mastering voice gain to zero before source voices are created. The
source queues, mixer, DSP and guest audio route continue executing. The default
audio behavior is unchanged. This option is output suppression for validation,
not an audio-correctness qualification.

The final native executable SHA-256
`aa24c67e00b5a51e9c14ca1f94c32ba643ca731b57180a42cdc19af3f3fcb1aa`
passes `projected-texture-delayed-reentry-20261009/run.aot-private.json`:
**340.204s / 33 presents**, all five real input transitions, and no pilot,
audio or input stop or fault. The reviewed frames show first demo/pause at
13-14, title return at 16-17, reentry at 22-23, continued demo movement at
24-30, and subsequent movie playback at 31-33. Both source audio buffers and
DSP/mixer processing execute with mastering output muted. The harness's return
code 1 is intentional bounded termination. This confirms the observed native
rendering/input sequence; original speed, A/V timing and complete scene-pixel
agreement remain separate gates.

The preceding 250.157s / 20-present run is retained as a limited observation:
it has no STOP/FAULT and confirms output mute, but delayed startup polling
missed the short Start pulse at 15-16s. It does not pass the complete replay
sequence. The corrective fixture uses one-second Start pulses at 45-46s and
140-141s, with initial release at zero and a 340s bound. This changes only test
stimulus timing; title/input implementation is unchanged. The earlier rebuild
also encountered Windows commit-limit exhaustion and passed on a serial retry.

Owner visual review can flag missing objects, wrong textures/lighting/fog,
flicker and transition defects using the retained `scene-review.png` and its
full-resolution `review-frame-*.png` files. Report the frame number and visible
difference. A controlled listening run can similarly identify distortion,
wrong pitch, clicks, missing sound and obvious sync drift. Human acceptance
records the experience in those observed scenes; exact pixel/channel values,
shader/state semantics and timing bounds still require independent measured
evidence. Automated graphics regressions use output mute; listening checks are
owner initiated.

The owner reviewed the retained sheet and reported: "Nothing observably looks
wrong at a glance." Record this as a visual sanity pass for the observed scenes;
the independent pixel and timing gates remain open. Reuse this observation for
documentation and LLE-only changes without repeating the title or listening run.

## Packed/compressed normalized 2D formats: LLE and host-precision bounds

The retained title resource census includes 21 A4R4G4B4 (`04`), 157 DXT3 (`0e`)
and 75 DXT5 (`0f`) textures. DXT5 is not L8; these format identities come from
the original Xbox D3D declarations and the pinned
[nxdk DXT format procedure](https://github.com/abaire/nxdk_pgraph_tests/blob/706a41e61188d3b08e5b1bb2a32b74ad062a883d/src/tests/texture_format_dxt_tests.cpp).
The new authored `tools/native_texture_formats.py` builds packed Morton texels,
compressed blocks and independent mathematical images. Expectations do not use
the maintained decoder. Block alpha/color equations follow the public
[S3TC specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt)
and [Microsoft block-compression documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression).
This establishes a finite software contract, without new Xbox silicon evidence.

Before the decoder changes, the original-XDK/LLE fixture stops at its first
ARGB4444 draw because the format is unsupported. After adding two-byte swizzled
nibble expansion and DXT5 alpha interpolation, the new corpus passes **972
cases / 1,944 original-XDK draws / 1,673,784 interior pixels**, within one channel
step. Complete repeated frames agree exactly. The corpus covers 8x8, 8x16 and
16x8 chains, compressed tails below 4x4, both alpha endpoint orders, ascending
and descending color endpoints, positive variable W/Q, basic MIN/MAG/MIP
combinations, bias, clamps and mixed repeat/edge/border addressing. DXT3/5
always use four color entries; DXT5 separately selects its eight-entry alpha
palette. Resources remain preowned and original callees execute without hooks.

Private retained evidence is `build/blinx/texture-formats-lle.aot-private.json`,
`texture-formats-native.aot-private.bin`, `texture-formats-lle.log` and
`formats-before-lle.log`. The native fixture now accepts a version-2 record
with direct format and separate width/height, while preserving legacy version-1
inputs. These first artifacts retain the strict two-step native comparison:
ARGB4444 passes all 324 cases, but 570 DXT3/DXT5 cases exceed that bound, with
maximum channel error six. Do not report this strict comparison as a pass.

Subsequent review fixes layout selection: normalized ARGB4444 and mip-owned
DXT5 must ignore stale linear rectangle/pitch state. Newly admitted packed/DXT5
cubemaps and DXT5 without an owned normalized mip layout stop explicitly.
Legacy linear DXT1/DXT3 behavior is retained. The latest serial rebuild and
permanent suite pass **854 cases / zero failures**. Coverage adds 320 texel draws across callback
and direct memory, interpreted and compiled shaders, all packed channel nibbles,
all DXT5 alpha selectors, equal alpha endpoints and both color endpoint orders;
12 rejection cases check the new layout bounds. Core NV2A, tile-command,
vertex-triangle and generic-scratch regressions also pass **4/4**.

The latest DLL rerun passes the same 972 format cases within one channel step.
Another **192 isolated texel cases / 384 original draws / 330,624 interior
pixels** pass LLE within one step. Point sampling at texel centers with a tiny
positive footprint isolates decoding from spatial/mip filtering. The native
strict comparison still fails 68 of these cases, with maximum error five.
This reproduces palette differences without a projected-footprint ambiguity.

The [Direct3D 11.3 specification, section 19.5.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#19.5.2%20Error%20Tolerance)
independently allows BC2/3 decoder error dependent on each channel's endpoint
span. A separate, explicit `--native-bc-precision` comparison propagates those
texel bounds through the authored positive filter/mip weights, then adds the
retained two-step sampling budget. Zero/one values, authored exact endpoints,
explicit DXT3 alpha and uncompressed texels add no decoder allowance; border
samples add none either. It does not change the LLE one-step bound or the
retained native gates. Version-3 binary records retain ideal RGBA expectations
and add independent per-pixel/channel limits; they do not use observed maxima
as limits. This is a host-API precision comparison, not an Xbox decoder golden.

The real native GPU passes **972 format cases / 1,944 draws / 1,673,784 pixels**
and **192 texel cases / 384 draws / 330,624 pixels** against those intervals,
with maximum errors six and five respectively. Complete repeated frames agree
exactly. Retained native affine cases pass their original one-step bound,
default-Q pixels stay exact, and projected cases pass their original two-step
bound; latest LLE also passes all three references. Private latest evidence uses
`texture-formats-layout-*`, `texture-formats-retained-*`,
`texture-formats-host-precision-*`, `texture-codec-host-precision-*` and
`texture-codec-strict-*`. Reproduce with the texture command above plus
`--formats-only --native-bc-precision` or `--codec-only --native-bc-precision`,
then give the emitted binary to `pilot_shader_test --texture-reference`.
Omitting the precision flag retains the strict native comparison and its
recorded failure for interpolated compressed palettes.

Owner resource constraints remain in force: CPU work runs at Idle priority on
one logical processor; small GPU fixtures run only with Marvel Rivals absent
and stop our own fixture if it starts. No title or audio rerun occurs here.
No native HLE production code changed in this batch; reuse the muted 340s title
check and owner review unless a new HLE change warrants another regression.
Native compressed storage, decoding, sampling and existing caches remain in
use; no CPU decode/upload expansion is added to title execution. Exact NV2A
DXT color/alpha precision joins the hardware-last queue. Full scene composition
still needs its independent pixel gate. Graphics remains partial, with
**4/9 service families qualified**.

## Remaining offline work

The latest 32-capture original scene-state audit now advances past the old RDI
transport failure and stops at `NV2A DXT1 noise pixel behavior remains
unqualified`. Evidence is `scene-firstworld-formats-followup.aot-private.json`.
No complete draw or scene pixel is qualified by that enabled-noise result.
Keep enabled-noise silicon questions last.

`native_scene_draw_reference.py --noise-disabled-fixture` now explicitly calls
the original render-state setter 159 with zero and records the captured value
one in each row's `fixture_render_overrides`. Both texture DMA contexts are
explicit preowned fixture state, as in the qualified texture references; their
missing setup was previously masked by the earlier guard. Immediate draw
faults are now recorded rather than terminating the audit before a report.
All **32 captured draws execute completely**, with no guard or skip census,
under this declared noise-off condition. It does not qualify the original
enabled-noise scenes. Default replay retains the enabled guard, and
`--require-qualified` still rejects execution without independent pixel proof.

The earlier noise-off audit differed from retained native after-images by
33/129/67/3,401/4,334 color words and 33/129/68/3,490/4,479 depth words for
draws 2/3/4/5/6. These counts predate the viewport fixture correction below;
they are diagnostic, not noise-off or Xbox golden results. Private evidence uses
`scene-firstworld-noise-disabled.aot-private.json`,
`scene-noise-enabled-control.aot-private.json` and
`scene-noise-disabled-require-qualified.aot-private.json`.

### Independent post-transform scene pixels (2026-10-09 continuation)

The complete scene fixture left the initialized XDK screen-offset fields at
zero. Its real `SetViewport` then rewrote reserved constants 58/59 without the
qualified SDK screen bias, shifting the captured geometry by roughly half a
pixel. The fixture now initializes both fields from the existing original
transform binding before the setter. Production LLE/HLE rendering is unchanged.
All 32 noise-off captures still execute completely without a guard or skip.

`tools/native_scene_draw_reference.py --diagnostics` saves actual color/depth
bytes, transformed vertices, fog, dispatched state, input/library hashes and
execution evidence. `--draws` selects particular retained inputs. These device
observations are explicit **post-transform fixture inputs**, never expected
shader results. `tools/native_scene_pixel_reference.py` computes coverage,
LESS depth outcomes, BC1 decoding, perspective gradients, trilinear repeat
sampling and modulated colors independently, without consuming after-images or
calling the production rasterizer/codec. Its closed state assertions reject
unsupported profiles, transparent BC1, constant-depth specialization and absent
execution evidence. RGB changes; destination alpha and stencil must be exact.

The mathematical inputs use exact rational planes on the four-bit subpixel
grid and ideal 565/palette colors. The RGB bound remains **one channel step**:
endpoint expansion plus palette rounding is below 1.2 texture channel steps,
convex filtering preserves that bound, and the admitted diffuse channels are
at most 0.5 before final UNORM rounding. No native mismatch maximum supplies a
tolerance. The BC1 equations and filling convention are documented by
[Microsoft's block-compression reference](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression)
and [rasterization rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules);
the fixture's four-bit grid remains the maintained software contract, not a
claim that a desktop rasterizer establishes NV2A silicon precision.

Depth has a separate exact rational nearest/even binary32 calculation, also
checked against the independent affine interpolation error bound `gamma(6)`.
The retained DLL was compiled with `-march=native`; disassembly at the depth
expression shows a rounded B product, fused A/C multiply-adds and the rounded
reciprocal. `--depth-arithmetic fma-b-first` declares that graph from build and
instruction evidence. The separate arithmetic graph remains available for
other builds; selecting a graph by observed pixel agreement is prohibited.
This distinction resolves depth-test decisions near existing target depths
without widening the gate. Exact NV2A arithmetic still belongs to the deferred
hardware precision question.

| Retained draw | Covered pixels | Depth-rejected fragments | Written pixels | Pixel contract |
| --- | ---: | ---: | ---: | --- |
| 2 | 25 | 0 | 25 | Pass |
| 3 | 116 | 0 | 116 | Pass |
| 4 | 164 | 106 | 58 | Pass |
| 5 | 3,741 | 325 | 3,416 | Pass |
| 6 | 5,425 | 1,015 | 4,410 | Pass |

All five cases pass: **8,025 written pixels** and **1,527,975 untouched pixels**,
zero coverage/depth/stencil/alpha failures and RGB error at most one step.
Seven corrupted-output controls reject no-op execution, an omitted pixel,
a stray write, incorrect color, depth, stencil and masked alpha, each with
qualification failure/exit 1. Private evidence is
`scene-firstworld-bias-fixed.aot-private.json`,
`scene-five-pixel-component.aot-private.json` and
`scene-pixel-oracle-negative-controls/controls.aot-private.json`.

Reproduction under the owner's existing Idle/one-processor wrapper:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' build\blinx\run_idle.py 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_draw_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --captures build\blinx\scene-inputs-firstworld-20261009 --noise-disabled-fixture --draws 2 3 4 5 6 --diagnostics build\blinx\scene-five-bias-fixed --output build\blinx\scene-five-bias-fixed.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' build\blinx\run_idle.py 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_pixel_reference.py --captures build\blinx\scene-inputs-firstworld-20261009 --diagnostics build\blinx\scene-five-bias-fixed --depth-arithmetic fma-b-first --output build\blinx\scene-five-pixel-component.aot-private.json
```

At this component milestone, `post_transform_pixel_qualified` is true for the
five exercised draws while shader/setter composition remains open. The next
continuation below closes that input gap for the same noise-off fixtures.
The original enabled-noise route remains guarded, and `--require-qualified`
still fails complete original-scene qualification. No GPU, title/audio run,
recompile or HLE runtime/performance change was required for this milestone.

### Independent captured-input scene composition (2026-10-09 continuation)

`tools/native_scene_shader_reference.py` derives expected vertices and fog
directly from captured stream bytes, elements, constants and the 49-instruction
program. Its instruction reader uses the published
[Kelvin XF bit layout](https://envytools.readthedocs.io/en/latest/hw/graph/xf/isa.html)
as one flat integer, without importing the maintained disassembler/interpreter.
Finite arithmetic uses exact rational binary32 nearest/even rounding; square
root candidates must satisfy exact midpoint-square intervals. Unimplemented
opcodes, indexed constants, nonfinite math and incomplete observations fail.
All **30 vertices / 870 output values** match the retained software route
exactly, including independently derived four-bit setup positions and fog.
Native raw outputs remain diagnostics and cannot supply or widen expectations.

`tools/native_scene_composed_reference.py` independently derives the consumed
state from the captured render/texture arrays, resource/mip metadata, viewport
and declared preowned fixture conditions. Closed API assertions admit the
single MODULATE texture stage, smooth shading, LESS depth, masked RGB/depth
writes, saturated source alpha, zero specular and EXP fog with zero density.
Cull selection follows requested versus front-face winding. The
[published NV097 fields](https://github.com/XboxDev/nxdk/blob/master/lib/pbkit/nv_regs.h)
and [NVIDIA combiner equations](https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt)
supply numeric/state semantics. Reserved viewport constants must match the
captured viewport and previously qualified XDK screen bias.

The original sampler helper at `0x13cbc0` adds device bias, multiplies by 256,
adds 0.5, stores binary32 and calls `CVTTSS2SI` at `0x13a460`, then clamps and
masks the signed bias. This reviewed instruction evidence supplies the packing
equation; observed packets do not determine it. In this fixture device bias
is zero. In particular, negative biases are truncated after adding 0.5, rather
than rounded to nearest. The independently constructed state matches all
**77 consumed method values per draw / 385 total**.

Those expected vertices/fog and expected state now feed the independent pixel
equations above. All five draw fixtures pass **captured inputs through final
noise-disabled software pixels**: 8,025 written pixels and 1,527,975 untouched
pixels, exact coverage/depth/preserved alpha/stencil and RGB within one step.
The tolerance and compiled depth graph are unchanged. These are five isolated
draws with supplied initial targets, not a composed whole frame or whole title.

`tools/native_scene_composed_controls.py` passes **26 controls**: one baseline,
23 deliberate failures and two independence controls. Missing/incorrect vertex
and fog observations, changed vertex/constant/program inputs, API/method state
corruption, enabled-noise admission, no-op/omitted/stray writes, wrong color,
depth, stencil, masked alpha and truncated target files are rejected. Input
mutations refresh capture hashes so stale provenance cannot explain rejection.
Changing the native raw vertex outputs or native after-image leaves the
software-fixture gate passing, proving those observations do not supply its
expectations. This does not qualify the native after-image itself.

Reproduce using the retained private inputs and device observations, without a
GPU, title run or rebuild:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_shader_reference.py --captures build\blinx\scene-inputs-firstworld-20261009 --diagnostics build\blinx\scene-five-bias-fixed --output build\blinx\scene-five-shader-independent.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_composed_reference.py --captures build\blinx\scene-inputs-firstworld-20261009 --diagnostics build\blinx\scene-five-bias-fixed --depth-arithmetic fma-b-first --output build\blinx\scene-five-composed-independent.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_composed_controls.py --captures build\blinx\scene-inputs-firstworld-20261009 --diagnostics build\blinx\scene-five-bias-fixed --depth-arithmetic fma-b-first --output build\blinx\scene-composed-independent-controls.aot-private.json
```

`captured_input_to_fixture_pixel_qualified` is true for these five fixtures.
**`complete_scene_pixel_qualified`, `original_noise_enabled_scene_qualified`
and `native_hle_scene_qualified` remain false.** This closes the supplied
vertex/state input gap only within the declared noise-disabled software scope.
The following continuation establishes the comparable native HLE scene check
and retained-target sequence. Continue broader offline raster/state coverage. Original enabled DXT1 noise,
exact NV2A precision and dependent startup stay in the hardware-last queue.
Graphics remains partial; overall family qualification remains **4/9**.
This batch changes no HLE runtime code or performance path and reuses retained
title evidence without another compile, GPU, title or audio run.

### Independent native scenes and retained-target sequence (2026-10-09 continuation)

The first-world corpus now passes **32/32 independently qualified noise-off
draws on both routes**, including a second run as an uninterrupted sequence.
The original XDK/LLE sequence initializes its targets once, retains its device
and target memory through all 32 original submissions, and completes every
draw without a guard or skip. Input resources are recycled through actual
original unbind calls. Cumulative device counters are differenced per draw;
an earlier successful draw cannot hide an omitted later submission.

The native observer executes the maintained HLE API with captured first-vertex
indices, declarations, streams, constants, programs, state and mip bytes. It
uploads the initial color/depth targets once in sequence mode, then performs
all 32 real GPU draws without replacing those targets. It repeats the sequence
across **three independent device lifetimes / 96 GPU submissions**. The common
shader remains cached; unchanged constants produce one upload per lifetime.
The input fixture contains neither native raw shader outputs nor after-images.

| Qualification | Independent LLE | Native HLE |
| --- | ---: | ---: |
| Retained input vertices | 304 | 304 per lifetime |
| Shader values checked | 8,816 exact software values | 11,552 raw plus 11,552 production shader values per lifetime |
| Consumed original method values | 2,464 | Captured API profile admitted before the pixel check |
| Written pixel observations per sequence | 8,025 | 8,021 |
| Untouched pixel observations per sequence | 9,822,375 | 9,822,379 |
| Target pixels checked per sequence | 9,830,400 | 9,830,400 |
| Missing/stray writes, alpha or stencil failures | 0 | 0 |

Only five draws write pixels. Draw 1 is culled; the remaining 26 are wholly
outside the viewport. Their exact preservation is checked rather than skipped.
The native mathematical model independently rejects 218 wholly clipped
triangles. Higher diffuse values in off-viewport LLE draws are admitted only
after a common-side proof establishes that they cannot reach a target sample;
they do not expand the qualified on-screen sampler profile.

`tools/native_scene_host_precision.py` propagates rational intervals through
the independent Kelvin decoder. Published D3D11 binary32 add/multiply and
reciprocal/root bounds supply each allowance, including every serial dot-product
ordering and the packed-normal divide. These intervals are generated before
observations are read. The backend's diagnostic raster probe executes the
**exact production `raster_main` bytecode, input layout and bound constant
buffers**, separately from the raw `main` probe. Both pass. Stream-output gaps
are excluded; all declared outputs and defaults are checked.

`tools/native_scene_host_pixels.py` admits those production shader outputs only
after their captured-input intervals pass, then evaluates the native raster
contract. D3D11 uses exactly eight fractional setup bits, nearest/even snapping
and top-left ownership; the software floor retains its declared four-bit setup.
The first strict cross-route diagnostic therefore had a few edge/depth decisions
that could not serve as a native golden. The independent eight-bit model has
**zero coverage/visibility errors**, without dropping edge or near-depth pixels.
See [D3D11.3 rasterizer precision and floating-point rules](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).

Native depth uses a declared affine numerical acceptance interval `gamma(8)`
plus 0.6 integer steps for UNORM conversion. This is our bounded HLE contract,
not proof of an implementation-specific interpolator graph or an Xbox silicon
result. The largest observed deviation is **two 24-bit depth steps**. RGB uses
per-texel/channel BC1 endpoint-promotion and palette budgets, convex sampling,
the four coarse-derivative pairings on aligned 2x2 quads, eight-bit sampler
weight costs and fixed-point filtering/output rounding. Original sampler-bias
packet quantization and the native float bias remain separate. The largest
derived RGB interval radius in this corpus is **3.782 byte steps**; observed
maxima never select or widen the allowances. Exact NV2A decoder/interpolation
precision remains deferred. These checks establish this finite host acceptance
model, rather than universal behavior across arbitrary geometry or adapters.

Each sequence draw's actual before-target must exactly equal the previous
qualified draw's output; the root must equal the supplied captured initial
targets. A failed prefix cannot admit later draws. Native raw/raster/target
observations and before-targets agree bit-for-bit across all three lifetimes.
Final LLE color/depth hashes are `ed38d13d9ab6d75a6c56173965bd9fa8a98739baf833e3e620f0996ccc677212`
and `bc320bb82854f9943f538eb9ffcee8c097331f87122c3b4b4cc4344aed79b2ff`;
native hashes are `4878dc3ea2947731073979ada27ee6b8fa654a6e57e9fca1639ae1c3b8260657`
and `a16bc81ef692d6fd57f145661788b2fbd0cac8e3df41dcbc02a224e2e6d90322`.
Their different grids/precision contracts do not require matching hashes.

The new native gate passes **28 controls**: two baselines, 24 deliberate
failures and two independence controls. Changed shader/fetch/constants/program,
cull/bias/texture inputs, missing/stray pixels, wrong RGB/depth/preserved channels,
truncated observations and broken sequence/device continuity are rejected.
Captured raw outputs and captured after-images remain unused expectations.
The preceding software gate's 26 controls still pass, and the existing native
shader regression passes after adding the diagnostic probe.

Private evidence is `build/blinx/scene-32-{composed-independent,hle-qualified}.aot-private.json`,
`scene-32-chain-{lle,hle}-qualified.aot-private.json`, actual observations in
`scene-32-chain-{lle,hle}-observed/`, and `scene-hle-independent-controls.aot-private.json`.
Replay the retained sequence with:

```powershell
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_hle_reference.py --captures build\blinx\scene-inputs-firstworld-20261009 --draws (1..32) --retain-targets --native-input build\blinx\scene-32-chain-hle-inputs.aot-private.bin --output build\blinx\scene-32-chain-hle-inputs.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' build\blinx\run_idle.py --guard-rivals build\blinx\host-calendar-lle\Release\pilot_shader_test.exe --scene-reference-observe build\blinx\scene-32-chain-hle-inputs.aot-private.bin build\blinx\scene-32-chain-hle-observed
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_hle_reference.py --captures build\blinx\scene-inputs-firstworld-20261009 --draws (1..32) --retain-targets --observations build\blinx\scene-32-chain-hle-observed --output build\blinx\scene-32-chain-hle-qualified.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' build\blinx\run_idle.py 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_draw_reference.py --xbe F:\Projects\xboxlle\xboxlle\build\phobos-audit-20261006\blinx-na\default.xbe --binding build\blinx\d3d-binding.aot-private.json --library build\visibility-lle\libxlle_nv2a_reference.dll --dependencies C:\msys64\mingw64\bin --captures build\blinx\scene-inputs-firstworld-20261009 --noise-disabled-fixture --retain-targets --diagnostics build\blinx\scene-32-chain-lle-observed --output build\blinx\scene-32-chain-lle-observed.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_composed_reference.py --captures build\blinx\scene-inputs-firstworld-20261009 --draws (1..32) --diagnostics build\blinx\scene-32-chain-lle-observed --depth-arithmetic fma-b-first --include-noop --retain-targets --output build\blinx\scene-32-chain-lle-qualified.aot-private.json
& 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe' tools\native_scene_hle_controls.py --captures build\blinx\scene-inputs-firstworld-20261009 --observations build\blinx\scene-32-hle-observed --chain-observations build\blinx\scene-32-chain-hle-observed --output build\blinx\scene-hle-independent-controls.aot-private.json
```

This qualifies the retained noise-off **32-draw section with supplied starting
targets**, including its sequential target lifetime. It does not establish a
complete newly rendered frame, all attract/title graphics states or original
enabled-noise behavior. Partial clipping, broader shader/raster/combiner layouts,
whole-frame/title resource integration and the deferred silicon questions remain
open. Graphics `.116` remains in progress and the family count remains **4/9**.
This batch changes diagnostic tooling and probes only; no shipping draw, shader,
upload or cache path gains work. Focused builds/GPU fixtures were authorized;
no full title or audio run was repeated.

Continue broader exercised texture/state layouts and raster outcomes toward
complete scene composition, retaining the bounded projected-footprint gates.
The original DXT1-noise control transport is implemented and independently
qualified above; its enabled pixel effect remains in the deferred hardware queue.
The two earlier corpora contain 64 programs writing `oFog.x` with fog enabled;
their original submitted state selects EXP/planar fog. The preceding fog batch
qualifies the finite programmable route and now observes those raw coordinates.
Full scene texture/state/pixel agreement still needs its independent gate.
The captured scene vertex programs now execute on maintained LLE; complete
state/texture/pixel composition still needs qualification.
These bounded declaration/fetch/arithmetic cases do
not qualify all vertex slots, arbitrary aliasing, tiled vertex input or the entire
graphics family. Retain
existing scene-reference checks; audit shared expectations before accepting
agreement. Console-dependent startup and disputed hardware timing stay at the
back of the burndown. LLE performance is not an acceptance gate; preserve HLE
shortcuts wherever they meet the observable contract.

### Alpha-volume composition and homogeneous clip depth (2026-10-09 continuation)

The final-attract retained corpus supplies another complete 32-draw section:
2,520 vertices, an 11-instruction shader, uniform opaque white ARGB8 mips,
constant diffuse alpha .025, flat shading, LESS without depth writes and an
alpha-only write mask. Paired front/back draws use ONE/ONE addition and reverse
subtraction respectively. This differs from the first-world BC1 strip profile;
neither corpus represents a complete newly rendered frame.

Two maintained LLE gaps surfaced. Both homogeneous viewport clippers directly
interpolated already divided window Z using a homogeneous edge parameter,
changing its depth plane when W varied. They now reconstruct/interpolate Z*W
and divide by the clipped W. The software blend floor also rejected reverse
subtraction; it now executes destination*destinationFactor-source*sourceFactor
with saturation. Generated packets lacking a blend equation remain restricted
to ADD. Native HLE already supports the equation and needs no runtime change.

The authored independent triangle has Z=100+8*(x-8), varying W and a vertex
beyond the right viewport edge. All 713 interior pixels failed before the fix;
all pass afterward through original XDK submission. The permanent LLE fixture
reflects/transposes it across four clip sides, with direct/callback DMA and
interpreter/compiled shaders: 16 draws and 11,408 interior samples. Native checks
713 samples in each of three fresh lifetimes. Depth is within one integer step;
RGB/stencil are exact. Another 64 permanent draws check alpha add/subtract,
saturation and masked RGB. The vertex-color suite now has 934 cases; four core
LLE regressions and the new `pilot_shader_clip_depth_reference` CTest pass.
Fresh first-world replay and its independent 32-draw retained-target gate also
pass after the LLE changes.

The alpha software oracle intersects edges in exact rational **screen space**,
preserving affine window Z and inverse W. It does not reproduce the maintained
homogeneous clipper or use an after-image for expectations. Four-bit raster
setup, top-left coverage, declared binary32 depth arithmetic, LESS, overlapping
blends and preservation are checked. Native independently admits raw and actual
production shader observations, then applies binary32 perspective/viewport
operations and eight-bit nearest/even setup. Missing viewport binary32 rounding
caused two one-pixel prediction errors; the published arithmetic resolves both
without changing HLE or widening a tolerance. Guard-band coverage is a declared
observed-host model; exact NV2A/cross-device clipping precision stays separate.

Both routes pass the uninterrupted 32-draw sequence with targets initialized
once and each actual before-target equal to its qualified predecessor. Native
repeats the sequence across three fresh device lifetimes; raw/production output,
before-targets and after-targets are bit-identical.

| Retained alpha sequence | Original XDK / LLE | Native HLE, per lifetime |
| --- | ---: | ---: |
| Draws / vertices | 32 / 2,520 | 32 / 2,520 |
| Shader values checked | 73,080 exact | 95,760 raw + 95,760 production |
| Original method values checked | 2,176 | Supplied native API inputs |
| Target pixels checked | 9,830,400 | 9,830,400 |
| Pixels receiving passing fragments | 465,121 | 465,088 |
| Passing / rejected depth fragments | 592,651 / 193,675 | 592,531 / 193,671 |
| Final color or depth/stencil mismatches | 0 | 0 |

Three native fragments are within the declared gamma(8)+.6 depth acceptance
radius of stored Z. They still require the exact predicted alpha byte: no
boundary pixel is omitted or accepted merely for being close. This qualifies
the finite observed-host model and measured outcomes, not identical comparisons
on every permitted host interpolator or real NV2A. Cross-device/exact silicon
precision remains an explicit question.

All 23 software and 24 native corruption/independence controls pass: no-op,
omitted/stray/wrong alpha, RGB/depth/stencil, truncation, shader/input/state and
texture changes, plus ignored diagnostic raw/after data. The program mutation
is DPH->MOV; DPH->DP4 was equivalent for these W=1 inputs. Private evidence
retains the original control run and focused corrected mutation separately.

Private results under `build/blinx` include
`scene-alpha-fixed-lle-qualified.aot-private.json`,
`scene-alpha-chain-{lle,hle}-qualified.aot-private.json`,
`scene-alpha-{lle,hle}-controls-qualified.aot-private.json`,
`clip-depth-{before,after}.aot-private.json`, `clip-depth-native-ctest.log`,
`alpha-volume-lle-regressions.log` and
`scene-firstworld-clip-regression-qualified.aot-private.json`.
Native preparation uses `native_scene_hle_reference.py --profile alpha-overlay
--retain-targets`, with the separate `native_scene_alpha_host_reference.py`
gate. Software uses `native_scene_alpha_reference.py --depth-arithmetic
fma-b-first --retain-targets`. Captures/observations remain private.

The retained alpha-volume slice and its clipping/blend floor defects are closed.
Whole-frame/title state/resource composition, broader layouts and enabled-noise
hardware evidence remain open. Graphics stays partial; 4/9 families qualified.
Shipping HLE shader/draw/cache/upload paths remain unchanged. Focused fixtures
reuse retained inputs without a title/audio rerun.

## Whole-frame operation accounting - 2026-10-10

The two qualified 32-draw sections start with supplied targets. To identify
the actual composition gap, a dedicated diagnostic build now brackets one
frame between consecutive accepted swaps. `PILOT_GRAPHICS_FRAME_AUDIT` is
OFF by default and defines code only for `runtime.c`. Normal builds contain
no frame-ledger calls or environment checks. This is optional observability,
not an opt-in correctness fix; the earlier LLE clipping/blend fixes remain
active by default.

The ledger records returned graphics adapter calls under the existing graphics
lock, including all reached draw families, clears and target setters. It starts
after the first accepted swap satisfying the configured trigger, ends at the
next accepted swap and never starts a second frame. Its 32,768-call bound stops
an incomplete diagnostic rather than claiming a frame. Existing programmable
bound-stream captures run only within this interval, still bounded at 256.
FVF and user-pointer submissions remain explicitly uncaptured. Vertical-blank
waits outside the graphics lock and arbitrary guest CPU resource writes are
outside this operation census.

One muted, hidden Blinx run, Idle on one logical processor, finished the
selected frame in 98.204 seconds. The harness guarded against Marvel Rivals
and terminated its own title process after the ledger completed. There is no
additional audio qualification or performance estimate from this run.

| Selected frame, swaps 1970 to 1971 | Count |
| --- | ---: |
| Returned graphics calls | 7,649 |
| Draw submissions | 3,789 |
| Programmable bound-stream draws | 3,783 |
| Programmable user-pointer draws | 2 |
| FVF bound-stream draws | 1 |
| FVF user-pointer draws | 3 |
| Clears / target setter calls | 2 / 3 |
| Draws with both input/target companions | 256 |
| Draws without those companions | 3,533 |
| Device create/reset/release within interval | 0 |

`tools/native_frame_audit.py` verifies consecutive boundaries, contiguous call
numbering, terminal counts and the correspondence of each captured draw/scene
header to its actual primitive, range and targets. All 256 pairs match.
It deliberately reports `complete_pixel_qualification:false`: matching
headers are linkage, not validation of companion contents or correct pixels.
Its 18 hardware-free tests pass, including lost/renumbered calls, truncated
boundaries/companions, invalid ABI inputs, wrong draw/target linkage and
invented FVF capture rejection. Both the diagnostic and restored default title
build pass; the default binary has no frame-audit option/marker strings.

Private evidence is `build/blinx/frame-audit-firstworld-20261009/`, including
`frame.jsonl`, `accounting.aot-private.json`, run/log metadata and 256 retained
draw/scene pairs. The 256 draws contain 1,697 vertices and three programs:
214 draws with 69 instructions, two with 21, and 40 with 49. None satisfy the
two existing closed state profiles. This identifies additional scope; it is
not evidence of 256 incorrect native draws. These complete programmable
inputs can be inspected without another title run. Next: classify those programs and
state profiles, qualify clear effects and FVF/UP inputs, and replace repeated
per-draw target dumps with bounded complete-frame input capture. The full frame
and broader graphics family remain open; existing qualified sections and the
**4/9** family count are retained. Shipping HLE rendering caches and uploads
are unchanged.

## Selected-frame shader and state census - 2026-10-10

All **256 retained draws / 1,697 vertices / 49,213 output values** now pass
the independent captured-input software shader/fetch gate. Expectations were
generated before observations from stream bytes, declarations, constants and
instruction words. The three nonindexed programs use already admitted finite
opcodes: 214 draws with 69 instructions, two with 21 and 40 with 49. Native raw
outputs remain diagnostics, never expectations or tolerance selection.

`native_scene_reference.py --diagnostics --isolated-draws` executes the original
load/select/stream/constant/draw functions without instruction hooks, using a
fresh preowned driver/device fixture per draw. Actual queue completion, vertex
count and executed/unguarded draw counters are required. The shared fixture
stalled in the original constant setter after eight draws; allocator/ring
rollover remains an integration gap. These positive isolated draws do not
qualify resource lifetime or sequential targets.

`native_scene_shader_controls.py` passes **39 controls** across one draw from
each program: three baselines, 33 deliberate shape/provenance/observation/live
stream/constant/program/unsupported-opcode failures, and three corrupted-native
raw-output independence controls. Live input hashes are refreshed, so the
mathematical failures cannot rely on stale provenance.

The 256 inputs group into **eight state profiles**, with121, 38, 34, 31, 24, 4, 2, 2
draws. All have one MODULATE DXT1 stage, triangle fans, RGB/depth writes,
disabled blending/alpha test, enabled zero-density fog and captured noise 1.
Their packed driver cache differs from the previous closed firstworld section;
texture dimensions and programs also differ. Grouping establishes additional
input scope, not texture samples or pixels. One representative of each profile
now passes **616 independently derived state words** and complete shader
output through the original setters and maintained reference. The two exact
initial cache patterns are named in `PACKED_PROFILES`; unknown patterns still
fail. The existing firstworld pixel gate keeps its original cache requirement.
`native_scene_state_reference.py` separates shader/state admission from pixel
qualification. Its **30 controls** across the three programs pass: three
baselines, 24 deliberate state/method/cache/provenance-context failures, and
three ignored native-after-image controls. No state expectation comes from
observed packets. The
3,533 uncaptured draw inputs and the frame's clears/target transitions remain
open alongside original enabled-noise and exact silicon questions.

The broader inputs also reach diffuse RGB above the previous closed pixel
range: the 69-instruction program reaches 1.1443374156951904, the 21-instruction
program produces 1, and the 49-instruction program reaches 0.5267830491065979.
Specular remains zero. Existing pixel guards stay in place; wider interpolation,
clamping and decoder-error contracts need independent equations and controls.

Private evidence: `build/blinx/frame-256-shader-{expectations,isolated-original,independent,controls}.aot-private.json`,
actual observations in `frame-256-shader-isolated-observed/`, and
`frame-256-state-profile-census.aot-private.json`. CPU work stays Idle on one
logical processor. No GPU/title/audio run, rebuild or shipping HLE change was
needed. Graphics remains partial; family count remains **4/9**.

The representative setter evidence is in
`frame-eight-state-{original,qualified,controls}.aot-private.json` and actual
observations in `frame-eight-state-observed/`. Replay the captured input
directory with `native_scene_draw_reference.py --draws 1 2 40 44 71 73 75 111
--noise-disabled-fixture --diagnostics <private-directory>` using the existing
original XBE/binding/reference-library arguments. Then run
`native_scene_state_reference.py --draws 1 2 40 44 71 73 75 111 --packed-profile
frame-firstworld`, supplying that capture and observation directory. Controls
use `--draws 1 71 73 --controls` with the same profile and observations.

The continuation extends original setter replay and independent shader/state
qualification to **all 256 draws / 19,712 state words / 49,213 shader values**.
`native_scene_draw_reference.py --observe-shader-state-only --limit 256` still
executes each complete draw, including original software-method service. It
omits target output files and native-after comparisons; it does not omit device
work or claim pixel agreement. The observations occupy one small JSON per draw.
Default target bytes remain identical to the preceding fixture; shader/fog/
method observations agree between both modes. Retained-target sequence mode
requires target observations and cannot combine with this option.

Retained actual `pilot_gfx_shader_probe` GPU outputs also pass the existing
input-derived host numerical interval gate: **256 draws / 1,697 vertices /
64,486 raw values**, with zero failures. This checks raw diagnostic shader
bytecode. Production `raster_main`, setup/interpolation, texture/combiner pixels
and the whole frame remain separate. `native_scene_host_capture_reference.py`
adds a hardware-free corpus entry point; **30 controls** across the three
programs pass: three baselines, 24 deliberate output/shape/nonfinite/live input/
program failures, and three controls proving its expectation ignores the
capture's stored output field. Bound equations and radii are unchanged and
never selected from observed deviations.

Additional private evidence is `frame-256-state-{original,qualified}.aot-private.json`,
`frame-256-state-observed/`, and
`frame-256-host-raw-shader-{qualified,controls}.aot-private.json`. This reuses
retained inputs and GPU observations; no host GPU/title/audio run, rebuild or
shipping HLE change occurs. **4/9** remains unchanged.

## Complete-frame input capture - 2026-10-10

The diagnostic build now records every admitted draw inside one accepted-swap
interval. Enable the existing `PILOT_GRAPHICS_FRAME_AUDIT` build option and set
`XLLE_PILOT_FRAME_AUDIT=<private-directory>`, `XLLE_PILOT_FRAME_INPUTS=1` and an
optional `XLLE_PILOT_DRAW_CAPTURE_TRIGGER=<private-file>`. The ordinary build
compiles out this writer and its environment options; the shipping HLE draw,
shader, texture upload and cache paths acquire no capture cost. The default
build was restored and its binary checked for absent diagnostic markers.

`frame-inputs.bin` is a bounded, little-endian version-2 container. SHA-256
assets store declarations, programs, constants, states, consumed stream bytes,
FVF descriptors, CPU texture inputs and actual sampled GPU mips without
repeating identical bytes. Every programmable/FVF bound/UP draw names its
returned ledger call and input sequence. A bound stream supplies exactly
`(count-1)*stride+consumed_extent` bytes, including stride-zero broadcasts and
resources without unused final stride padding. These diagnostic snapshots do
not adopt aliases or change guest texture ownership/exposure.

Each color/depth subresource supplies its initial target once. Observations
are taken before target switches and before the terminal Swap, when the old
buffers still identify this frame. Samplers explicitly link to prior target
producers, including native texture aliases. Back/front storage remains
distinct from depth storage. Later observations cannot replace supplied roots
or expected outputs from earlier draws. Storage creation, device reset/release
and CPU target locks inside the interval stop this scoped capture explicitly.
Original allocation/startup and arbitrary resource mutation remain open.

One new hidden, muted title run at Idle priority on logical processor 15
completed in 101.797 seconds. Its guard checked for Marvel Rivals before and
during execution, and terminated the owned title after the completed frame.
The independently verified interval is swaps **2033 to 2034**:

| Captured input coverage | Count |
| --- | ---: |
| Returned graphics calls | 8,718 |
| Draws / vertices | 4,331 / 29,396 |
| Programmable bound / UP draws | 4,325 / 2 |
| FVF bound / UP draws | 1 / 3 |
| Clears / target switches | 2 / 3 |
| Initial target subresources | 5 |
| Sampler references to target producers | 10 |
| Unique assets | 4,863 |
| Container bytes | 20,334,984 |

This is a new complete interval, rather than an assertion that it is the
earlier swaps1970-1971 interval. The new inputs reveal **20 programs, 41 state
profiles, five declaration layouts and three packed caches**. The earlier
256-draw shader/state gates cover only a subset. Newly exposed inputs include
multiple streams, additional shader operations, alpha/state combinations,
depth sampling, fixed-function draws and target-produced textures. These now
have concrete inputs for offline qualification; they are not newly qualified
shader/state or pixel results.

Run `tools/native_frame_inputs.py <private-directory>/frame.jsonl --output
<private-directory>/coverage.aot-private.json`. It verifies asset checksums and
extents, exact returned draw linkage, root/bind/observation continuity, mip
encodings and producer identities. It rejects missing pre-bind observations,
missing draws even with adjusted footer counts, corrupt/truncated/forward
assets, changed target identities, missing producer links and unsupported
lifecycle scope. All **24 independent authored parser tests**, the existing
**18 ledger tests**, and the actual C writer fixture pass. The latter includes
all four draw kinds, minimal bound spans, back/front aliases and a deliberately
failed readback that cannot produce a complete input capture.

Private evidence is `build/blinx/frame-inputs-firstworld-20261010/`, including
the container, ledger, `coverage.aot-private.json`,
`profile-census.aot-private.json` and run metadata. The diagnostic executable
and default-build exclusion check are retained privately. Complete input
coverage is now established for this interval; independent complete-frame
shader/state/pixel qualification, original resource lifetime/startup, enabled
noise and exact silicon questions remain. **4/9** remains unchanged.

## Complete-frame programmable shader component - 2026-10-10

All **4,327 programmable draws / 29,380 vertices / 852,020 output values**
in the swaps2033-2034 input corpus now pass exact independent software
shader/fetch checks. This covers all20 programs and both bound and
user-pointer submission. Each draw executes the unmodified original XDK
Load/Select/SetConstant/SetStream/Draw path on fresh preowned reference
resources. The independent evaluator computes expectations from captured
inputs before inspecting the actual LLE outputs. The input adapter leaves
native outputs absent; they cannot become expected values.

The evaluator now admits FLOAT1/2/3/4 defaults, multiple streams, signed ARL
floor and relative constants within the192-entry context, paired old-address
operand reads, and fractional positive LIT powers. ARL values where a small
positive donor bias could change the floor remain guarded. Published fields
come from [the Kelvin ISA](https://envytools.readthedocs.io/en/latest/hw/graph/xf/isa.html);
ARL/LIT semantics come from
[NV_vertex_program](https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program.txt).
Nearest/even binary32 is the selected software contract, rather than a claim
about NV approximation accuracy or exact silicon results.

Fractional LIT uses the independent interval arithmetic in **mpmath1.3.0**,
with successively higher precision. Both outward interval endpoints must round
to the same binary32 value. Exact polynomial midpoint tests independently
check representative roots. This is an offline reference dependency; install
`mpmath==1.3.0` in the Python environment already used by the original-XDK
reference tools. Shipping HLE shaders and their performance are unchanged.

The first two UP fixtures stopped in the original copy loop because the
fixture requested bound-buffer dirty bits. Original helper0x13DE90 builds the
inline declaration/copy plan when bit0x20 is dirty. Correcting that input makes
both original DrawVerticesUP calls return and match232 independently computed
values. No copy plan, instruction hook or successful return is fabricated.
The retained union includes4,325 bound observations from the full batch and
these two corrected UP observations; conflicting observations are rejected.

The new admission and input/math suites pass65 authored tests. All70 retained
corruption/independence controls pass, including changed live inputs with
refreshed provenance, missing/guarded draws, wrong submission routes and
ignored native-output fields. Rechecking the previous256 captures preserves
their49,213 exact software values,64,486 raw native interval checks and30
native controls, without new title/GPU/audio runs.

Private evidence is `build/blinx/frame-inputs-firstworld-20261010/`, chiefly
`shader-all-retained-qualified.aot-private.json`, `shader-all-observed/`,
`shader-up-corrected/` and the two controls reports. Run
`tools/native_frame_shader_reference.py --captures <private-directory>
--diagnostics <bound-observations> --additional-diagnostics <up-observations>
--output <private-report> --all-draws --audit-existing --library <reference-dll>`
to re-audit retained observations against independently recalculated inputs.
The report keeps full-scene pixels, state delivery and resource lifetime false.

Four FVF draws, all41 full-frame state profiles, clears/target-produced texture
composition, production raster bytecode, enabled noise and original resource
startup/lifetime remain open. No shipping change, rebuild, host GPU, title or
audio run was needed for this milestone. **4/9** remains unchanged; the entire
LLE checklist remains active. Xbox access still awaits the earlier requested
screen inspection after the unexpected GP SG-address readback.

## Complete-frame vertex components including FVF - 2026-10-10

The remaining four FVF draws now pass an independent pretransformed component
check: **16 vertices / 464 values**, with original SetVertexShader and actual
bound/UP draws. Together with the programmable results, all **4,331 draws /
29,396 vertices / 852,484 values** in this captured frame have qualified
software vertex-output components. State, raster and full-scene pixels remain
separate; this does not turn the family count into5/9.

The FVF oracle derives active declarations from XYZ(RHW), TEX1 and optional
D3DCOLOR diffuse, using the
[published FVF fields](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dfvf).
It admits the captured zero-depth, disabled-fog profile, unit fixture pixel
scale, captured screen bias and RHW0/1. The independently checked XDK screen
offset and software1/16-pixel truncation determine X/Y. The final bound quad
has positive zero RHW; the finite reciprocal limit comes from the special-case
rule in [NV_vertex_program2](https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program2.txt),
not an observed output or an invented divide-by-zero success. Negative zero,
other RHW/depth profiles and arbitrary fixed-function transforms remain
guarded. Exact silicon precision remains separate.

Original FVF declaration generation runs from an empty preowned descriptor.
Active formats, stream numbers and offsets must agree with independent fields
and the captured input descriptor. Unconsumed offsets in disabled slots may
retain stale bytes and cannot become fetched inputs. Original calls return,
FIFO GET reaches PUT and the observed vertex count is exact. QUADS use the
separate LLE quad path, whose triangle execution counters remain zero; guarded
service counters still reject the draw. The triangle-strip FVF case requires
its actual triangle execution as well.

All10 authored FVF tests and60 retained failure controls pass. The complete
focused Python suites pass75 tests; combined programmable/FVF controls total
130. The FVF controls reject changed live inputs with refreshed identities,
wrong screen bias, missing vertices/fog, pending FIFO, wrong routes,
non-returned calls and active declaration corruption. No production code,
shader bounds, rebuild, host GPU, title, audio or Xbox request changed.

Private evidence adds `fvf-observed/`, `fvf-retained-qualified.aot-private.json`
and `fvf-controls.aot-private.json` under the complete-frame input directory.
Use `tools/native_frame_fvf_reference.py --captures <private-directory>
--diagnostics <fvf-observations> --output <private-report> --audit-existing
--library <reference-dll>` to repeat the retained check. The report explicitly
leaves state delivery, resource lifetime and complete-scene pixels false.

The41-profile state admission census also records concrete differences from
the earlier closed model: additional blend/mask/fog settings, custom pixel
programs, a third packed cache and mixed target dimensions. These require
independent state and composition work; the census is not a pass. Full-frame
state, target/texture composition, production raster output, enabled noise and
original startup/lifetime remain open. **4/9** and the full goal remain active.

## Multiple linear PROJECT2D textures - 2026-10-10

Original setter/draw replay of all41 sampled state profiles in the complete
input corpus exposed a default LLE raster limitation. Three draws using
multiple pitch-linear textures reached `unsupported-texture-format`; only38
completed. The existing linear sampler now admits explicit LU A8R8G8B8 and
X8R8G8B8 on independent PROJECT2D stages. Unknown formats, insufficient row
pitches and unqualified depth layouts retain their guards. This correctness
fix applies by default and reuses the existing sampler implementation.

The independent test uses authored endpoint colors on two/four textures with
different sizes and padded pitches. It checks projected S/Q,T/Q coordinates,
ARGB/XRGB alpha, exact interior pixels, unchanged background and live texture
updates without rebinding. These follow the published
[rectangle texture coordinates](https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_rectangle.txt)
and [independent texture stages](https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_shader.txt).
Both direct and callback memory paths run. Six negative draws require the
named format guard and no target writes. Before the fix, the new positive
cases produced1,072 failed assertions; afterward all956 vertex-color draw
cases and the five NV2A/tile/zeta regression suites pass.

With the rebuilt CPU reference, all41 original sampled profile draws now
return, consume their FIFO and produce their expected vertex counts without
LLE guards. These are representative raw state-cache profiles, not proof of
state delivery for all4,331 draws. The input-only fixture uses retained CPU
texture storage and fresh preowned target roots; sampled GPU bytes and native
after-images never supply expected pixels. Noise remains explicitly disabled.
Delivered state, target continuity, scene pixels and original resource lifetime
remain unqualified. The family count remains **4/9**.

Private evidence adds `state-profile-original.aot-private.json` and
`state-profile-linear-fixed-original.aot-private.json` beneath
`build/blinx/frame-inputs-firstworld-20261010/`, plus the
`multistage-linear-*-20261010.aot-private.log` build/test logs. One focused CPU
rebuild was needed; no new title, audio, host GPU or Xbox run occurred. Shipping
HLE shaders and capture defaults are unchanged.

## Complete-frame sampled state-word components - 2026-10-10

The41 sampled raw cache profiles now pass **3,251 independently derived
delivered/setup words** against actual original calls with the corrected CPU
reference. This includes2,648 scalar/sampler/default-final words,285 custom
pixel-state words and328 explicit fixture target-setup words, with10 overlaps
between default/custom final fields. These are state-word components; fixed
function combiner compilation and complete-frame state admission remain false.

`tools/native_frame_state_reference.py` adapts the content-addressed container
to original bound/UP and programmable/FVF replay. It preserves minimal consumed
stream spans and each target role's own dimensions/pitch. CPU texture storage,
sampled GPU inputs and target roots stay distinct. The adapter has no native
shader outputs or after-image. GPU aliases/views require a separate fixture.
Input-only replay rejects pixel comparisons and retained-target claims before
creating a reference device. The shared replay now reports original draw
return, FIFO, route, guards, input identity and additional logic/final words.

`tools/native_frame_state_delivery.py` computes expected words before opening
observations. The admitted scalar/sampler equations cover the new blend,
mask, fog, cull, logic, filter and pitch-linear resource combinations. Disabled
fog/cull/resource words are unconsumed. Custom pixel constants come from the
active API cache, preserving its mapped updates rather than substituting
definition defaults. The reviewed original SetPixelShader helper0x1365E0
records whether definition words8/9 supply a final combiner; helper0x13CDB0
supplies the fog/specular final when absent. The register identities and fields
are published in
[the pinned nxdk definitions](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h).
Fixture target words verify separate color/zeta pitches and do not qualify
original SetRenderTarget or allocator behavior.

All21 new authored input/state tests pass, bringing the focused Python total
to96. All575 retained controls pass, including changed live API/resource inputs
with refreshed provenance, mapped custom constants, wrong/missing methods,
pending FIFO, guarded draws, wrong original routes/builds and ignored native
outputs/sampled GPU bytes. The older eight original scene representatives
still pass616 state words and4,060 shader values with the rebuilt reference;
their30 retained controls also pass. No further rebuild, title, audio, host GPU
or hardware request was needed for these checks.

Private results are `state-profile-delivery-original.aot-private.json`,
`state-profile-delivery-observed/`, `state-profile-delivery-qualified.aot-private.json`
and `state-profile-delivery-controls.aot-private.json` in the complete-input
directory. Replay with `native_frame_state_reference.py --captures <directory>
--diagnostics <observations> --output <report> --xbe <private-xbe> --binding
<private-binding> --library <reference-dll> --dependencies <mingw-directory>`.
Then audit with `native_frame_state_delivery.py --captures <directory>
--diagnostics <observations> --output <report> --library <reference-dll>`;
add `--controls` for failure checks. Both tools select one draw per raw state
cache profile by default; `--all-draws` explicitly selects the entire interval.

All4,331 draws still need complete combined state admission, including fixed
function combiners, followed by original clear/target/texture composition and
pixels. Production HLE raster bytecode, enabled noise, original startup/lifetime
and hardware precision remain separate. **4/9** and the full goal stay active;
the earlier Xbox screen inspection is still pending.
