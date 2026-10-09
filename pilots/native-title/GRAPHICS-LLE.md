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
and variable perspective footprints, broader texture layouts and complete
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

## Remaining offline work

Continue variable projected footprints, broader exercised texture/state layouts
and raster outcomes toward complete scene composition.
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
