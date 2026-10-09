# Bounded original audio contract

Tracked by `beads-xbox.1.113`. Audio is the active independent offline family;
the original-scheduler/GPU startup experiments are not prerequisites. The family
remains **open**, and the overall burndown remains **4/9 qualified**.

The native title continues to select its real XAudio2/DSP implementation.
The audio LLE floor still needs remaining envelope curves/filtering, GP transport/completion and
whole-driver integration before it is a qualified selectable title backend. Existing
audible HLE output does not remove that requirement.

## GP FIFO transport - 2026-10-09

The maintained DMA engine now transfers real bytes through GP output FIFOs
0..3 and input FIFOs 0..1, using the separate FIFO scatter/gather table and
each ring's own BASE/END/CUR. PCM16 truncation and both PCM24 byte layouts
are implemented. Scratch and FIFO share packing and physical-page transport;
FIFO transfers leave the scratch-only descriptor fields unchanged.

[Published DSP research](https://xboxdevwiki.net/DSP) supplies the ring and
sample-lane contract. Pinned xemu `dsp/gp_ep.c` and `apu_regs.h` corroborate the
register/SGE layout; no implementation body was copied. The original NTSC
`0x17b870` routine independently shows GPFADDR/GPFMAXSGE setup, FIFO0 bounds
`0..0x2000`, and initialization of output cursors from their bases. That routine
was inspected, **not qualified as a complete executable owner**: its reset and
bootstrap accesses remain outside this transport. Phobos's audited APU README
describes its GP effects bypass/EP passthrough, so those paths were not adopted.

Validation includes **108 transport cases**, **12 guards**, and **16 device
lifetimes / 2,064 repeated transfers**. Tests compare all physical RAM, including
untouched scratch storage, noncontiguous page crossings, exclusive-end wrapping,
multiple ring laps, distinct input/output cursors and real completion status.
The reused MIT DSP core executes six authored instruction-program cases that perform
actual scratch/FIFO output and input through these peripherals. Their expected
sample bytes come from independent published examples, not native output.

Scope remains noninterleaved unit-stride transfers, at most 4,096 words, and
low-24-bit rings with 256-byte aligned bounds. Odd PCM16 counts, other formats,
unknown control bits and invalid ring state stop explicitly. A later page fault
can leave bytes already physically written; it earns neither a completed node
nor CUR advancement. Invalid-state silicon behavior and precise DMA latency
are not claimed. Original GP boot/framework, interleaved output, EP/AC97 and
whole-driver audio remain open. This is a new runnable floor component, not
qualification of the native effects shell or whole-title LLE audio.

Final validation: maintained APU **8/8 in 0.22s**, complete native/contract suite
**56/56 in 35.70s**, plus all prior original configuration/source/control and
30 IRQ cases pass. Reference DLL SHA-256:
`090cedf7bcaac1e5f9ff6d751ce0520781799aebd3ebc34bbf70cdd6b09e7f55`.
Logs and private fixture output use `build/blinx/audio-fifo-*`.
The final rebuilt native title remains exact SHA `76ff2a4e...ff12f730`, so the
preceding **190.578s / 18-capture** Blinx regression is retained without a
duplicate run. Stable game pin unchanged; audio stays open and the family
count stays **4/9**. DSP bit 9 `.114` and the FE fence `.115` stay deferred.

## Original IRQ5, unlink and deferred-work queue - 2026-10-09

`native_audio_interrupt_reference.py` connects the maintained APU to the
maintained dual PIC and reuses the display family's captured-kernel mappings,
interrupt entry and port transport. Original kernel allocation, interrupt
initialization/connection and actual Blinx ISR `0x17856e` execute unchanged.
The ISR handles the actual inactive-voice trap, invokes original unlink worker
`0x178d7c`, publishes its software status and queues original deferred work.
The fixture does not call that worker or manufacture its completion record.

**30 cases / 66 ISR deliveries / 30 original unlinks** pass: mono/stereo,
three fresh device lifetimes for each of combined notification/trap,
notification-before-frame, device-IRQ masking, original disconnect/reconnect
and command-stop cases. Interrupted registers/stack/flags and IRQL are restored;
acknowledging the reasserted trap causes no duplicate unlink. A second original
queue request is rejected while the real DPC is pending. Original cancellation
removes it once, and original allocation statistics return to baseline.

This intentionally validates delivery with the interrupted caller at
DISPATCH_LEVEL. **Deferred completion remains pending**, with its original
status/list state checked before teardown. Cancelling the DPC for fixture
teardown is not successful execution of that deferred work. Original derived
completion is still independently exercised by the earlier control fixture.

The `--barrier-audit` run lowers IRQL through the original kernel, enters the
real DPC `0x17838e` and reaches fence helper `0x177df0`. It stops at the strict
`0xfe801510` read, before the completion consumer executes. The helper toggles
that register's bit 0, writes FEMEMADDR/FEMEMDATA and polls actual RAM. Pinned
xemu describes its immediate FEMEMDATA write as provisional; Phobos, sp00nznet
and NoRain's audited APU cores share that assumption. They are not independent
proof of the fence's ordering or control ownership.

The maintained APU's older unconditional FEMEMDATA guest write is now removed.
The control/address/data writes fail before mutation, and the strict reference
continues to reject the control read. No guessed memory completion is admitted.
Root negative cases verify caller RAM and interrupt state remain unchanged.
The precise unresolved question is **`beads-xbox.1.115`**, deferred alongside
other silicon questions. Independent GP/FIFO and graphics work can continue;
do not repeatedly rerun this guarded DPC or the parked title startup.

Reproduction (owned private inputs):

```powershell
py -3 tools/native_audio_interrupt_reference.py --xbe <owned-default.xbe> --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_apu_reference.dll --output build/blinx/audio-irq-original.aot-private.json
```

Add `--barrier-audit` with a separate output to reproduce the expected nonzero
guard result. This is a bounded IRQ/queue qualification, not whole audio-device
initialization, working deferred completion, timing fidelity or LLE title audio.
The audio family remains open and the count remains **4/9**.

Final validation: maintained APU **7/7 in 0.28s**, complete native/contract
suite **56/56 in 35.71s**, the 30 new original IRQ cases, and all prior original
configuration/source/natural-completion/command-stop fixtures pass. The guarded
DPC reproducer still stops before the completion consumer. Reference DLL SHA-256:
`00f4382e6be49b83c9fe2b6bb0dffbc3b2b83368d31c3f647722797d671a2266`.
Evidence: `build/blinx/audio-irq-{original,barrier-guard,configuration-original,source-original,natural-original,stop-original}.aot-private.json`
and `audio-irq-{machine-build,root-tests,full-build,full-tests}.log`.
The fully rebuilt native title retains SHA `76ff2a4e...ff12f730`; its exact
**190.578s / 18-capture** regression from the preceding batch remains valid.
No duplicate title run or new LLE-title claim is made. Stable game pin unchanged.

## Finite-buffer processing and original completion - 2026-10-09

The current hash-bound native audio bindings expose DirectSound buffers and
effects, with no stream-object API. Original nonspatial initialization uses
`DATA_TYPE=buffer`; movie audio also rewrites real buffer storage. This makes
finite-buffer completion a more immediate integration step than SSL breadth.
SSL remains required wherever reached; it has not been waived for the console
or later titles.

`McpxApu::process_2d_voice` now connects real SGE source consumption, persistent
rate conversion, bounded envelopes and final 2D sends to static retirement.
The admitted original ordinary defaults are filter bypass and format headroom
7. Unknown filter/headroom, spatial, linked, multipass, SSL and phase-inversion
modes fail explicitly before source consumption. Pause/lock/inactive voices
remain stationary. Final samples reach the actual 32-bin destination before
ACTIVE clears, the real status byte changes and notification IRQ is raised.
Natural retirement does not invent a `VOICE_OFF` method or pad the destination.

**Timing scope:** this nominal reference retires after the converter's actual
finite output is drained, at a 32-sample processing boundary. Detecting drain
can take a final zero-output call. Pinned xemu retires while fetching the last
source samples; it does not establish our chosen drain/IRQ latency as silicon
behavior. Exact prefetch, interpolation and completion phase remain explicit
unqualified timing questions, deferred behind offline work. This batch proves
finite data/ownership and observable eventual completion, not sample-exact IRQ
timing, downstream GP drain, all-voice scheduling or complete title audio.

Validation connects independent original and native observations:

- Root `apu_completion`: **72 PCM/ADPCM mono/stereo finite cases**, 1..129
  source frames at half/unity/double rate. Exact nominal frame counts, final
  CBO, state/status/IRQ ordering, untouched unused output, no repeated
  completion, looping/pause/lock and unsupported-stage guards pass.
- Original control fixture `--kernel ... --natural`: **eight complete original
  initialization/start cases** consume 128 real samples and retire without a
  command-stop. The actual inactive-frame trap precedes original unlink worker
  `0x178d7c`; original derived consumer `0x17a34b` reads and resets the
  software completion record published by that worker. Stack/callee-saved
  registers and real guest/device list ownership are checked. No callee or
  result is substituted. Full ISR/DPC dispatch remains to be connected, and
  notifier payload accesses remain guarded to the qualified status byte.
- Native `pilot_audio_completion_lle`: **six actual XAudio2 captures**, mono/
  stereo at 24/48/96 kHz with 1,057 finite source frames and a distinctive
  final 32-frame tail. Integrated per-channel output agrees within three peak
  sample-frames plus 0.2% of LLE integrated output. Maximum measured area
  difference was 0.386 normalized sample-frames. Both outputs retain the tail;
  native status stops, unused bins stay silent, and a subsequent capture
  contains no repeated audio. Different finite-edge interpolation is permitted;
  this is not a waveform-exact comparison or host-time completion simulation.

Both maintained-machine and native builds succeed. Maintained APU tests:
**7/7 in 0.27s**; complete native/contract suite: **56/56 in 37.06s**. Earlier
original configuration/source/command-stop/settings fixtures also pass.
Reference DLL SHA-256:
`faed837429f0f553dedcb843d5a21dcb45ac1aca5e47f7f22c1b6d32b9a3c5d5`.
Evidence: `build/blinx/audio-completion-{configuration,source,stop,control,detach}-original.aot-private.json`
and `audio-completion-{machine-build,root-tests,full-build,full-tests}.log`.
The rebuilt native title SHA-256 is
`76ff2a4e836246508dac2992a77972aa552cfb8ff8ceb87950196530ff12f730`.
Fresh native regression `audio-completion-floor-attract` ran **190.578s**;
all **18 captures** were inspected: movie/title progression, demo gameplay,
Start interruption/loading, title return and demo reentry. Both scheduled
Start press/release pairs were delivered, with no logged STOP or audio fault.
The harness terminated the still-live process at its time limit; return code 1
is not a crash. This protects native playback and is not whole-title LLE audio,
cadence or endurance qualification. The stable private game pin stays unchanged.
The full family remains open: **4/9**. No new donor bodies or licenses are added.

## Original static-buffer stop and unlink - 2026-10-09

`McpxApu::voice_off` now implements the bounded ordinary-buffer stop route.
It reads the real voice state, clears ACTIVE, discards owned resampler history,
then writes record A's DONE status and raises the actual notification IRQ.
The voice remains linked until the original driver removes it. Source data,
format/routing, cursor and other voices are retained. Failed state/status DMA
does not publish a successful interrupt; a status-write failure can follow a
real stop and is reported as a terminal partial operation. SSL, linked and
multipass owners still stop at explicit guards instead of assuming record A.

The supported notifier contract is **status only**. The original exercised
reader tests byte 15 for value 1. Bytes 0..14 are preserved by this model and
are **not qualified hardware payload values or hardware preservation claims**.
The original-code fixture rejects any attempted access to those payload bytes.
This restores real static stop work without reinstating the withdrawn constant
envelope-information byte. Timestamp/envelope payload semantics remain open.

With `--kernel <owned kernel-live.bin>`, `native_audio_control_reference.py`
now executes the actual derived `CMcpxBuffer` vtable and **complete original
stop owner `0x179a44`**, including the original kernel ordinal-128 time read
and original command-submission callees. The time routine reads a coherent
fixed shared-clock input; this does not qualify clock generation. Original
`0x179e0e` observes pending/done through the device's actual shared notifier
RAM. A maintained setup frame emits the inactive-voice trap; complete original
unlink worker `0x178d7c` then updates the real list registers/voice memory,
software list and pending-stop flag. No callee, result or notifier is replaced.
The worker is invoked explicitly after verifying the real trap; the intervening
full ISR/DPC dispatch is still an integration requirement.

All **eight mono/stereo/envelope cases** pass, preserving ABI registers and
stack on every complete original call. Root `apu_stop` verifies state-before-
status-before-IRQ ordering, guarded failures, retained caller storage and
**32 stop/rebind lifetimes** without stale resampler output. The native audio
test additionally verifies both cursors freeze after a real Stop. Existing
native detach/rebind tests retain eight output/silence/storage lifetimes and
mono/stereo ADPCM rebinding; original settings detach/rebind remains five cases.
Phobos `bcccf8c` stop/lifecycle behavior was rechecked. Its host-time cursor
does not replace this sample/state evidence; existing native XAudio2 Stop,
FlushSourceBuffers and destruction already provide the matching host operations.

Validation: maintained APU tests **6/6 in 0.23s**, native suite **55/55 in
35.31s**, followed by the expanded native Stop test **1/1 in 0.89s**. All
earlier original configuration/source cases and the eight new stop cases pass.
Both maintained-machine and native builds succeed. Reference DLL SHA-256:
`a09888c9a13fd8915dd7eb3269aabd0127eeb1ba2f8231d2528f0105f675e968`.
The native title executable is unchanged at `67b1af85...05a8e600`, preserving
the previous **190.141s / 18-capture** Blinx regression. No redundant title
run, new whole-title LLE audio qualification or family closure is claimed.
Evidence: `build/blinx/audio-stop-{configuration,source,control,detach}-original.aot-private.json`
and `audio-stop-{machine-build,native-build,full-tests,final-native-build}.log`.

The next integration step was updated by the reachability audit above:
finite-buffer processing/completion precedes SSL breadth. `.88` and `.113`
remain open; **4/9** families remain qualified. Hardware-only questions stay last.

## Current GP memory/DMA batch and deferred question - 2026-10-09

The maintained APU now owns real 24-bit GP X, MIX, Y and P banks shared by
host MMIO and the DSP core. `apu_gp_dma.hpp` performs actual circular scratch
transfers through GPSADDR/GPSMAXSGE and physical 4-KiB SGE pages. The bounded
contract covers noninterleaved unit-stride formats 2 and 6, linked descriptors,
offset writeback, freeze/unfreeze, explicit STOP and end-of-list acknowledgement.
No status read invents completion; unknown registers, formats and modes stop.
Mid-transfer DMA faults can leave actual earlier bytes written but cannot
publish completed nodes or end-of-list. GP reset/bootstrap remains guarded.

The independently authored `apu_dsp_core.c` bridge reuses the already pinned
MIT mborgerson DSP core (`c60aeedb6a6014850204c71e53bc0cf5b3104222`). The new
`pilot_gp_lle_contract` executes an authored program whose real MOVEs start
linked output/input DMA, crossing nonadjacent physical pages in both formats.
The root `apu_gp_dma` test also covers circular wrap, four banks, frozen work,
invalid descriptors/pages, MMIO ownership and 64 lifetimes. This is executable
CPU/device integration, not retail GP firmware or a whole audio backend.

[Published DSP byte examples](https://xboxdevwiki.net/DSP) distinguish format 2
(24-bit sample in the high bytes of a 32-bit container) from format 6 (low
bytes). They exposed a native adapter error: format 2 previously used the
low-byte layout. Both directions are corrected in `audio_dsp.c`; a literal
byte assertion now tests the native adapter independently of round-trip output.
Pinned xemu treats these formats alike and labels the interpretation uncertain;
its implementation body is not imported. The current native original-graph
probe passes **1,501 blocks / 73,549 real DMA nodes**, with real impulse/tail
output and program bytes matched to the original-x86-decoded private image.
That checks retained native execution, not independent whole-graph correctness.

The separate maintained-LLE original-graph probe **does not pass**: its first
descriptor has transfer control `0x004bd0`, including unknown bit 9. The published
reference leaves that bit undocumented; xemu and the current native adapter
ignore it. Their agreement cannot qualify it. The floor rejects the descriptor
before data transfer. This question is **deferred as `beads-xbox.1.114`** behind
available offline work; preserve the reproducer instead of repeatedly rerunning
it or weakening the guard. No console interaction occurred. Existing hardware
evidence is preferred before any separately authorized measurement.

Build/run the root `test_apu_gp_dma`, and the native CTest
`pilot_gp_lle_contract`. To reproduce the open original-graph boundary, run
`pilot_gp_lle_test <owned dsp.bin> <private seed in hex> <original-decoded image>`;
it currently fails explicitly at that descriptor. This diagnostic's existing
six-output graph shell also does not qualify GP bootstrap, frame signalling,
output FIFO or EP. Its prospective exact native comparisons share the CPU core,
so they would not be independent proof of DSP instructions.

Focused APU/source/rate/envelope/GP tests pass **5/5 in 0.25s**; the complete
native/contract suite passes **55/55 in 33.55s**. Original configuration,
source and whole init/start-owner fixtures all pass again. Full maintained
machine and native builds pass. The fresh default-native Blinx regression runs
**190.141s**, with 18 inspected captures showing Demo, interruption/loading,
title return and Demo reentry. Both Start press/release pairs were delivered;
no STOP/audio-stop/fault was logged. The harness terminated the still-running
title at its budget, so this is neither an exit failure nor endurance/cadence
sign-off. No LLE-audio title run is claimed; family count remains **4/9**.

Private evidence: `build/blinx/audio-gp-*-original.aot-private.json`,
`audio-gp-{machine-build,native-build,full-tests,native-original-graph}.log`,
and `audio-gp-floor-attract/{run.aot-private.json,contact.png}`. The explicitly
failing graph probe is preserved in `audio-gp-original-graph.log`.
Reference DLL SHA-256:
`7eb8bd120ef77ffe412d03a0698545f4710342a2fe217df4deb19f73fe9224c1`;
native executable SHA-256:
`67b1af858416c4ed25876eb0002fd373605321212044e4cdba13423505a8e600`.
The stable private Blinx repository pin remains unchanged.

Continue offline with original control/SSL/notifier ownership and remaining
voice scaling/filter processing. GP bootstrap/FIFO/EP remain implementation work; only
the specifically unresolved bit and other silicon-only observations go last.
Do not make the unknown bit a prerequisite for these independent services.

## Executable configuration floor

`src/machine/apu.hpp` now applies selected-voice configuration methods to the
actual guest physical voice table. Routing/format, envelope/filter parameters,
three packed volume words, pitch and buffer base/loop/current/end fields are
no longer merely echoed in the PIO register file. Pitch preserves the voice
list link; 24-bit buffer offsets preserve the neighboring envelope byte.
Configuration alone does not set ACTIVE, advance playback, signal completion
or raise an IRQ. Invalid voice ownership/DMA stops the reference; address
arithmetic cannot wrap a bad voice-table pointer into valid RAM.

`tools/apu_reference.cpp` supplies a strict C transport to this maintained
implementation. It accepts implemented 32-bit configuration/control writes
explicit PCM/ADPCM source pulls, nominal rate conversion, submix headroom writes and final 2D send-stage
processing. These are distinct pipeline boundaries; no missing upstream stage
is silently supplied by the final mixer.
Unsupported methods/widths fail persistently before successful completion or
later RAM modification. The older broader machine register-file defaults are
not admitted through this qualification boundary.

`tools/native_audio_configuration_reference.py` executes the owner's complete
NTSC original routing and pitch helpers without substituted callees. The XBE
is bound to SHA-256
`e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249`.
It verifies real return/stack/callee-saved-register behavior and checks output
against independently expressed field expectations. Those actual output words
then enter the maintained APU and update its guest voice table.

Five routing layouts and eleven pitches give **55 original cases**, including
empty/default-bin filling, reordered bins, signed pitch and saturation. The
original internal pitch helper saturates to **-32767..8191** before producing
the hardware word. This does not yet qualify public SetPitch validation or
audible resampling; native public range handling is a follow-up comparison.
Another **16 buffer-field cases** check masked ownership, and **five negative
cases** reject unknown methods, wrong widths, bad voice IDs, out-of-RAM DMA and
32-bit address wrap. The source RAM and another voice remain intact.

The expanded `test_apu` checks all thirteen full-word configuration methods,
pitch/link separation, buffer/envelope separation, whole-RAM write isolation
and terminal-fault behavior. Its earlier VOICE_OFF/notifier claim is corrected
below; the independently specified inactive-voice IRQ case remains. The private original/model fixture passes;
evidence: `build/blinx/audio-configuration-original.aot-private.json`, reference
DLL SHA-256 `41d8fdcea694dd1107f7f73d909773ab55382a97aff3b7f1816b3d3393e2e3df`.

This is original-helper plus real device-configuration evidence. It does not
execute the enclosing DirectSound lock/command emission path end to end, nor
qualify every declared parameter's downstream signal-processing behavior.

## PCM source consumption and native comparison

The maintained APU now fetches PCM8/16/24/32 mono/stereo through its actual
scatter/gather table and guest RAM. It handles nonadjacent pages, including a
single sample or stereo pair straddling a page boundary. Each consumed frame
advances the guest CBO while preserving its envelope byte. Interior loops wrap
to LBO only after consuming the inclusive EBO sample. Pause and voice-lock
prevent reads/advancement; a later seek or caller data write is visible on the
next pull. Unsupported SSL/multipass/linked processing fails explicitly;
ADPCM is added by the subsequent batch below.
No host clock estimates samples, and no silence is invented to fill a request.

This is the source stage before resampling, envelopes, routing and GP effects.
Its `ended` output hands the real final sample to the pipeline owner; it does
**not** retire the guest voice or publish a notifier. The owner must handle
that event before requesting more source data. Stop/restart state is currently
an input to this stage, not a newly qualified whole control/notification path.

`test_apu_source` checks eight PCM format/channel combinations, splits requests
around 31/32/64-frame boundaries, and verifies the last sample, one-frame buffers,
interior/repeated loops, pause/lock/seek and inactive ownership. Fault cases cover
unsupported formats, out-of-range cursors, descriptor wrap, invalid/misaligned
SGEs and a failed second-channel page read without cursor advancement.

`native_audio_source_reference.py` executes the original region-setting routine
from entry `0x179efd` through its calculations, stopping at its first PIO poll
`0x179f9b`. All reached helper calls execute unchanged, with an explicit IRQL-2
caller precondition; no FIFO capacity or enclosing return is substituted.
The original subtracts one from the converted end, corroborating inclusive EBO.
Its resulting LBO/EBO then drive our real source consumption. **40 cases** cover
PCM8/16 mono/stereo, lengths 1/31/32/33/65 and one-shot/interior loops. Complete
original byte/sample helpers independently round-trip the consumed cursor.
The emitted PCM samples and cursor units match the independently authored input.

`pilot_audio_source_lle` compares that source stage with actual XAudio2 mixer
samples for PCM8/16 mono/stereo at 48 kHz, unity gain and interior loops.
All four cases pass with a **3e-5 normalized-sample tolerance**; actual native
play/write cursors stay in the chosen loop. These are sequential test runs,
never duplicate production execution. Other rates, pitch interpolation and
downstream envelopes/mixing/GP behavior remain separate work.

Validation: source and APU tests **2/2 in 0.03s**; mixed suite **53/53 in 21.69s**.
After removal of the notifier placeholder below, both original fixtures and
the native-source test pass again (the latter in 1.21s). Private evidence:
`build/blinx/audio-source-{original,configuration}.aot-private.json`,
`audio-source-{build,native-build,full-tests}.log`. The title executable hash
still matches the recorded 190.141s regression, so it was not rerun for this
device-stage/test-only addition; no new actual audio-LLE title run is claimed.
Final reference DLL SHA-256:
`c4498668be4949a07821fd0006f480355fab3a5fb50bfa1667d80f4e37147cac`.

## ADPCM source consumption using an independent permissive decoder

The same maintained source stage now fetches complete Xbox ADPCM blocks through
the real SGE/RAM path, decodes them and advances CBO in source frames. Each
36-byte/channel block supplies 64 played frames, including the header predictor.
Mono/stereo, mid-block pulls/seeks, inclusive end, loops across block boundaries,
pause/lock and a fresh guest-data/page-map read on each new pull are covered.
A failed block fetch does not advance CBO or invent output for that block;
already consumed earlier blocks remain correctly reported. This still hands
source end to the unfinished downstream owner rather than publishing completion.

The codec is David Reid's unmodified **dr_wav**, pinned at
`dfe8377631000664666519fdb83da193fd8037f4`, using its offered MIT-0 license.
Our separate `apu_adpcm.c` adapter presents one block through the public IMA WAV
memory API and consumes the first 64 frames. Its signed header-index clamping
and ignored reserved byte follow the pinned emulator reference; unusual-header
behavior is not a new hardware measurement. No GPL decoder body is added to
this maintained implementation. Full upstream source, license, hash and credit
are retained under `src/third_party/dr_wav`; pilot notices accompany test output.

Qualification is bundled with the existing source fixtures:

- **512 blocks** compare maintained dr_wav output with the separately linked
  private Phobos HLE and xemu-derived APU reference. All 256 header-index byte
  values, mono/stereo, saturation, reserved bytes, the unused final high nibble
  and output guards are covered. These two older donor references share
  provenance and are not counted as two independent silicon observations.
- Authored +/-1 ramps independently test channel interleave, cross-page blocks,
  requests around the 64-frame boundary, loops starting/ending inside blocks,
  seek/write/remap visibility and partial DMA failure. PCM cases remain intact.
- **56 original region/unit cases** now include 16 ADPCM cases. Original NTSC
  region arithmetic and complete conversion helpers supply the real LBO/EBO
  and the 64-frame byte-position quantization; no enclosing API/MMIO completion
  is substituted. Exact source samples are checked independently.
- **Six actual native mixer cases** compare PCM8/16 and ADPCM mono/stereo
  interior loops at 48 kHz, unity gain, with the existing 3e-5 normalized-sample
  tolerance. ADPCM uses the existing private HLE decoder into the actual native
  PCM transport, independently of the maintained APU decoder. This comparison
  covers that decode/transport boundary, not an original whole-driver run.

APU/source tests pass **2/2 in 0.02s**, native source comparison in **1.85s**,
and the combined native/contract suite **53/53 in 23.88s**. Evidence:
`build/blinx/audio-adpcm-{decoder-build,source-build,native-build,full-build,full-tests}.log`
and `audio-adpcm-source-original.aot-private.json`. Reference DLL SHA-256:
`34862b33c0a9f44c7d3a5eeed8131becc5da69420b7326f605ae8975dcbac5ec`.
The complete rebuild changed the native executable identity. A fresh
**190.344s** default-native Blinx regression captured 18 frames: actual Demo,
replayed interruption, loading/title return and Demo reentry. Both Start
press/release pairs were delivered; no STOP/audio-stop/fault was logged.
Evidence is `build/blinx/audio-adpcm-floor-attract/{run.aot-private.json,contact.png}`
and `build/blinx/run-audio-adpcm-floor-attract.log`. Executable SHA-256:
`24f32feb2c5bcfea8aa0818b19c674b12096649decc769f20389059ef976a358`.
The harness terminated the still-running process at its budget; this is not
natural-exit, endurance or cadence sign-off. No LLE-audio title run or family
closure is claimed. The stable private Blinx repository pin remains unchanged.

Reproduce the maintained-device checks with the root `test_apu_source` and
`xlle_apu_reference` targets. Run `native_audio_source_reference.py --xbe <owned
NTSC image> --library <reference DLL> --dependencies <DLL dependency directory>
--output <private JSON>` and the pilot's `pilot_adpcm_reference` /
`pilot_audio_source_lle` CTests. These select separate diagnostic reference and
native executions; production does not run duplicate implementations.

## Original format setter and final 2D routing/gain stage

Executing the complete original format setter `0x178805` exposed a gap in the
previous hand-authored source setup: NTSC DirectSound programs ADPCM as
**S24 + ADPCM container** (`0xa...`), while the earlier fixtures used S16 + ADPCM
(`0x9...`). Both decode identically under the pinned emulator model, but the
maintained source previously rejected the actual title form. It now accepts
both. All **56 source cases** first run the original format setter and its
unmodified IRQL helpers with the voice still unallocated and caller IRQL 2;
then original region arithmetic and unit helpers drive the maintained DMA path.
Stack and callee-saved registers are checked for complete helper executions.
No MMIO availability or helper return is substituted.

`McpxApu::mix_2d_samples` now implements the **final 2D send stage** using actual
guest voice-table routing and eight packed attenuation values, plus the 32
submix-headroom method values. Ordered send slots select left/right channels;
repeated destinations accumulate. Attenuation has 1/64-dB units, `0xfff` mutes
a send, and each submix headroom bit halves its contribution. The stage adds
to the existing 32-by-32 planar bins; it does not clear other voices, advance
CBO, change envelopes or publish completion. Spatial voices, unknown routing
phase bits, bad DMA, non-finite inputs and invalid destination shapes fail.

This is a real component of the LLE pipeline, **not a whole voice processor**.
Its input must already have undergone resampling, filters, envelopes and voice
scaling. The new tests compose it with real source DMA at 48 kHz, no filtering
and unity upstream scaling. Those explicit preconditions do not implement or
qualify the missing upstream stages for the title. General/voice headroom,
nonstationary envelopes, spatial processing and stream ownership remain open.

The complete original volume helper `0x178a7b` supplies **84 cases** across
mono/stereo, empty/default/reordered/eight/duplicate routes and volume/mute
boundaries. Its packed words feed maintained RAM methods and the source/send
chain. Independent expectations check all bins and unchanged guest RAM across
the final stage. `test_apu_source` additionally checks multi-voice accumulation,
submix headroom masking, live gain changes, unused frame slots/output guards,
and terminal failures. No original code bytes are committed.

The real native mixer comparison now runs **24 cases**: PCM8/16 and ADPCM,
mono/stereo, each with default, eight-bin, repeated-bin and empty routing. It
retains actual native consumption and interior-loop cursors. The native path
keeps continuous dB arithmetic: tolerance is 3e-5 normalized sample plus 0.2%
of the reference amplitude for 1/64-dB quantization. A hardware-muted send
allows at most 0.000632 times its source amplitude as quiet native output.
This bounded difference is documented instead of forcing HLE to reproduce
LLE internal arithmetic. Upstream silence/noise, arbitrary rates and post-GP
output are not established by these stationary pre-GP comparisons.

Validation: **84 volume/mix + 55 routing/pitch + 16 fields + 5 negative** original
configuration cases; **56 original format/region/source cases**; root APU/source
**2/2 in 0.06s**; actual native comparison **7.11s**; mixed suite **53/53 in 28.32s**.
Both the maintained machine library and private native targets build. Evidence:
`build/blinx/audio-mix-{configuration-original,source-original}.aot-private.json`
and `audio-mix-{source-build,machine-build,native-build,full-build,full-tests}.log`.
Reference DLL SHA-256:
`892a6f410261ac6a541d11532b259bb3c28ebedc2d890fed3c4aa38d74c95317`.
The title executable retains SHA `24f32feb...976a358`, so the immediately prior
190.344s native attract regression is reused; no redundant attract run or new
LLE-audio title qualification is claimed. **4/9** remains unchanged.

Pinned xemu explicitly describes its sinc interpolation as a quality choice
rather than a confirmed silicon algorithm. [libsamplerate 0.2.2](https://github.com/libsndfile/libsamplerate/tree/0.2.2),
BSD-2-Clause, was reviewed as a reusable non-GPL candidate; no library code was
imported in this batch. Keep source-rate progression and interpolation error
as explicit contracts. Consult existing evidence before any new hardware
measurement; if needed, exact silicon interpolation belongs at the back of
the queue and must not block independent processing or control work.

## Persistent nominal source-rate processing

`src/machine/apu_rate.cpp` connects the real PCM/ADPCM source stage to a
persistent converter per voice. The actual signed pitch word determines source
consumption versus the 48-kHz output clock. Source pulls perform the existing
SGE DMA and CBO writes; filter-buffered output is consumed before requesting
more guest input. An initial test exposed premature prefetch that made CBO
depend on request partition. That is corrected: bulk and seven-frame requests
now agree on samples and fetched source frames in the tested cases.

Pause/lock freeze both history and source consumption. A seek, source binding
change or new voice activation discards obsolete history; pitch changes retain
it. A finite buffer is fetched exactly once, then the converter drains and
reports no further frames. It never repeats the final source sample, fills a
request with invented silence or publishes a guest notifier. Required DMA
failure and unsupported SSL/modulated-pitch requests stop. The caller still
owns amplitude envelopes, filtering, GP submission and completion delivery.

The actual converter is unmodified **libsamplerate 0.2.2**, commit
`c96f5e3de9c4488f4e6c97f59f5245f22fda22f7`, with BSD-2-Clause library sources.
`SRC_SINC_FASTEST` follows pinned xemu's reference choice; its acknowledged
uncertainty about the silicon interpolation kernel remains. This establishes
an executable nominal-rate floor and explicit numerical behavior, not a new
hardware golden for interpolation, prefetch latency or pitch-transition shape.
The upstream developer-only GPL assembly-check script is not built or packaged.
The adapter, lifecycle rules and tests are authored here; xemu's wrapper and
starvation-silence workaround are not copied. Attribution and license are
retained beside the build artifacts and in the repository.

Validation is bundled around actual source consumption:

- Root tests verify commanded tone frequency/amplitude/residual, persistent
  chunking, 36 finite PCM/ADPCM cases with untouched output guards, pause/lock,
  seek, pitch changes, explicit failures and 64 independent lifetimes. Finite
  source counts and CBO remain exact even while output is filtered/resampled.
- **12 complete original pitch-helper cases** drive maintained rate conversion
  and independent tone fits, with original byte/frame helpers reading the
  resulting CBO. The earlier 55 configuration, 84 volume/mix and 56
  format/region/source cases remain green. Public SetPitch/SetFrequency range
  handling is not inferred solely from these internal-helper cases.
- **12 actual native comparisons** compose our maintained source, rate and 2D
  send stages, then compare with real XAudio2 capture at nominal source rates
  from 12 kHz to approximately 192 kHz, mono/stereo. Tests fit phase-independent
  tones at the commanded frequency, check amplitude/residual and unused bins,
  and read real native cursors. LLE amplitude error is below 0.001 normalized
  and RMS residual below 0.0003; native amplitude error/difference is below
  0.002 and residual below 0.001. Native integer-Hz rate rounding is explicit.
  These are bounded steady-tone checks, not transient or whole-title A/V proof.

The full machine and native builds pass. Root APU/source/rate **3/3 in 0.11s**;
combined native/contract suite **54/54 in 32.45s**. Evidence:
`build/blinx/audio-rate-{configuration-original,source-original}.aot-private.json`
and `audio-rate-{build,test-build,machine-build,native-build,full-build,full-tests}.log`.
Reference DLL SHA-256:
`b26e54f73adc27dbf343f16377d14e650f0f1e115fdb5d5677ab51fb77daea25`.
The rebuilt native executable SHA-256 is
`c5ff3de946b93d17c4ec83697443244c8cfbb0c931f10b0ce975d0a727009cfa`.
Its fresh default-native regression `audio-rate-floor-attract` ran **190.329s**:
18 inspected captures show demo gameplay, Start interruption/loading, title
return and demo reentry. Both scheduled Start press/release pairs were delivered;
there was no logged STOP or audio fault. The harness terminated the still-live
process at its time limit; its return code is not a crash or natural-exit claim.
This protects the native path, not whole-title LLE audio or endurance/cadence.
The family stays open: **4/9**.

Build root target `test_apu_rate` and run CTest `apu_rate`; the real native
comparison is pilot CTest `pilot_audio_rate_lle`. The original-code fixture is
`native_audio_configuration_reference.py` with the same owned-XBE/reference-DLL
arguments already documented above. No production backend switching or duplicate
execution is introduced.

## Original voice start and bounded envelope processing

`src/machine/apu_envelope.cpp` adds an executable source/envelope quantum. Each
call processes one 32-output-sample interval; the envelope clock is independent
of source pitch. It composes real source DMA, persistent rate conversion,
signed pitch modulation and amplitude scaling. Filtering, voice scaling,
final sends, GP and completion remain separately owned stages.

The authored envelope model supports disabled, delay, ordinary nonzero-rate
attack, hold and sustain. It writes actual counter/phase/level fields while
preserving the neighboring source offsets. Both envelopes are evaluated before
either is changed; unsupported decay/release/forced-release and zero-rate or
overflowing attack stop without silently advancing the other envelope. Hold
can enter decay, but the next processing call stops at that unqualified curve.
Pause/lock/inactive voices consume no source or envelope time.

`VOICE_ON` previously ignored both envelope-start fields. It now initializes
their independent phases/counters before playback. Unknown start states/flags
and nonzero initial CBO are guarded before list mutation. The latter preserves
the unresolved cursor-reset question instead of adopting xemu's FIXME as fact.
No envelope or source operation clears ACTIVE or fabricates a notifier.

Independent evidence and its scope:

- [XboxDevWiki's APU observations](https://xboxdevwiki.net/APU#Envelopes)
  describe the phase/counter interface, sustain updates and pitch modulation.
  Pinned xemu remains a behavioral cross-check. Some transition details and
  curves are explicitly uncertain in these sources; this is a documented
  reference model, not a new silicon trace. No GPL implementation body is
  copied. New console observations, if needed, stay behind offline work.
- `native_audio_control_reference.py` executes **eight complete original NTSC
  init/filter-default/start cases**: `0x178705`, its actual original vtable
  format callee, `0x179283`, and `0x179c4b`, without replacing callees or returns.
  Preconditions are an allocated nonspatial voice, empty original owner list,
  DISPATCH_LEVEL and synchronous method consumption. Real method writes update
  the maintained voice table; original linked-list writes, notifier IN_PROGRESS
  initialization, phase/counters and 128 actual output frames per case are
  checked. Only admitted methods can report free PIO capacity after processing;
  unsupported transactions stop. This does not emulate queue latency or qualify
  full allocation/interrupt/SSL/completion ownership.
- Root `apu_envelope` tests cover all **16 start-phase pairs**, disabled sample
  identity, delay/attack/hold sequences at two source pitches, stable gain and
  modulation spectra, live sustain updates, pause/lock/inactive state and guards.
  They assert real memory/source effects, including preservation of source
  offsets and refusal to invent downstream completion.
- Native `pilot_audio_rate_lle` now has **18 source/rate/envelope/send cases**.
  Real XAudio2 gain/frequency reproduce stable envelope effects within the
  existing spectral/amplitude tolerances. Integer-Hz and hundredth-dB rounding
  are explicit; mute is compared against the native -100 dB floor. This does
  not claim native time-varying envelope API support.

The original-control fixture uses the same owned-XBE/reference-DLL arguments
as the earlier fixtures, writing
`build/blinx/audio-envelope-control-original.aot-private.json`. Root CTest is
`apu_envelope`. Earlier original configuration/source fixtures still pass.
The full machine/native builds pass; root APU/source/rate/envelope **4/4 in
0.15s**, combined native/contract **54/54 in 33.29s**. The reference DLL SHA-256
is `4222dd53882ee9165208665d529b1fdecf99ff3263940a7ce922d0a7ea5d38b6`.
Logs are `build/blinx/audio-envelope-{build,native-build,machine-build,full-build,
full-tests}.log`; the control, source and configuration original fixtures retain
private JSON under the same batch prefix. The rebuilt native executable SHA-256
is `995adc6fd3982ed97115f9996a16dd74299691e6954e0f8905de37f1267c3b35`.
Fresh `audio-envelope-floor-attract` ran **190.156s**; 18 inspected captures
show demo gameplay, Start interruption/loading, title return and demo reentry.
Both scheduled Start press/release pairs arrived; no STOP/audio fault was logged.
The harness terminated the live process at its limit. This is a default-native
regression, not whole-title LLE audio, cadence or endurance qualification.
Family **4/9** stays unchanged: this does not close audio.

## Withdrawn notifier assumption

The audit found the older `post_voice_notifier` wrote a literal `1` into byte
14 and selected SSLA even for streaming voices. Pinned xemu explicitly marks
its envelope-information byte provisional. That is not independent correctness
evidence. That placeholder was removed, initially guarding all VOICE_OFF work.
The subsequent static-stop batch above restores only the independently bounded
status/stop/idle/unlink route. Unknown payload bytes and SSL owners remain
unqualified. The strict reference rejects payload reads and unsupported owners.

This intentionally withdraws the old LLE notifier qualification. The default
native audio/title route is unaffected. The broader LLE machine still stops on
unqualified stream/linked/multipass VOICE_OFF ownership.
Original settings and real native detach/rebind evidence still stand. Before
closing audio, establish the actual notifier byte/record meaning and delivery
through original caller evidence and independent documentation/observations;
do not restore a fixed envelope value. New console observations, if actually
needed, remain at the end of the burndown. Their necessity is not established.

## Remaining work, in order

1. Preserve the now-connected original IRQ/PIC/ISR/unlink and DPC-queue route.
   Completing the deferred consumer through the original fence is `.115` and
   stays behind independent offline work. Unknown notifier payload fields stay
   explicit. SSL descriptor/source ownership remains open, behind the currently
   reached buffer path rather than ahead of it.
2. Complete the remaining voice-processing path around the maintained source,
   rate/envelope quantum and final 2D send stages: remaining envelope curves, voice
   scaling and filtering, then connect the existing GP donor. Validate actual stream
   output and transitions, not only stationary mixer comparisons.
   Existing original byte/sample conversion, detach, decoder and real native
   output tests are reusable evidence, not a completed LLE audio device.
3. Execute the original DirectSound owner over maintained DMA/interrupt/memory
   services; integrate static title selection and validate actual Blinx audio,
   resource reuse and movie-relative timing against documented tolerances.
4. Put only specifically unresolved silicon/timing observations at the end of
   the burndown. Do not request a console merely because the LLE route is slow.

The shared runtime regression for this batch uses the **default native audio**:
52/52 mixed tests, followed by 190.141s of Blinx with demo/interruption/title/
reentry and no stop/audio-stop/fault. See `SCHEDULING-LLE.md` for exact evidence.
That run protects the existing title route; it is not a new audio-LLE title run.

## References and reuse

- Original NTSC private input: full helpers at `0x1789a0` and `0x178c97`;
  original command emitter also uses the selected-voice/routing/format/volume/
  pitch method addresses. Bindings are isolated in the reference fixture.
  Region-prefix and complete unit-conversion bindings are isolated separately
  in `native_audio_source_reference.py`; no original bytes are committed.
- xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
  `hw/xbox/mcpx/apu/{apu_regs.h,vp/vp.c}`: register locations, field widths and
  memory-side effects. No new GPL implementation body is copied or adapted.
- Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`,
  `src/hle/dsound_buffer_model.c`: reviewed its frequency/position handling and
  host-time cursor model. Existing native real-output transport is retained;
  that host-time model does not establish the device's sample-consumption floor.

The new configuration handler, strict transport and fixture are authored here.
Existing private-pilot and older dependency restrictions remain documented in
`LICENSE-SCOPE.md` and `THIRD_PARTY_ATTRIBUTION.md`; this is no distribution
clearance for the current combined reference executable.
