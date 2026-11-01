# Native listener settings with 2D voices

Owner: beads-xbox.1.147. An optional source-bound listener family models
orientation, position, velocity, distance/rolloff/doppler factors, all-parameter
updates and explicit deferred commits. Admission verifies each original public
wrapper and its called internal helper, including whole-body hashes and exact
RET cleanup. Older profiles retain their explicit unsupported boundary.

The owner stores pending and applied scalar bit patterns, original dirty masks
and apply-low-bit semantics. AllParameters copies the fifteen scalar fields;
the reviewed original ignores its dwSize field. An immediate update commits
other pending listener changes too. Final device retirement clears this state.
Known masks distinguish explicitly supplied values from unqualified original
defaults, so a future spatial implementation cannot accidentally use zeroes as
verified defaults.

The current audio owner admits only 2D buffers. Their samples, gain, transport,
rate and explicit mix-bin routing are independent of these listener transforms.
3D buffers/voice transforms remain guarded and GetCaps does not invent available
spatial voices. This work does not claim spatial rendering or I3DL2 reverb
parameter support.

The real native audio component test passes in 0.67 seconds. It checks all eight
methods through their actual dispatch/stack/ownership interface, deferred and
immediate transactions, independent apply values, original dwSize behavior,
caller input preservation, untouched 2D voice state and retirement. Existing
PCM playback, lock, cursor, loop and release checks pass in the same test.
Actual native JSRF passes its listener setup and reaches the separate
SetI3DL2Listener boundary. No complete scene or attract cycle is claimed.
