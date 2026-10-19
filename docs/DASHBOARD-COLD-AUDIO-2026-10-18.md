# Dashboard cold audio initialization

Tracking: `beads-xbox.1.17`.

Full-system boot constructs a board-reset audio device. The native title's
initialized-kernel handoff and the captured idle experiment are different inputs;
neither is a valid substitute for running the retail kernel initialization.

`McpxApu::initialize_board_audio_reset` establishes the fresh register state.
Only exact DWORD accesses can read the modeled reset configuration. FE control
read/write does not complete DSP work or a notification. Cold DSP holds are
accepted only with no executable generation, pending work or CPU owner; release
still requires the actual sole DSP processor. Once the original driver programs
FECTL, cold and initialized-kernel routes share the existing dynamic FE status.
Queued FEMEMDATA tokens use the existing serialized RAM publication owner;
control reads and writes do not publish them.

The source reference is xemu commit
`482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/xbox/mcpx/apu/apu.c`: `mcpx_apu_reset_locked` clears top-level registers;
ordinary register transport includes FE control. Its immediate FEMEMDATA write
is not adopted as permission to skip our retained command ordering or guards.
This is a software-reference cold model, not new physical reset evidence.

Cold AC97 control `0 -> 2` releases AC_RESET# while every descriptor owner is
stopped. Codec-ready is published by advancement of the shared clock after
`ceil(clock_hz / 48000)` ticks from release. The deadline is overflow checked.
Polling cannot finish reset, and repeating the release cannot postpone the
deadline. This delay is a nominal complete-frame model, not a measurement of
silicon startup latency. Warm/active reset transitions remain guarded.

Cold device clocks can be primed once before original MMIO or elapsed device
work. Priming changes the frame ordinal; it performs no descriptor/DSP work and
does not import warm kernel register values. Binding the sole processors before
priming is allowed. Repeated priming, priming after original control accesses,
or priming a codec whose shared clock has already started rejects.

Full-system integration uses the existing nominal1125000Hz audio clock, converted
rationally from the733333333Hz retired-instruction time base. Its GP-before-EP
dispatch order,32-sample setup chunks, eight-chunk EP grouping and codec phase
remain modeled scheduling policy; exact silicon sub-frame timing is deferred.

Cold GP startup is distinct from overrunning an admitted frame. The retail
kernel can release GP immediately before a setup boundary, leaving only119
actual DSP cycles before that boundary. The first real program may still be
initializing there. Only the explicit board profile, first bootstrap, running
initialization and zero prior admitted MIX/setup generation permit waiting for
the program's actual first C4 ready/yield. DSP instructions and independent codec
time continue; no MIX frame, EP notification or completed audio is fabricated.
Once ready, normal frame admission begins. Stopped cores, warm title startup,
restarted generations and unfinished already-admitted frames retain the strict
overrun refusal. xemu's `dsp/gp_ep.c` reset/frame routes likewise bootstrap and
run an initial DSP program without requiring a prior ready/yield at reset.

The authored shared-clock test releases a300-NOP real DSP bootstrap one nominal
tick before a setup/codec boundary, observes running initialization and zero MIX,
then observes the actual C4 yield and subsequent real frames. Separate cases
retain warm-start refusal, cold STOP refusal and an infinite CPU after its first
admitted MIX frame. All existing shared-clock timeline/output checks still pass.

Audio MMIO first services that clock at the exact instruction prefix, before
the access changes device state. `CpuFloor::mmio_instruction_clock` defaults
to unavailable. Unicorn resolves the active helper's host return address using
the pinned translation block's existing SLEB128 instruction search metadata.
The query does not restore registers, materialize flags, alter retirement or
install per-instruction instrumentation. Block mode subtracts the charged TB
suffix; instruction mode uses its current instruction index. Outside, stale and
recursive host callbacks reject without changing the supplied output.

The authored MMIO clock check passes multiple accesses in one TB, partial/full
deadline slices, both scheduling modes, read/write halves of one INC memory
instruction, lazy arithmetic flags/register preservation and callback-context
negatives. This validates exact callback time independently of dashboard assets;
it does not qualify the DSP's nominal timing or dashboard frame rate.

The same check also runs an existing generated authored AOT fixture over owned
RAM. Its native INC retires before an unsupported MMIO store declines without
effects; Unicorn then performs that actual store. Baseline, ordinary native
register transfer and bulk native register transfer all observe instruction
clocks2,6,10,14,18,22 with identical flags/register effects. Native cases require
positive actual native retirement and memory-decline counts, and their native
plus fallback retirement equals25; a skipped-clock substitute cannot pass.

Repeated configuration now preserves identical derived-source content and
timestamps using `cmake/WriteIfDifferent.cmake`. The two headers written
unconditionally by pinned Unicorn's own configure are snapshotted under this
build directory and restored only when their complete SHA256 matches. Changed
content keeps its fresh timestamp; Ninja still sees compiler command changes.
An unchanged configure preserved hashes and precise timestamps for all10 derived
source/header files. The next focused build reported `ninja: no work to do`.
This removes the observed70-file unnecessary CPU rebuild; it does not remove
the upstream configure script's own capability-check time.

Wolfson WM9709 production datasheet Rev1.3 describes reset/clock startup,
48kHz frames and the DAC-ready slot on pages5,6,11:
[manufacturer datasheet](https://consolemods.org/wiki/images/7/7e/WM9709.pdf).
Intel's AC97 Rev2.3 sections4.4.1 and9.2.1 describe reset and readiness:
[component specification](https://course.ece.cmu.edu/~ece545/F16/resources/ac97_r23.pdf).
xemu/QEMU `hw/audio/ac97.c` itself leaves reset control unimplemented and
hard-wires codec-ready on reads; that behavior is not a timing qualification.

Focused checks cover cold register initialization, convergence to initialized
FE status, real serialized token publication, invalid/unaligned accesses,
unowned DSP release, codec poll independence, late-frame release, fractional
clock rounding, deadline overflow and retained PCM/SPDIF descriptor lifetimes.

The initial current-source boot stopped at FE801510 after17,955,113
instructions. Cold initialization subsequently reached the actual GP reset
release `FE83FFFC=3` after19,923,572 instructions. The enhanced build recorded
140,765 native retired instructions there; this proves partial AOT execution,
not dashboard rendering speed or a fully native dashboard. Sole DSP processor,
shared device clock and private EP lookup integration are separate required
steps before a completed modern dashboard boot can be claimed.

The full-machine build can now select the same maintained sole GP/EP owners
and shared device clock through `XLLE_DSP_SOURCE`, `XLLE_DSP_LIBRARY` and
`XLLE_EP_Y_LOOKUP`. All three are required together. The prepared Rust static
library must match the host compiler ABI; the MinGW dashboard uses the GNU
Windows target, separate from the native title's MSVC library. The owned EP
lookup is size/hash checked and embedded only into private generated output.
DSP dependency notices are copied alongside the executable.

The authored `machine_audio` check exercises actual CPU MMIO, two-page SG
bootstraps, real GP/EP stores and STOP instructions, and early/late reset
release under117- and4096-instruction CPU chunks. All four variants pass,
together with the focused MMIO timestamp, FE fence and codec checks. This
establishes board wiring and clock partitions, not original dashboard output.
The dense DSP maps exceed the old768MiB process cap; the integrated boot probe
uses a2048MiB cap. An allocation failure is not a dashboard boot result.

The first integrated boot stopped at19,555,556 retired CPU instructions and
audio tick30000. GP had genuinely bootstrapped and executed119 cycles to PCbe;
the first active setup edge incorrectly rejected this cold initialization as
an unfinished frame. The scoped first-ready fix above passes the shared-clock
regression and advances original boot to20,060,741 instructions, GP PCfd after
9097 cycles. The current stop is the circular input descriptor at GP node1b4:
`0001bb 004bd0 000020 000524 ff7d80 040100 002b7f`. Its initial cursor exceeds
the ring size; implementing the reference's initial-cursor rule is pending.
The first device fault now survives between CPU slices and is reported with
both DSP phases, program counters and cycle counts. The newer scheduling
changes interrupt boundaries, so cumulative CPU counts alone do not rank boot
progress against older device-only runs. No modern menu frame rate is claimed.

### Reached GP circular initial cursor

Modern cold boot reached GP descriptor 4BD0 input, count32, cursor FF7D80,
ring size2B80. The xemu `dsp_dma.c` scratch circular contract resets an initial
cursor at/above ring size to zero, rather than reducing it modulo the size.
The model now admits this only for the existing exact GP 4BD0 profile and
copies actual scratch bytes before publishing samples, final cursor and node
completion. EP, output and adjacent profiles retain their qualification guards.
This is source-reference functional behavior, not new physical timing evidence.

The authored GP DMA suite passes exact-size / size+3 / FF7D80 cursors,
11-byte and2B80-byte rings,4/32-word counts, multiple wraps, nonzero data,
failed physical read without sample/cursor/completion publication, and rejected
adjacent/EP profiles. Standalone optimized test passed on2026-10-18; no dashboard
boot or menu/performance success is inferred from that component result.

### Reached GP stereo FIFO0

Boot012 reached descriptor control080403/count201/sourceC00:32 planar frames
from X:C00 and X:C20, PCM16 stereo FIFO0 output. Added only that exact GP
shape; existing EP and adjacent shape guards remain. Source-reference xemu
`dsp_dma.c` interleave ordering and PCM16 high16 truncation are implemented
through existing byte packing and actual PAPU FIFO/SGE transport, without new
completion, ACK, or timing shortcuts.

Optimized full GP DMA test passes authored literal two-channel signed24
samples,128 physical output bytes, three-byte initial FIFO tail then wrap,
FD->7D cursor, unchanged DSP banks and descriptor sentinels, and actual
completion only after successful transport. A second-segment physical write
failure preserves the three real bytes already written without advancing the
cursor or publishing node completion. Eight neighboring profile cases reject.
This component result does not establish dashboard boot or audible output.

### Cold VGA attribute initialization

Boot013 passed the GP audio output descriptor and reached an attribute write
without a retained phase owner. Added an explicit fresh-board VGA attribute
initialization: index0, index phase, zeroed21 attribute registers. Unknown
retained-state construction remains uninitialized; cold initialization refuses
already owned or programmed VGA state. It does not initialize mode timing,
retrace, display clocks, encoder, or scanout readiness.

Source reference: xemu NV2A reset in hw/xbox/nv2a/nv2a.c calls
vga_common_reset; hw/display/vga.c sets ar_index0, zeroes ar, and resets
ar_flip_flop0. Its3C0 read returns index in index phase and0 in data phase;
reads do not toggle the phase. Both explicitly established and cold owners now
share that reference behavior. There is no new physical NV2A observation.
Authored NV mirror tests cover first index write without status polling,
zeroed current data, alternating writes, non-toggling reads, invalid-width
preservation, status-read phase reset, rejected reinitialization retaining
programmed data, and fresh unknown warm-state rejection. Root owns integrated
compilation and validation; component editing alone is not a boot result.

### Kernel context-load configuration

Boot014 reached the original kernel context load at48,458,355 instructions.
Diagnostic boot015 (muted,50M budget,2048MiB cap,no HDD writeback) identified
exactly one configuration mismatch: DEBUG_7=0008CFFF rather than the title
initializer0028C3FF. All ten pipeline mode registers were initialized; FIFO
was disabled, engine idle, original subengine reset/FD clear/cache invalidation
completed, channel user0 and instance111D explicitly selected.

The bounded context image transfer now accepts either exact existing DEBUG_7
profile, retaining the same initialized-byte extent, decoded field encoding,
idle/preparation and first-load guards. It preserves DEBUG_7 readback; it does
not change the separate boot-profile command-execution quarantine or claim
draw/timing equivalence for unexplained debug bits. Source reference xemu
pgraph.c CHANNEL_CTX_TRIGGER reads the actual instance-RAM CTX_USER independent
of DEBUG_7; existing title physical observations still bound our fuller image
field projection. The new cross-renderer focused context-control target reuses
authored transfer/preparation/quarantine cases for both exact profiles.

Validation: focused nv2a_context_control passed all shared authored cases in
the current HLE/native-draw build. Muted no-writeback boot016 passed the real
context image LOAD and following kernel control writes, advancing to48,458,398
instructions before the retained boot DEBUG_7 command-execution quarantine
stopped at8005AACA. No draw, completion or modern-menu success is claimed.
Build/probe directory was released to root after this bounded validation.

### Known boot DEBUG_7 method policy

Probe017 retained the blocked packet: channel0/subchannel2, method0000,
object handle0000000E, parser GET03880008/PUT03881300,256 prefetched methods.
The boot profile0008CFFF differs from title0028C3FF by bits10/11/21. nxdk
pbkit labels it UNKNOWN_OPTIONS and uses it during initialization. Inspected
xemu PGRAPH has no DEBUG_7-specific functional method interpretation. Envytools
names the analogous older NV20 register SURF_UNK880, not evidence of NV2A bit
meaning. No new names or silicon/cache/timing equivalence are asserted.

After a real qualified context LOAD, this exact boot value now uses the same
existing method/provenance checks and handlers. Unknown draw state, unknown
methods, bad objects, FIFO/channel holds and actual memory bounds remain
checked. Boot-profile state without a qualified LOAD remains quarantined. The
context-managed bulk bypass remains disabled, so every consumer still runs
the loaded-context preflight. Source-reference functional modeling is selected;
exact silicon effects of the unknown bits remain explicitly deferred.

The expanded focused suite passed supported loaded-context effects and negative
consumers under both exact DEBUG_7 values (clear, blit, semaphore, bad RAMHT,
missing draw inputs, compression/cursor/begin-end guards). Large test fixtures
now use heap storage to fit Windows' default2MiB test thread stack. Boot018
retired227 real methods (219 Kelvin,3 surfaces,3 blit) and trapped the real
NV097 NOP0100/031C software call. DATA_ERROR IRQ/FIFO hold then led the kernel
to CTX_SWITCH1 readback, where the still-unknown graph-cache projection stopped.

The next scoped cache model captures only the initialized object header DWORD
at successful SET_OBJECT per subchannel, then selects the active header at
real method dispatch. This follows xemu pgraph_method's object-cache/switch
ordering; it does not alias later PRAMIN edits or switch on prefetch/IRQ ACK.
The other loaded graph words remain guarded. Authored tests exercise separate
subchannels, FIFO-held prefetch, explicit rebind, cached bytes across RAM edits,
and the real031C software NOP/IRQ/hold/ack sequence.

Header validation passed the focused suite and real boot019, which serviced
the software interrupt and reached307 real methods before a clear dependency
stop at48,459,114. The next correctness distinction is CLEAR versus drawing:
reference xemu gl/draw.c clear_surface uses CLEAR plane/channel masks, the
rectangle, and SURFACE_FORMAT sample-layout scaling; its draw rasterization
path separately consumes ANTIALIASING enable. Thus a known single-sample
surface clear does not require receipt of the draw multisample control/mask.
The clear preflight now retains known single-sample SURFACE_FORMAT and all
plane/storage/encoding guards, while draw multisample requirements remain.
Focused tests cover absent/zero/enabled/changed raster sample-mask controls
with literal written pixels and untouched neighbors, plus rejection of
unknown or nonzero surface sample layouts and reserved clear-mask bits.

Clear validation: focused context-control suite passed all12 raster-control
variants and3 rejected layout/mask cases. Boot020 passes the corrected clear
preflight, then stops at the explicit tiled-storage/HLE-residency boundary:
`NV2A tiled clear requires the software RAM path`,48,459,114 instructions.
The current renderer also deliberately guards tiled3D, IMAGE_BLIT, scanout,
and CPU aperture residency. Existing CPU tile translation/clear/storage logic
must not be combined with a linear resident host image by deleting a guard.
A coherent host tile/residency integration (or separately validated current
source CPU tile renderer configuration) is needed beyond register transport.
No menu, GPU clear or performance success is claimed from the component tests.

### Actual tiled clear assessment (probe021)

Passive rejection diagnostics and both overlapping/unrelated-tile fixtures
passed; the actual boot is not a false positive. Selected mask3 clears depth
and stencil. Format128 is a pitch, single-sample Z24S8/color8 setup, pitches
14001400 (5120 bytes each), clear rectangle1280x480. Resolved depth DMA11160
covers033D0000..03627FFF, exactly00258000 bytes, matching active tile1:
base/flags033D0003, limit03627FFF, pitch1400, ZCOMP84000000. Unselected color
starts03628000; tile0 spans03628000..0387FFFF with base/flags03628001,
pitch1400, ZCOMP0. All three PFB/PGRAPH/texture banks match. These are actual
clear/allocation dimensions, not independently verified visible scanout size.

A correct host bridge must detile/decompress snapshots and retile/reencode
readback through the captured storage layout. Compressed depth writeback needs
actual ZROP selection and compression-tag publication, not only row detiling.
Tile/bank reconfiguration must resolve old GPU ownership with the old immutable
layout before latching a new one. Existing generic resolve functions currently
write linear bytes directly, and HLE surface/texture snapshots read raw memory.
These are paired transport changes; CPU physical accesses and translated
scanout must use the same ownership. Unknown CPU-aperture-over-tile semantics
may remain guarded rather than assume CPU aliasing equals GPU tile translation.

Build division matters: HLE submit_vertex_batch explicitly rejects tiles even
without a renderer pointer; the strict device/raster pipeline is the opposite
compile-time branch. The HLE backend does not accept generic LLE draw packets.
A current-source LLE build with SDL, the enhanced OpenGL renderer, and
XLLE_NATIVE_DRAW retains GPU generic/generated/native draw acceleration, but
still requires the tiled transport/residency bridge and qualification. That
path transforms vertices on the CPU; it is not a promise of equal performance
to HLE raw-vertex submission or a software-only handoff. No source selection or
renderer configuration was changed during this assessment.

## CPU/GPU phase ownership and tiled HLE transport (2026-10-18)

The current HLE/native-draw build now owns an entire GPU-producing MMIO/frame
burst as one phase. Entry resolves any prior ownership and invalidates clean
resident carriers because CPU/DMA physical RAM writes have no per-access hook.
Exit drains queued jobs and resolves dirty color/depth before CPU execution
resumes. Residency is preserved within the burst. Failed publication latches a
terminal phase failure; a later callback cannot replay the submitted draw.
Tile-bank and compression-selector mutations resolve/invalidate using the old
layout before changing the register. Linear RAM mirrors retain their existing
callback transport and the barriers cover the complete128MiB address range.

Tiled snapshots/readbacks use ram_tile_transfer; depth GPU writeback uses ZROP
and actual compression tags. Full/partial tiled clears retain the CPU codec,
including real tag initialization. Translated scanout is admitted; unknown CPU
aperture-over-tile interpretation remains guarded. Tiled texture reuse retains
the strict existing pitch-linear, one-level2D color format/extent constraints;
Morton, cube, compressed-color and depth-texture tile layouts remain guarded.
Loaded HLE draws now run the existing known-field/dependency preflight without
executing a CPU vertex shader or producing shadow transformed vertices.

The focused context-control suite passes authored linear/tiled two-draw reuse,
CPU physical-write visibility, compressed-depth clear/upload/ZROP writeback,
selector/layout epoch barriers, required-field rejection, and partial physical
write failure/no replay. The muted COW cold-phase-022 probe passes the reached
tiled clear and stops at48,891,486 instructions on the pre-existing HLE DXT1
noise guard, before any host draw. No dashboard performance or visual result
is claimed. The documented native HLE compressed-sampling approximation is an
appropriate policy here, but the separate native D3D fixture's measured error
bounds do not certify this OpenGL backend.

The next cold-phase-023 probe retains the working tile/phase bridge and removes
only the HLE noise guard contradicted by the existing accepted approximation.
Both raw noise-control banks still agree or reject; authored off/on draws pass
and disagreement rejects. Nullable linear memory callbacks retain their guards.
The probe stops at the first loaded draw's polygon-stipple provenance check,
again48,891,486 instructions and zero host draws. LOAD deliberately leaves
unmapped context fields unknown. Envytools maps NV20 polygon-stipple enable to
BUNDLE_RASTER bit4 (pgraph_class_kelvin.cc, MthdKelvinPolygonStippleEnable), but
this does not establish that register's offset in our retained context image.
No zero default or hardware context projection has been fabricated. Source
reference mapping or a separately explicit HLE feature policy is needed next.

Backend consumer split: an absent polygon-stipple receipt is now an explicit
HLE cosmetic/context-projection omission matching the consulted xemu path;
known enabled stipple remains rejected and software/LLE admission is unchanged.
No raw state or receipt is synthesized. Authored absent/enabled tests pass.
Cold-phase-024 logs every first-draw initializer receipt together: context-write
known0, inclusive clipping known0, stipple unknown, two-sided known0,
MSAA knownFFFF0001, opaque09F8 known4; all texture controls are fully known.
The only next failure is the exact draw-MSAA receipt. With known surface AA=0,
HLE storage remains one sample regardless of raster enable bit0. The bounded
HLE policy therefore admits knownFFFF0000 orFFFF0001 only, retaining the full
sample mask, no alpha-coverage bits, and the known single-sample layout check.
Strict LLE remains unchanged. Changed mask/coverage/layout authored negatives
remain rejection cases; raster enable does not manufacture sample storage.

Cold-phase-025 passes those initializer checks and stops on a real shader input:
vertex input3 requires all four components but its provenance mask is zero.
This first draw uses INLINE_ARRAY (12 words), not indexed/DRAW_ARRAYS input.
Decoded inline attributes gain real known masks; a disabled/current attribute
must retain its producer requirement. No host draw has been submitted. This
input dependency is preserved while the actual format/program/context producer
is inspected. The focused sample-enable equivalence and changed mask/coverage/
storage rejections pass. No owner-facing fallback or speculative input default
has been launched.

Input diagnostics026/028 establish the actual first draw: three inline vertices,
position0 FLOAT2 and texture9 FLOAT2, other arrays disabled; program/constants
are fully known. HLE observable color dependencies now follow actual combiner
register/channel consumers rather than the software floor's unconditional
interpolation. The strict floor keeps its old requirement. Authored unused-color
positive, used-color negative, and final-alpha versus unrelated-RGB cases pass.

This correctly removes unused RGB/specular dependencies but leaves a real
primary-alpha dependency: one stage passes texture RGB and primary-color alpha
into R0; final output writes all color lanes. v3's retained numeric value is
(0,0,0,1), with provenance unknown after LOAD, so it remains rejected. No default
has been manufactured. Envytools pgraph_state.cc2321 identifies the17-vector
VAB current-attribute bank at RDI0x15, using reversed four-word ordering. The
NV2A context-image offset remains unestablished. Generic NV20 Nouveau context
initialization is a reference lead, not a compatible offset oracle: its program/
constant layout differs from our independently mapped NV2A constants. Probe
cold-input-028 stops at48,891,486, no host draw; all private input/program values
stay in ignored runtime logs. Next work is actual LOAD/RDI producer projection
or a separately documented, source-backed HLE API default contract.

## Explicit fresh HLE API W default

The owner accepts the existing numerical HLE default W=1 as a scoped API
approximation, not an NV2A VAB LOAD projection. Eligibility begins only at the
explicit fresh-board initialization before GPU commands/context ownership.
The first successful LOAD preserves eligibility only for untouched canonical
W bits3F800000 without a prior W receipt. Subsequent context ownership or a
replacement LOAD attempt invalidates it; explicit W-producing setters and
fetches cannot renew it. A disabled/current attribute may omit only its missing
W dependency while that eligibility and exact numeric default remain. Raw
receipts, guest RAM and input values are unchanged. RGB/XYZ, enabled-array data,
constants, programs and storage retain normal checks. Software/LLE admission
remains unchanged. Explicit producers retain their actual values and authority.

Every actual use increments hle_default_w_uses(), and first use per input emits
an approximation notice. VAB context-image projection remains explicitly
deferred. Authored coverage checks fresh acceptance, nonfresh rejection, required
RGB rejection, unchanged default numeric input, explicit nondefault authority,
and retained/replacement context refusal with zero default use.

Validation cold-default-029 reaches the full50,000,000 instruction budget with
no device STOP (final EIP8001F3A6). The enhanced OpenGL HLE backend submits its
first actual draw:1 draw,3 vertices,1 shader,2 uploads,1 resolve. The input3 W=1
approximation notice is present. Focused context-control passes. This proves
first GPU execution through the cold route, not a validated interactive menu,
visual equivalence or a dashboard performance result. Owner launch still waits
for the actual dashboard to be reached.

Stock baseline cold-menu-030 completes100,000,000 instructions without fault,
final EIP80074F55 in kernel x87 processing. Eight vblanks and unchanged1 host
draw show this is still early boot/logo work, not the main menu. IDE reads5665
sectors, one emulated COW sector, writeback0. Native AOT retires350460;
fallback54089308, idle45560232. At the explicit733333333Hz retired-time scale,
100M is only about0.136 emulated seconds. No performance inference follows from
this earlyboot workload. A larger bounded capture is needed to reach actual
menu rendering/input before testing the prepared Credits clone or launching
owner-facing. Clone image path supplied by root is credits-private/
dashboard-credits-004.aot-private.img; it has not been used by this probe.

Cold-menu-031 was bounded at500M with100M periodic captures and wall timing.
It reaches144132855 instructions in13.342s, then rejects texture shader mode2.
The100M checkpoint attempted capture after8vblanks/11.84s but strict output
processing refused the known TV setup receipts. No menu/performance result.

Cold-shader-032 identifies stage3 PROJECT3D, program00010C61, control4003FFC0,
format0001302A (2D linear depth16). The existing HLE psh implementation already
handles this projective shadow sampler and its native texture path. HLE
admission now follows existing NONE/PROJECT2D/PROJECT3D/CUBE/PASSTHRU consumers,
including real snapshot descriptor knowledge for all actual sampling stages,
known projective-depth comparison, and actual coordinate dependencies only
where a pixel consumer exists. CLIPPLANE/dependent side effects remain guarded.
The software/LLE subset stays unchanged. Focused actual depth-descriptor and
missing pitch/compare/side-effect negative cases pass. Root separately owns
strict AV-versus-HLE host-picture capture separation and its authored tests.

## Host RGB presentation is separate from physical output

The HLE build's host window and frame captures now call explicitly named
host-picture APIs. This domain presents the actual framebuffer RGB with the
existing blanking, DAC LUT, storage bounds, tile translation and residency
checks. TV matrix/filter processing and analog encoding are omitted under the
HLE presentation policy; a startup log labels that domain. Receipt of the
bounded AV setup commands is not an RGB-presentation dependency and supplies
no hardware latch, register readback, active timing bank or encoder evidence.

The strict capture_scanout/scanout_direct_bytes APIs keep their programmed
display-processing guard. Both domains share their actual fetch/conversion
implementation; the compile-time LLE host-picture route also remains strict.
No live renderer selection or diagnostic framebuffer substituted for finished
pixels is introduced. Authored tests check strict refusal before reads, actual
HLE RGB bytes and direct/copy agreement, malformed pitch, incomplete LUT,
blanking, unmodified guest RAM and unchanged command/timing receipts. Physical
TV-output qualification remains deferred independently of this host endpoint.
