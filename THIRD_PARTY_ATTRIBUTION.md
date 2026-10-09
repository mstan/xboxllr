# Third-party attribution

Third-party material keeps the terms of its authors. The owner's PolyForm grant
does not override these licenses. This inventory records the directly fetched
dependencies and the EEPROM port; transitive and incorporated-source provenance
review remains open. [LICENSE-SCOPE.md](LICENSE-SCOPE.md) explains the unresolved
GPL/PolyForm combined-executable conflict.

For each new import, record the upstream repository, exact revision, imported
files and local changes here or in a linked component inventory. Preserve
copyright headers and full applicable license/NOTICE files with the source and
build artifacts. Identify behavioral references separately from copied or
adapted implementations; a root repository license never replaces component
notices. Preparation/build steps must report missing required notices.

## Private native-title reference pilot

### Deferred stream upload - 2026-10-08

Phobos `05040ec103dfbaca5e3daed28bbc569bd896f59f`,
`src/hle/hle_d3d8_vertex.c`, supplies the binding/consumption strategy:
`D3DDevice_SetStreamSource` records the resource and stride; draw submission
consumes the vertex range. `graphics_vertex.inc` independently applies that
strategy to the existing native draw-range transport, preserving its separate
guest resource ownership. No new donor implementation body is copied.
`tools/native_stream_binding_reference.py` runs the owner's original NTSC
setter with unmapped payloads to bound the ABI, references and fence effects.
The native fixture verifies real draw pixels after persistent-pointer writes.
Existing renderer attribution and the private GPL-dependent pilot's shipping
exclusion remain unchanged.

### Movie queue/stall audit - 2026-10-08

The [movie stall audit](docs/BLINX-MOVIE-STALL-AUDIT-2026-10-08.md)
records the pinned Phobos audio queue/buffer/diagnostic references and the
independent FFmpeg ADX comparison. The new bounded timeline, sparse timings and
event-trace gate are independently authored. No additional donor or FFmpeg
implementation was incorporated, and the existing private pilot shipping
exclusion remains in force.

The subsequent exact PCM ingress/submission capture and
`tools/native_audio_ingress.py` verifier are also independently authored.
Phobos `05040ec103dfbaca5e3daed28bbc569bd896f59f` audio transport was rechecked
for queue ownership; no additional source body was imported. FFmpeg is run
externally on owned media/captured PCM solely to compare decode and resampling
output. Comparison data remains private and is not a shipped implementation.

### Independent display waits - 2026-10-08

The reviewed original NTSC `D3DDevice_BlockUntilVerticalBlank` body, recorded
by `tools/native_d3d_binding.py`, clears and waits for a display notification;
it submits no graphics command. The existing machine PCRTC implementation and
its new stalled-FIFO interrupt regression provide the runnable LLE boundary.
Behavioral reference: xemu `hw/xbox/nv2a/nv2a.c`,
`nv2a_vga_gfx_update`, at `482088490c2d5983ed5e1eafbd09906157c1eca0`,
where display updates latch the PCRTC interrupt. No xemu implementation is
copied. Phobos `src/hle/hle_d3d8.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` was also reviewed for its separation
of guest pacing from host swap pacing. Its `src/kernel/kernel_bridge.c`
notification-event generation and QPC display clock were reviewed as well:
clearing a notification after an edge must not unready existing waiters.
The native display lifetime lock,
waiter regression and timing diagnostics are independently authored here.

### Full-movie observation - 2026-10-08

Independent diagnostics observe the existing attributed Phobos YUV renderer
and native XAudio2/GP transport. Phobos `src/d3d/d3d8_movie.c`,
`src/hle/audio_output_xaudio2.cpp`, `hle_dsound.c` and
`dsound_buffer_model.c` at `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`
were reviewed for presentation and queue/cursor ownership; no new donor body
is imported by these diagnostics. Existing xemu MCPX VP CBO/loop behavior is a
reference for the cursor investigation. Independently decoded local FFmpeg
movie frames and audio are comparison data only, kept with owned media under
ignored build directories. No FFmpeg implementation is incorporated here.

### Texture exposure ownership - 2026-10-08

Phobos `src/hle/hle_d3d8_texture.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` was reviewed for per-resource
rendered-storage ownership and cached texture preparation. Our independent
`graphics_texture.inc` now carries a conservative CPU-exposure hint on each
texture owner, set by locks on the texture or any surface view. It avoids
repeated whole-table exposure searches until a new lock. No donor body was
copied. In particular, this does not adopt once-per-frame checks for ordinary
CPU-owned textures: their actual bytes are still checked at every consumption.
The existing attributed renderer and original/LLE texture contracts remain.

### Native GPU completion notifications - 2026-10-08

Phobos `src/hle/hle_d3d8.c` (`D3DDevice_InsertCallback`) and its D3D11 RHI at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` were reviewed for the remaining
callback cost. Its inline callback path assumes prior completion; that body
was not copied into our asynchronous renderer. The existing attributed donor
renderer remains in use, while our independent query/DPC ownership now uses
[Microsoft's D3D11 Flush1 completion-event API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext3-flush1)
to wake the worker before a polling deadline. This adds no imported source or
license change. Existing original-packet, bounded NV2A WAIT_FOR_IDLE/trap and
native real-GPU callback fixtures retain the observable contract.

### Draw diagnostic configuration caching - 2026-10-08

Phobos `src/hle/hle_d3d8_state.c`, specifically the cached disabled diagnostic
lookup in `hle_d3d8_shadow_apply_states`, at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` supplies the performance approach.
The local `graphics_diagnostics.inc` independently caches owned configuration
strings, including absent values, instead of searching the process environment
for each primitive. No donor body was copied. Draws, state, resource updates,
GPU ordering and enabled diagnostic captures retain their implementations.
The configured draw-capture trigger file is still checked while running.

### Asynchronous writes and installed HDD buffering - 2026-10-07

Phobos `src/kernel/kernel_file.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` supplies the behavioral approach:
real host writes, truthful byte counts, optional completion events, and avoiding
desktop no-buffering alignment rules. No donor function body was copied.
The local implementation extends our existing native asynchronous-read owner,
including pending completion and file-handle notification omitted by that donor.
Installed HDD file backing may use host buffering; explicit write-through and
flush remain real operations. The optical reference is unchanged.

Original owner-provided XAPI code establishes submission layout in
`tools/native_file_write_reference.py`; existing original event transitions
and the in-tree ATA write/readback fixture bound completion behavior. These
are not a complete original FATX/IRP implementation. The host behavior also
references [Microsoft Nt/ZwWriteFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwwritefile).

### Xbox ADPCM buffers - 2026-10-07

`tools/native_adpcm_donor.py` prepares Phobos `src/hle/xbox_adpcm.c/.h`
at `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, retaining its
NoRain211/doaxbv-re provenance and GPL-3.0 notice. The unchanged decoder is
linked only in the opt-in **private GPL-dependent pilot**, not the intended
non-GPL shipping product. Imported source stays in ignored build output.

The same revision's `src/apu/apu_state.h` supplies a separate test-only full
decoder (xemu copyrights, LGPL-2-or-later). `apu_vp.c` and `apu_regs.h`
establish consumption of 64 frames despite the reference computing 65.
The extraction preserves the original body/header and adds only a test entry.
The preparation manifest records source/prepared hashes; full root GPL,
upstream MIT and component LGPL notices accompany the prepared sources and
private executable. No APU decoder runs alongside HLE in production.

The local adapter independently translates compressed storage and byte
positions to the existing native PCM engine. Original owner-provided NTSC
MCPX helpers establish frame/byte conversion and calling convention in a
private executable fixture. Donor silent-output and clock-only completion
fallbacks were not adopted. See the [bounded contract](pilots/native-title/README.md#xbox-adpcm-buffers).

### Shared system clock - 2026-10-07

Phobos `src/kernel/kernel_hal.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, specifically
`xbox_KeQuerySystemTime` and its coarse-host-clock polling notes, was consulted
as a behavioral/performance reference. The independent local adapter uses
Windows' precise FILETIME API with the existing dispatcher clock; no donor
clock body or ignored system-time setter was copied. The same revision's
`kernel_rtl.c` calendar conversions were consulted for the host-API approach;
the local adapter uses native NT conversion with the original Xbox field layout
and does not copy donor bodies or its zero-fill-on-conversion-failure path.
The original private kernel supplies the bounded snapshot/calendar/output/ABI
reference fixtures.

### Presentation reset - 2026-10-07

The independent reset adapter reuses the already prepared Phobos
`src/d3d/d3d8_device.c` real `create_render_targets` and RHI/surface ownership
helpers at `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`. Its success-only
`dev_Reset` remains guarded. The added adapter is recorded in the prepared
`MODIFICATIONS.md` and source/prepared hashes. This remains the private
GPL-dependent reference renderer; no shipping license scope changed.

Original owner-provided NTSC routines establish buffer replacement, embedded
surface-header reinitialization and retained device state. Cxbx-Reloaded
`src/core/hle/D3D8/Direct3D9/Direct3D9.cpp` at
`585c49a50af1255ab155099e06f24505f9c5a800` corroborates that Xbox reset
retains the device and reuses its surface-header addresses. It was consulted
as a behavioral reference only; its implementation was not copied.

### Input replay and persistent display references - 2026-10-07

The independently authored timed host-controller source follows the approach
in Phobos `src/hle/input_host.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, which credits NoRain's DOAXBV work.
No donor replay parser or sampler body was copied. Existing physical controller
mapping retains its separate sp00nznet MIT attribution.

Display persistence was checked against original owner-provided binaries,
nxdk `lib/hal/video.c` at `29638d0b001f179b73c3513489af10ddc2986216`, and Cxbx
`Direct3D9.cpp` at `585c49a50af1255ab155099e06f24505f9c5a800` as a behavioral
reference. Cxbx's incomplete implementation and Phobos's persistence stub were
not copied. The local surface/readback/lifetime adapter and bounded reference
fixtures are independently authored. This does not change the existing GPL
scope of the Phobos-linked private pilot.

### Guest CPU affinity experiment - 2026-10-07

Phobos `src/kernel/xbox_memory_layout.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, specifically
`RECOMP_GUEST_ONE_CPU`, supplies the behavioral approach of pinning guest
threads while leaving native workers free. `thread_affinity.inc` independently
uses documented Windows group-affinity APIs with checked startup selection;
no donor implementation body is copied. The original kernel priority-query
oracle remains private. Exact host/Xbox scheduling equivalence is not asserted.

### Native event objects and asynchronous movie reads - 2026-10-07

Phobos `src/kernel/kernel_sync.c` and `kernel_file.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` supply the host-event/file approach.
The new adapters are independently authored: donor read code omits pending
handling, and donor event state shortcuts are not copied. Original supplied
kernel routines establish event layout and transitions in a private oracle;
the optical DMA fixture supplies work-before-completion scaffolding.
[Microsoft NtReadFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntreadfile)
defines native IO completion/buffer lifetime, while
[CloseThreadpoolWait](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-closethreadpoolwait)
defines callback cleanup. No GPL implementation body is copied into these
adapters; this does not alter the linked private pilot's license scope.

### Movie YUV enable and bounded pixel reference - 2026-10-07

The native state adapter reuses the Phobos renderer's already imported packed
YUY2/UYVY conversion at `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`.
Original owner-supplied NTSC XDK execution establishes cache/packet/ABI behavior;
Cxbx render-state definitions were a comparison only (its later YUV state index
is not used for this revision). The new software NV2A floating-point BT.601
reference and ABI adapter are independently authored, without copying GPL
implementation bodies. Existing donor renderer notices and private-pilot GPL
scope remain; original binaries and oracle output remain private.

### Donor device-lifetime correction - 2026-10-07

The preparation of Phobos `src/d3d/d3d8_device.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` now releases and resets its cached
empty-stage RHI view at device destruction. D3D11 validation exposed an old
device's view being rebound to a new device. This is an adaptation of the
already imported private renderer; its original notices and GPL scope remain.
Prepared source hashes and MODIFICATIONS.md record the change. Driver debug
validation is available through XLLE_PILOT_D3D_DEBUG.

### Directory enumeration - 2026-10-07

Phobos `src/kernel/kernel_file.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` supplies the real-host enumeration /
Xbox entry-translation approach; Cxbx `EmuKrnlNt.cpp` at
`585c49a50af1255ab155099e06f24505f9c5a800` corroborates single-entry handling and
dot-entry exclusion. nxdk supplies the class-1 ABI. No donor service bodies are
copied. Our adapter uses the existing host NT handle and checked bounded writes.
[Microsoft NtQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile)
is the host cursor/completion reference.
[XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso/blob/master/extract-xiso.c)
(reviewed 2026-10-07) supplies XDVDFS layout facts for the authored LLE optical
fixture; no implementation copied. Original kernel/XDK bytes remain private.

### Contiguous page protection - 2026-10-07

Phobos `src/kernel/kernel_memory.c` at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` supplies the native VirtualProtect /
VirtualQuery approach. The adapter independently maps owned guest pages,
checks host failures and retains per-page logical cache policy. No donor body
is copied. Original owner-supplied kernel exports 179/182 establish permission,
range and invalid-flag behavior by actual execution; Cxbx VMManager::Protect
at `585c49a50af1255ab155099e06f24505f9c5a800` is a behavioral cross-check.
No original kernel bytes or disassembly are committed.

### Shared GPU image aliases - 2026-10-07

The independent adapter reuses the already imported Phobos RHI image retain,
wrap, view creation and texture ownership services at
`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`. The same revision's
`src/hle/hle_d3d8_texture.c` storage-identity logic was a behavioral reference;
no HLE implementation body was copied. Original XDK header execution and an
in-tree LLE shared-RAM fixture establish the bounded contract. Existing donor
notices and the private pilot's GPL-dependent distribution scope still apply.

### Ordered GPU callbacks - 2026-10-07

`graphics_gpu_callback.inc` and its dispatcher integration are independently
authored. Original NTSC XDK execution establishes the packet/cdecl boundary;
the existing NV2A software-method floor supplies runnable IRQ/parameter/ack
behavior. Cxbx-Reloaded `Direct3D9.cpp` at `585c49a50af1255ab155099e06f24505f9c5a800`
was a behavior reference for FIFO callbacks and WRITE completion. Phobos
`hle_d3d8.c` at `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` supplied the donor
comparison; its immediate-call policy was not adopted. No donor bodies copied.
[XboxDev/nxdk pbkit](https://github.com/XboxDev/nxdk/blob/master/lib/pbkit/pbkit.c)
(reviewed 2026-10-07; Guillaume Lamonoca and nxdk contributors) corroborates
GPU interrupt processing on a serialized DPC. Its source is not incorporated.

### Visibility reference and native query contract - 2026-10-07

The new `src/machine/nv2a.cpp` report/counter handling, visibility fixtures and
`pilots/native-title/graphics_visibility_backend.inc` are independently authored.
Register/layout and behavior references are
[abaire/nxdk_pgraph_tests Z-pass tests](https://github.com/abaire/nxdk_pgraph_tests/blob/main/src/tests/zpass_pixel_count_tests.cpp)
(reviewed 2026-10-07), xemu NV097 definitions/report dispatch at
`482088490c2d5983ed5e1eafbd09906157c1eca0`, Cxbx-Reloaded D3D visibility behavior
at `585c49a50af1255ab155099e06f24505f9c5a800`, and the owner's original NTSC XBE
ABI executions. No GPL donor implementation bodies are incorporated by this
change. Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` was audited; its
64-poll fabricated visibility result is not adopted. Cxbx policies that differ
from the reviewed original routine (nesting/index reuse) are not adopted.
The native implementation uses Microsoft D3D11 occlusion/timestamp/GetData APIs
and the existing donor device. Existing linked-component license obligations
remain; these new independent adapters do not make the pilot non-GPL.

### Renderer, audio, input and movie adoption — 2026-10-07

This batch adds actual Phobos renderer implementation to the **private** title
pilot. Earlier checkpoint descriptions below are historical; their “no Phobos
graphics implementation” statement does not apply to the new PHOBOS build.

| Source and revision | Use here | Retained credit and terms |
| --- | --- | --- |
| [phobos665/xboxrecomp](https://github.com/phobos665/xboxrecomp/tree/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0), `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` | Prepared `src/d3d` D3D11/RHI renderer, resources/conversions, state/shader caches, combiner compiler, display resolve and UP upload ring; config, platform vocabulary and pure `src/kernel/nv2a_vsh` helper. Source copies stay in ignored build output. Vulkan files may be retained in the source set but are not compiled here. | Fork GPL-3.0; original LICENSE, NOTICE, LICENSE.upstream-MIT, LICENSES and per-file comments preserved. Adaptations and source/prepared hashes recorded beside the copies. |
| [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp/tree/1409a7d7801d3e931fb1074be6104209ddd9a33e), `1409a7d7801d3e931fb1074be6104209ddd9a33e` | Existing selectable renderer and adapted physical XInput conversion; also the upstream foundation of Phobos. | MIT, Copyright (c) 2026 sp00nz; original license/NOTICE with prepared renderer and [input MIT notice](pilots/native-title/input-sp00nz-MIT.txt). |
| Phobos `src/input/input_bindings.c`, `bcccf8c` | Design reference for the independently implemented startup-selected keyboard controller. | Credit in `input_host.c`; no donor parser/sampler body copied. |
| Phobos `src/hle/hle_d3d8.c::shadow_viewport_constants`, `bcccf8c` | Reference for the reserved vertex-constant 58/59 handoff missing from the native adapter. Independently implemented using the original NTSC packet emitter and existing LLE NV097 registers; retains original pixel bias and zero W rather than donor approximations. | Design reference only; no GPL implementation body copied. Actual renderer remains the private integration credited above. |
| Phobos `src/hle/hle_d3d8_vertex.c`, `bcccf8c` | Draw-consumed vertex-range strategy, adapted to the existing multistream resource adapter using ordered partial updates of the borrowed renderer's DEFAULT buffers. | Design reference; original UP-ring implementation remains in the prepared private renderer. No HLE guest-body/shadow-execution code copied. |
| Phobos `src/hle/audio_output_xaudio2.cpp` and `dsound_buffer_model.c`, `bcccf8c`; upstream [NoRain211/doaxbv-re](https://github.com/NoRain211/doaxbv-re/tree/aa1d3c8afcd8d063171d819e27307070b63f056b), `aa1d3c8afcd8d063171d819e27307070b63f056b` | Actual donor XAudio2 transport compiled by the separate donor probe. Queue ownership and loop-span behavior referenced by the independently implemented title transport, retaining real device completion and DSP processing. | Donor files declare GPL-3.0 and credit doaxbv-re. Probe uses the external checkout with notices. No source bodies copied into the title transport; credit in `audio_xaudio2.cpp`. This is not wholesale Phobos audio adoption. |
| Phobos `config/seeds/4D530013.json` and `53450036.json`, `bcccf8c` | Sofdec discovery strategy: recover the actual NTSC XBE section entries/pointer table, avoiding imported PAL addresses. Original title decoder remains private. | Source and pin in `native_movie_seeds.py` and generated contract manifest. No donor movie decoder or title binary bytes added to the repository. |

The movie texture adapter now exercises Phobos's packed YUY2/UYVY conversion
and its documented empty-texture-stage behavior. No decoder implementation is
copied: the selected decoder is generated from the owner's private NTSC XBE.
Audio mix-bin routing was independently implemented using original XDK setter
behavior, [xemu MCPX voice processing](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/vp/vp.c)
(`482088490c2d5983ed5e1eafbd09906157c1eca0`) and Cxbx Xbox sound type definitions
(`585c49a50af1255ab155099e06f24505f9c5a800`) as behavioral references. No xemu or
Cxbx implementation body was incorporated. External FFmpeg validates private
movie assets only; it is not linked or bundled by the title pilot.

`tools/native_phobos_d3d.py` retains license/notice bytes, checks the pinned
source, records original/prepared hashes and adds provenance comments to
adapted C files. Its `MODIFICATIONS.md` identifies local changes.
`tools/native_pilot.py` retains the Phobos notices beside generated CPU helpers.
The existing MIT DSP56300 implementation and permissive NV2A decoder remain
credited below; no Phobos APU or NV2A device implementation replaces them.
These are private reference artifacts, not additions to the intended non-GPL
shipping product. Owner-authored adapters retain this repository's terms;
those terms do not replace upstream licenses.

The private title build copies this inventory, repository scope/notice, donor
licenses and modification manifests beside the executable in `third_party/`.
That includes the sp00nz MIT notice covering input and non-local jumps, the
NV2A reference license, and DSP core plus Cargo dependency notices. Credit
retention does not change the private pilot's distribution status.

`tools/native_pilot.py` invokes Phobos xboxrecomp at
`63f18f0334fbd8c00313c4bb565accf00627a4fd` from an external checkout. Its
generated code copies `templates/runtime/recomp_types.h` from that GPL-3.0
fork into ignored private output. The header is modified there to use
thread-local diagnostic state; missing generated bodies become terminal
unsupported faults. The resulting executable is reference-only and excluded
from the normal product build. No donor kernel/GPU acknowledgement runtime is
linked by this pilot. See [pilot scope](pilots/native-title/README.md).

Phobos kernel bridge signatures/host lock mapping and Cxbx-Reloaded `585c49a`
Xbox type/EEPROM definitions were consulted for the launch adapter's contracts.
These references do not clear the Phobos header or the combined reference
executable for non-GPL distribution. Preserve the external donor's LICENSE,
NOTICE and per-file notices with its source; no generated game source belongs
in this repository.

The native pilot dispatcher independently implements the observed API boundary
using host waitable timers and an owned callback queue. Microsoft's NT timer,
DPC and wait documentation plus Cxbx's Xbox layouts/ordinals supplied contract
references. The MIT sp00nznet `1409a7d` timer bridge was inspected for its worker
architecture and known gaps; no donor timer service bodies were imported.
Links, exercised behavior and remaining qualification limits are recorded in
the pilot README. Its overall reference-only distribution status is unchanged.

The native VM adapter uses host NT services for page backing and independently
tracks guest reservation ownership. nxdk prototypes/flags, Cxbx address-range
constants and Microsoft NT memory documentation supplied ABI references.
sp00nznet `1409a7d` memory/file bridges were inspected for useful mechanisms and
limitations. No donor memory/file service bodies were incorporated by this
checkpoint; private CPU-header licensing remains unchanged.

The installed-media adapter uses actual host NT file services. MIT sp00nznet
`1409a7d` `kernel_file.c` and `kernel_path.c` informed device-path/handle design;
nxdk supplied Xbox structure/signature references and Microsoft documented the
host I/O contract. No donor file-service bodies or drive-authentication success
responses are incorporated. The private byte-hash-bound wrapper selects the
owner-authorized installed-media contract or the original generated routine.
Original title code stays in ignored private output; the Phobos CPU helper
continues to make that pilot a GPL-dependent reference artifact.

The native HDD/namespace checkpoint also consulted all three donor file
backends: sp00nznet `1409a7d`, Phobos `63f18f0`, and the pinned NoRain review
checkout. sp00nznet's 16 KiB logical-volume correction and actual link/path
mapping informed the design; host NT services implement create/write/flush,
metadata and volume operations. Donor fixed-capacity fallbacks and ignored
flush errors are excluded. nxdk and Microsoft supply ABI references. Existing
libfatx was checked as an external GPL reference for partition/header facts;
no libfatx or GPL filesystem implementation was imported. Local LLE disk header
inspection corroborates geometry only, not API equivalence.

`pilots/native-title/sections.inc` owns a page-residency ledger over the verified
XBE and uses host NT allocation/decommit services. nxdk declares the API;
Cxbx `585c49a` `EmuKrnlXe.cpp` was consulted for section/boundary count and shared
page lifetime behavior. No Cxbx loader body is incorporated. sp00nznet's
all-resident reference-count-only routines were inspected and not adopted.
The private Phobos CPU helper and earlier distribution limitations remain.

The utility-drive service is independently implemented with host NT file APIs
and a native ownership manifest. Its caller boundary was reviewed from the
owner's original XBE; generated original bodies remain ignored private output.
Microsoft directory/file-information documentation supplies host API contracts.
nxdk's LaunchDataPage declaration and Cxbx's null cold-launch initialization
were consulted as ABI/initial-state facts; no GPL cache/launch service body is
incorporated. The existing GPL-dependent CPU-helper restriction is unchanged.

The native read-through file cache independently allocates real buffers and
uses the existing host NT read path. nxdk supplies its page-count signatures;
Cxbx `EmuKrnlFs.cpp` supplies default/maximum count facts. Main sp00nznet and
Phobos's size-only bridges were inspected but not incorporated. Microsoft's
NtReadFile documentation supplies synchronous cursor and EOF contracts. This
adds no GPL service body and does not change the private CPU-helper restriction.

`tools/native_d3d_donor.py` prepares the Windows D3D backend from MIT sp00nznet
`1409a7d7801d3e931fb1074be6104209ddd9a33e`: seven `src/d3d/*.c` files,
their six headers, and `src/platform/xbox_winnt.h`. Original LICENSE/NOTICE and
per-file notices remain with the ignored build-tree copies. The manifest records
source/prepared hashes and excluded methods. Changes remove unqualified COM
bodies, make failures explicit and clean up partial device creation. No APU,
kernel, NV2A device model or LGPL `nv2a_regs.h` is imported. The guest adapter
uses original XBE ABI inspection and Cxbx resource-layout/constant/front-buffer
facts as external references; no Cxbx/Phobos graphics service body is imported.
The already existing Phobos CPU helper still makes the combined executable a
private GPL-dependent reference artifact, not a cleared shipping product.

The graphics state extension uses original XDK instruction/table inspection,
nxdk NV2A constant declarations and Cxbx Xbox state/layout declarations as ABI
facts; no external GPL conversion bodies are imported. Pinned MIT donor state
and shader files receive prepared changes for diffuse argument zero and sampler
W/border/LOD/filter behavior. Source/prepared hashes retain that provenance.
The DOS-directory pseudo-handle correction follows nxdk's
`ObDosDevicesDirectory()` declaration and the main donor's handle-adaptation
reference, while resolving through this pilot's existing owned namespace.

The pilot's `pilots/native-title/nonlocal_jump.inc` adapts the thread-local
native jump-slot mechanism from main MIT sp00nznet `1409a7d7801d3e931fb1074be6104209ddd9a33e`,
`src/kernel/xbox_memory_layout.c` (non-local jumps section). Copyright (c) 2026
sp00nz; the complete notice is retained in
[LICENSES/sp00nznet-MIT.txt](LICENSES/sp00nznet-MIT.txt). Changes add explicit
capacity/missing/corrupt-buffer failures, caller-frame lifetime and sequence
tracking, and validation before native transfer. Original guest CRT code retains
its unwind/notification work; the donor's shortcut around that work is excluded.
The CPU capability service is independently expressed from the original guest
probe's observable contract and the pilot's configured CPUID profile. Neither
change clears the pre-existing GPL dependency of the combined private pilot.

The texture adapter additionally uses the pinned main MIT donor's
`d3d8_CreateTextureImpl`, texture lock/unlock, unswizzling and release code.
Prepared changes make upload-allocation failures terminal. The shared guest
adapter is expressed from original XDK routines and Xbox resource/descriptor
layout facts in Cxbx; no Cxbx texture service implementation is imported.
Phobos's CPU-shadow/upload boundary was reviewed as an external design reference.
Main, Phobos and NoRain capability/display query bodies were checked and excluded
because they report success without producing their required outputs. The
original title's capability data remains in ignored, hash-bound generated files.

## Unicorn Engine / QEMU

The private native-title calendar LLE build additionally uses the **Unicorn
2.1.4 Windows Python package's unmodified static C library and headers**, from
[Unicorn Engine 2.1.4](https://github.com/unicorn-engine/unicorn/tree/2.1.4).
Its original [COPYING](https://github.com/unicorn-engine/unicorn/blob/2.1.4/COPYING)
and `COPYING.LGPL2` are copied beside the private executable. The static library is hash-pinned in
`pilots/native-title/calendar.cmake`; no Unicorn implementation source is copied
into maintained project source. `PILOT_CALENDAR=HLE` does not link this library.
The maintained LLE adapter executes owner-supplied original kernel calendar
routines; those private bytes are generated only under ignored build output.
This reference configuration remains GPL-dependent and excluded from the
intended non-GPL shipping product. See
[calendar scope and qualification](pilots/native-title/CALENDAR-LLE.md).

[Unicorn Engine](https://github.com/unicorn-engine/unicorn), version `2.1.1`,
is fetched by CMake and statically linked as the CPU floor. Preserve its authors'
notices, `COPYING`, component licenses, and corresponding source when applicable.
A copy of its top-level GPL v2 text is in [LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt).
The QEMU and bundled-library tree includes additional notices and licenses;
this top-level copy is not a substitute for those.

`cmake/UnicornPreciseFault.cmake` derives hash-checked build-tree copies of the
pinned QEMU TLB, translation and MSR helper sources for precise memory-fault
restoration, instruction-clock/TSC repair, and code coherence through RAM aliases. The generated
copies retain their upstream license headers. The bridge in
`src/cpu/unicorn_aot_bridge.c` uses this pinned target's private structures;
upgrading Unicorn requires requalifying these integration changes.

`cmake/UnicornX87.cmake` also derives the pinned FPU helpers and declarations.
`src/cpu/x87_status.inc` adapts exception merging from the portable SoftFloat
path in xemu's `target/i386/tcg/fpu_helper.c` at commit
`482088490c2d5983ed5e1eafbd09906157c1eca0`. Copyright (c) 2003 Fabrice Bellard;
LGPL 2.1 or later, with the text in [LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt).
Modifications include per-operation status/C1 handling and staging floating
store conversion state until the actual memory operation succeeds. Arithmetic
uses the existing extended-precision SoftFloat implementation. xemu's separate
host-FPU path is not incorporated.

## nv2a_vsh_cpu

[nv2a_vsh_cpu](https://github.com/xemu-project/nv2a_vsh_cpu), commit
`863fc0f50aa4027af113718d2a63e45ac3be7a42`, is fetched by CMake as the NV2A VSH
interpreter. Its upstream license is The Unlicense, copied verbatim into
[LICENSES/nv2a_vsh_cpu-Unlicense.txt](LICENSES/nv2a_vsh_cpu-Unlicense.txt).

`cmake/Nv2aVshArl.cmake` applies an independently authored scalar-address-store
correction to a generated emulator copy, shared by LLE and private native
reference tests. The pinned decoder gives ARL a zero vector writemask; the
upstream emulator therefore discarded its result. The correction writes a0.x
directly. Original donor files and notices remain intact; each build records
original/corrected SHA-256 hashes in its `*-arl/provenance.txt`. The compiled
LLE executor implements the same scalar destination. Signed/fractional address
fixtures and actual Blinx captured draws qualify this bounded correction
(`beads-xbox.1.91`); it is not an assertion of exhaustive hardware accuracy.

## SDL

[SDL](https://github.com/libsdl-org/SDL), tag `release-3.2.0`, is fetched by
CMake for the host window, input and presentation layers.
Copyright (C) 1997-2025 Sam Lantinga and contributors; the pinned top-level
zlib-style notice is in [LICENSES/SDL-zlib.txt](LICENSES/SDL-zlib.txt).
Retain the applicable bundled-component notices when packaging SDL.

## xemu EEPROM generator

[tools/create_eeprom.py](tools/create_eeprom.py) is a modified Python port of
xemu's EEPROM-generation logic, with SHA-1/RC4 support following the referenced
implementation. Source reference: [xemu commit 482088490c2d5983ed5e1eafbd09906157c1eca0](https://github.com/xemu-project/xemu/tree/482088490c2d5983ed5e1eafbd09906157c1eca0),
`hw/xbox/eeprom_generation.c`, `hw/xbox/eeprom_generation.h`,
`util/sha1.c`, and `util/rc4.c`.

Copyright (c) 2020 Mike Davis. Copyright (c) 2024 Ryan Wendland.
The referenced files grant GNU Library/Lesser GPL version 2 or, at the
recipient's option, any later version. The port and its modifications retain
those terms; [LICENSES/LGPL-2.0.txt](LICENSES/LGPL-2.0.txt) supplies the version 2
text. This is a standalone source tool, not code relabeled as PolyForm.
Changes include Python standard-library operation, local file creation/reuse
safeguards, integrity validation and a locally administered virtual MAC address.

## Other xemu references

The experimental `--nv2a-renderer xemu` path now directly incorporates the
production pixel-shader generator at the same pinned xemu commit. Files and
mechanical changes are listed in
[src/third_party/xemu/README.md](src/third_party/xemu/README.md). The pixel
translator retains GPL version 2-or-3 terms and its espes, Jannik Vogel,
Matt Borgerson, Cxbx and Xeon attribution; supporting files retain their LGPL
version 2-or-later notices. The integration supplies actual draw state and
uniforms, without importing a QEMU machine shim. Existing combined-executable
distribution questions in LICENSE-SCOPE remain unresolved.

The project documents xemu-derived hardware behavior and source references in
its design and implementation comments. Those comments are not an assertion
that all referenced source was copied or that all implementation is original.
Any incorporated code must retain its upstream terms and notices. The remaining
file-level provenance review is part of `beads-xbox.1.21`.

## NoRain texture layout implementation

[NoRain211/doaxbv-re](https://github.com/NoRain211/doaxbv-re/tree/aa1d3c8afcd8d063171d819e27307070b63f056b),
revision `aa1d3c8afcd8d063171d819e27307070b63f056b`, supplies the unmodified
`recomp-runtime/d3d_texture_model.c` and `.h` used by the experimental machine
HLE renderer's texture layout/unswizzling. See the
[component inventory](src/third_party/norain/README.md) and retained
[GPL-3.0 text](LICENSES/GPL-3.0.txt). This earlier direct implementation import
is distinct from the native pilot's audio design references and remains within
the research dependencies described in LICENSE-SCOPE.md.

## Native-pilot DSP processor

[mborgerson/dsp56300](https://github.com/mborgerson/dsp56300/tree/c60aeedb6a6014850204c71e53bc0cf5b3104222)
is MIT-licensed. `tools/native_dsp_donor.py` copies its pinned tracked source
into private build output, preserves LICENSE, records source/patch hashes and
locked Cargo dependency metadata, and adds explicit unsupported-feature stops.
`tools/native_dsp_notices.py` also collects verbatim notices for the Windows
FFI's normal dependency closure and the Rust standard library. Its inventory
records package versions, authors, source URLs, notice origins and hashes.
For Cranelift crates whose published archive omits the monorepo LICENSE, it
retains that file from a sibling at the identical source commit with identical
declared terms. Cargo feature unification can make this inventory conservative;
it is not a claim that all listed code is linked. Build/dev tools remain in the
separate full dependency record. These notices accompany the private executable.
The C FFI static library supplies actual instruction execution. Xbox image,
DMA and XAudio2 adapters are independently authored; no GPL audio implementation
was imported. The dependency record includes the MPL-2.0 cbindgen build tool;
the MIT root license does not replace dependencies' own terms. See the
[DSP audit](docs/NATIVE-DSP-DONOR-2026-10-07.md) and existing
[private CPU pilot distribution limits](LICENSE-SCOPE.md).

The maintained GP path additionally uses this same pinned MIT core through
`src/machine/apu_dsp_core.c`, an independently authored C/C++ bridge. Its real
memory banks and physical scratch-DMA transport are authored in `apu.hpp` and
`apu_gp_dma.hpp`. The GP test target retains the same upstream/Cargo notices.
[XboxDevWiki DSP research](https://xboxdevwiki.net/DSP) supplies the independent
format-2/format-6 byte examples used to correct native packing; xemu
`482088490c2d5983ed5e1eafbd09906157c1eca0` supplies behavioral register/SGE
references, not imported implementation bodies. No new GPL runtime body was
copied or adapted. The unknown DMA control bit reached by Blinx is still
guarded in the maintained floor. See [scope and evidence](pilots/native-title/AUDIO-LLE.md#current-gp-memorydma-batch-and-deferred-question---2026-10-09).

The subsequent GP FIFO extension uses the same published DSP ring/PCM16/PCM24
contract, the pinned xemu `hw/xbox/mcpx/apu/dsp/gp_ep.c` and `apu_regs.h` as
behavioral register/SGE references, and private original NTSC driver setup as
corroboration. FIFO transport, byte packing and tests are authored here; no GPL
implementation body was copied or adapted. The original driver setup is not
claimed to execute end to end. Phobos's pinned `src/apu/README.md` identifies
GP effects bypass/EP passthrough; those substitutes are not reused in this floor.
The same MIT DSP core and upstream/Cargo notices cover the real instruction
tests. Existing private-pilot license restrictions remain unchanged.

## Borrowed render-target feedback implementation

The private native pilot reuses `feedback_scratch`, `feedback_begin/end` and
target-view rebinding from Phobos
[`src/d3d/d3d8_device.c`](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/d3d/d3d8_device.c).
That source explicitly describes Blinx's scene-alpha shadow compositing.
`tools/native_d3d_feedback.py` exposes the existing GPU copy/cache to our target
and draw interfaces, recognizes aliases by underlying D3D storage, makes failed
copies terminal, and adds scratch teardown. The original implementation stays
in ignored prepared donor output. Original notices, source/prepared hashes and
the generated MODIFICATIONS inventory are retained by native_phobos_d3d.py.
The established private GPL-dependent research scope applies; this is not
imported into the intended non-GPL shipping product. The independent bounded
NV2A RAM/pixel fixture verifies pixel-local feedback without a native renderer.

## Empty vertex-declaration binding reference

The same donor metadata audit found `GetPixelShader` incorrectly described
with an extra argument. The original SHA-qualified Xbox function supplies the
one-argument RET4 contract. `tools/native_d3d_binding.py` records that correction
and checks all identified return cleanups, rather than treating donor metadata
as ABI authority. This is an independent compatibility correction.

`tools/native_vertex_binding_reference.py` runs the full original NTSC XDK
setter on private captures, including an empty FVF. It qualifies descriptor,
cached handle and retained constant/program state. Private bytes/captures are
excluded from source control. Phobos's `dev_SetVertexShader` in
`src/d3d/d3d8_device.c` caches the selected handle; the existing native adapter
likewise binds the actual donor pipeline at draw. This change adds the missing
Xbox empty-declaration state transition, not a copied implementation or empty
drawing substitute. The existing donor pin and notices apply.

## Native mapped pixel-constant references

The independent Xbox ABI adapter follows the complete original NTSC Blinx
XDK setter/getter, exercised locally by `tools/native_pixel_constants_reference.py`.
Private game bytes and generated bindings remain outside source control.
It reuses the already attributed Phobos renderer and constant-buffer pipeline;
Phobos's `src/hle/hle_d3d8_state.c` separates shader structure from live render
state constants, and `src/d3d/d3d8_combiners.c` uploads them. The desktop COM
constant setter is a stub and is not enabled. Reference pin:
[`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`](https://github.com/phobos665/xboxrecomp/tree/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0).
NV097 register declarations identify the secondary constant-zero packet as
EYE_VECTOR, not diffuse color. No Theseus implementation is used. The private
pilot's existing GPL-dependent research scope remains unchanged.

## Native logic COPY contract references

`beads-xbox.1.80` reuses Phobos's existing `d3d8_states.c` blend-state creation,
cache and `rhi_d3d11.c` output merger. The adapter implements Xbox logic COPY
by disabling effective blending while retaining the requested blend state;
color-write masks remain active. No external logic-operation body is copied.
The original NTSC XDK setter is executed unchanged in a private input fixture.
NV097 method/mask declarations and
[OpenGL 1.1 sections 4.1.6 and 4.1.8](https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf)
inform the bounded software pixel reference. In particular, logic COPY bypasses
blending and copies source color, subject to masks. The reference fixture also
exposed/corrected reversed red/blue mask bits in the generic software floor;
NV097 uses red bit16, green bit8, blue bit0 and alpha bit24.
Other bitwise logic operations remain unqualified in the native adapter.

## Native pixel-shader query reference

The adjacent pixel-shader query (`beads-xbox.1.79`) references Phobos's real
`dev_GetPixelShader` in `src/d3d/d3d8_device.c` and executes the original NTSC
XDK query in a private fixture. The independent guest adapter returns the
existing guest handle/ownership snapshot; it does not expose the donor's
internal combiner selector. No donor query implementation body was copied.

## Native flat-shading contract references

Flat shading (`beads-xbox.1.78`) retains the pinned Phobos renderer, shader
compilers and caches. Its `src/hle/hle_d3d8_state.c` explicitly documents
SHADEMODE as forwarded but unused; no functioning flat-shading body was
available to copy there. The new native geometry stage and software NV2A color
broadcast are independently written. Behavioral references: original NTSC XDK
initialization (private, SHA-pinned fixture), public NV097 method/value
declarations, [Microsoft triangle interpolation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/triangle-interpolation)
and [triangle strips](https://learn.microsoft.com/en-us/windows/win32/direct3d9/triangle-strips),
and xemu's `hw/xbox/nv2a/pgraph/glsl/geom.c` at the xemu revision listed above
for provoking-vertex and quad conventions. That GPL geometry implementation
was consulted, not copied or linked by this addition. Existing private-build
GPL dependencies retain their existing scope and notices.

## Native audio storage detachment references

`beads-xbox.1.88` consults Phobos
[`bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, `src/hle/hle_dsound.c`](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/hle/hle_dsound.c)
`IDirectSoundBuffer_SetBufferData`: stop the old voice, replace storage, reset
play/loop regions, and retain the guest interface and other settings. No body
from that GPL file is copied. The adapter reuses the already attributed native
transport and ADPCM decoder. `tools/native_audio_detach_reference.py` executes
the original NTSC caller's NULL/zero argument prefix and complete caller-owned
settings detach/rebind helper without substituted instructions. Private original
code and generated bytes remain excluded from Git. The existing MCPX VOICE_OFF
test covers the bounded device stop/notifier/idle-IRQ lifecycle separately.
[Microsoft DestroyVoice](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-destroyvoice)
establishes when source readers/callbacks have retired and native storage may be
freed. This is a behavioral reference and local adapter change; the private
pilot's existing license scope is unchanged.

## Guest scheduling behavioral references (2026-10-08)

The private pilot's `thread_scheduler.inc` follows the service boundaries and
ADX race analysis documented by [Phobos and contributors at
05040ec103dfbaca5e3daed28bbc569bd896f59f](https://github.com/phobos665/xboxrecomp/blob/05040ec103dfbaca5e3daed28bbc569bd896f59f/src/kernel/xbox_memory_layout.c),
with `kernel_bridge.c` and `kernel_thread.c` consulted for blocking calls and
cooperative suspension. The implementation is independently authored; no donor
scheduler body is copied. It excludes the donor's starvation escape, expiring
CPU reservation and callback execution without exclusive ownership.
The same revision's `src/hle/hle_d3d8.c` and its `rhi_set_wait_hooks` setup
also inform the narrower renderer integration: release guest execution at real
waits rather than every setter/draw. No RHI implementation body is copied.

[xemu 482088490c2d5983ed5e1eafbd09906157c1eca0](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/xbox.c)
provides the single-CPU machine reference. The runnable bounded ordering and
priority oracle executes the owner's original kernel using Unicorn, with no
instruction replacement; `tools/native_scheduler_reference.py` records its
limits. Neither original kernel bytes nor xemu code are incorporated. Existing
GPL-dependent components of the private pilot remain excluded from the intended
non-GPL shipping product.

## Original event/timer qualification (2026-10-09)

`tools/native_event_timer_reference.py` and the corresponding compiled bridge
tests are independently authored against the owner's hash-bound kernel capture.
They execute original event/object-manager and timer-queue operations without
replacing callees. No kernel bytes, disassembly or capture is incorporated.
The existing attributed private interpreter and HLE event implementation remain
unchanged; no new donor implementation body or dependency was imported.

[Microsoft: Timer Accuracy](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/timer-accuracy)
is consulted for the documented clock-granularity limit of relative native
waits. The accepted 1 ms HLE tolerance matches the original exported clock
resolution; it is not evidence that original Xbox waits return early.
Existing nxdk export declarations corroborate the ordinal/calling interfaces.
These are behavioral references, not copied implementation code or proof of
complete original timer interrupt delivery.

The follow-up `tools/native_timer_expiry_reference.py` executes the original
clock programming, IRQ0, timer expiration/DPC dispatch and real thread
creation/block/wake/exit. Its test worker/DPC and transport changes are authored
here. Owned INIT bytes are loaded privately at runtime, never redistributed.

- Intel **8254 datasheet 231164-005 (September 1993)**, pages 5-11,
  [university-hosted copy](https://www.scs.stanford.edu/23wi-cs212/pintos/specs/8254.pdf):
  binary counter programming, access order, latch retention, mode aliases and
  one-shot versus periodic behavior inform the bounded PIT corrections.
- xemu **482088490c2d5983ed5e1eafbd09906157c1eca0**, `hw/timer/i8254_common.c`
  and `include/hw/timer/i8254.h`: consulted for clock/transition comparison.
  Those files carry permissive notices crediting Fabrice Bellard and, for the
  common implementation, Jan Kiszka/Siemens. No new bodies are copied/adapted.
  Its Xbox +1 Hz workaround is not adopted as a physical-clock fact.
- nxdk **29638d0b001f179b73c3513489af10ddc2986216**, `lib/xboxkrnl/xboxkrnl.h`
  and `.exe.def`: original thread/wait/DPC/fastcall interface declarations.
- Phobos **bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0**,
  [kernel_sync.c](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/kernel/kernel_sync.c):
  timer-queue approach reviewed against original DPC argument and execution
  contracts. No new donor implementation is incorporated by this batch.

Existing private-pilot dependency restrictions remain unchanged. This batch
adds no GPL-derived implementation to the intended non-GPL shipping product.

The subsequent original-DPC/native bridge and HLE cadence correction reuse those
same references and the existing native callback transport. The owner's original
kernel independently establishes both dispatch call sites, actual DPC stack
ownership, four-argument callback semantics and the nine-period/45-callback
timing observations. Native deadline arithmetic is authored here against those
observations; Phobos's timer-queue implementation is not copied. Test callback
instruction bodies are authored fixture programs. No kernel instructions,
disassembly, private images or GPL implementation bodies are added to the source.

## Original thread/native body qualification (2026-10-09)

The new thread transport, lifetime bookkeeping and test programs are authored
here against the owner's hash-bound original kernel capture. Original execution
establishes start call sites, actual stack/TLS/PCR identity, suspend counts,
termination status and reclamation. No original bytes, disassembly or captures
are incorporated into the repository.

- [nxdk declarations](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/xboxkrnl/xboxkrnl.h)
  provide KTHREAD/ETHREAD/PS_STATISTICS layout and thread-service declarations;
  existing nxdk attribution applies. Layout facts are checked by original execution.
- [Phobos kernel_thread.c](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/kernel/kernel_thread.c)
  was reviewed for host callback/termination transport. It does not retain all
  required original TLS/extension/second-context behavior, so no additional
  implementation body is copied or adapted for this batch.

The HLE null-TLS correction follows the independent original observation.
The existing private interpreter and donor restrictions still apply; this batch
adds no GPL-derived implementation to the intended non-GPL shipping product.

The blocking-continuation follow-up retains native C stacks/TLS with Win32 host
threads, the same transport primitive reviewed in the pinned Phobos wrapper.
Its event handshakes are authored here: only an actual original-kernel entry or
service return selects a worker. No donor scheduler, thread-body implementation
or success substitution is copied. The independently authored x86 handshake
program corroborates original wait/signal, priority and self-suspend ordering;
the maintained PIT/PIC supplies the controlled native-wait timeout input. The
interrupt-frame alias correction reuses this repository's shared-PFN mapper.

[xemu acpi_xbox.c](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/acpi_xbox.c)
was also consulted as a reference lead for unrelated captured workers' GPIO
access. Its default zero read does not qualify the pending `0x80cf` behavior.
No ACPI/GPIO implementation body or fallback value is incorporated here.

## Original device-manager reference execution (2026-10-08)

`pilots/native-title/io_device_lle.c` and
`tools/native_io_device_reference.py` are independently authored bridges executing
the owner's original kernel over a private RAM capture from this project's
full-system LLE. They reuse the already pinned Unicorn 2.1.4 static library and
its retained COPYING/COPYING.LGPL2 notices. No kernel/capture bytes are committed,
installed, or authorized for distribution by this repository.

[Cxbx-Reloaded's IoCreateDevice/IoDeleteDevice](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlIo.cpp)
was consulted for lifecycle questions, including its stated ReactOS provenance.
No GPL/ReactOS implementation body is copied. Differences, such as Cxbx's extra
extension back-pointer, are resolved through original execution.
[Phobos's kernel I/O manager](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/kernel/kernel_io.c)
was inspected and its fake device/stub implementations rejected. The recovered
original XPP initializer uses the already attributed private Phobos `63f18f0`
CPU translator and helpers. Existing private/GPL-dependent pilot restrictions
remain in force; this change introduces no GPL implementation into the intended
non-GPL shipping product.

## Original input page/USB transport references (2026-10-08)

`tools/native_input_dependencies_reference.py` and the private page/USB adapters
execute the owner's original kernel and reuse this repository's maintained
`src/machine/ohci.cpp`/XID implementation. MMIO transport and DMA ownership glue
are independently authored. Existing OHCI behavioral provenance and Unicorn
notices still apply; no original kernel, capture or generated bytes are published.

Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`, `kernel_memory.c` and
`kernel_hal.c`, were consulted for locking/interrupt services. Their no-op page
locking and stub interrupt connection are rejected. Cxbx `585c49a`,
`EmuKrnlMm.cpp`, was consulted for signatures/ownership; no body is copied.
Original execution establishes the locking behavior. Existing private-pilot
restrictions and the intended non-GPL shipping scope are unchanged.

## Original input interrupt/clock bridge (2026-10-08)

`io_device_lle.c`, `usb_lle.cpp` and the native scheduler reuse this repository's
maintained `src/machine/pic.cpp`, OHCI/XID model and the architectural interrupt
frame pattern in `src/cpu/unicorn_floor.cpp`. Their existing behavioral
provenance remains applicable. No new xemu implementation body was copied.
The private original-kernel capture supplies the executed interrupt gates,
IRQL/connect/disconnect/stall and shutdown-registration routines; those bytes
remain owner-supplied and excluded from source/publication.

The actual registered ISR uses the original generated body from the already
attributed Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd` translator.
That checkout's `src/kernel/kernel_hal.c` IRQL transition/dispatch discussion
was consulted for callback eligibility. Its fake interrupt connection is not
adopted. IRQ/PIC transport, callback state preservation and idle-CPU clock glue
are independently authored; `tools/native_input_interrupt_reference.py` runs
the owned software for bounded expected effects. Existing private/GPL-dependent
pilot restrictions remain; this does not authorize a non-GPL product to ship
the interpreter, original software, or incompatible donor components.

## Original input reports, flags and DMA backing (2026-10-08)

The input reference bridge now executes the owner's original pool and virtual
allocator, page pinning and XID callbacks. The two recovered callback bodies
use the previously attributed Phobos `63f18f0` translator; the private game
repository owns their image hashes and recovery extents. No original bytes
are committed. CLI/STI lowering, mapping synchronization and DMA ownership
transport are independently authored. The existing MIT-attributed sp00nznet
host controller mapping and Phobos-inspired replay source now also feed the
LLE peripheral; they do not replace its USB transactions or original driver.
The owned-code `native_input_state_reference.py` observes original report
filtering, per-report packet increments and disconnected output bytes. HLE
corrections follow those observed caller effects; no emulator or original
instruction body is copied into the native input implementation.

The shared APU sample clock uses the 48-samples/ms relationship in the original
USB driver. [Cxbx GetAPUTime](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/DirectSound/DirectSound.cpp)
was consulted for its 48 kHz rate. The pinned xemu and Phobos APU counter paths
use a different rate (xemu marks its conversion uncertain); their conversion
is not adopted. No GPL clock implementation was copied. This is software and
source-reference evidence, not a new hardware measurement.

[xemu's XID gamepad](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/xid-gamepad.c)
was consulted for periodic IN-report behavior and descriptor/capability
structure. No body is copied. Existing private-pilot/GPL dependency limits
remain in force for the intended non-GPL shipping product.

## Original memory and residency qualification

`tools/native_memory_reference.py` and the maintained memory selection execute
the owner's hash-bound original kernel as private input. No kernel bytes or
implementation bodies are shipped. Original execution establishes partial
release, protection/query effects, statistics, pool/contiguous extents and
relocation of unpinned virtual pages during constrained contiguous allocation.
The transport and regression tests are authored here.

Phobos revision `63f18f0334fbd8c00313c4bb565accf00627a4fd` memory services were
consulted for native namespace/allocator design. Its coarse query and no-op
decommit implementations were not adopted. The existing pinned Cxbx memory
manager was consulted for allocator structure; no GPL implementation body was
copied. Existing private-pilot dependency restrictions still apply. See the
[bounded memory contract](pilots/native-title/MEMORY-LLE.md) for evidence and
limits, including the distinction between actual Nt-backed residency and the
later loader/cache/thread family qualification.

## Original filesystem and asynchronous notification qualification

`tools/native_filesystem_reference.py` and `tools/storage_reference.cpp` are
authored qualification transports. They execute the owner's original Nt/FATX
and cache code as private input over the existing IDE/PIC implementations, whose
xemu references and license restrictions remain recorded with those components.
No new xemu/GPL implementation body is imported. This private reference target
is excluded from install/default shipping targets and is not a new non-GPL
distribution claim.

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd`, `src/kernel/kernel_file.c`
and the Fsc bridges in `kernel_bridge.c`, were consulted for native-file
implementation strategy. Their service bodies are not copied. Cxbx
`585c49a50af1255ab155099e06f24505f9c5a800` kernel declarations supply ABI names
and field descriptions; original execution, rather than those implementations,
establishes the recorded return/memory/notification effects. See
[FILES-LLE.md](pilots/native-title/FILES-LLE.md) for exact inputs and limits.

`tools/native_filesystem_contract_reference.py` extends that authored transport
with asserted original metadata/directory/error/namespace observations and real
FATX lifetimes. Phobos's same pinned `kernel_file.c` was reviewed again for its
host-enumeration strategy. Native corrections follow the independently executed
original output/status contract; neither donor nor original implementation
bodies are copied. nxdk declarations supply the counted-string and information
class layouts. The original undersized-directory overflow is recorded as
evidence, with a documented safe native rejection instead of reproducing it.

The original-files runtime adapter additionally consults Phobos's
`kernel_hal.c` and `kernel_thunks.c` at that revision for the ordinal 40 data
export shape. Its value is read from the owned original kernel; the donor's
virtual-disk constant is not substituted. Shared RAM/PIC, file/event ABI
transport and tests are authored here.

The external [mborgerson/fatx](https://github.com/mborgerson/fatx) preparation
tool at `baed16e3a2d62fc247f2bd6a3ed5deffe75da150` (Matt Borgerson and
contributors, GPL-2.0-or-later) prepares a private installed-title HDD. A local
directory-boundary repair and its source identity remain in private preparation
artifacts. `native_hdd_install.py` is an authored orchestration/readback script.
No libfatx implementation body, binary or patch is linked/copied into the
intended non-GPL shipping product. FATX at runtime executes the owned original
kernel over the previously maintained private reference hardware dependency.

## Original GPU completion qualification

`tools/native_gpu_completion_reference.py` is an authored transport executing
owned original XDK routines over the maintained NV2A software reference. It
copies no original code into the repository. Existing nxdk Z-pass wire facts
and authored software-raster fixtures supply geometry/report setup; original
End/kick, callbacks and fence waits supply independent command/ABI observations.

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd`, `hle_d3d8.c` and
`kernel/nv2a_pb_exec.c`, were reviewed for reuse. Its fabricated visibility
fallback, immediate callback and skipped-geometry behavior were not adopted.
No donor implementation body was copied in this checkpoint.

xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`, `hw/xbox/nv2a/pfb.c` and
`nv2a_regs.h`, provide the PFB write-buffer flush-bit/status reference. Our
synchronous coherent-RAM publication uses an authored host memory fence; no
GPL body was copied. Existing private reference dependency restrictions remain.
See [GPU-COMPLETION-LLE.md](pilots/native-title/GPU-COMPLETION-LLE.md).

The whole original-renderer transport (`gpu_lle.inc`) reuses our maintained
MMIO/DMA/PIC interfaces and preserved original AOT bodies. It does not copy a
donor renderer or original title/kernel bytes into source control. The same
pinned xemu revision's `pbus.c`, `pfb.c`, `ptimer.c` and `nv2a.c` supply PBUS
mirror, installed-RAM size, timer wire facts and reset oscillator references.
The [envytools PTIMER documentation](https://envytools.readthedocs.io/en/latest/hw/bus/ptimer.html)
supplies independent counter/ratio/alarm/interrupt expectations for authored
timer tests and implementation. Credit belongs to the respective xemu and
envytools authors/contributors; no implementation bodies were copied. Reset,
PLL and phase limits are recorded with the qualification evidence.

Initialization-control qualification additionally consults nxdk/pbkit commit
`29638d0b001f179b73c3513489af10ddc2986216`, `lib/pbkit/outer.h` and `pbkit.c`:
PCI/ROM/latency controls, inactive overlay parameters, context-table addressing
and the Xbox PLL formula. Credit: Guillaume Lamonoca, espes, Jannik Vogel,
Stefan Schmidt, Lucas Jansson, Erik Abair and other nxdk contributors (MIT).
Only behavior/register facts are used here; no additional implementation body
was copied. The authored original-GPU-reservation probe executes owned kernel
bytes privately and records actual page effects. Envytools PMC documentation
informs the bounded pristine-engine reset; broader lifecycle remains guarded.

## GPU platform behavioral references

GPU platform qualification additionally uses **Advanced Micro Devices,
AMD-766 Peripheral Bus Controller Data Sheet, publication 23167B, March 2001,
page 82** ([document transcription](https://dtsheet.com/doc/261442/amd-amd))
for GPIO register behavior. Applying this compatible-chip layout to MCPX is
explicitly an inference, supported by original Xbox input setup and xemu field
wiring, rather than a new hardware observation. `mcpx_field.hpp` and its tests
are authored here; no document prose or donor implementation was copied.
The pinned xemu PCI register definitions and nxdk/pbkit AGP command names inform
PCI/PBUS coherence and request-depth control.

Nouveau's `nv04_timer_time` in
[Linux commit 9aad54d5c7ae9c0b24ba28d367f5b3b00e751cc5](https://github.com/torvalds/linux/blob/9aad54d5c7ae9c0b24ba28d367f5b3b00e751cc5/drivers/gpu/drm/nouveau/nvkm/subdev/timer/nv04.c)
is a behavioral reference for programming the two timer-counter halves.
Credit belongs to the Nouveau authors/contributors. The maintained counter
write model and tests are authored independently; no implementation body was
copied or linked. Envytools remains the counter-format/alarm reference.

## GPU engine setup and FIFO context references

The authored `nv2a_setup.inc`, `nv2a_fifo_context.inc` and corresponding tests
consult nxdk/pbkit `29638d0b001f179b73c3513489af10ddc2986216` for the Xbox
PGRAPH reset/context-control sequence and three RAM-configuration banks. The
owned original NTSC driver executes privately and supplies its actual setup
sequence; its binary and generated title bodies are not committed here.

[envytools](https://github.com/envytools/envytools/tree/f102b82381f3f11cee113d16374c87091db039d9),
commit `f102b82381f3f11cee113d16374c87091db039d9`, supplies independent field
maps in `rnndb/fifo/nv4_pfifo.xml`, `rnndb/fifo/nv_object.xml`, internal-memory
observations in `hwtest/pgraph_state.cc`, and the
[PFIFO reference-counter contract](https://envytools.readthedocs.io/en/latest/hw/fifo/puller.html#syncing-with-host-reference-counter).
Credit: Marcelina Kościelnicka, Ben Skeggs and the other envytools contributors
(MIT). Register facts and behavioral descriptions were consulted; no
implementation bodies were copied. NV20 observations are not automatically
treated as NV2A silicon proof. Fixed pipeline-mode controls and their remaining
qualification limits are recorded in `GPU-COMPLETION-LLE.md`.

The pinned xemu `pgraph/pgraph.c`, `pgraph/rdi.c`, `pfifo.c` and register
definitions inform context mismatch interrupts and parser fields. Its partial
context READ_IN/WRITE_OUT and ignored unknown RDI writes were not adopted.
Phobos's pinned `kernel/nv2a_pb_exec.c` was checked for applicable FIFO work;
this checkpoint adds no Phobos or GPL implementation body. Existing private
reference-library dependency restrictions still apply.

## GPU bootstrap and tile-boundary references

The physical-zero bootstrap/cache-publication contract uses the pinned nxdk
`pbkit.c` bootstrap sequence and the owned original title/kernel mapping as
independent evidence. Phobos
`63f18f0334fbd8c00313c4bb565accf00627a4fd`, `src/kernel/kernel.h` and
`kernel_bridge.c`, documents the low-physical-page reservation hazard and
physical alias convention. We retain original kernel ownership rather than
copying its fixed bump-allocator reservation. Credit belongs to the respective
nxdk and Phobos authors/contributors; no new implementation body was copied.

Intel's [Software Developer's Manual, Volume 2D, WBINVD entry](https://cdrdv2-public.intel.com/789589/334569-sdm-vol-2d.pdf)
provides instruction/ordering and unchanged-flags behavior. Pinned xemu's
`target/i386/tcg/decode-new.c.inc` was consulted for coherent-emulator treatment.
Our authored lowering verifies original opcode bytes and invokes an actual host
memory fence; it does not copy Intel prose or xemu implementation code.

Tile-boundary research consults pinned envytools `nvhw/tile.c`,
`nvhw/chipset.c` and `hwtest/nv10_tile.cc`; nxdk `pb_assign_tile`; and xemu PFB
and surface handling. XboxDev contributors' [nv2a-trace Texture.py](https://github.com/XboxDev/nv2a-trace/blob/master/Texture.py)
documents use of the linear readback aperture. Cxbx-Reloaded contributors'
`src/common/AddressRanges.h` at `585c49a50af1255ab155099e06f24505f9c5a800`
provide the tiled/write-combined aperture naming reference. These are
investigation leads with unresolved client-specific behavior, not claimed
hardware qualification. No tile implementation was incorporated in this
checkpoint; existing GPL code remains external reference material.

## Adapted envytools tile address layout and public hardware evidence

`src/machine/nv2a_tile_layout.hpp` **contains adapted implementation code** from
envytools `nvhw/tile.c` at
`f102b82381f3f11cee113d16374c87091db039d9`, copyright 2012 Marcelina
Kościelnicka. It specializes the NV20 uncompressed address interleave for the
bounded Xbox geometry and adds checked configuration/overflow handling. The
full MIT permission and disclaimer are retained in the source header.
`hwtest/nv10_tile.cc` and its memory-controller field descriptions also inform
qualification. This is distinct from the earlier reference-only consultations.
The component now drives the software image-blit, clear, scanout and bounded
3D color/depth surface memory
paths. It also adapts the donor's NV20 partition/tag address calculation; that
addition has donor-format evidence but is not qualified by the uncompressed
Xbox blit golden. Tiled texture/vertex fetch and CPU-aperture clients remain
guarded. The 3D integration and rectangle/depth/visibility tests are authored
here using the existing rasterizer; they do not add a donor body or extend the
public capture into a new 3D hardware qualification.

Hardware expectations come from Erik Abair and contributors'
[nxdk_pgraph_tests_golden_results](https://github.com/abaire/nxdk_pgraph_tests_golden_results/tree/06b041d6a50c060b4974d93617405f86a03a0b8f),
`results/Image_blit/BlitBeyondWidth.png` (CC0-1.0). The capture remains unchanged
under private `build/`; maintained tests contain a small set of its expected
pixel values and the verifier checks its pinned hash. No game assets are used.

The authored fixture follows the hardware procedure in
[nxdk_pgraph_tests](https://github.com/abaire/nxdk_pgraph_tests/tree/706a41e61188d3b08e5b1bb2a32b74ad062a883d),
`src/tests/image_blit_tests.cpp:TestBlitPastWidth` (MIT, copyright 2023
nxdk_pgraph_tests contributors). Its RGB source-pattern definition comes from
pbkitplusplus `72600fab67a9972fe2d3a1c6937f64e99dfa5290`,
`src/texture_generator.cpp:GenerateRGBTestPattern`; credit belongs to Erik
Abair and the pbkitplusplus contributors. No donor test implementation body is
copied. The test's pinned nxdk/pbkit at
`e22ab118fe9afbf5a3f51c47d70e9d3a5c516d6d` supplies the historical tile-flag
sequence. Hardware qualification and its limits are recorded in
`GPU-COMPLETION-LLE.md`; no GPL implementation body is incorporated here.

`test_nv2a_tile_commands.cpp` additionally executes the hardware test procedure
through the maintained PFIFO and 2D engines. The client integration and tests
are authored here; the translated addresses use the attributed envytools
component above. Pinned xemu `nv2a.c:nv_clip_gpu_tile_blit` was consulted as a
behavioral reference, while the public hardware capture supplies the clipping
and overdraw expectations. No xemu implementation body was copied. Envytools
`nvhw/comp.c` is additionally adopted in the checkpoint described below.

## Adapted depth-compression decoder

`src/machine/nv2a_zeta_compression.hpp` **contains adapted implementation code**
from envytools `nvhw/comp.c` at the pinned `f102b823` revision above, copyright
2012 Marcelina Kościelnicka. Its complete MIT notice is retained in the header.
The adaptation specializes the NV20 non-MS, little-endian Z24S8 gradient
decoder, including the 8-byte/16-byte payload distinction. The encoder,
compressed-memory integration and command fixtures are authored here.

The donor's `rnndb/memory/nv10_pfb.xml`, `hwtest/nv10_tile.cc` and `nvhw/tile.c`
inform register fields, packed data, and partition/tag selection. Original
owned driver execution and pinned nxdk's `pb_assign_tile` independently show
that Xbox uses the NV20-style ZCOMP register fields; generic envytools chipset
`0x2a` selects a different format family and is not used blindly.

`tools/native_gpu_compression_reference.py` builds unmodified pinned envytools
sources and their generated chipset tables under private `build/`, comparing
the adapted decoder and checking the authored encoder with the original
decoder. This demonstrates donor equivalence, not new NV2A hardware proof.
The public nxdk_pgraph_tests `depth_format_tests.cpp` compression cases were
inspected as future rendered-output references; they have not been reproduced
by this checkpoint. Xbox tag capacity/debug access and exact compressor
representation choices remain explicitly unqualified. No new GPL
implementation body or game asset is incorporated.

## Original AV / encoder-control reference audit

The GPU LLE AV bridge reuses this repository's maintained SMBus/PIC transport.
`src/machine/cx25871_control.hpp` and its tests are authored from register facts
in [Conexant CX25870/871, 100381B, September 2001](https://datasheet.octopart.com/CX25871-15-Conexant-datasheet-180221357.pdf),
sections 2.3/2.4 and tables 2-1/2-5. The document is a behavioral specification;
neither its document contents nor vendor software is redistributed. Its
readback-enable requirement prevents assuming an all-zero captured encoder.

[AMD-766 23167B, March 2001](https://dtsheet.com/doc/261442/amd-amd), pp84-86,
informs the corrected status W1C and command-strobe behavior in `smbus.cpp`.
Pinned xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/xbox/amd_smbus.c` and `smbus_cx25871.c`, were inspected for behavioral
comparison; these individual source files carry espes's MIT notices. No new
implementation bodies were copied. Their unrestricted encoder storage and
zero-write completion behavior are not qualification evidence and were not
adopted for the strict AV floor.

Pinned Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd`,
`src/kernel/kernel_hal.c`, was inspected. Its option numbering/null-output
handling differs from the owned original kernel and nxdk's video definitions;
that AV switch was not incorporated. Original kernel binaries and generated
observations remain private. This checkpoint does not qualify analog output.

The encoder-boot provenance follow-up consults nxdk `29638d0`'s
`lib/pbkit/outer.h` and `pbkit.c` for the boot DEBUG_7 value `0008CFFF`.
Pinned xemu `hw/xbox/nv2a/pgraph/gl/draw.c` documents a fragment-limit check
controlled by bit 11. That behavior is not implemented by this checkpoint:
only the known register value's transport is admitted, and GPU command
execution under it stops explicitly. The existing title configuration remains
separate. No xemu implementation body was copied.

Conexant 100381B section 1.3.4 and the `SRESET`/`TIMING_RST` descriptions in
table 2-5 distinguish software-register reset from timing reset and specify
self-clearing commands. This prevents treating the legacy encoder's stored
last-write bytes as a hardware snapshot. The newly recorded boot write journal
is software-execution evidence only; its private contents are not shipped or
silently installed as title state.

The bounded encoder reset/autoconfiguration implementation additionally uses
sections 1.3.4/1.3.8 and table C-1's factual 13.500-MHz mode 0/1 register values.
Table 2-5 establishes the shared DIV2 latch. Conflicting EACTIVE/CHECK_STAT
descriptions remain unknown until explicit driver programming. This code was
authored from hardware facts; no donor implementation body, vendor source,
private kernel table or game asset was copied. Pinned nxdk's
`lib/xboxkrnl/xboxkrnl.exe.def` corroborates original HAL export ordinals 45/50
and their four-argument stdcall interface; the actual owned routines execute in
private qualification fixtures. Timing reset and encoded output are not
qualified by software-register reset/readback.

The encoder's settled clock-rate contract additionally uses Conexant 100381B
sections 1.3.6/1.3.6.1 and table 2-5 for the PLL equation, input/encoder clock
separation, 3:2 mode, DIV2, HALF_CLKO, asynchronous input and EN_OUT/external
clock pin gating. Exact rational calculations and tests are authored here;
no vendor programming-example code is incorporated. This frequency query does
not assert PLL settling, timing-generator execution or analog output.

`src/machine/cx25871_timing.hpp` and its control/transport tests additionally
implement the bounded synchronous timing contract from sections 1.3.4,
1.3.26/27 and 1.3.31: odd-field reset origin, horizontal/vertical progression,
subcarrier accumulation and four/eight-field phase reset, progressive slave
HSYNC/VSYNC synchronization, two-CLKI FIELD delay, polarity and output gating.
Table C-1 supplies independent PAL/NTSC counter and increment expectations.
This is an authored event-driven model, not copied vendor or emulator code.
Tests supply explicit board clock/sync events; connection to the title's
actual NV2A timing source, asynchronous/fractional
timing, PLL settling, filtering and analog waveforms remain unqualified.

The subsequent slave-phase extension additionally uses sections 1.3.14/15:
VSYNC precedes or coincides with the HSYNC starting the progressive frame.
Independently authored tests connect the existing FP timing component at
explicit active-mode origins and exercise the original HAL/AV caller boundary.
Unspecified transient raster/color phase is tracked as unknown, not inferred
from FIELD. This does not establish the guest mode-activation transaction.

[XboxDev Cromwell](https://github.com/XboxDev/cromwell/tree/86f547387184d1001d377cce97d3756c8acf91cc),
revision `86f547387184d1001d377cce97d3756c8acf91cc`, was inspected on 2026-10-09:
`drivers/video/BootVgaInitialization.c` and `drivers/video/VideoInitialization.c`
show GPU programming followed by encoder configuration, timing reset and
output enable. Credit belongs to the Cromwell authors and contributors.
Its GPL-2.0 source remains an external behavioral reference; no code bodies,
tables or linked dependencies were incorporated. It did not resolve FD6806A0.

## AV GPIO and digital screen-off behavioral references

`src/machine/mcpx_av_outputs.hpp`, the NV2A sequencer screen-off behavior and
their transport/tests are authored from the following hardware facts:

- [NVIDIA MCP-1 0.15, August 2000, section 5.15](https://consolemods.org/wiki/images/1/14/MCP_1x_DS_015.PDF-1.PDF):
  contiguous GPIO19/22/24 port locations. AMD-766 23167B p82 and original AV
  programming corroborate 04/05 static-output encoding; AMD's different port
  map is not used for these Xbox pins.
- [Xbox Hardware Design Specification 1.02, figures 16/55](https://consolemods.org/wiki/images/a/ae/Xbox_Hardware_Design_Specification_1.02.pdf):
  GPIO19/22 SCART wiring and LVLCNT truth table.
- [Retail launch schematics, indexed GPIO table](https://consolemods.org/wiki/images/e/e5/Launch_Schematics.pdf):
  GPIO24 unused test point. This is document evidence, not a new board probe.
- [IBM VGA/XGA Technical Reference, May 1992, p2-49](https://bitsavers.trailing-edge.com/pdf/ibm/pc/cards/IBM_VGA_XGA_Technical_Reference_Manual_May92.pdf):
  sequencer Clocking Mode bit 5 stops picture fetches while sync continues.
- Nouveau at Linux **v6.12**:
  [hw.h, NVVgaProtect](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/nouveau/dispnv04/hw.h),
  copyright 2008 Stuart Bennett, and
  [nvreg.h, NV_PRMVIO_SRX/SR](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/nouveau/dispnv04/nvreg.h),
  copyright 1996-1997 David J. McKay. These individually MIT-noticed files
  corroborate the screen-off control and NV register addresses. No function
  bodies were copied or adapted.

Documents, vendor code and original kernel bytes are not redistributed. The
implementation does not claim analog output, whole GPIO register semantics or
complete original display-mode initialization. The existing Phobos AV switch
audit remains applicable; no donor shortcut replaces the original option 9
execution used here.

MakeMHz/Dustin Holden's [Project Stellar 1.8.0 hardware investigation](https://makemhz.com/blogs/news/project-stellar-firmware-update-1-8)
(April 28, 2025), including its public notes/code screenshots, was inspected as
a further display-timing reference lead. No pictured implementation or tables
were copied, no proprietary source was obtained, and the article does not
qualify the unresolved FD6806A0 handshake.

The follow-up board-clock audit uses Xbox Hardware Design Specification 1.02
sections 2.4.1.5 and 2.8.1 for encoder-to-GPU PIXCLK and GPU-to-encoder sync
direction, plus the already pinned envytools `rnndb/display/nv3_pramdac.xml`
for FP timing/control register names and fields. Existing private original-boot
write records supply exercised values only. No new implementation is imported;
these facts do not qualify active timing-bank selection or the undocumented
FD6806A0 transaction. Public documents and private captures are not redistributed.

## Isolated NV2A digital timing component

`src/machine/nv2a_fp_timing.hpp` uses factual FP total/sync/valid-region encodings
and polarity fields from envytools `f102b82381f3f11cee113d16374c87091db039d9`,
`rnndb/display/nv3_pramdac.xml`, corroborated by Nouveau at Linux **v6.12**:

- [dfp.c](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/nouveau/dispnv04/dfp.c),
  individually MIT-noticed, copyright NVIDIA (2003), Dave Airlie (2006), Maarten
  Maathuis (2007), Stuart Bennett (2007-2009). SHA-256 of consulted file:
  `aeae4c211be96acf03aec4a8a6603898970ba5d19ff1863672a17448c2f9fe90`.
- [nvreg.h](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/nouveau/dispnv04/nvreg.h),
  individually MIT-noticed, copyright David J. McKay (1996-1997). SHA-256:
  `c2c46c2f5a2d4e1039074d42e32e1354f8388ae17011e7360c95bd9b3ae91921`.

The event traversal and independent dense-clock test are authored here; no
driver function bodies are copied or adapted. PC driver programming establishes
encoding evidence, not NV2A silicon phase or activation semantics. The component
requires an explicitly established active mode/origin; it does not implement
FD6806A0, timing-bank readback, pixel scaling/serialization or board clock routing.

## Tiled color texture integration and sampling contract

The linear color client in `src/machine/nv2a.cpp` reuses this repository's
already attributed tile mapper and software sampler. The new command and
native-backend cases are independently authored; no external implementation
body is imported by this change.

- [nxdk/pbkit](https://github.com/XboxDev/nxdk/tree/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit),
  revision `29638d0b001f179b73c3513489af10ddc2986216`: `pbkit.c` supplies the
  PFB/PGRAPH/RDI mirror programming reference; `nv_regs.h` identifies linear
  color formats, DMA selection and texture shape fields. Existing notices apply.
- [nxdk_pgraph_tests](https://github.com/abaire/nxdk_pgraph_tests/tree/706a41e61188d3b08e5b1bb2a32b74ad062a883d),
  MIT, copyright nxdk_pgraph_tests contributors: `texture_render_target_tests.cpp`
  informed the render/write/sample-again scenario. Its shadow-comparator tests
  redirect depth writes to separate texture memory and do not establish active
  tiled-texture behavior. Existing CC0 blit goldens remain 2D evidence only.
- [Phobos texture resources](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/d3d/d3d8_resources.c)
  were consulted for native XRGB storage selection. The test exercises our
  already attributed pinned private donor build; no new Phobos body is copied.
- [XboxDev nv2a-trace Texture.py](https://github.com/XboxDev/nv2a-trace/blob/master/Texture.py)
  was inspected on 2026-10-09 as a memory-layout reference lead. Its CPU aperture
  comments do not establish GPU texture-client semantics, and no code is reused.

The bounded integration composes existing modeled surface/texture behavior;
these references do not replace a missing hardware texture capture. Existing
private-pilot/GPL dependency restrictions remain in force for the intended
non-GPL shipping product.

## Original-scheduling title bootstrap

The experimental original-scheduling bootstrap additionally consults nxdk
`lib/xboxkrnl/xboxkrnl.h` at the already pinned revision for `OBJECT_TYPE`'s
28-byte shape. The original export, object, thread and GPIO programming execute
from private owned inputs. The loader and handoff adapter are authored here;
no donor kernel or loader body is incorporated. See
[SCHEDULING-LLE.md](pilots/native-title/SCHEDULING-LLE.md) for the guarded
startup result and remaining integration limits.

## Bounded APU configuration floor

The selected-voice configuration handler, strict `tools/apu_reference.cpp`
transport and original-code fixture are authored here. They consult:

- [xemu APU register definitions](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/apu_regs.h)
  and [voice processor](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/vp/vp.c)
  for factual method locations, masks and guest-memory effects. These files
  credit espes (2012), Jannik Vogel (2018-2019) and Matt Borgerson (2019-2025;
  the register header ends at 2021), under LGPL-2.0-or-later notices. No
  implementation body is copied or adapted by this batch.
- [Phobos buffer model](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/hle/dsound_buffer_model.c),
  which credits NoRain211's `doaxbv-re` buffer model and carries GPL-3.0
  provenance. Its position/frequency/host-clock strategy was reviewed;
  no new body is incorporated.
- The owner's hash-bound original NTSC DirectSound helpers and command emitter,
  executed/inspected privately. No original bytes or generated private output
  are committed or distributed.

See [AUDIO-LLE.md](pilots/native-title/AUDIO-LLE.md) for the bounded evidence.
Register programming is not a sample-processing or whole-title qualification.
Existing older APU/dependency attribution and distribution restrictions are
unchanged; this batch does not certify the entire machine as non-GPL.

The subsequent PCM source stage consults those same xemu files for scatter/gather
addressing, format fields and source-cursor behavior, plus `fpconv.h` (LGPL-2.0-
or-later, copyright Matt Borgerson 2020-2025) for normalized PCM conventions.
The implementation, page-splitting checks, waveform/index expectations and
real native-mixer comparison are authored here; no source-processing body is
copied or adapted. Original region arithmetic and byte/sample helpers execute
from the owner's private NTSC image. The independently observed inclusive end
index drives per-sample handling instead of inheriting xemu's chunk-dependent
end comparison. This is not a new hardware timing or interpolation measurement.

Phobos's `audio_output_xaudio2.cpp` at `bcccf8c` was revisited for its actual
`SamplesPlayed` cursor observation. Our already attributed native transport is
reused unchanged. The audit also identified xemu's explicitly provisional
notifier information byte: our older literal-byte/SSLA assumption is removed,
and the unsupported completion boundary now fails explicitly. Neither that
placeholder nor native audible success qualifies an LLE notifier contract.

The subsequent final 2D send-stage implementation consults the same pinned
xemu register/voice references for packed routing/attenuation, channel-slot
selection and submix headroom. It is independently authored. The owner's
complete original format and volume helpers supply additional private evidence,
including the S24+ADPCM format encoding absent from earlier authored fixtures.
The native mixer implementation is reused unchanged; documented output
tolerances permit continuous HLE gain arithmetic. See the expanded audio
qualification for stage boundaries and missing upstream processing.

[libsamplerate 0.2.2](https://github.com/libsndfile/libsamplerate/tree/0.2.2),
copyright Erik de Castro Lopo and upstream contributors, was initially consulted
as a resampling candidate. The subsequent source-rate stage now links its
unmodified **BSD-2-Clause library** at commit
`c96f5e3de9c4488f4e6c97f59f5245f22fda22f7`. Full license text is retained in
`LICENSES/libsamplerate-BSD.txt`; pin/hash/build scope are recorded in
`src/third_party/libsamplerate/README.md`, and both accompany built adapter
artifacts. Its GPL developer-only `src/check_asm.sh` is neither built nor
packaged. The Xbox DMA/lifecycle adapter is authored here; no xemu resampler
wrapper or starvation-silence body is incorporated. Pinned xemu's acknowledged
interpolation uncertainty is retained rather than relabeled as hardware proof.

The maintained envelope/control stage additionally consults
[XboxDevWiki's APU research](https://xboxdevwiki.net/APU#Envelopes), credit to
the XboxDevWiki contributors, and the same pinned xemu register/phase reference.
The new quantum processor and original-code control fixture are authored here;
no wiki prose or GPL function body is incorporated into runtime code. Reference
uncertainties about decay/release and exceptional attack behavior remain explicit
unsupported boundaries. Phobos's stated incomplete envelope handling is not
adopted as an implementation. Complete original NTSC initialization/start code
supplies private command/owner evidence; its bytes are not committed.

## dr_wav: maintained ADPCM source decoder

[dr_libs / dr_wav](https://github.com/mackron/dr_libs/tree/dfe8377631000664666519fdb83da193fd8037f4),
by David Reid (mackron), revision `dfe8377631000664666519fdb83da193fd8037f4`:
`src/third_party/dr_wav/dr_wav.h` and `LICENSE` are unmodified upstream files.
We use the offered **MIT No Attribution (MIT-0)** option while retaining full
credit and license. Exact file hashes are in the adjacent README. The separate
authored `src/machine/apu_adpcm.c` adapter uses the public memory WAV interface
for Xbox blocks; the donor implements the codec. Native-pilot artifact notices
include this donor's license and provenance.

Xbox block consumption and header policy consult the already pinned xemu/Phobos
behavioral references. The existing GPL/LGPL private decoders are separately
linked into comparison tests only; no body from them is incorporated into this
new adapter or vendored donor. These references are not independent hardware
measurements. The combined pilot's earlier licensing restrictions still apply.
See [audio qualification](pilots/native-title/AUDIO-LLE.md) for exact scope.

## Original audio stop and notifier reference

The 2026-10-09 static audio stop/status/unlink batch also consults pinned xemu
`482088490c2d5983ed5e1eafbd09906157c1eca0` for voice/notifier register layout,
and Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` for stop/detach lifecycle
behavior. The device handler and fixtures are independently authored. Owned
original XDK/kernel routines execute as private qualification inputs; no original
instruction bytes are committed. xemu's provisional notifier payload write is
not copied. No new GPL implementation body is incorporated; the existing
private-pilot license limits remain. [Contract and evidence](pilots/native-title/AUDIO-LLE.md#original-static-buffer-stop-and-unlink---2026-10-09).

The subsequent finite-buffer composition and original completion fixture reuse
these references plus the already attributed dr_wav/libsamplerate components.
Its source/rate/envelope/send wiring, output comparison and retirement adapter
are authored here. Original unlink and completion routines are executed only
from the owned private XBE. xemu's source-fetch retirement timing is explicitly
distinguished from the nominal converter-drain model; neither is claimed as a
new hardware timing observation. [Scope and tests](pilots/native-title/AUDIO-LLE.md#finite-buffer-processing-and-original-completion---2026-10-09).

The original audio IRQ/queue batch also reuses this repository's maintained PIC
and existing display-family private kernel transport. Its Python harness and
APU/PIC adapter are authored here. Original kernel/XDK bytes remain private
qualification inputs. Pinned xemu's `apu.c` FEMEMDATA comment and the audited
Phobos/sp00nznet/NoRain APU cores were compared for the fence question; their
shared provisional immediate-write behavior is not accepted as independent
qualification or copied into this batch. The older immediate write in the
maintained APU was removed and the unresolved boundary guarded. [Scope](pilots/native-title/AUDIO-LLE.md#original-irq5-unlink-and-deferred-work-queue---2026-10-09).
Audited APU-core pins: Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`,
sp00nznet `b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4`, and NoRain211
`74649ef5b0f023e7b878043bc6de2534c55e717e`.

## Packed vertex color qualification

The BGRA correction in `src/machine/nv2a.cpp` and tests in
`tests/test_nv2a_vertex_colors.cpp` and the native graphics fixture are authored
here. No new external implementation or dependency is imported.

- MIT [nxdk_pgraph_tests](https://github.com/abaire/nxdk_pgraph_tests/tree/706a41e61188d3b08e5b1bb2a32b74ad062a883d),
  copyright its contributors: `src/tests/surface_as_vertex_array_tests.cpp`
  informed the rendered-surface-as-diffuse and successive-update scenarios.
  The independent fixture uses bounded GPU clears and locally authored packets.
- GPL-2.0-or-later xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
  `hw/xbox/nv2a/pgraph/{gl,vk}/vertex.c`, was consulted only for the distinct
  BGRA/RGBA input-format behavior. No implementation body is copied or linked
  by this correction; source inspection is not a new runtime comparison.
- Existing CC0 Image_blit captures do not cover this vertex-color procedure.
  No matching hardware golden is newly incorporated or claimed.

Exact qualification and limits are in
[GRAPHICS-LLE.md](pilots/native-title/GRAPHICS-LLE.md).

The subsequent original declaration/fetch qualification additionally consults
[XboxDevWiki vertex attributes](https://xboxdevwiki.net/NV2A/Vertex_attributes),
credit to its contributors, and the same pinned xemu vertex-format conversions
for signed/normalized short and CMP inputs. Only behavioral facts are used;
no wiki prose or GPL implementation body is copied into the harness or runtime.
Raw input corpus and mathematical expectations are authored here. Original
XDK functions execute from the owned private Blinx XBE; their instruction bytes
and generated captures are not committed. The new native checks reuse the
already attributed renderer and shader translator without a new dependency.

The original constant/cache/query follow-up uses the same owned XDK input and
already attributed maintained/native shader implementations. Its harness and
expected cache effects, input values and MOV-output expectations are authored
here. It adds no donor implementation, original instruction bytes or dependency.

## Theseus behavioral reference

[MrMilenko/Theseus](https://github.com/MrMilenko/Theseus/tree/5fc7ddddd03b4b5a7e6233769ad6a8fe46009965),
revision `5fc7ddddd03b4b5a7e6233769ad6a8fe46009965`, was inspected as a
behavioral reference at the owner's request. Credit belongs to its authors
and contributors. Its dashboard reverse-engineering notes, XAP interface
inventory and desktop source informed the
[reference assessment](docs/DONOR-SERVICE-ADOPTION-2026-10-07.md#theseus-reference-audit-owner-follow-up-2026-10-07).
The repository declares GPL-3.0-or-later and includes separate component
notices. No Theseus implementation, assets or linked dependency were added by
this assessment; the external checkout is research material only. This entry
records consultation, not permission to incorporate GPL code into the intended
non-GPL product.
