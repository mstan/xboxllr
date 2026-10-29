# Shared native audio pause

The native buffer owner admits ordinary Xbox Pause modes 0 (resume) and 1
(pause). Pause preserves source position, looping, queued owned PCM and the
notification sample-counter epoch. It does not implement Stop followed by a
new Play. Seeking while paused keeps the voice paused; explicit Stop or
storage replacement ends that paused lifetime.

The original software distinguishes active from inactive buffers. An active
paused buffer reports status 2, or 6 with looping; ordinary playback reports
1 or 5. An inactive Pause returns the mode unchanged: pausing an inactive
buffer returns informational result 1, while resuming it returns 0 without
starting playback. Synchronized mode 2 and unknown modes remain guarded in
the runtime. Their shared-playback owner is a separate capability, NTD-11.

The XAudio2 backend stops and starts the existing source voice without
flushing its queued data. Its feeder continues observing real consumed samples
for notifications while withholding additional PCM during pause. Resume
retains queued samples even if the caller rewrites the source ring meanwhile.
The DirectSound backend owns equivalent paused/loop state around its native
buffer. The LLE reference writes the actual modeled VOICE_PAUSE method and
consults its voice-state bit before source fetching; other voices and the
output clock continue advancing.

Binding selection remains title-owned. The optional `PILOT_AUDIO_HAS_PAUSE`
marker is enabled only when the image-bound profile contains the reviewed
public entry. Older generated headers default to zero. Absent enum additions
are appended, preserving previously emitted absent-entry values. This does not
add a callable Pause wrapper to titles without that admission.

Evidence retained privately with the owned dashboard inputs:

- Eighty cases execute the unchanged original public Pause/GetStatus wrappers
  and software implementations with authored active/paused/loop state. Lower
  lock/IRQL helpers and the hardware pause boundary are hooked. These establish
  the software result/status rules, not physical timing.
- The real guest ABI fixture covers PCM and decoded ADPCM, inactive results,
  paused status, frozen cursors, retained queued samples after ring edits,
  real event notifications, paused seek/stop/storage replacement and repeated
  complete lifetimes. Separate real DirectSound and maintained APU fixtures
  cover the corresponding backend state transitions.
- Eleven combined Windows checks pass in 2.03 seconds. The original full audio
  fixture keeps its partial-loop and pitch assertions when those APIs are
  admitted; a subset title exercises the common contract without invoking
  absent entry numbers. Thirty-four driver and ten profile checks pass.
- Actual dashboard binding refresh changes only its audio metadata header
  among 119 generated files. A separate nonlaunchable Blinx compatibility
  scratch preserves the real prior binding record, wrapper and existing enum
  values, and is idempotent. This is not a Blinx gameplay regression result.

The exact in-flight device quantum and interrupt latency remain under NTD-07.
Menu audio, navigation and overall dashboard behavior require actual title
acceptance; passing these component checks does not establish those outcomes.

Actual executable `5f226640...` passes the reached pause boundary, creates the
remaining startup sound-effect buffers and stops at `D3D_MakeRequestedSpace`.
The bounded run exits 2 before its deadline with 254179 stopped-title-thread
entries and no captured menu. All 103 generated CPU objects remain unchanged.
