# Native title TCP controls

The shared native-title runner supports the PSX-style automation workflow:
send real controller input and request the current rendered image remotely.
It reuses xboxllr's existing `obs::DebugServer` transport through a title-neutral
mailbox. No physical Xbox is involved.

Add `--debug-port 4381` to `tools/native_title.py run`. The endpoint binds
localhost and is recorded in the run's `launch.json`. Without this option,
there is no listener or screenshot capture. Automated runs stay bounded and
muted. With no replay, their TCP controller is connected and neutral until
an input arrives. Owner launches retain ordinary controller/audio defaults;
a live TCP lease temporarily supplies player 1 input.

Use `tools/native_debug.py --port 4381` with these commands:

```
status
press down --ms 150
press a --ms 150
press up,a --lx -12345 --ly 12345 --ms 250
clear_input
screenshot build/dashboard-review.png
```

`press` supports the Xbox button names up/down/left/right, start/back,
lstick/rstick, a/b/x/y, black/white and lt/rt. Axes use signed 16-bit values.
Leases last 1..5000ms and then release; absent/expired automation returns to
the selected input source. `status` reports published and actually delivered
input sequences separately. Sending a report does not establish that a menu
handled it; inspect the resulting image/behavior.

Screenshots capture the composed swap-chain image on the graphics thread,
before presentation. This includes adaptive margins, window aspect, upscale
and presentation filters. Desktop occlusion and minimized visibility do not
substitute another image. Readback happens only on request, uses the existing
RHI pitch conversion, and crosses a bounded copied-pixel mailbox. Socket I/O
and PNG encoding happen outside the graphics thread. A missing frame times
out after three seconds and reports an error. The current presented-image
consumer is the Phobos renderer; unsupported renderers report that explicitly.

The wire format extends the existing xboxllr line protocol, rather than
copying PSX-specific CPU or memory services. Commands are `status`,
`set_input HEX36 LEASE_MS`, `clear_input` and `screenshot`; responses are
newline-delimited JSON. Screenshot replies contain shape/frame metadata and
base64 RGBA8. The Python client saves a standalone PNG using its standard
library. Existing `ping`, `rings` and `query` commands remain available.

The shared transport handles partial sends, fragmented requests, bounded
request lengths and slow/idle clients. Mutable input and screenshot state
has its own mailbox; the socket thread never enters guest CPU or D3D calls.
The focused transport fixture exercises actual socket communication, leased
signed-stick/button delivery, a large image response and shutdown with an
idle connection. Actual native-title input and GPU capture are checked
separately in the dashboard run. The live muted run confirms delivery of
Start/A, successful 640x480 presented PNG captures, input release and continued
execution until its 30-second deadline. The images still lack menu labels;
this validates the remote tooling, not menu acceptance. Eighteen combined
Windows checks and 35 title-driver checks pass.
