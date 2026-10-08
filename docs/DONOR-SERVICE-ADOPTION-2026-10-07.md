# Whole-service donor adoption for Blinx

Owner direction: survey and reuse services together, especially renderer and
audio. Keep the console LLE basis; do not make pixel-perfect rendering or
hardware-internal equivalence an admission requirement for native services.
Tracking: `beads-xbox.1.62`; branch `audit/donor-services-20261007`.

## Decision

Proceed with a **Phobos renderer + native audio service package**, using our
existing Xbox-facing ownership, memory, file, thread and completion interfaces.
Keep the original compiled Sofdec path for Blinx's movies initially. Preserve
our installed-media, section residency and console services; replacing those
wholesale would lose already established behavior without a demonstrated gain.

This is feasible without putting a second live console inside Phobos. The
renderer and audio output both build as separate native modules. Their adapters
still need work. Passing the host-module probes is not guest-ABI qualification,
and a donor playing a level is not proof of our complete-attract acceptance.

Use whole subsystem choices fixed by the build. Retain the machine LLE build
and bounded original-routine/reference fixtures. HLE owns its native objects;
only observable guest fields must remain compatible. No shadow execution in
production, no requirement to recreate every device register for a future
handoff, and no instruction-history comparison as a general acceptance gate.

Small image/numerical differences may be documented tradeoffs. Missing draws,
missing audio, fabricated completion and movie skipping are not tolerances.
CPU-visible resource bytes, ownership and callback ordering remain contracts.

## Refreshed sources

All three requested repositories were fetched into new isolated worktrees.
Previous pins and the uncommitted presentation experiment remain intact.

| Donor | Current reviewed commit | Finding |
| --- | --- | --- |
| [Phobos](https://github.com/phobos665/xboxrecomp/commit/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0) | `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0` | New commit says Blinx support, but its **tree is identical** to our previous `63f18f0`: `f0904620dc78fa5519ee13dcdc6d6e6a8a6ac928`. No newly landed implementation to explain away our current gap. |
| [sp00nznet](https://github.com/sp00nznet/xboxrecomp/commit/b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4) | `b3700e1d60bcd9c3dfaad734f45149b6f0bc5cb4` | v0.13 includes new guest VM work and CPU/lifter fixes. Its graphics/audio/video/input source trees are unchanged from our `1409a7d` pin. |
| [NoRain211 fork](https://github.com/NoRain211/xboxrecomp/commit/74649ef5b0f023e7b878043bc6de2534c55e717e) | `74649ef5b0f023e7b878043bc6de2534c55e717e` | Framework/lifter donor. The title-specific service models cited by Phobos live in **doaxbv-re**, not in this fork's `src/hle`. |

Also inspected the existing doaxbv-re reference at
`aa1d3c8afcd8d063171d819e27307070b63f056b`: DirectSound/input service models,
CRI batching adapter and movie research. Its CRI adapter contains DOAXBV
addresses and runs original guest service routines; it is not a transferable
native Sofdec decoder.

This feasibility pass assumes permitted reuse, as requested. The executable
probes link the external reference checkout and stay outside product builds;
they do not change the intended product's source or license boundary.

## Service adoption map

| Service family | Borrowable implementation | Adaptation / boundary | Decision |
| --- | --- | --- | --- |
| Renderer, shaders and resources | Phobos `src/d3d`: D3D11/RHI, vertex and combiner translators, resource conversions, targets, feedback copies, shader/state caches, movie surfaces | Keep Xbox ABI adapter. Separate host resource objects from guest headers; preserve CPU-visible locks/readback, aliases and lifetimes. Its HLE wrappers normally call original SDK bodies and then host work; extracting the backend avoids taking that shadow architecture wholesale. | **First adoption package**, whole native renderer. Broader material/state coverage together, not another sequence of isolated math optimizations. |
| Graphics submission and pacing | `src/hle/hle_d3d8*.c`: UP/indexed/buffer draws, nonzero declaration streams, render-target feedback, pacing rationale | Translate guest enum/count/layout once. Remove unsupported-draw skipping. Native GPU completion feeds our fences/callbacks; original Swap cannot own progress for a separate native device. Our state-block ownership work remains useful. | Adapt alongside renderer. Pixel-perfect comparisons are not a prerequisite; actual required draws, correct resource access and repeatable progress are. |
| Audio buffers and output | Phobos `src/hle/audio_output_xaudio2.cpp`, `dsound_buffer_model.c`, `xbox_adpcm.c`; NoRain's corresponding models | Guest buffer/interface layout differs by SDK. Phobos buffer state can advance for formats it cannot play. Keep real sample consumption and our effects graph; qualify loop/seek/volume/pitch as one family. | **Same first package**. Existing XAudio2/DSP path need not be replaced merely to change donors; reuse missing mechanics where they fit. |
| Audio streams | `src/hle/hle_dsound_stream.c`: packet queue, host-position feedback, completion/status/callback shape, backpressure | Remove silent completion when output/format is unavailable. Bind exact object/interface relationship instead of nearest-address guessing. Future-timestamp FlushEx, sync playback, 3D/envelopes are incomplete. | High-value adoption with buffers. Preserve packet data until consumed or explicitly cancelled; callback exactly once with truthful status. |
| Blinx movies and CRI service loop | Phobos's working route keeps compiled Sofdec sections; NoRain CRI batching/service-boundary findings | Local installed files contain SFD and ADX. Recover NTSC callbacks from our image; donor seeds refer to a different layout. Native graphics/audio can serve the original decoder. | **Retain original decoder initially**, accelerated I/O/render/audio beneath it. Do not stall renderer/audio adoption on inventing a movie decoder. |
| General XMV movies | Phobos `src/video/xmv_demux.c`, `xmv_decode.c`, `hle_xmv_play.c`: real decode, queued audio and guest-surface delivery | `hle_xmv.c` still reports early completion when decoding cannot start, and exposes zero guest audio streams. Current Blinx has no exact XMV HLE-name matches. XMV is not Sofdec. | Keep as a later shared-family candidate. Require real decode or truthful failure; do not import its fake-done path. |
| Input | Phobos/NoRain input models; host SDL3/XInput mappings, four ports, hotplug and rumble | Our NTSC adapter already preserves its 22-byte state, 25-byte capabilities and feedback layout. Keep exact ABI packing and real connection/error results. | Reuse controller breadth later; retain current working service during initial renderer/audio adoption. |
| File/storage and installed-media auth | Donor `kernel_file.c`, `kernel_path.c`, object-path/directory findings | Keep sandboxed title mounts, real bytes/positions/errors, save durability, handles and cancellation. Installed media remains the approved alternative to physical authentication; no fabricated optical replies. | Selective extensions where actual Blinx needs them. No reset to a donor success-only import table. |
| VM and XBE residency | sp00nznet v0.13 `guest_vmem.c`; Phobos demand-section discoveries | Phobos's Blinx route eagerly maps demand-loaded assets with extra address space; its notes admit reload does not restore section bytes. Our reserve/commit/free and load/unload path already has stronger relevant evidence. | Retain current implementation; borrow fixes/tests. Do not replace the console memory contract to acquire a renderer. |
| Threads, timers, DPCs and events | Donor Win32 bridges, ABI fixes and scheduler observations | Guest TLS/registers, return conventions, suspension, event state and actual completion remain shared obligations. Raw driver paths still need a bounded hardware model. | Keep current console services; extend from donor evidence as needed. This is the less replaceable foundation. |
| APU/DSP and raw GPU device access | Emulator references, our maintained machine models and permissive DSP/NV2A helpers | Phobos's NV2A library says PGRAPH is counted rather than emulated. Its `src/audio/dsound_device.c` explicitly contains no-op sound methods. Neither establishes a faithful console floor. | Retain our floor and effects implementation. Native rendering/audio replace those service families; unhandled raw access must not silently succeed. |
| CPU, discovery and bindings | All three: function boundaries, x87/MMX/flags, `ret N`, library naming and data-section classification | Keep full-image identity, original return/register ABI and revision-specific bindings. Exact symbol presence is not coverage; unresolved indirect calls cannot return fabricated success. | Continue selective reuse. Current private generated CPU pilot already uses Phobos; no new full lift required for this survey. |

## Applied qualification this pass

The standalone [probe project](../pilots/donor-services/CMakeLists.txt) links
Phobos's actual sources. No kernel, device-accessor or audio-output substitute
was added to satisfy linking. Renderer extraction needed one real helper,
`src/kernel/nv2a_vsh.c`, in addition to `src/d3d`, platform types and config.

Both Release executables built successfully. With an isolated empty display
configuration and shader-disk caching disabled:

| Probe | Observed result | Scope |
| --- | --- | --- |
| Native audio output | **PASS**: three voice lifetimes, 19,200 bytes each at 48 kHz stereo PCM16; actual `SamplesPlayed` consumption, zero queued at completion, 57,600 bytes submitted, zero drops | Real host audio device and transport. Silent PCM by design. Not audible title audio, ADPCM correctness, guest stream callbacks or DSP equivalence. |
| Native renderer surface pixels | **FAIL**, retained openly: D3D11 device created and shaders compiled; clear `0xff204060` reads back as `0xff604020` through the surface advertised as X8R8G8B8 | Backend extraction succeeds; guest-facing channel order needs adaptation. Scene image is RGBA, surface wrapper reports X8R8G8B8, lock exposes native bytes. This is not a reason to abandon the whole renderer or demand identical final images. |

The renderer failure is not marked expected-pass, fixed by relaxing the pixel
assertion, or claimed as title progress. It is recorded for the integration
adapter. No attempt was made to qualify every renderer method in this pass.

Final CTest result: **1/2 passed**, 1.80 seconds; both binaries built with exit
zero. Logs remain under `build/donor-services/`, including
`qualification.log` and `Testing/Temporary/LastTest.log`.

The [reproducible inventory](../tools/donor_service_inventory.py) records
[source pins, family sizes and named entry locations](evidence/donor-services-20261007.json).
It finds 89 Phobos HLE markers and 48 exact name matches in our NTSC Blinx
symbol file: 31 graphics, 10 sound and 7 input. Audio streams and XMV have
zero exact matches, which requires binding investigation if reached; it is
not evidence the title never uses streaming audio. Forty-five of the 47
graphics markers declare original bodies in their source file. This directly
shows why the HLE wrapper layer is not a drop-in independent backend.

The other two framework trees have no such `HLE_EXPORT` layer. They still
contain native backend code; zero markers is not zero reusable functionality.
These counts are not a percentage of title support or dynamic reachability.

## Next integration batch and stop conditions

1. Keep the existing console runtime, installed-media mount and current Blinx
   bindings. Add the whole Phobos renderer as a separate build-selected backend;
   route resources, states, shaders and draws through it. Carry forward our
   owned guest objects and completion adapter. Handle the measured channel-order
   boundary and reject skipped required draws without imposing pixel perfection.
2. In the same batch, expose native buffer/stream support behind the audio ABI,
   borrowing packet queues, real host progress and backpressure while retaining
   our effects path. Qualify storage, play/stop/seek/loop and completion together.
3. Run fresh/repeat original NTSC Blinx with both selected. Judge the batch by
   actual progression beyond the loading frame, presented images, consumed title
   audio and the next reached boundary. Keep original Sofdec decoding until an
   observed movie problem warrants a separate decoder replacement.
4. Only then spend effort on input breadth, additional file/VM/kernel cases or
   another movie format. No JSRF work before the serial Blinx goal is validated.

If a module needs a fabricated completion, silently loses required content,
cannot preserve resource lifetime, or still requires duplicate rendering,
record that concrete boundary and adapt it. Do not turn it into another broad
LLE rewrite. If a complete donor implementation works with acceptable output,
adopt it without reimplementing its internals to resemble hardware.

This pass changes no title runtime. Blinx's last validated result remains its
first loading image; the separate presentation experiment remains uncommitted
and stops at the host thread-naming diagnostic. No new menu, attract or speed
result is claimed.

## Blinx asynchronous write service (2026-10-07)

`.86` reuses our existing native asynchronous read/completion owner for writes.
Phobos `kernel_file.c` supplies the host-file and buffering approach, but its
pending/APC omissions are not adopted. Original XAPI submission and KeEvent
fixtures plus ATA gather/write/readback form the bounded reference. This is
behavioral reuse, with no copied donor body. See the
[contract](../pilots/native-title/README.md#asynchronous-writes-and-installed-hdd-files).

## Blinx Xbox ADPCM adoption (2026-10-07)

`.85` borrows the unchanged Phobos `src/hle/xbox_adpcm.c/.h` (NoRain DOAX
provenance) at the same pinned revision into the private GPL-dependent pilot.
The external APU decoder is a separate executable reference; the original
NTSC MCPX helpers establish units and ABI. The existing device-driven
XAudio2/DSP engine remains responsible for playback, cursors and completion.
The adapter handles compressed storage, block-dependent partial writes and
byte/PCM conversion. Donor clock-only and silent fallback behavior is excluded.
See the [contract and validation](../pilots/native-title/README.md#xbox-adpcm-buffers)
and [attribution](../THIRD_PARTY_ATTRIBUTION.md#xbox-adpcm-buffers---2026-10-07).

## Blinx render-target feedback adoption (2026-10-07)

Adopted in the private pilot (`beads-xbox.1.84`): Phobos
[`src/d3d/d3d8_device.c`](https://github.com/phobos665/xboxrecomp/blob/bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0/src/d3d/d3d8_device.c)
`feedback_scratch/begin/end` documents Blinx shadow compositing that samples
the current render target. It caches a matching GPU scratch image and copies
the target before the draw. Actual capture confirms Blinx's aligned pixel-local
scene pass. Bounded NV2A pixels and native copy/cache/alias/lifetime fixtures
pass, and actual title execution advances to Xbox ADPCM buffer creation (.85).
Our adapter keeps unsupported layouts guarded, stops failed copies and releases
scratch on teardown. No Theseus code is used for this.

The next audio candidate is Phobos `src/hle/xbox_adpcm.c` and its buffer/stream
handling; that decoder attributes NoRain211/doaxbv-re. Track the reached format,
LLE contract and full provenance in `.85` before enabling playback.

## Theseus reference audit (owner follow-up, 2026-10-07)

Tracking: `beads-xbox.1.74`. Reviewed
[MrMilenko/Theseus](https://github.com/MrMilenko/Theseus/tree/5fc7ddddd03b4b5a7e6233769ad6a8fe46009965)
at `5fc7ddddd03b4b5a7e6233769ad6a8fe46009965`, in the separate read-only
reference checkout `../refs/Theseus-review-20261007`. No source, assets or
executables were imported into this project, and no Theseus code was built.
The owner's direction is behavioral reference only.

Theseus reconstructs the dashboard engine for Xbox and native desktop. The
current desktop D3D8-shaped shim uses bgfx; desktop Xbox game launches use xemu.
Its value here is dashboard semantics and subsystem boundaries. It does not
supply a general Xbox machine reference or establish Blinx compatibility.

| Reference | Applied use | Qualification / limit |
| --- | --- | --- |
| `docs/decomp/README.md` and subsystem notes | Map dashboard profiles to script VM, scene traversal, animation, assets and audio families before designing another small helper optimization. | Cross-check our binary revision and original routines. Their 5960 addresses are revision bindings, not shared runtime constants. The index explicitly excludes D3DX, DirectSound mixer, MCPX, AC97 and kernel video drivers. |
| `docs/xap-contract.md` | Inventory script-visible inputs, outputs, properties and callbacks when considering a whole native dashboard service. | XAP is an application contract above the Xbox binary ABI. Guest memory, object lifetime, errors and completion still require our own bounded reference fixtures. |
| `docs/decomp/AudioSystem.md` | Cross-check streaming start, segment consumption/refill, drain, stop, seek and ownership cases against original caller behavior. | Useful test-case hypotheses for shared audio; confirm which apply to Blinx. Desktop SDL_mixer is not an MCPX/DSP reference, and this does not replace the original Sofdec/ADX route. |
| `theseus/desktop/d3d8_sdl.h` | Compare persistent resources, dirty uploads and cached transforms with our existing donor backend when profiling shows relevant cost. | Many such mechanisms already exist in Phobos. No performance gain is measured by this audit; avoid repeating the parked matrix experiment on instruction counts alone. |
| Asset/input/media notes | Use format descriptions and caller state transitions when a concrete blocker reaches them. | Desktop libmpv playback and audio-CD TOC handling do not establish Blinx movie or game-DVD authentication contracts. |

The current renderer source also demonstrates exclusions: `IDirect3DSurface8`
returns a four-byte dummy from `LockRect` (line 1238), and the render-state
switch accepts several states without implementing them (lines 1481-1512).
These behaviors cannot become successful implementations here. Rendering a
dashboard with that subset is not evidence that our required game operations
are fulfilled. The older `docs/desktop/d3d8-translation.md` describes OpenGL;
the current source and build workflow show bgfx, so check code against docs.
The inspected build workflow checks compilation/artifacts, not our end-to-end
contract acceptance. We did not run Theseus or measure its cadence.

Owner follow-up: dashboard correctness/performance remains deferred behind
Blinx. Preserve this map in `beads-xbox.1.74` (audit) and `.17` (dashboard),
then consult it only where a concrete Blinx blocker benefits. It does not
authorize a dashboard implementation detour. `.78` demonstrates the current
loop: establish the small NV2A flat-color contract, check Phobos (its host
ignores this state), add the missing behavior around the borrowed renderer,
then run the actual title. No Theseus code is used for that change.

Decision: keep Theseus as a pinned, attributed behavioral reference. First use
its audio notes to select relevant integration cases; use its dashboard map
when dashboard profiling resumes. For each selected case, corroborate with the
original binary or hardware-oriented reference, establish the bounded LLE
fixture, then adapt HLE. Retain Phobos as the current game-renderer donor.
At this follow-up our native Blinx run has reached the animated title screen
after the opening movie; full attract, input interruption, audio quality and
intended cadence are still unvalidated. This reference audit changes no runtime.
