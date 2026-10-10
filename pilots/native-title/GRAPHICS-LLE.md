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

## Complete-frame joint shader and state components - 2026-10-10

All **4,331 original full-state draw replays** in swaps2033-2034 now return,
consume their FIFO and execute without guards using the corrected CPU reference.
The independent joint audit compares the same actual calls against captured
inputs: **29,396 vertices, 852,468 vertex values and 470,947 delivered/setup
words**, covering programmable/FVF and bound/UP submissions. This closes the
sampled-profile gap for these components. Each draw still uses fresh preowned
resources; original target binding, interdraw continuity and pixels remain open.

`tools/native_frame_combiner_reference.py` derives fixed-function source routing,
complement/alpha replication and SELECTARG1/SELECTARG2/MODULATE/ADD words from
the [published Direct3D operations](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtextureop),
[arguments](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dta)
and [NV register-combiner equations](https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt).
Reviewed original helpers0x140070/0x140110 establish the consumed-argument
missing-texture fallback and unused-slot clearing. Neither the maintained
compiler/renderer nor observed packets supply an expected word. Unsupported
operations, factor/TEMP inputs and transforms remain guarded. All36 sampled
fixed-function profiles pass1,260 added words; the combined41-profile audit
passes4,511 words and755 failure/independence controls.

`tools/native_frame_shader_state_reference.py` prepares independent shader and
state expectations before opening the actual observation. Full original FVF
dirty state requests TEXCOORDINDEX0 on all four stages, so the sole TEX1 input
is independently replicated to each requested output. Fog is disabled in the
admitted FVF profile: its16 raw fog coordinates are explicitly unconsumed and
their scalar values are not qualified. This explains the difference from the
historical852,484-value bare vertex-component audit; the joint audit compares
852,468 values. Enabled FVF fog and other unadmitted FVF profiles remain guarded.

All115 focused authored Python tests pass, including eight new combiner tests
and11 joint tests. The joint41-profile controls pass455 cases, rejecting changed
live inputs with refreshed identities, missing/corrupt components, wrong builds,
pending FIFO and guarded/nonreturned calls. Ignored native after-images and
disabled FVF fog cannot supply expected values or cause a false rejection.

Private evidence adds `fixed-function-first-audit.aot-private.json`,
`state-profile-combined-delivery-qualified.aot-private.json`,
`state-profile-combined-delivery-controls.aot-private.json`, `all-state-observed/`,
`all-state-original.aot-private.json`, `all-shader-state-qualified.aot-private.json`
and the joint profile/control reports under the complete-frame input directory.
Use `tools/native_frame_shader_state_reference.py --captures <private-directory>
--diagnostics <all-state-observations> --output <private-report>
--library <reference-dll> --all-draws` for the retained complete-frame comparison.

Next are original clears, target switches and linked texture producers, followed
by complete-scene pixels. Production HLE raster bytecode, enabled noise,
original startup/lifetime and hardware precision remain separate. This batch
needed no rebuild, title, audio, host GPU or Xbox request and changes no shipping
HLE code or capture defaults. **4/9** families and the full goal remain active.

## Original target and clear components - 2026-10-10

`tools/native_frame_target_reference.py` now executes the unmodified original
SetRenderTarget0x131E90 and Clear0x135A60 entry points on one CPU reference with
five retained preowned linear target roots. The explicit root bind, three
captured target switches and two captured clears pass **112 delivered/setup
words and31,334,400 target-byte comparisons**. The comparison checks all five
roots after every call, including untouched roots. Original calls return,
FIFO GET reaches PUT and no LLE guard occurs.

Expected fields come from captured resource dimensions/pitches, declared
fixture addresses and the
[public NV097 surface/clear fields](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h).
The byte model independently converts supplied RGBA/BGRA and native D24S8 root
inputs to NV097 linear storage. Color/depth masks preserve unused channels,
stencil/depth components, row padding and storage outside the color viewport.
Only exact endpoint clear depths are admitted. Mixed320x240 color/640x480
depth uses each role's own pitch. Neither emitted words, maintained renderer,
native after-images nor later target observations supply expectations.

All12 new authored tests and96 retained failure controls pass. They reject
wrong/missing words or targets, corrupt untouched bytes, changed dimensions
with refreshed input identity, incorrect binding/viewport, wrong builds/routes,
pending FIFO, guards and nonreturned calls. Private evidence adds
`target-clear-observed/`, `target-clear-qualified.aot-private.json` and
`target-clear-controls.aot-private.json` beneath the complete-frame directory.
The tool's `--audit-existing --controls` repeats the controls without another
original execution; generated evidence stays private.

This is an explicit **full-target viewport fixture**. The compact clear record
does not include the live viewport, and
[Clear operates within the viewport](https://learn.microsoft.com/en-us/windows/win32/direct3d9/viewports-and-clipping).
Intervening draws are omitted, so retained target storage here does not qualify
whole-frame continuity, texture producers or complete-scene pixels. Owner
references also do not qualify original allocation/deletion or lifetime.
The next integration must preserve producer-backed storage through the draws
and resolve the live initial clear viewport. No runtime/HLE code, rebuild,
title/audio, host GPU or Xbox request changed. **4/9** and the full goal remain
active; hardware is still at its existing inspection gate.

## Retained original frame composition - 2026-10-10

`tools/native_frame_composition_reference.py` now executes the captured root
bind, clears, target switches and **all4,331 original draws /29,396 vertices**
in their recorded order on one reference device. Five preowned roots survive
the complete sequence. All10 producer-backed sampler bindings point at the
actual retained color/depth storage, including two self-feedback bindings and
the four-stage downsampled producer. Sampled native GPU bytes never replace
these producer inputs. The same actual draws pass **852,468 independent vertex
values and462,399 consumed/setup words**.

`tools/native_frame_storage.py` validates each role's dimensions, pitch,
addresses and producer identity. Unrelated CPU textures retain separate
storage; producer-backed textures use the captured linear resource layout.
Original SetRenderTarget establishes the device context, including its unit
minimum line scale. Consequently LineWidth derives from the original API
float and that context rather than the older zero-scale fixture. Normalized
texture bindings leave prior linear rectangle pitch/size words untouched;
those two words are unconsumed by normalized sampling. Omitting these stale
fixture fields explains the8,548-word difference from the fresh-device audit,
without narrowing its active sampler contract.

The first complete attempt exposed a **reference observation bug**, after
draw4304: the six still-bound surface parameters had aged out of the4096-entry
method recorder. `tools/nv2a_reference.cpp` now retains last decoded parameters
across bounded transport advances and explicitly rejects an observation gap.
These remain decoded packet observations, not a new semantic-register oracle.
No NV2A core or shipping renderer change was needed. The actual ABI regression
fails against the preserved older DLL and passes against the new DLL: retained
parameters after6141 later methods, live updates, independent instances,
untouched target bytes and rejection of history overflow. All123 focused
authored Python tests also pass.

The successful full run uses34 explicit drained-PFIFO fixture jumps to recycle
its preowned pushbuffer while retaining PGRAPH and target state. This does not
qualify original ring rollover/allocation. Both reference DLLs are preserved
for their corresponding reports. Private evidence adds
`composed-all-v2-qualified.aot-private.json`, `composed-all-v2-observed/` and the
native/LLE diagnostic images beneath the complete-input directory. Replay with
`native_frame_composition_reference.py --captures <private-directory>
--diagnostics <private-observations> --output <private-report> --xbe <private-xbe>
--binding <private-binding> --library <reference-dll>
--dependencies <mingw-directory> --all-draws`.

This proves executable retained submission and its shader/state components,
**not complete-scene pixels**. The composed image differs materially from the
native capture in sky color and blur. Sky differences already exist before the
first feedback quad. Additionally, the generic triangle rasterizer treats
stencil enable as inert: retained LLE stencil is all zero, while the native
producer has7300 pixels with stencil128. Independent stencil/raster
qualification is next; a plausible image does not qualify either route.
Noise remains disabled in the LLE fixture, the initial clear viewport remains
explicit, and original startup/lifetime, feedback caching, production HLE
pixels and hardware precision remain open. No new title/audio/host GPU/Xbox
run occurred; only the reference observation wrapper was rebuilt, at Idle
priority on one logical processor. **4/9** and the full goal remain active.

## Generic stencil correctness and initial-clear independence - 2026-10-10

The generic triangle rasterizer incorrectly classified stencil-enable bit5 as
inert. It recorded the requested state but performed no stencil comparisons
or operations. The first684 authored stencil draws reproduce1,122 failed
assertions before the fix. The maintained LLE now executes masked S8
comparisons, FAIL/ZFAIL/ZPASS operations and masked writes while preserving
D24. Shader/alpha rejection suppresses every stencil operation; stencil
rejection suppresses color, depth writes and visibility. Color/depth masks
remain independent of stencil writes. This correctness fix applies by default.

Expectations follow [OpenGL1.3 sections4.1.4-4.1.6](https://registry.khronos.org/OpenGL/specs/gl/glspec13.pdf),
the [wraparound extension](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_stencil_wrap.txt)
and the [pinned NV097 stencil fields](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h).
All eight comparisons and eight operations pass authored endpoint/mask cases,
including saturation, wrapping, reference-vs-stored operand ordering,
alpha-before-stencil, all three operation branches, stencil-only draws,
visibility, a live second draw and separate padded color/zeta pitches.
Malformed active fields, undersized pitch, Z16 and unbound zeta stop before
raster writes; disabled stencil ignores stale fields. Linear zeta storage now
uses its own pitch and covered-row extent. Existing unusable depth-only
fallback behavior is unchanged. Accelerated LLE packets carry neither stencil
state nor a separate zeta pitch, so these draws remain on the software floor.
Shipping native HLE shaders, upload caches and capture defaults are unchanged.

All **1,662 authored draw cases**, including706 new positive/guard cases, pass.
The five NV2A/tile/zeta regression suites,131 focused Python tests and actual
reference-observer ABI regression also pass. The corrected complete original
frame replay still passes all **4,331 draws /29,396 vertices**,852,468
independent vertex values and462,399 consumed/setup words. The prior reference
DLL is preserved beside the earlier report; private new results are
`composed-stencil-qualified.aot-private.json` and `composed-stencil-observed/`
under the complete-input directory.

The corrected composed image removes the large sky discrepancy. Its stencil
producer has7264 marked pixels versus7300 in the native capture, with76
differing stencil positions. Diagnostic mean absolute RGB error drops from
12.43 to2.39 channel steps. These comparisons are **not independent pixel
qualification**: native noise is enabled, fixture noise is disabled, and
remaining blur/raster differences need independent explanation. Counts or
visual plausibility cannot choose the correct route.

`tools/native_frame_clear_dependency.py` separately resolves the missing live
initial clear viewport as an input dependency for this bounded fixture. The
supplied initial depth root already contains depth1/stencil0 at all307,200
pixels, so that clear is idempotent for any viewport. No draw samples or renders
the initial color root before its first draw4301, which samples other roots
and independently derives a complete640x480 opaque strip: full RGBA writes,
no destination blend/logic read, alpha/depth/cull rejection or shader clipping,
and stencil ALWAYS. Positive W and unit texture Q cover all307,200 color pixel
centers. The prior color bytes cannot reach final targets. Eight authored tests
reject changed depth/stencil inputs, incomplete geometry, prior reads,
destination-dependent state and discard paths. Private proof
`initial-clear-dependency.aot-private.json` uses captured inputs and independent
vertex/state math, never native after-images or observed LLE pixels. This does
not observe the live viewport or qualify retail startup/lifetime, and needs
no new title capture.

Complete-scene pixels, production HLE raster equivalence, feedback caching,
enabled noise, original startup/lifetime and exact hardware questions remain
open. CPU builds/checks used Idle priority on one logical processor; no new
title/audio/host GPU/Xbox run occurred. **4/9** and the full goal stay active.

## Complete QUADS raster execution - 2026-10-10

The three captured QUADS calls2767,2820 and4331 returned with independently
qualified vertices/state, but the old witnessed-quad route did not recognize
their state and left them recorder-only. The earlier input-only qualification
exception allowed these zero-execution draws to carry a `complete_draw_executed`
flag. That flag was too broad: returned calls and transformed vertices did
not establish raster execution. The replay and independent state audit now
require a recorded raster execution for QUADS as for other complete draws.
Historical vertex/state evidence remains component evidence, not a pixel pass.

Unrecognized witnessed QUADS now enter the default general software rasterizer,
using two triangles per independent four-vertex group and one flat-shading
provoking vertex per quad. Recognized legacy profiles retain their route.
Authored cases check every64x64 target pixel for array/indexed/inline/immediate
submissions, one/two independent quads, visibility/no-query routes and exact
single coverage of a blended shared diagonal. Last-vertex flat shading follows
[the published quad convention](https://registry.khronos.org/OpenGL/specs/gl/glspec13.pdf).
The [provoking-vertex extension](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_provoking_vertex.txt)
makes first-vertex behavior for quads implementation dependent; that request
remains explicitly guarded pending silicon evidence. Incomplete groups are
also outside this admitted contract and stop before raster writes. Neither
guard affects the captured smooth four-vertex Blinx quads. General QUADS use
the software floor; shipping native HLE code and caches are unchanged.

The first26 new cases reproduce17,122 failed assertions before the fix.
All **1,692 authored draw cases**, including30 new QUADS positive/guard cases,
now pass. Five NV2A/tile/zeta suites and137 focused Python tests also pass.
The actual original seven-draw smoke passes all10 producer bindings. Replay
discovers counter names from the reference ABI, including its two added
stencil guard reasons; the independent audit explicitly admits the retained
26-counter and current28-counter layouts and checks every guard value.

The final corrected frame executes **all4,331 original draws /29,396 vertices**
with852,468 independent vertex values and462,399 consumed/setup words. Each
full-screen QUADS call now records one raster execution and307,200 color
writes; the glyph records one execution and zero writes after pixel tests.
All28 counters are observed. The final build keeps the unqualified first-flat
convention guarded. Private evidence adds `composed-quads-final-observed/`,
`composed-quads-final-qualified.aot-private.json` and the diagnostic image under
the complete-input directory; previous DLLs/reports remain preserved.

Complete-scene pixels remain false. Residual native/LLE blur and raster
differences need independent pixel/consumer qualification, with enabled noise
and exact silicon behavior still separate. The initial-clear dependency proof
remains valid; no new title capture is needed. This batch used CPU-only Idle
work on one logical processor, with no title/audio/host GPU/Xbox run.
**4/9** and the full goal remain active; hardware stays at its inspection gate.

### Independent postprocess consumer pixels — 2026-10-10

The two self-feedback quads and the final three postprocess rectangles now pass
an independent consumer pixel contract. `tools/native_postprocess_pixel_reference.py`
derives rectangle coverage, PROJECT2D texel-space nearest/bilinear filtering,
register-combiner input mapping, simultaneous stage writes, dot products,
blue-to-alpha, constants, MSB mux, final combination and source-alpha blending.
It imports no maintained renderer, decoder or native shader compiler. A separate
ALWAYS/KEEP-or-REPLACE stencil equation preserves depth, unselected stencil bits,
padding and uncovered rows.

The equations come from the public
[NV register-combiner specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt),
[per-stage constants](https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners2.txt),
[rectangle texture specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_rectangle.txt)
and pinned [NV097 fields](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h).
The declared software contract uses ideal floating equations and a one-code
UNORM8 output interval. MSB mux chooses AB below 0.5 and CD at or above 0.5;
minimum selector margin is reported. Exact NV2A fixed-point/filter rounding is
not asserted. LSB mux, undefined register reads, other geometry, texture
layouts, raster tests and logic operations remain guarded; unblended COPY is
admitted. Single-level filters admit matching nearest or linear min/mag modes.

`tools/qualify_native_postprocess.py` takes the retained color/depth producer
snapshots as **supplied inputs**. Their hashes must match the completed original
composition report and the current reference DLL. Independent input-derived
shader vertices/state are prepared before original execution. The two feedback
quads run as isolated components. For the final three draws, expected images
form a chain: the expected blur mask and expected base copy feed the blend
expectation. No actual intermediate or native after-image becomes a golden.
All observed depth/stencil roots are compared byte-for-byte; unwritten color
roots must remain exact and earlier color outputs stay inside their declared
intervals. This does not qualify the earlier scene producers or feedback cache
behavior beyond the admitted same-pixel components.

All five actual original draws pass **1,305,600 pixel /5,222,400 channel**
comparisons:

| Input | Component | Pixels | Maximum channel-code difference |
| --- | --- | ---: | ---: |
| 2767 | First feedback quad | 307,200 | 0 |
| 2820 | Second feedback quad | 307,200 | 0 |
| 4300 | Half-size blur mask | 76,800 | 1 |
| 4301 | Base scene copy | 307,200 | 0 |
| 4302 | Blur blend using expected intermediates | 307,200 | 1 |

All depth/stencil bytes and untouched roots pass. The blur-mask MSB selector
has minimum ideal margin about `7.68935e-6`; one pixel falls within `2e-5` of
the threshold. Its actual software result agrees with the independently chosen
ideal decision. Silicon precision at that boundary remains a deferred question.
All **19 authored Python tests** pass, including negative input mapping, stage
simultaneity, dot/blue-to-alpha, selector boundaries, view interpretation,
sampler/raster guards, blend and masked partial stencil writes.

Private evidence is `postprocess-independent-qualified.aot-private.json`
(SHA-256 `a501a41ee1f36931996eb3bc6af1d4ef0ff91cb9d20c15d3b6d049788c4d4ab8`)
and `postprocess-independent-observed/` beneath the complete-input directory.
The reference DLL remains
`9fdec5189881eabf77d00a52a8442ee57f5f2fe8199d7c22335594ee28cedf95`;
no core or shipping pilot rebuild was needed.

The depth consumer points to an HLE view mismatch worth correcting next. Its
captured sampling header declares raw ARGB while the retained producer stores
physical Z24S8. Those bits supply A from the high depth byte, R from the middle
depth byte, G from the low depth byte and B from stencil. The native donor's
current `R24_UNORM_X8_TYPELESS` view instead supplies depth R, zero G/B and
one A, as specified by
[D3D11.3 section 19.1.3.3](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#DefaultsForMissingComponents).
Substituting that documented typed-view equation changes **31,913** of the
76,800 expected blur-mask alpha pixels. This is an independent counterfactual,
**not** a native GPU measurement or complete-HLE equivalence pass. The next
step is to verify and correct the native view while keeping conversion and
filtering on the GPU with cached resources; routine per-draw CPU downloads are
not required by this consumer contract.

Complete-scene pixels, native equivalence, earlier producers, enabled noise,
original startup/lifetime and exact hardware behavior remain open. This batch
used one Idle CPU logical processor; no title/audio/host GPU/Xbox run occurred.
The full goal stays active at **4/9**, with the existing hardware inspection
gate intact. Standing owner authorization covers read-only/benign checks;
dangerous operations still require explicit approval.

## Default HLE depth storage sampled as ARGB - 2026-10-10

The independently qualified blur-mask consumer exposes a real native mismatch:
its allocation remains `LIN_D24S8` (`2E`), while its live sampling header is
`LIN_A8R8G8B8` (`12`). The native adapter previously selected its allocation's
depth SRV and the donor's automatic shadow classification. Both have to follow
the live sampling interpretation. This correction works by default and preserves
the original allocation, depth target, and resource ownership.

The guest adapter reads the live header on binding and each draw, including
header changes without a rebind. The prepared donor uses that interpretation
for its shadow shader key and texture constants. An authored compute shader
loads unfiltered depth and stencil, recovers the original D24 integer, and
produces the equivalent RGBA8 image. Ordinary native sampling then applies
the requested nearest or bilinear filter. A per-resource content serial
invalidates the conversion after CPU upload, depth/stencil clear, or a target
draw; read/completion serials do not invalidate it. Multiple stages and repeated
draws share the cached image and shader. Ordinary sampling adds no CPU download
or upload. Only single-level, single-sample owned D24S8 allocations reinterpreted
as ARGB are admitted; other reinterpretations retain their guard.

The integer inverse uses the IEEE-754 exponent/mantissa of a nearest-rounded
D24 UNORM load, avoiding a shader multiplication's rounding ambiguity.
All **16,777,216** integer inputs pass an exhaustive CPU check of the declared
[D3D11.3 UNORM conversion contract](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
This check establishes that inverse under the stated conversion contract,
not undocumented NV2A silicon arithmetic.

The actual GPU fixture checks authored texel bytes, boundary depth values,
all **256 stencil codes**, nine rational bilinear samples over 28-by-28 pixel
interiors, and changes through CPU upload, stencil clear, and depth/stencil
target drawing. An actual negative control observes the donor's unchanged
depth/shadow interpretation; the ARGB interpretation instead returns the
independently authored bytes. Two stages share the same view, identical uploads
and repeated reads cause no reconversion, and a reused ownership slot gets
fresh storage. Each of three device lifetimes observes **2 allocations /
5 conversions /1 shader compilation**. A separate guest ABI fixture verifies
live header mutation without rebind, unchanged allocation/native ownership,
binding retention, and full device/guest-memory cleanup over three lifetimes.

The six focused graphics/shader CTests pass. After the final stencil-coverage
adjustment, both new focused GPU/ABI tests pass again. All 39 prepared donor
files reproduce exactly from the final preparation tool. The shipping private
title was rebuilt with frame auditing disabled. Actual title regression
evidence is recorded with the final private qualification report.

That executable (`5f117fa7866ed6cb13d742653fe2dc77bcd37c97d8ff8a4e564ad70498306139`)
then runs for **330.360s /33 captures** through startup, title, Demo, subsequent
movie, title return, and Demo reentry, with no stop/audio-stop/fault. All five
scheduled controller states are delivered. The second Start occurs during the
movie, so this is not a fresh Demo-interruption qualification. Earlier narrow
replays remain preserved as incomplete: startup skipped an expired first pulse
in the one-core run, and a loading interval skipped the second one-second pulse
in the first Normal-priority run. Wider pulses resolve delivery in the final
bounded replay. The title is muted/hidden, with frame auditing off, using four
shared logical processors at Normal priority; this is a correctness regression
and does not establish an HLE performance benchmark.

The earlier broader graphics ABI failure is resolved by the later test-fixture
corrections below: exact process-handle and byte checks now pass. Investigation
of the original full driver-startup handle delta remains separate. An earlier
complete backend run timed out during
teardown; subsequent full and focused three-lifetime checks terminate and pass.
No reason for that earlier timeout has been established.

This qualifies the authored native view/filter/cache/ownership component.
Production HLE postprocess pixels against the retained independent equations,
complete scene producers/composition, enabled noise, original startup/lifetime,
and exact hardware behavior remain open. The family count stays **4/9**.

## Independent scene producer prefix - 2026-10-10

The first **13 original producer draws** in the complete retained frame now
execute continuously against input-derived shader/state and pixel equations.
The closed profile admits opaque filled strips, unclipped surviving faces,
repeat/trilinear BC1, RGB diffuse in `[0,0.5]`, saturated alpha, density-zero fog,
zero specular, LESS depth, and RGB/depth writes. Target alpha and stencil stay
preserved. Positive-W fully culled faces may extend outside the XY viewport.
Original line width remains exactly state-qualified; only that unconsumed word
is normalized for the earlier authored filled-triangle pixel equation fixture.

The expected chain starts with supplied root inputs and independent captured
clear equations. Every draw receives expected predecessor bytes; observed
intermediates never become goldens. All five owned roots are compared after
each draw: **11,267 written fragments /16,972,800 target-pixel comparisons**,
with at most one UNORM8 code for written RGB, exact depth/stencil, and exact
unwritten color bytes. Four original target/clear operations also match.
All **10 authored tests and 21 artifact controls** pass, including altered
depth, stencil, masked alpha, untouched pixels, missing roots, and combined
shader/state corruption.

Private evidence is `producer-prefix13-qualified.aot-private.json`
(SHA-256 `d36116586894e2bfd07fc61b299f4b4124ea04a712d286c77b1078e782a4cee6`)
and `producer-prefix13-controls.aot-private.json`
(`703df7917d5c9756942b00cd8b2089acbaf172ae6475ea0073269e1ab10dbde6`)
under the complete-input directory. The reference DLL and source frame hashes
remain unchanged. The authored tools are `native_scene_producer_reference.py`,
`qualify_native_scene_producer.py`, and `native_scene_producer_controls.py`.

Draw **14** is the next exact domain boundary: surviving geometry crosses the
right viewport edge. Earlier equations without a clipping guard produced
differences; that establishes an oracle scope gap rather than an LLE bug.
The new contract guards that domain before applying its closed equations.
Complete-scene/native/enabled-noise/silicon/startup/lifetime qualification
remains false. This CPU-only component is tracked in `.117` and keeps **4/9**.
The owner authorized two workers alongside the primary agent; all share
LP12-15 at Normal priority, lowering priority when a game is running.

## Independent viewport-clipped producer prefix - 2026-10-10

The independent expected-image chain now covers original draws **1-28**,
including draw14's XY viewport boundary. Input-derived homogeneous plane
intersections, perspective division, viewport reconstruction, and fan
decomposition supply expected geometry. The actual submission still executes
each whole original draw unchanged; observed vertices and after-images never
supply expected clip vertices or predecessor images.

The geometric equations follow [D3D11.3 sections 15.4-15.6](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
The numeric graph is explicitly declared from the exact retained DLL's
disassembly before pixel comparison: binary32 reconstruction/division,
rounded differences followed by fused multiply/add for intersections, and
four-bit XY snapping with half ties away from zero. Four-bit snapping is this
software contract; D3D11's eight-bit native raster precision and NV2A silicon
precision are separate. Positive W, in-range Z, and intersections safely beyond
the software clip early-out tolerance are admitted. Near-W/depth-plane and
near-tolerance clipping remain guarded.

All **28 original draws /16 clipped source triangles /117 expanded triangles**
pass, with **27,655 written fragments /36,556,800 target-pixel comparisons**.
Written RGB stays within one UNORM8 code; every depth/stencil byte, masked
alpha byte, untouched color byte, and all five owned roots remain exact.
All four original target/clear operations and the complete original shader/
state components pass. Nine authored clipping tests pass alongside the ten
producer tests; all **21 artifact controls** pass through the public CLI.

Private evidence is `viewport-prefix28-public-qualified.aot-private.json`
(SHA-256 `f899361238a3dabfe580d8a6a40e1961f7e2b98db509d18079e0c152e5a0037c`)
and `viewport-prefix28-public-controls.aot-private.json`
(`c0143e1cc79af0fd21005d79b648a657dbd25e7726f48ab6b10681df46fed959`)
under the complete-input directory. The source frame and reference DLL hashes
are unchanged. The public runner now accepts an explicit expectation/scope;
`native_scene_viewport_reference.py` and `qualify_native_scene_viewport.py`
extend its admitted equation domain without changing guest submission.

Draw **29** is the next guarded boundary: one diffuse color exceeds the earlier
`[0,0.5]` profile. Deriving its consumed combiner and decoder error bound is next.
This CPU-only component changes no shipping HLE/core code and requires no new
title/audio/GPU/Xbox run. Complete scene pixels, production HLE equivalence,
enabled noise, original startup/lifetime, and exact silicon remain open; **4/9**
families are qualified.

## Independent diffuse/clamped producer prefix - 2026-10-10

The original expected-image chain now passes through **480 whole draws**.
The intermediate 61-draw proof exhausts encoded five/six-bit channel pairs
before observing pixels. MSB-expanded endpoints, nearest thirds and floored
halves bound the software BC1 error by 35/31 and 23/21 codes. Diffuse at most
3/4 leaves at most 105/124 code before quantization; convex spatial/mip
filtering preserves that bound. All eight authored diffuse tests pass.

The 65-draw extension admits full clamped diffuse using an exact rational
precision proof for each supplied texture's selected palette entries.
Every consumed channel must independently prove error below one code before
execution. Primary colors clamp before attribute clipping, following the
declared [ARB vertex-program pipeline, section 2.14.4.4](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_program.txt).
An agreeing software result does not establish NV2A silicon ordering.
Nine authored clamp tests reject incorrect clamp order, insufficient precision,
transparent/incomplete storage and unsupported primary/alpha inputs.

The same frozen equations admit the 256- and 480-draw extensions. The final
480-draw run compares **126,422 independently expected written fragments /
626,688,000 owned pixels**, including 387 clipped source faces. Written RGB
differs by at most one code; depth, stencil, destination alpha and every
untouched owned byte remain exact. Complete original shader/state components
and all **21 artifact controls** pass. Observed predecessors never supply the
expected chain.

Three drained fixture pushbuffer jumps preserve independently expected
storage. They recycle the test allocation explicitly; **original ring rollover
is unqualified**. The runner checks all owned storage at each recycle and
records that limitation. Draw **481** is the first unmodelled input: all twelve
vertices have negative W and window Z outside D24 range. The run stops before
that original draw; a behind-camera rejection proof is the next software
oracle domain, requiring no new hardware assumption.

Private final evidence is `clamp-prefix480-qualified.aot-private.json`
(SHA-256 `daa7c1fa395d9acaf63894f26cdffb4c235db36f5435a20860ea75f0cc57d6ce`)
and `clamp-prefix480-controls.aot-private.json`
(`4bd2455e16e4277f9b0884cad9cdd6156264de162f83f8fb505380264c467e87`).
The public CLI is `tools/qualify_native_scene_clamp.py` with the preceding
fixture paths, `--last-draw 480 --depth-arithmetic fma-b-first
--clip-arithmetic fma`. This CPU-only milestone changes no shipping renderer.
Complete scene/HLE pixels, enabled noise, startup/lifetime and exact silicon
remain open; the service-family count stays **4/9**.

## Native window-coordinate and lifetime corrections - 2026-10-10

Original post-transform software vertices truncate window X/Y to four
fractional bits before rasterization. Production native FVF and programmable
paths previously retained a 1/32-pixel offset in the retained consumers.
The default FVF GPU shader now rounds source XY plus screen bias before
truncation; the programmable raster wrapper truncates its final window XY
before viewport mapping. Raw programmable outputs remain intact. Both paths
keep conversion on the GPU, with no CPU vertex rewrite, sampling readback,
new draw resource or opt-in setting.

Authored FVF pixel cases cover signed coordinates, bias/grid boundaries and
RHW across three device lifetimes. Negative clipped cases qualify coverage
using constant texel coordinates; they do not qualify high-frequency clipped
filter interpolation. A separate programmable stream-output fixture checks
32 signed-grid/W cases per lifetime, with exact raster positions, raw outputs
and texture coordinates. Five interval controls derive the host shader bounds
before reading observations, retaining both outcomes when an arithmetic
interval crosses a truncation boundary.

The retained-input native observer runs all five original postprocess
consumers over three lifetimes. Only source streams/programs/states and supplied
producer roots enter the GPU; expected after-images are never supplied.
The last three draws retain actual GPU predecessors while the independent
oracle chains only expected bytes. Native payload bytes must match the
independently re-encoded original inputs, and both raw and production raster
shader observations must pass input-derived arithmetic intervals.

The two feedback consumers and base-copy destination pass their existing
one-code pixel bounds. The blur mask improves from 13,279 differing channels
to **one alpha channel** at `(x=0,y=137)`. The minimum independent MSB selector
margin over the whole mask is `7.689350250084104e-6`; the disputed pixel itself
is below the threshold by `7/130050` (about `5.382545175e-5`). All mask RGB
channels stay within one
code. The final blend improves from 8,706 to **40 channels outside one code**.
An explicitly unqualified diagnostic inverts only that expected mask alpha;
39 of the final failures disappear, leaving one RGB channel differing by two
codes. This establishes a useful cause to investigate, not permission to
change the oracle or tolerance. Exact near-threshold filtering/selector and
remaining blend precision stay open. Depth/stencil/untouched bytes, retained
root continuity and repeat-lifetime bytes are exact.

The broad ABI handle baseline now starts after a no-op host worker joins and
closes. That worker exercises driver thread attachment without executing the
display timer path; the original exact process-handle assertions still cover
every timer/waiter lifetime. Driver initialization accounts for part of the
previous count increase; the entire original delta is not attributed.
The broad texture fixture's format `0x07` is **SWIZZLED_X8R8G8B8**. Its prepared
native BGRA alpha must be 255 while every guest byte stays unchanged; the old
raw-X comparison was stale after the published XRGB correction. Exact expected
bytes replace that assertion without a mask or tolerance. A focused fixture
checks ARGB controls and both rectangular four-level XRGB mip chains, dirty
uploads, sampled alpha, surface aliases and parent release over three lives.

All **11 focused CTests** pass across the final nine-test batch and the shader
failure/scheduler pair: broad ABI, backend, depth view/ABI, XRGB ABI, FVF/window
snap, raw shader, clip/depth and scheduler contracts. Seven new Python interval/
payload-binding tests pass. Private evidence lives under
`postprocess-native-20261010`, including the intentionally failing
`qualification-window-snap.aot-private.json` and
`selector-causal-diagnostic.aot-private.json`.

The rebuilt shipping private executable
(`963d708cd3e20e82941a8a9d1df1d4f7100293ed6436b3ebd6d8d9a3f2b2d7a8`)
completes a **330.188s /33-capture** regression through startup, title, Demo,
subsequent movie, title return and Demo reentry. Both stage-start and opening
movie file sequences occur twice, and all five replay rows are delivered.
No stop/audio-stop/backend-stop/fault occurs; the planned harness kill ends
the bounded run. It is muted/hidden, with frame auditing off and four shared
logical processors at Normal priority. The second Start again falls during
movie playback, so this adds no new Demo-interruption or performance claim.
All 39 prepared donor source files reproduce exactly. Full graphics stays open.

## Independent whole-primitive behind-camera rejection - 2026-10-10

The original producer chain now passes **481 whole draws /627,993,600 owned
pixels**, retaining 126,422 independently expected written fragments. Draw481's
ten primitives have strictly negative W at every vertex. Affine convex W
cannot reach the positive clip volume; each primitive is empty before raster
setup, regardless of its window XY/depth values. The exact retained software
DLL hash and ten PE code/data sites independently bind its first `W-epsilon`
plane, no-transition/no-append path and empty-polygon return before comparison.
The threshold is the declared binary32 `1e-6` software value, not a claim about
NV2A silicon clipping.

All original shader/state and owned-storage checks pass. Written RGB remains
within one code; depth/stencil/alpha/untouched bytes are exact. Seven authored
rejection controls and 21 retained artifact controls pass. Existing XY/clamp
equations are unchanged. Private evidence is
`rejection-prefix481-qualified.aot-private.json`
(SHA-256 `3f65db9b5bedae6d3d54841d6b17bb43789dfbf03bd7b63b5061307cb4c6f26b`)
and `rejection-prefix481-controls.aot-private.json`
(`5bf0565701a48b20d9997febfa09c23380b4ec1e17675fd91d1b913b21539183`).
Use `tools/qualify_native_scene_rejection.py` with the preceding fixture paths,
`--last-draw 481 --depth-arithmetic fma-b-first --clip-arithmetic fma`.

At that milestone, draw **482** was the next guarded domain: three whole negative-W primitives and
eight mixed-W primitives. A strip containing any positive W cannot inherit the
whole-strip rejection proof. Mixed-W and surviving depth-plane clipping remain
unqualified, alongside complete scene/HLE pixels, enabled noise, original
startup/lifetime, ring rollover and exact silicon. No new title/audio/GPU/Xbox
run is needed for this CPU-only extension; family count stays **4/9**.

## Independent empty mixed-W clipping - 2026-10-10

The original producer prefix now passes **512 whole draws /668,467,200 owned
pixels**, with 126,422 independently expected written fragments. Nine mixed-W
draws independently derive 61 empty whole primitives and 64 homogeneous
intersections. Nineteen polygons empty at `W-epsilon`; the other 42 empty at
`X+W`. Nineteen whole negative-W draws separately reject 130 primitives. Mixed
W alone never establishes emptiness: every admitted whole primitive completes
the input-derived clip equations before original execution and comparison.

The exact retained DLL and 22 code/constant sites bind the clip order
`W-epsilon, X+W, W-X, Y+W, W-Y`. Distances, differences and division round to
binary32; intersections use the declared FMA graph. Homogeneous Z is preserved.
The software projects before recursive clip classification and snaps only at
the raster leaf. It has no geometric Z clipping planes; fragment depth range
is a separate contract, not an NV2A silicon claim. The empty-only extension
explicitly refuses surviving mixed geometry and near/zero-W original inputs.

Nine authored controls pass, including a surviving mixed triangle and an
independent one-ulp FMA/double-rounding fault. All 21 retained pixel/state
controls pass. Original shader/state/draw execution uses independent expected
predecessors. Written RGB stays within one code; depth/stencil/alpha/untouched
bytes stay exact. Four drained fixture pushbuffer jumps are recorded without
qualifying original ring rollover. Existing positive-W/clamp and negative-W
equations are unchanged.

Private evidence is `mixed-prefix512-qualified.aot-private.json` (SHA-256
`49dfa6e959b360114ff14e14c790549ffae76e1ecc83cbc9a95185a6f73dae86`),
`mixed-prefix512-controls.aot-private.json`
(`157c57115cc22d65acdf60c18b85c32b37fc71a0e60a3df2a75deb87842dab29`),
and `mixed-prefix512-handoff.aot-private.json` under the complete-frame capture
directory. Run `tools/qualify_native_scene_mixed_clip.py` with the preceding
fixture paths, `--last-draw 512 --depth-arithmetic fma-b-first
--clip-arithmetic fma`. The original replay ends at 512. Surviving mixed-W
pixels, fragment depth boundaries, full scene/HLE pixels, enabled noise,
original startup/lifetime and exact silicon remain open; family count is **4/9**.

## Native cached-format discriminator - 2026-10-10

The original native payload reconstructs byte for byte from the recorded
composition and supplied producer roots (SHA-256
`a90bef4eb4a4239e586cb5e8c7b8b911bdbc1d60d962015f471a688374dd628a`).
A bounded diagnostic changes only the cached depth-as-ARGB image format from
`R8G8B8A8_UNORM` to `R32G32B32A32_FLOAT`. The normalized compute output,
original pixel shader, source bytes, sampler state and oracle remain unchanged.
All five original consumers execute over three device lifetimes. The mask and
retained base-copy now pass the existing bounds; the final blend has one RGB
channel outside one code, differing by two. Depth/stencil/untouched roots,
predecessor continuity, shader intervals and lifetime bytes remain qualified.
The default source is restored exactly and rebuilt afterward.

This establishes native cached-format dependence for the disputed mask, not
the precise UNORM arithmetic mechanism or real NV2A filtering. A source-only
UNORM16 nearest-even hypothesis flips that single mask branch; basic binary32
variants stay below the threshold. The remaining final-blend error stays a
failure. No expected mask bit is inverted and no bound is widened.

The FLOAT cache retains GPU conversion and content-serial caching, but a
640x480 image grows from 1,228,800 to 4,915,200 bytes. Filtering and bandwidth
costs need measurement before adopting a default correction. Observation wall
time includes device creation/readback and is not a performance benchmark.
Private evidence is `postprocess-native-20261010/float-cache-diagnostic`, with
original/diagnostic/restored source and executable hashes, exact commands,
observations and the intentionally failing `qualification-diagnostic` report.
No title or Xbox run is part of this diagnostic.

## Independent empty positive-W depth-domain clipping - 2026-10-10

The original producer prefix now passes **768 whole draws /1,002,700,800 owned
pixels**, with 126,422 independently expected written fragments. Positive-W
draws 522 and 669 contain out-of-range original vertex depth, but their ten
whole primitives independently derive empty W/XY clipping results. The proof
precedes any bypass of fragment depth arithmetic. Negative vertex depth alone
never establishes rejection; surviving depth/mixed-W pixels and original
near/zero W remain guarded. The cumulative chain also derives 187 empty
mixed-W and 753 whole-negative-W primitives with unchanged published equations.

Six new authored depth/false-emptiness controls and all 21 retained pixel/state
controls pass. Every original whole shader/state/draw executes with independent
expected predecessors. Written RGB stays within one code;
depth/stencil/alpha/untouched bytes stay exact. Six drained fixture pushbuffer
jumps are counted without qualifying original ring rollover. The new wrapper
explicitly selects the reviewed 768 limit; shared/default/public producer
admission remains capped at 512. No later original execution is claimed.

Private evidence is `empty-depth-prefix768-qualified.aot-private.json`
(SHA-256 `be7474c9d1a8662ac27fcc06e512d6ed07dde7496de825625f1a2c14d7153a99`),
`empty-depth-prefix768-controls.aot-private.json`
(`16308eb69adf3a77e720f7468a6afbf4dad7f0423240121d378f0798237d98b0`)
and `empty-depth-prefix768-handoff.aot-private.json` under the complete-frame
capture directory. Run `tools/qualify_native_scene_empty_depth.py` with the
preceding fixture paths, `--last-draw 768 --depth-arithmetic fma-b-first
--clip-arithmetic fma`. Full scene/HLE pixels, enabled noise, original
startup/lifetime and exact silicon remain open. Family count stays **4/9**.

## Independent producer continuation through 1,536 draws - 2026-10-10

The independently expected producer chain now qualifies **1,536 unchanged
original whole draws /2,005,401,600 owned-pixel comparisons**, with 199,934
expected written fragments. An input-only preflight admitted draws 769-1536
using the existing equations before original execution. This continuation
adds 73,512 expected writes across 148 draws; every final expected root hash
matches the preflight. Written RGB stays within one code, with exact depth,
stencil, alpha and untouched bytes.

Cumulative empty domains cover 447 whole-negative-W draws /1,477 primitives,
39 mixed-W draws /203 primitives, and three positive-W draws with out-of-range
vertex depth /16 primitives. Mixed W and negative vertex depth alone never
establish rejection. Each empty primitive completes the input-derived clip
proof; surviving mixed-W/depth boundaries and near/zero W remain guarded.

All 21 retained artifact controls, ten continuation-write fault controls and
14 default/cap/delegation assertions pass. New fault locations come solely
from differences between the independently expected 768- and 1536-draw roots;
observed images are used only for comparison and deliberately damaged copies.
The controls reject missing new color/depth writes, restored predecessor
roots, wrong depth/stencil/alpha and a two-code RGB error while accepting the
declared one-code RGB bound. Twelve drained fixture pushbuffer jumps preserve
expected storage; original ring rollover remains unqualified.

The new `tools/qualify_native_scene_empty_depth_continuation.py` explicitly
selects the reviewed 1536 limit. Shared/default/public producer admission
remains 512, and the existing empty-depth CLI remains 768. Use the preceding
fixture paths with `--last-draw 1536 --depth-arithmetic fma-b-first
--clip-arithmetic fma`. Private evidence under the complete-frame capture
directory is `empty-depth-prefix1536-qualified.aot-private.json` (SHA-256
`aa145155a6cf59e3be3a328be046318a0d871c775a1348faa3a50ba6a6cc2663`),
`empty-depth-prefix1536-controls.aot-private.json`
(`36615f16d658ae42709eeb0da7bfe46ae9f2abcd21773e2db41f53a83fb39937`),
`empty-depth-prefix1536-continuation-controls.aot-private.json`
(`4f4786f90782170a34fa91c796150b459290ab4e08ab5be3570a9140a1b577db`),
and `empty-depth-prefix1536-handoff.aot-private.json`. No later original draw
was inspected or executed for this milestone. Complete scene/HLE pixels,
enabled noise, startup/lifetime and exact silicon remain open; **4/9** stands.

## Default FLOAT depth-as-ARGB cache correction - 2026-10-10

The default HLE cache now uses `R32G32B32A32_FLOAT`. Its normalized compute
output, ordinary GPU sampler, pixel program and content-serial invalidation
are unchanged. Repeated reads reuse the converted view; no routine CPU
readback, upload or shader recompilation is added. Device support is checked
once per allocation and unsupported devices stop explicitly.

An independently authored five-stage bilinear/MSB selector supplies a
meaningful regression: the previous UNORM cache chooses the wrong branch at
`32509/65025`, below one-half by `7/130050`. The FLOAT cache passes that case
and both threshold neighbors across three device lifetimes. The existing
depth recovery, all-256-stencil, serial-invalidation and resource-reuse cases
also pass. All **12 focused CTests**, including broad graphics ABI/lifetime,
scheduler and FVF/programmable coordinate checks, pass.

A private GPU timestamp A/B/B/A comparison exercises the original mask
program with cached reads and explicitly forced reconversion. Median cached
draw times are 0.001275/0.001285 ms for UNORM and 0.002704/0.002699 ms for
FLOAT; conversion-plus-draw medians are 0.024544/0.033024 ms and
0.040352/0.044640 ms respectively. These bounded measurements include possible
CPU submission gaps and are not a title performance benchmark. A 640x480
cache grows from 1,228,800 to 4,915,200 bytes, adding 3,686,400 bytes. The
default correction keeps conversion and filtering on the GPU.

The new default title runs **330.187 seconds /33 captures** through title,
Demo, movie, title return and Demo reentry without stop/audio-stop/fault or
backend-stop guards. All scheduled controller states arrive; the second Start
occurs during a movie, so no new Demo-interruption qualification is claimed.
The run is muted and hidden, and is not a performance benchmark or a new
human complete-pixel approval.

The original five native consumers run across three device lifetimes. Mask,
base-copy and both feedback draws pass the existing checks. One final-blend
RGB channel differs by two codes against the then-current prequantized-source
oracle; the strict one-code gate remains failed at that milestone. An isolated unchanged final
consumer over independently authored gray roots also exposes this rounding
order question. Exact retained LLE source/DLL route review must decide the
oracle premise before any correction; neither expected outputs nor bounds
are changed to match observations. Depth/stencil/untouched bytes, predecessor
continuity, shader intervals and lifetime bytes still pass.

Private evidence is under `postprocess-native-20261010/float-cache-default`,
`cache-perf-diagnostic` and `isolated-final`, plus
`float-cache-robust-regression-20261010`. Earlier failed UNORM and diagnostic
reports are retained. Complete scene/native-chain, enabled noise, original
startup/lifetime and exact silicon remain open; **4/9** stands.

## Source-derived postprocess blend-order correction - 2026-10-10

The independent oracle incorrectly packed the final combiner source to
UNORM8 before blending. The retained DLL's selected software route clamps
floating RGB/alpha, blends them, then packs only the output. Its earlier
packed alpha is solely an alpha-test operand. Original draw 4302 uses
primitive 6 and combiner control `0x11104`, excluding immediate QUADS and
the witnessed profile fingerprints; the miss calls the generic software
rasterizer. Source `src/machine/nv2a.cpp:15503-15620`, its host GLSL counterpart,
the exact retained DLL hash and 15 compiled code/constant sites establish
this order before comparison. This is a maintained LLE contract, not exact
NV2A silicon precision.

`tools/native_postprocess_pixel_reference.py` now passes clamped floating
source values into the unchanged blend equation, with one final quantization.
All **21 authored tests** pass. Two exact Fraction fixtures distinguish
preblend source packing: bilinear RGB/alpha over black yields `[0,0,0,207]`
instead of the old `[1,1,1,207]`; fractional alpha over gray yields
`[21,21,21,243]` instead of old alpha 244. Their expectations come from
authored input fractions, not native measurements. The one-code comparator
and all admission guards are unchanged; no production shader is modified.

Fresh original LLE execution passes all five postprocess consumers and
**1,305,600 pixels /5,222,400 channels**, with exact depth/stencil and untouched
roots. The native five-consumer replay over three device lifetimes passes
both feedback draws, the mask and the base-copy. The previous residual at
(295,257) is resolved, but the corrected chain exposes one different two-code
blue residual at (342,322): expected 7, observed 5. The strict chain gate
remains failed. Retained predecessors, raw/raster shader intervals and
device-lifetime bytes pass.

A fresh isolated unchanged original final consumer with the fixed supplied
producer and independently authored gray destinations 16/0/255 passes the
same one-code bound across all three device lifetimes: **921,600 component
pixels**, exact untouched/depth/stencil/before bytes and zero lifetime drift.
This supports the local consumer contract; it does not qualify arbitrary
predecessor intervals or the complete native chain. Propagating independently
allowed predecessor errors is the next source-derived audit, with the current
strict failure retained until its premises are validated.

Private proof is `postprocess-native-20261010/blend-order-proof.aot-private.json`
(SHA-256 `7aa6e382fc98abaf93a0b670f24e312abea0eed724fed11161ab49776d397afb`).
Fresh source/expected/DLL/executable bindings are in `blend-order-corrected`;
the intentionally failed native report has SHA-256
`059684f0d3f555764b74da97aeeaae4f97dffe1a697f008dea602384ce810582`.
`isolated-final-source-order/qualified.aot-private.json` passes
(`72e5f4f4eb7f38c2953ea8f6185c0dfab85b014b101a12e34ec4558423a8eed3`).
Historical wrong-premise and failed native reports remain intact. Earlier
scene producers are supplied inputs to these consumer components; complete
scene/HLE, enabled noise, startup/lifetime, ring rollover and exact silicon
remain open. Family count stays **4/9**.

## Native fixed-format blend precision discriminator - 2026-10-10

The two authored Fraction cases now run as a private direct-D3D diagnostic:
two source formats, two blended target formats and an unblended FLOAT32
readback, repeated across three fresh devices (**36 tiny draws**). Constant
quarter-grid UVs remove varying-coordinate and snapping ambiguity. The same
passthrough pixel bytecode, texture inputs and production-matched RGB/alpha
SRC_ALPHA/INV_SRC_ALPHA ADD factors are used throughout.

Both UNORM8 and FLOAT32 source textures give `[1,1,1,207]` and `[21,21,21,244]`
on the UNORM8 target, matching the independently prepared prepacked-source
counterfactual rather than the maintained LLE float-blend equation. FLOAT32
targets satisfy the rational floating controls; FLOAT32 raw source error is
at most 2.97e-8 and UNORM raw source error is at most 3.82e-6. The predeclared
floating guard cannot cross either decisive output-code boundary. All pixels
are uniform and lifetime byte drift is zero. This isolates a fixed-target
output-merger precision effect in these authored cases; it does not establish
the exact algorithm for every GPU or the NV2A silicon rule.

The hardware diagnostic finishes in 0.484 seconds, without a window, audio,
title or Xbox operation. Production executables and shader generation remain
unchanged. Private evidence is
`postprocess-native-20261010/quarter-grid-blend-diagnostic/diagnostic.aot-private.json`
(SHA-256 `bad2c3200eeff13c84081efe28187f07ffa43ebec459fce7d087edbb29b9dfce`).
An explicit floating blend in the shader, reading a GPU destination snapshot
and retaining the UNORM target/storage, is the next bounded original-consumer
diagnostic. Its copies, allocations, cache behavior and GPU cost must be
validated before a default production correction. The canonical oracle and
strict native-chain failure remain unchanged; **4/9** stands.

## Original scene producer through draw 1943 - 2026-10-10

The independent expected-image chain now passes **1,943 original draws**,
**227,679 expected writes /2,536,780,800 owned-pixel comparisons**. The newly
admitted continuation contributes 27,745 expected writes. Written RGB keeps
the existing one-code bound; depth, stencil, alpha and untouched bytes stay
exact. All five final expected roots match hashes frozen before execution.
Observed predecessor or after images never supply expected pixels.

Original input 1943 exercises a narrowly admitted surviving mixed-W strip.
Its eight source triangles independently clip to seven empty results and one
surviving triangle, which writes all **160** expected pixels with no depth
rejections. The homogeneous W/XY clipping and fan sequence are source-derived;
40 exact retained DLL code/constant sites bind clipping, snapping, culling,
coverage and depth to the maintained LLE route before observation. Its
surviving leaf has positive W, integer in-range vertex depth and the already
admitted color/texture domain. Other original mixed survivors, near-zero W,
signed or depth-boundary leaves remain guarded. This is a qualified captured
component, not general mixed-W pixel or exact silicon qualification.

All **14 authored tests** and **35 replay controls** pass. The new controls
independently identify all 160 pixels as changing both color and depth from
the draw-1942 expected roots. Restoring old color, old depth or both at
(613,474) is rejected. Retained controls continue to reject wrong state,
shader delivery, alpha/stencil/untouched changes, missing roots and a two-code
RGB error. Fifteen drained fixture pushbuffer jumps preserve expected storage;
original ring rollover remains unqualified.

`tools/qualify_native_scene_surviving_mixed.py` requires explicit original
`--last-draw 1943`, the reviewed capture/scene hashes and the source/DLL-bound
`--depth-arithmetic fma-b-first --clip-arithmetic fma` declarations. The
shared runner admits only reviewed limits 512/768/1536/1943. Its default stays
512 and the existing empty-depth CLI default stays 768; this new component
cannot admit a later or differently hashed original draw.

Private evidence in `frame-inputs-firstworld-20261010` is
`surviving-mixed-prefix1943-qualified.aot-private.json` (SHA-256
`5ed2038b267ad81bd569ed6c5e49607da33f7315a9460765a74c22024662ee00`),
`surviving-mixed-prefix1943-replay-handoff.aot-private.json`
(`35782527d038ff9b907a52cce560505a9b121ce70c942e651ad41dcffecb4ee0`),
and its retained, continuation and final-leaf control reports. No later draw
was inspected or replayed for this milestone. The next input-only continuation
starts at 1944. Complete scene/HLE pixels, enabled noise, original
startup/lifetime, ring rollover and exact silicon remain open; **4/9** stands.

## Private explicit float blend correctness and cost - 2026-10-10

The isolated shader variant passes all **five original postprocess consumers**
under the unchanged strict one-code comparator. Depth/stencil/untouched roots,
predecessor continuity, raw/raster shader intervals and all three device
lifetimes remain qualified, with zero byte drift. It also passes **60 authored
GPU draws**: two exact Fraction blend fixtures with both source formats and
four final-pack boundary fixtures across three lifetimes. These produce the
canonical `[0,0,0,207]` and `[21,21,21,243]` outputs, including their alpha
packing, rather than selecting an alternate expected equation.

All 139 source/proof/expected files were hash-verified before the private
build and observation. Original payload/state/vertices/program/texture inputs
are byte-exact. The actual current combiner's original generated prefix is
preserved and independently hashed; the separate cached variant adds only a
GPU destination load, floating source-alpha ADD blending and final output
packing. It disables native blending during that original draw, clears t4
and restores the exact OM state/factors/sample mask. No source prepacking,
extra draw, FLOAT render target or routine framebuffer CPU transfer is added.

Cold ownership counters record one borrowed snapshot image allocation, one
copy and one variant compile per affected lifetime. The snapshot is 1,228,800
bytes at 640x480 and uses the existing feedback image/SRV teardown. Warm
batches make exactly one GPU copy per affected draw and no snapshot allocation
or shader compilation. These cache counters do not claim to count internal
GPU-driver allocation behavior.

A/B/B/A runs each use three lifetimes and nine batches of 32 final draws.
Canonical after-readbacks precede every timing batch and all 162 retained
observation files stay byte-stable; that is a stability check, with independent
correctness established by the preceding source-bound replay. GPU timestamps
include snapshot plus draw; CPU submission includes the complete draw path.
Median GPU times are **0.003040 /0.016672 /0.016570 /0.003001 ms per draw**;
CPU times are **0.002641 /0.003219 /0.003141 /0.002534 ms**. One early explicit
device median is 0.025792 ms; the remaining explicit device medians are
0.016544-0.016671 ms. The bounded median increment is about 0.014 ms GPU and
0.0006 ms CPU per affected final draw, with possible submission gaps in the
GPU span. This is a component measurement, not a title FPS benchmark.

Private correctness report is
`postprocess-native-20261010/explicit-blend-private/root-correctness-qualified.aot-private.json`
(SHA-256 `f8206d6821695e18df46e7972af090a3ee70314e61a53be7b172453ae389b86b`);
`root-timing.aot-private.json` is
`20e1044a2d378e66727fa881a831e3a93fe7ffc13722d672d77ddd79bec0883f`.
Emitted original/generated HLSL hashes are
`1a3cd938b36a601cd02cf5f530c94a52b2cc5a809539e66f5f676c8730925b03` /
`f74b2871bf7fd0b837b489ee36ca501956f1d34378558e7bd578c3af4ba5e7ce`.

Production sources, active donor and executables remain unchanged; their
previous strict failure is preserved. The private admission is bound to the
retained payload and nonoverlapping rectangle. Default adoption requires a
runtime program/state/geometry proof without a capture-input switch or opt-in,
plus focused tests and one coherent title regression. Overlapping primitives
cannot use a single predraw snapshot. Complete scene/HLE pixels, enabled noise,
original startup/lifetime and exact silicon remain open; **4/9** stands.

## Producer continuation stopped at draw 2031 - 2026-10-10

One original replay requested through draw 2120 stopped at its first mismatch,
draw **2031** (original call 3384). No later draw or retry executed. Rows
1-2030 pass the existing component comparisons, but the complete requested
prefix report and its dependent controls were not produced. Published producer
qualification remains **1943**; preserved per-draw snapshots at 1969 and 1983
are evidence for further review, not a completed prefix qualification.

Draw 2031 has **345 failing red channels**, with differences of 2-10 codes.
Independent coverage is 771 fragments, including 108 LESS-depth rejections
and **663 writes**, matching the original write counter. Depth, stencil,
alpha, untouched pixels and other target roots remain exact. Shader/state
components pass; all skip counters are zero and every original FIFO drains.

Original vertex 6 carries red 1.2049747705 and green 1.0212996006; all nine
source triangles undergo viewport clipping. The declared independent
[ARB vertex program pipeline](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_program.txt)
clamps vertex colors before attribute clipping. The maintained generic LLE
instead interpolates raw colors while constructing clipped vertices, then
clamps their colors at raster setup (`src/machine/nv2a.cpp:14961`). This
source/input distinction is the correction candidate. The reference equation
and one-code bound remain unchanged; a small authored discriminator and
bounded implementation review precede another original replay. The published
pipeline contract does not establish exact NV2A silicon ordering.

Frozen private failure handoff is
`frame-inputs-firstworld-20261010/surviving-mixed-prefix2120-failure2031-handoff.aot-private.json`
(SHA-256 `57edff3a74386abd0d5617afc7b77cd609e1154fea7becc04e436715d546cede`).
Root independently verified all 2031 original observation hashes, the ten
failed expected/actual target buffers and the red-only error counts. Complete
scene pixels and the graphics family remain open; **4/9** stands.

## Generic LLE colors normalized before clipping - 2026-10-10

The maintained generic primitive dispatcher now normalizes consumed diffuse
and specular colors on local original-primitive copies, after selecting flat
provoking colors and before homogeneous attribute clipping. The existing
sanitizer is reused: RGB NaN becomes zero, alpha NaN becomes one, and all
components clamp to [0,1]. The late leaf clamp remains defensive. Raw shader
observations, consumed-attribute gates, positions, textures and fog retain
their prior behavior. Recursive clipped fans receive normalized endpoints.

An independently authored triangle crosses the viewport and creates two fan
leaves. Its input colors give R/alpha=79/128, G=49/128 and B=1/4 at both
interior samples: exact packed **0x9d9d6240**. The old post-clip order gives
**0xd2d22d40**. The preserved old DLL reproduces the latter in eight authored
draws across diffuse/specular and all four viewport sides; a separately linked
new DLL produces the declared pre-clip result with the identical harness and
inputs. Raw out-of-range original shader values remain observable in both.

The permanent vertex-color suite passes **1,844 authored draw cases**, including
**152 new cases** across compiled/interpreted shaders, direct/callback memory,
DMA A/B, all four clip sides, diffuse/specular/alpha, unclipped controls,
nonfinite defaults, unconsumed secondary colors and clipped-away first/last
flat provoking vertices. Depth and outside color bytes are preserved. Three
focused CPU CTests pass; the OpenGL-only test and embedded GL fixtures are
explicitly unavailable in this no-SDL3 build, so no new GPU-collection claim
is made by those tests.

The independent empty mixed-W routine also no longer rejects an unused
palette precision bound after proving zero raster leaves. It still validates
complete encoded texture, finite/profile inputs and owned target storage;
surviving geometry retains the existing one-code precision gate. Four new
controls use an exact **64/63** green-code palette error, reject changed visible
geometry and incomplete texture/target storage, and preserve all independent
roots for the empty case. All **76 scene-reference tests** pass. Clipping and
projection equations are unchanged. This does not qualify original draw 2121.

Private source/test handoff is
`frame-inputs-firstworld-20261010/producer-color-clamp-correction-handoff.aot-private.json`
(SHA-256 `cc46a62a702a0c292d499a8a5e55e6063bc3ef1293daee3806fe9e5f5bc5786a`).
The side DLL in `producer-color-clamp-reference-20261010` has SHA-256
`bf0c9c51224e9fa593d83dc525bc7c588a9af02738bce35f96749df57b357e19`;
the old `9fdec518...` DLL and failed draw-2031 evidence remain unchanged.
New compiled clipping/depth evidence must be reviewed before another original
prefix replay. The reference equation and tolerance are unchanged; producer
qualification remains **1943** and the full family remains partial **4/9**.

## Default native float blending - 2026-10-10

The default draw route now selects GPU float source clamping, source-alpha
blending and final byte packing when the decoded vertex/combiner semantics,
current uploaded constants, rectangle geometry, state and owned allocation
shapes prove the supported case. Selection uses no capture hash, input ID,
shader override or opt-in setting. Ordinary draws retain their existing route.

The proof checks a six-instruction programmable vertex shader, four disjoint
rectangle-strip vertices and the parsed four-tap pixel program. All four taps
refer to the same owned, single-level, single-array, single-sample, noncube
allocation. Supported sources are ordinary linear ARGB8 with an actual
BGRA8/RGBA8 UNORM image, or D24S8 interpreted as ARGB8 through the existing
FLOAT conversion cache. Output aliases, invalid geometry, dirty constants,
unsupported state and enabled noise are rejected. The output-only default
D24S8 allocation can be typed; sampled depth storage stays strictly typeless.
Relative-address checks apply only to context operands because the decoder
leaves that field unspecified for ordinary input registers. Fixed-function
texture-coordinate selectors do not affect the proved programmable shader.

One dedicated cached destination snapshot and one cached shader variant
implement the selected draw. A warmed draw adds one GPU copy, with no new
scratch allocation, variant compilation, CPU framebuffer transfer or extra
draw. Allocation descriptors are checked and retained at resource creation.
The original output-merger object, factors and mask are restored, the snapshot
view is unbound, and ordinary feedback/completion/content lifetime remains
connected. The 640x480 RGBA8 snapshot uses approximately 1.2 MiB. The previous
private A/B/B/A measurements establish the bounded algorithm cost; automatic
admission overhead has not been separately measured, so no title FPS claim
is made.

The default authored GPU test passes across three device lifetimes. Both
ordinary ARGB8 and D24 reinterpretation use independent Fraction-derived
quarter-tap inputs whose final colors distinguish source packing from final
packing. Bound and UP draws, current constants, duplicate/folded geometry,
wrong vertex/pixel semantics, noise/state rejection, allocation aliases,
ordinary feedback interleaves, warm reuse, exact OM restoration, close and
failed/successful reset are checked. Depth bits are preserved exactly; masked
stencil REPLACE is checked independently. Ordinary UNORM sampling retains
its existing precision: these exact authored observations do not establish
a general FLOAT-filter precision bound for UNORM textures.

All **14 focused native CTests** pass. A fresh default replay of the five
original consumers **2767, 2820, 4300, 4301 and 4302** passes the unchanged
strict gate across three lifetimes, with predecessor continuity, raw and
production raster-shader intervals, exact untouched/depth/stencil storage and
no lifetime byte drift. The former two-code blue residual is cleared. Original
inputs and independently supplied producer roots precede the observation;
measured after-images are never expected-image goldens. The failure and
private diagnostic observations remain retained.

Default source freeze is
`postprocess-native-20261010/default-blend-runtime-frozen-v7.aot-private.json`
(SHA-256 `1edbeab27f8cde25187c5af9f836df8b901a96e486ce7594cdba469442a01f4f`).
Strict default qualification report is
`postprocess-native-20261010/default-native-v7/qualified.aot-private.json`
(SHA-256 `f1640b4b601ecb36601474710c7c5b9972d71efe5f711e785b09b7f3fdaa5af7`).
The single combined muted hidden **330.235s** title regression passes: all 33
captures are reviewed, title/Demo/pause-overlay/movie/title-return/reentry and
a subsequent movie are visible, all five controller-state transitions are
delivered, and no stop/fault guard appears. The run ends at its requested
duration; its forced termination is not a natural title-exit qualification.
The measured native title SHA-256 is
`096a8437a729642cbb902b6bfdf961d537a039a3f9c9e6371109f049f88a62d0`.
The same production code also passes added relative-context and unsupported
linear-mip controls. Complete scene pixels,
enabled noise and original resource startup/lifetime remain open; **4/9** stands.

## Corrected producer generation and empty continuation - 2026-10-10

The independently selected compiled evidence now binds the corrected source
and side DLL to **65 code sites**, including the new local-copy, flat-provoking,
consumed-attribute, nonfinite and pre-clip clamp operations. The preserved
old generation reproduces its original **40 sites**; crossed source/DLL pairs
fail closed. Existing clipping, snap, cull, top-left and depth arithmetic
are unchanged. The producer admission dispatch selects this evidence by the
actual DLL digest and verifies the matching source generation before replay.

A positive-W triangle whose complete viewport clipping leaves no raster leaf
now validates every original input/profile and independently owned root
without requiring a precision bound for a texture it cannot sample. Surviving
geometry retains the existing strict one-code gate. Root review passes
**69 authored controls**, including a 64/63 green-code texture error, changed
visible geometry, malformed texture storage, missing/short/inactive roots,
vertex/fog counts, raster state and target roles/shapes. Four existing numeric
functions are AST-identical to the previous version; no expected pixel or
tolerance changes.

One input-only continuation covers draws **2121-2128**: all 37 source
primitives leave zero raster writes and all five independently expected
roots remain unchanged from draw 2120. This prepares a prefix replay through
2128; it does not qualify that original execution. Draw 2129 is visible. Its
conservative sampled set includes 808 texels across all ten mips and possible
address/filter neighbors. The complete source-derived proof binds the
interpolation, filtering, normalization and packing graph, the actual loaded
MSVCRT `_hypot` code and constant, and the AMD x87 accuracy premise. A broad
logarithm bound proves base-mip selection without claiming exact logarithms.
Palette plus arithmetic bounds are below one code in every RGB channel
(approximately 0.74212/0.85733/0.74212). Both root and an independent reviewer
accept this proof; the original draw still needs a corrected-generation replay.

The bounded sampled-precision admission preserves every nominal pixel equation
and the existing tolerance. All five independent predecessor roots and complete
current input fingerprints must match before the draw is admitted. The live
source, input frame, host and loaded CRT checks precede reference creation.
All 24 focused admission/migration controls pass against the installed code.
The retained full-frame vertex/state report keeps its old generation identity;
a narrowly pinned source/PE transition requires fresh original state validation
for every row in the new prefix. It does not transfer the old 4,331-row pass to
the corrected DLL. The default exact-library precondition remains enforced.

The first input-only continuation admits draw 2129 with 582 independent writes,
then stops at draw 2130's global palette guard. All its primitive W values are
strictly negative, so no palette value can be sampled. No original draw was
executed by that continuation. The subsequent empty-only correction and
prepared replay are recorded below. Full scene, noise and startup/lifetime
stay open.

Root reviewed handoffs are
`frame-inputs-firstworld-20261010/producer-color-clamp-generation-root-review.aot-private.json`
and `positive-w-empty-root-review.aot-private.json`. The latter binds 12 source
files and 21 artifacts to the frozen input-only continuation. Published
original producer qualification remains **1943** until the corrected replay
and dependent controls pass. Full graphics remains partial **4/9**.

## Empty-only precision admission and retained replay - 2026-10-10

Complete negative-W rejection and positive-W depth rejection now validate the
full original input, texture, state, counts and every owned root before returning
unchanged independently expected storage. The guard omits only the precision
requirement for sampling that the original geometry proves cannot occur. Visible
geometry keeps the strict one-code bound. A local validation-only geometry proxy
is used after complete original emptiness and finite-attribute checks; it never
provides a raster expectation or an original payload. The clipping/depth proofs
and nominal pixel equations are unchanged. The combined focused suite passes
**79 tests**, including malformed inactive roots, texture/fog/count faults and
visible-geometry counterexamples.

The input-only continuation now admits **2130-2132**, with 14 source primitives,
zero writes and all five independently expected roots unchanged from 2129.
The prepared prefix has **321,921 cumulative expected writes**. Draw 2133 needs
a tighter consumed-sample precision proof; no actual output supplies that proof.

A frozen private session harness retains one original reference, independently
expected predecessors, cumulative written indices, event cursor and qualified
rows across explicitly reviewed batches. Every draw still executes original
code, current-generation shader/state qualification and the unchanged storage
comparison. Root review passes **34 hardware-free controls**, including boundary
bind/clear ordering, a permitted observed one-code difference that must not enter
the expected predecessor, source migration and retirement on faults. The event
body and report equations are AST-identical to the shared runner. The driver
uses blocking terminal input between batches; no idle worker or polling starts.
Real idle CPU/performance and serialized restart are not qualified by these
authored controls.

The first corrected original replay passed rows 1-1144, then stopped before
submitting draw 1145. Its positive-W geometry leaves four empty polygons at
the X+W clipping plane; a validation-only integer-depth restriction rejected
one original fractional depth. The captured pixels did not choose a new
expectation. The reviewed complete input-only preflight now passes all
2,132 draws, with 321,921 expected written fragments and all five independently
frozen point roots unchanged. The new positive-W XY-empty helper first derives
every original strip primitive's complete clip/fan result; only when all leaves
are absent does it use a local XYZ/W proxy for complete input/storage validation.
Visible draws retain their previous precision and raster equations. Its 49
authored controls include fractional depth, intersected empty clipping,
invalid attributes/storage and independently calculated visible pixels; the
combined scene-reference suite passes 87 tests.

A fresh corrected original replay passes through 2132. Exact encoded-input
palette bounds are memoized, with every original draw's input/predecessor/result
hashes and write count checked against the full uncached preflight before
submission. The final write footprint also matches. Selected draw snapshots
pass dependent controls below. Source/input/loaded-CRT guards precede reference
creation. These preparation results alone did not extend the then-published
1943-pixel prefix or transfer the old full-frame state result to the corrected
DLL. Private evidence is
`producer-zero-sample-root-source-review.aot-private.json`,
`producer-zero-sample-final-handoff.aot-private.json` and
`producer-persistent-session-proposal/root-review.aot-private.json`, all under
`frame-inputs-firstworld-20261010`. Full graphics and **4/9** stay open.

## Corrected original producer prefix 2142 - 2026-10-10

The continuous current-generation original prefix now qualifies draws
**1-2142**, with **361,948 independently expected written fragments** and
**2,796,595,200 owned-pixel comparisons**. Every row passes current original
shader/state and unchanged pixel/storage checks. Draw 2031 now passes its 663
writes after the generic pre-clip consumed-color correction. The retained
4,331-draw state baseline keeps its earlier DLL identity; current-generation
state qualification covers this new prefix, not the whole frame.

Draw 2133's seven leaves contribute 18,855 writes and 62,587 LESS-depth
rejections. A separate source-bound packed-byte proof encloses encoded BC1
filtering, actual primary interpolation and floating modulation, then applies
the independently selected finite RGB saturation/FMA/floor/conversion graph.
All possible integer outputs stay within the existing one-code RGB contract
of unchanged nominal pixels. All 22 packing sites, inherited sampler/CRT
premises, 34 authored packing controls and 24 admission controls are reviewed.
The prior strict prequantization proof remains failed at 161 fragments; this
new integer-output proof neither changes the nominal equation nor widens its
comparison. Depth/stencil/alpha/padding/untouched roots remain exact.

The same frozen arithmetic graph separately proves draw 2139's 21,172 writing
fragments across five writing leaves. Two leaves have no writing fragments and
consume no sampled-color precision. Every compact possible-byte set is bound
to the independently classified geometry/depth footprint and unchanged
nominal RGB. The original draw passes; the earlier coarse proof and 136
weighted prequantization failures remain false. Its 24 source-first admission
controls and 30 additional actual-artifact fault controls pass.

The corrected replay reuses one reference for reviewed extensions, including
draws 2134-2138 and 2140-2142 whose independent expectations prove zero writes.
All **117
actual-artifact fault controls** pass: 21 general pixel/state controls, 28
omitted-leaf controls at original draws 1943/1969/1983, 28 at the seven leaves
of draw 2133, 20 at draw 2139's five writing leaves, and 20 continuation
controls. Expectations and selected fault
sites come from independently derived before/after bytes, never observed
after-images. The exact encoded-input palette cache records 28 misses and
2,110 hits through 2132, with every cached draw result matched to the full
uncached input preflight before original submission; this establishes
equivalence, not a title-performance measurement.

Private evidence under `frame-inputs-firstworld-20261010` includes
`producer-prefix2142-final-qualified.aot-private.json`
(`0493ad6cf962a2a0624f3b13f3645c3add1771568011d6f7b76ff68874c2c6bd`),
the frozen original checkpoints, separate expected/actual snapshots, all four
inherited and two new artifact-control reports, and the separate frozen
`producer2133-packed-complete-handoff.aot-private.json` and
`producer2139-packed-complete-handoff.aot-private.json`.
The next unqualified draw is 2143, stopped by the sampled-precision admission
guard before original execution. A reusable source-first same-profile batch
through at most 2200 is being prepared; each new geometry/UV/LOD/depth and
complete packed-byte result still requires its own proof before original use.
Full producer/frame and native HLE scene pixels, enabled noise, original
startup/lifetime, ring rollover and exact silicon remain open; **4/9** stands.

## Batched prefix 2162 and architecture-first continuation - 2026-10-10

The already-prepared next 20 draws pass one reviewed continuous original replay,
advancing the current-generation state/pixel prefix to **1-2162**. It retains
**361,948 independently expected writing fragments**, now with
**2,822,707,200 owned-pixel comparisons**. The batch adds zero writes and passes
72 hardware-free batch controls. All five expected roots remain exact; actual
roots are separately preserved exactly from the preceding original checkpoint.
Earlier allowed RGB differences remain confined to the independently written
footprint under the unchanged one-code contract. Exact depth/stencil/alpha and
untouched storage checks remain in force. The 117 actual-artifact fault controls
are retained; the new source-only controls are a separate count.

Draw2143's complete original geometry produces nine raster leaves and 23,512
tested fragments, all rejected by independently derived LESS depth. No new
sampled-color, LOD or CRT precision is claimed from that empty writing footprint.
The next original input,2163, remains unqualified. A separate source-only
empty-ARGB/stencil proposal and its105 controls are retained for later use;
they do not extend the original prefix. The preserved2163-2458 geometry scan
finds surviving clip leaves first at2261, without establishing storage writes.

Private evidence is `producer-prefix2162-final-qualified.aot-private.json`
(`4619d3eff2b926268fe5f39fd7191a77dc364a0b2cf83fca70fa7d127ed670be`),
the original2162 checkpoint
(`55e7b7c6f4bef85950b809e6caa15c6c797697cf4cde7f9977c42e819b71e13d`),
and `producer-packed-batch2162-root-review.aot-private.json`
(`969a60c0729977fae892f535b68cdf10d1a43ddd78889b8418d8421297dea989`).
All are under `frame-inputs-firstworld-20261010`, with the frozen expected/actual
snapshots and their source-first input bindings.

The owner reviewed the latest corrected default-blend attract scene sheet on
their desktop and replied **"Looks right at a glance"**. The review is bound to
the image, reports and33 retained presents in the private immutable
`default-blend-attract-regression-20261010/owner-scene-review-20261010.aot-private.json`.
It establishes a still-image appearance baseline; no additional title/audio
run occurred and motion/audio correctness is not inferred.

The owner now prioritizes safe, extensible architecture and broader operation
batches. Further exhaustive per-draw admission work has stopped. Retain the
maintained LLE floor, current clipping correction, independent tests, captures,
replay tooling and open qualification scopes. Address bounded resource-contract
risks next, and append safely deferred clipping, pixel, integration or hardware
questions with explicit reopening triggers to the
[graphics foundation/question queue](GRAPHICS-DEFERRED-QUESTIONS.md).
Whole graphics qualification and the **4/9** family count remain unchanged.
