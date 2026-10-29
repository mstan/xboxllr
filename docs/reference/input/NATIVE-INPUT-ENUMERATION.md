# Initial native controller enumeration

Owner: beads-xbox.1.145. `XInitDevices` starts initial enumeration. The shared
native service previously sampled every host port immediately, exposing a new
insertion before the first original input wrapper could finish initialization.
JSRF's original wrapper at `166970` rejects a change during its enumeration pass
with `E_FAIL`. Its caller then skips creating the renderer helper and later
faults through that missing object. Native debugger return observations establish
that chain; skipping the later null access would not fix it.

Native input now retains a pending initialization owner for 500 milliseconds.
Queries do not sample or expose devices during that phase, Open returns the real
not-connected result, and the optional enumeration-status API reports busy.
The first query at or after completion samples actual host ports and publishes
their ordinary insertion masks. GetDevices acknowledgement and subsequent
hotplug, stale handle generations and input reports retain their existing rules.
There is no sleep, extra polling thread, forced successful game return, or
title-address special case.

The initial interval follows the established HLE policy in
[Cxbx-Reloaded Xapi.cpp](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/XAPI/Xapi.cpp),
which documents premature-enumeration failures including Panzer Dragoon Orta.
The owner's original XInitDevices body starts the USB initialization chain;
original GetDeviceChanges computes current/previous/changed masks. Exact physical
enumeration latency is deferred precision, rather than a hardware timing claim.
Implementation is independently written for the existing native service.

Nine component checks pass across the dashboard and full JSRF input profiles
(four in 0.10 seconds and five in 0.11 seconds). Deterministic checks cover no early sampling/handles, the instant before and
exactly at completion, busy/idle status where admitted, the first real insertion
exactly once, and the existing hotplug/handle contracts. Component tests honor
each bound profile's optional capabilities/feedback APIs. Actual native JSRF
passes the failing initialization, reads its font, creates a native font shader,
and reaches the next explicit packed-byte vertex declaration boundary.
