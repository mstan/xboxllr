# Bounded original audio contract

Tracked by `beads-xbox.1.113`. Audio is the active independent offline family;
the original-scheduler/GPU startup experiments are not prerequisites. The family
remains **open**, and the overall burndown remains **4/9 qualified**.

The native title continues to select its real XAudio2/DSP implementation.
The audio LLE floor still needs sample processing, cursor/completion and whole
driver integration before it is a qualified selectable title backend. Existing
audible HLE output does not remove that requirement.

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
and explicit PCM/ADPCM source pulls.
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

## Withdrawn notifier assumption

The audit found the older `post_voice_notifier` wrote a literal `1` into byte
14 and selected SSLA even for streaming voices. Pinned xemu explicitly marks
its envelope-information byte provisional. That is not independent correctness
evidence. The placeholder is removed; VOICE_OFF now fails before writing a
completion record or changing voice state. The strict audio reference never
admitted that unqualified method. The inactive-voice trap/IRQ implementation
remains executable and tested with its own stated input state.

This intentionally withdraws the old LLE notifier qualification. The default
native audio/title route is unaffected, but the broader legacy LLE machine now
stops at VOICE_OFF until its record/SSL ownership contract is implemented.
Original settings and real native detach/rebind evidence still stand. Before
closing audio, establish the actual notifier byte/record meaning and delivery
through original caller evidence and independent documentation/observations;
do not restore a fixed envelope value. New console observations, if actually
needed, remain at the end of the burndown. Their necessity is not established.

## Remaining work, in order

1. Advance the independent signal-processing path: connect routing, pitch,
   gain and the existing GP donor to the maintained PCM/ADPCM source stage.
   Existing original byte/sample conversion, detach, decoder and real native
   output tests are reusable evidence, not a completed LLE audio device.
2. Complete original control ownership, notifier/SSL record qualification and
   delivery, stop/restart and pipeline timing. Keep the unqualified boundary
   guarded while independent processing work advances; its need for a new
   console observation has not been established.
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
