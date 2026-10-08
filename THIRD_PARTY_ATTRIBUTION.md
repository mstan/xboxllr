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
