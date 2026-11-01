# Native spatial audio

Tracking: `beads-xbox.1.152`; gameplay consumer: `beads-xbox.2.6`.

The original JSRF New Game route requires a mono spatial voice. The native
output consumer now runs that voice and reaches Corn's introduction in the
GG's Garage (`new-game-003`). This establishes functional entry, not complete
audio or campaign qualification. Unsupported backends retain their Play guard.

The shared native CPU service now accepts zero to four guest DWORD arguments
with callee cleanup, alongside the existing ECX object argument. It preserves
the caller's CPU registers and floating environment, stages arguments before
altering a possibly overlapping guest stack, and retains pending suspension
requests. Invalid entrypoints, stack ranges, cleanup and preserved registers
stop explicitly. Generated `g_ebp` and `g_seh_ebp` are published-frame caches;
the bridge restores them after nested calls rather than requiring their
intermediate values to remain unchanged.

An optional image-bound `original-native-spatial-math-v1` proof supplies 17
semantic roots: table, listener and calculator initialization; HRTF lookup;
relative position, HRTF selection, distance, cone angle/gain, weights and
Doppler; source I3DL2 initialization/calculation; packed gains, frequency-to-pitch
conversion and pitch composition.
Addresses and defaults belong to title revision data. Shared runtime code
contains no JSRF addresses. Generation verifies original body hashes, ABI,
closed dependencies and exact default-data hashes. Machine effects and
unreviewed indirect calls are rejected.

The original HRTF lookup uses one mutable SDK function pointer. The prior
private audit missed this dependency. The new proof records the exact call
site, slot and target and verifies its original initializer store. Playback
integration must run that initializer and check the selected target before
using the native calculation. No generic missing-call success is permitted.

Validation so far:

- Production CPU-service bridge tests pass with MSVC and MinGW, including
  real floating rounding/exception-state restoration and fatal ABI cases.
- The exact JSRF image admits 26 helper bodies, 17 roots and seven default-data
  blocks. An older profile has no new arithmetic admission implicitly.
- Sixteen vectors executed the actual generated C helpers through the
  production bridge and the original x86 bytes through Unicorn 2.1.4, with
  control word `027f`. All 76 calculator-result bytes matched in every vector.
  Cases include zero distance, both horizontal directions, front/back,
  elevation and both speaker-derived HRTF modes.

The native consumer calculates source I3DL2, packed send gains and pitch through
those original native helpers. A host-owned snapshot supplies portable FIR,
interaural delay, separate direct/room HF filters and sends to the existing GP
graph. A single XAudio2 operation set publishes pitch, filter parameters and
routing. The audio callback never enters guest code. Mono PCM source positions,
looping and notifications remain source-owned. Known original defaults preserve
genuinely deferred listener/source updates. Original 3D routing uses five sends
and zero source headroom, distinct from the 2D defaults.

Focused impulse checks cover identity output, ear delay, independent room/direct
HF response and transport reset; eight binding checks pass. The optimized MSVC
build and original menu-to-Garage route execute successfully. No audible review,
hardware filter equivalence or exhaustive live-update qualification is claimed.
Those supporting checks remain explicit deferrals while gameplay proceeds.

The owner subsequently directed that deferrable supporting validation follow
JSRF coverage. The game's `docs/NATIVE-GAMEPLAY-BURNDOWN.md` records SV01–SV06
and their revisit triggers; Beads retains that decision. Implement and check
the services actually consumed by JSRF now. Broader vectors, hardware precision,
stress/portability matrices and complete xemu setup do not gate that progress.

Private generated code, original data and evidence remain outside Git under
`G:/Temp/Codex/JSRFRecomp/gameplay-20261020/spatial-probe-001`.
