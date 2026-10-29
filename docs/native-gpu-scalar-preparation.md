# GPU callback scalar preparation

CullMode and StencilEnable may lower an immutable local host-state tuple before
an outstanding GPU callback completes. Guest render-cache writes, native state
changes and their batch flushes still occur after the original callback barrier
and immediate-mode guard. Another guest can run during that wait, so publishing
state early would be incorrect. The input is read again afterward; a changed or
unavailable input takes the original complete service path.

Preparation requires the actual guest CPU owner and disjoint persistent output
for every queued and running callback. Callback ABI setup, stack writes and
checkpoint execution remain serialized by the existing guest CPU ownership.
The metadata does not assert that all guest writes are disjoint: preparation
publishes nothing, and rereading the input after the barrier rejects changes
from stack writes or any other intervening execution. No numerical stack
non-aliasing assumption is required. The short native-reader lease excludes outstanding
OS writers and prevents a new tracked native writer from starting during input
capture. It does not exclude guest writers; guest CPU ownership supplies that
separate condition. No resource pointer or guest stack pointer is retained by an
asynchronous renderer operation.

`tools/native_pilot.py` emits `pilot_gpu_callback_effects.h` and records its hash
and proof in `generation.aot-private.json`. Existing private generated inputs can
be refreshed with `tools/native_gpu_callback_effects.py --xbe <image> --output
<generated-output>`, then configured again. The refresh validates the complete
existing inventory first and changes no generated CPU body. Unknown images or
changed complete callback closures emit version0; artifact tampering is an error.
For titles using `tools/native_title.py`, use its existing `bind` action before
configuring instead: it revalidates the prior preparation and every selected
binding, refreshes this metadata, then publishes the new preparation receipt.

The reviewed Blinx metadata in
`pilots/native-title/profiles/blinx-gpu-callback-effects.json` identifies the
persistent eight-byte timestamp output of the complete original BCC00/F4679
closure and binds both original and generated
body hashes. This is an audited assertion, not automatic semantic inference from
a hash. Its original timestamp callback still executes. The writable `.data`
section also has an executable flag; no non-executable-section assumption is made.
Dynamic context arithmetic refuses overflow and validates the current logical
write permission and output window. Unknown callbacks always keep the old path.

The admitted configuration uses native scheduling, QPC timestamp service and the
Windows write observer. Original scheduling, USB, I/O, memory or file devices,
reference/device audio and the original kernel clock have broader effects and
compile the original dispatch path. Unqualified images and portable hosts do
the same. This removes preparation and running-callback tracking overhead from
those configurations. `XLLE_PILOT_GPU_SCALAR_PREPARATION=0` is a startup diagnostic
control; qualified builds enable preparation by default. It is not a game UI mod.

The synthetic generation tests cover exact closure/extent and writable-window
qualification, changed bodies, inventory tampering, idempotence and unsupported
compile configurations. The real GPU fixture checks old guest/native state and
completed pixels in the callback, then late publication, changed input and
alias/writer refusals. Its explicitly logged 600ms idle tail supports telemetry
observation only; it is not timing or GPU-load evidence.

The R06 investigation measured an isolated private-admission Blinx package at
`ef475888405a0a81522e5df778df159e22283976` against accepted engine
`31a89ec9f2e432415f88cd0e95558ea9d128a1be`. Two adjacent matched comparisons observed
7.72% and 9.29% lower CPU per update, median 8.49%; repeated baseline/candidate
samples varied 3.06%/4.71%. Output remained near its 30Hz cap and host load varied.
This is a bounded route result, not an uncapped FPS or all-games claim. Supported
integration and final package results belong to Beads issue`beads-xbox.1.206` and
campaign`beads-2wy8.1`. This scalar family does not claim to replace the complete
draw-preparation, submission or resource-retirement owner.
