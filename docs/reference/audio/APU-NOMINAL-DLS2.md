# Nominal ordinary-voice DLS2 model

`nominal-dls2-linear-v1` is an independently authored, portable signal model.
It is neither MCPX silicon qualification nor xemu waveform equivalence. The
immutable `ApuSignalModel` constructor policy defaults to `Strict`; only an
explicit nominal selection admits the new filter. Graphics selection does not
choose audio policy. The full-system CLI logs this selection and rejects its
use for unqualified netplay. Existing bounded Blinx qualification is unchanged.

The reached ordinary mono profile uses FMODE1, explicitly omitted cutoff-envelope
modulation, headroom7 and the already supported adjacent ownership controls.
Stereo, ParaEQ/multifunction, spatial/multipass, unknown filter history and other
adjacent modes remain guarded. Complete-list
preflight validates this before source/cursor work or new GP publication.
Bypass retains its previous reads and arithmetic.

## Replaceable signal contract

The packed field identities and source-reference normalization are retained:
signed low16 cutoff projects through `exp2(cutoff/4096)`, limited to
`[0.003906,1]`; unsigned high16 projects through `/32768`, limited to
`[0.079407,1]`. Treating the latter as damping/reciprocal Q is this nominal
model's choice, not established MCPX coefficient encoding. No frequency in Hz
is passed into the recurrence.

For each owned channel, the independently authored linear recurrence is
`band'=(1-f*damping)*band-f*low+f*input`, then `low'=low+f*band'`.
The output is low-pass state clamped to normalized sample bounds; history is
not clamped. This deliberately excludes xemu's GPL filter implementation,
nonlinear stabilizer and coefficient-dependent input gain. Its linear DC gain
is unity. Microsoft publishes generic coefficient/state-variable concepts in
[XAUDIO2_FILTER_PARAMETERS](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/ns-xaudio2-xaudio2_filter_parameters);
that is a mathematical reference, not proof of Xbox register semantics.

The pure DSP interface owns two low/band histories and accepts an explicit
coefficient snapshot plus an owned interleaved block. It performs no MMIO,
resampling, envelope advancement, allocation, mixing or completion. Parameters,
input finiteness and history are checked before mutation. It runs after rate
conversion and before amplitude/final sends. Mono coefficient A serves both
owned channels. No silence, filter tail frames, descriptors or completion are
invented.

Histories persist across quanta, coefficient changes and ordinary source/rate
binding changes. Paused/locked/inactive/bypass voices do not advance the LPF.
Successful actual VOICE_ON resets it and establishes history ownership;
replacing the device voice-table mapping invalidates that ownership. Device
construction begins unowned. An unknown retained filtered voice cannot adopt
zero history silently.

## Validation and limits

Authored tests cover exact impulse fractions at f=.5/damping1, unit-coefficient
identity, analytic frequency-response gain, partition continuity, channel
isolation, coefficient changes without history loss, invalid blocks without
mutation, nominal-versus-bypass GP waveform identity, pause/resume, actual
VOICE_ON restart, strict rejection, unknown history, strict modulation and adjacent
unsupported modes before source/GP effects. These validate the declared model,
not physical output accuracy.

Open precision/projection questions and reopening triggers are recorded as
[AUDIO-D17](../../../pilots/native-title/LLE-DEFERRED-QUESTIONS.md#audio-d17--nominal-dls2-signal-projection).

The explicit nominal profile also admits attack rate=0 only with current count=0:
level255 followed by the existing hold phase/count publication. This follows the
reference emulator's instantaneous-attack policy; hardware zero-rate
transients/crackle remain deferred. Strict rejects this case; nonzero count with
zero rate and overflow remain rejected. No counters are reset to manufacture
eligibility.

V1 explicitly omits nonzero FCSCALE cutoff modulation, matching the consulted
reference vp.c FIXME rather than inventing MCPX units. Signed scale is logged on
first processed use and affected frames counted. Base FC0/FC1, raw ENV words and
normal envelope stepping/lifetime remain authoritative. This can audibly differ
from silicon. Reopen when a valid mapping or audible modulation requirements
become available.
