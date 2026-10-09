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
implementation. It accepts only the implemented 32-bit configuration writes.
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
and terminal-fault behavior, retaining the existing VOICE_OFF/notifier/IRQ
lifecycle tests. It passes in 0.01s. The private original/model fixture passes;
evidence: `build/blinx/audio-configuration-original.aot-private.json`, reference
DLL SHA-256 `41d8fdcea694dd1107f7f73d909773ab55382a97aff3b7f1816b3d3393e2e3df`.

This is original-helper plus real device-configuration evidence. It does not
execute the enclosing DirectSound lock/command emission path end to end, nor
qualify every declared parameter's downstream signal-processing behavior.

## Remaining work, in order

1. Buffer DMA/sample consumption and current/end/loop transitions, including
   stop, seek, pause, restart and ownership. Use original XDK programming/cursor
   helpers and xemu APU behavior; compare native output/cursors after the LLE
   route works. Retain explicit guards for unsupported processing modes.
2. Connect routing, pitch, gain, ADPCM and the GP graph to that transport.
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
