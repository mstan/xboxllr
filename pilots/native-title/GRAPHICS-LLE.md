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

All 32 audited captures currently stop at **`NV2A unsupported RDI write bank`**:
the original DXT1-noise setter emits software method `0x28`; its original service
attempts RDI index `0x00e00050`, value `1`, followed by `0x00df0008` in the same
original handler. The first write is not implemented, so the service does not
return and the queued draw is not qualified. The report records actual MMIO,
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
ignored controls are not an independent pixel oracle. Control/state implementation
comes first; any unresolved silicon noise/precision question stays at the end.

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
10–13, then demo re-entry at 18 and continued playback at 20. Both final runs
use title SHA-256
`02b7917222f2f1725954512a241d9942db4e8d6d2c9062d4e48e01c6fa7352d4`.
The harness intentionally terminates at the requested duration; its exit code
1 is not a title fault. This preserves the observed attract/input behavior;
it is not a new frame-rate or full A/V qualification. Graphics `.116` remains
partial and the family count remains **4/9**.

## Remaining offline work

Implement and qualify the original DXT1-noise RDI control path exposed by the
complete-draw audit, then continue its fog, texture mip/filter and raster outcomes.
The captured scene vertex programs now execute on maintained LLE; complete
state/texture/pixel composition still needs qualification.
These bounded declaration/fetch/arithmetic cases do
not qualify all vertex slots, arbitrary aliasing, tiled vertex input or the entire
graphics family. Retain
existing scene-reference checks; audit shared expectations before accepting
agreement. Console-dependent startup and disputed hardware timing stay at the
back of the burndown. LLE performance is not an acceptance gate; preserve HLE
shortcuts wherever they meet the observable contract.
