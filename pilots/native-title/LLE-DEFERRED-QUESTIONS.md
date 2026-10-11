# LLE questions safely deferred behind the working foundation

Owner steering on 2026-10-10 permits deferring precision and coverage questions
that do not prevent a usable, repairable foundation. Append questions with their
evidence and reopening trigger; preserve resolved questions and their outcomes.
Deferral does not count as qualification. Missing service routing, clocks,
ownership or required processing stages remain implementation work.

The existing 14 graphics questions and their replay evidence remain in the
[graphics queue](GRAPHICS-DEFERRED-QUESTIONS.md). This companion queue records
questions that also affect other service families.

| ID | Question | Evidence and reason it can wait | Reopen when |
| --- | --- | --- | --- |
| LLE-D01 | What additional x87 precision, empty-tag, exception and lazy-NPX semantics must the native generated FP model preserve? | The owned original thread creator establishes saved CW `0x027f`, SW/tag zero and MXCSR `0x1f80` through twelve actual lifecycles. Native startup control/status can be corrected independently. The current generated stack remains double-backed and does not represent empty tags; those explicit limits are preserved. This defers broader arithmetic fidelity, not the original callback's required state transport. | A supported title observes these omitted semantics, a representative arithmetic mismatch appears, or a replacement generated FP model is admitted; replay the retained original context fixture and transport cases. |
| AUDIO-D01 | What are the measured decay/release coefficients, level quantization, sustain threshold, phase endpoint and completion latency? | Pinned xemu `4820884`, `vp.c` lines 770-817, explicitly marks curve details uncertain. The working ordinary-buffer model isolates a declared exponential -60 dB policy, eight-bit floor and bounded endpoint in `apu_envelope_curve.hpp`; it does not claim measured silicon curves. | An independent waveform/counter observation exceeds the declared model's tolerance, or an original consumer needs exact level/phase/completion timing. Replace the isolated policy and replay the lifecycle/ownership fixture. |
| AUDIO-D02 | What are zero/overflow attack and nonzero initial-CBO reset semantics? | Pinned `vp.c` lines 712-737 note uncertain overflow/equality behavior; startup also has an unresolved reset detail. Those modes retain explicit guards while ordinary supported attacks and buffer starts remain usable. | A valid original caller reaches these guarded parameters; establish their behavior before widening admission. |
| AUDIO-D03 | How do repeated release and live release-rate/source rebind affect continuity? | Pinned `vp.c` lines 277-278 mark restart/count behavior uncertain. The declared nominal policy starts an explicit release from the current stored level and resets private history at start/stop/source/table/SG rebind, preserving the replaceable control boundary. | An original repeated release or live reconfiguration requires different phase/continuity behavior; compare retained control and source-clock cases before changing policy. |
| AUDIO-D04 | What are exact DLS2 coefficients, range/rounding, filter history and cutoff-envelope modulation? | Pinned `vp.c` line 1440 leaves cutoff modulation incomplete and line 1467 leaves ParaEQ incomplete. Precise filter behavior can be measured after a working stage exists; the new unamplified rate/control boundary reserves the correct insertion point. Missing DLS2/ParaEQ/spatial stages remain active implementation gaps. | After static DLS2 implementation, independent impulse/tone/cutoff observations exceed tolerance, or a supported original mode requires modulation/history extension. |
| AUDIO-D05 | What gain mapping consumes the voice HEADROOM bits? | `apu_regs.h` defines bits 13-15, but the pinned xemu and audited donor do not consume them. The ordinary `HEADROOM=7` path remains usable; other values retain a guard. Existing submix headroom is a separate implemented stage. | An original caller emits another voice-headroom value or measured volume requires this stage. Establish the mapping before admitting it; broader missing scaling remains active work. |

Initial FP values no longer need a console query. Original/native callback state
transport and the scheduling idle/compute clock path remain active integration
gaps in [SCHEDULING-LLE.md](SCHEDULING-LLE.md), rather than deferred questions.

Audio source evidence and active stage/owner dependencies are recorded in
[AUDIO-LLE.md](AUDIO-LLE.md). The private source handoff retains the pinned
register/source hashes. No new audio playback or console writes are required
merely to append this queue.
