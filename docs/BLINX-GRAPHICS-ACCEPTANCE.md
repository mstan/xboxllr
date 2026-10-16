# One-run connected graphics acceptance

This is the bounded final execution sequence for G02-G05 in
[the governing checklist](BLINX-QUALIFICATION-CLOSURE.md). It does not close
those rows by itself. Run the maintained GPU-LLE selection with enabled-noise
behavior implemented and independently checked. A private noise-omitting DLL
cannot supply acceptance, even if it reaches every listed milestone.

Use the existing coherent original CPU/USB/files/memory/scheduling and native
APU_REFERENCE selection. Preserve the normal source clock, original rendering,
interrupts, callbacks, waits, ownership and resource transitions. Keep output
muted, with four host cores at Normal priority unless an owner game is active.

## Sequence and evidence

1. Retain actual original launch, AV3/FIELD return, instance production/LOAD and
   successful original drawing. Reuse unchanged startup and representative
   component evidence; do not repeat the entire old matrix.
2. Observe rendered title output and neutral-input entry into actual3D attract.
   Retain two completed moving gameplay pictures, each with FBD1/GPD1, the
   paired original Swap caller and separate completed/submitted driver counts.
3. After gameplay is established, deliver one genuine Start press/release through
   the selected controller source. Retain its delivery journal, actual demo exit,
   relevant content retirement and visible title return. No direct scene change,
   fabricated callback, ready bit or substituted original call is permitted.
4. Keep input neutral and observe fresh attract reentry, renewed original
   submissions and two further completed moving gameplay pictures. Retain the
   required visibility/callback/order and normal content-lifetime evidence.
5. Seal the run only after these milestones. If normal attract retains the device,
   reuse accepted G01 original Reset/Release components for full destruction;
   still observe the actual content lifetime crossed by exit and reentry.

An output snapshot, a marker or a title-like picture alone does not establish
resource retirement. Bind each required lifetime claim to the actual original
producer/consumer and retained completion/owner evidence. A run guard, observation
failure or deadline means partial evidence. Supervisor termination is never
guest teardown. Reuse the owner's accepted HLE appearance evidence and unaffected
state/shader/texture/fetch/alias comparisons. Full4331-draw numerical composition,
unreached precision variants and analog TV behavior are not added to this run.

## Bounded captures selected during the run

Set an existing empty private `XLLE_PILOT_FRAMEBUFFER_CAPTURE_DIR`,
`XLLE_PILOT_DIGITAL_INPUT_CAPTURE=1` and `XLLE_PILOT_FRAMEBUFFER_REQUESTS=1`.
This requests distinct actual driver completion counts, with a maximum of16
records. The existing default four-record diagnostic is unchanged without the
request option. The supervisor must use this milestone stopping condition rather
than the earlier loading-picture runner's first-three-record stop.

Publish `framebuffer-01.request`, then02 and so on in that directory. Each file
must contain exactly `capture-v1` followed by one LF byte. Write a private
temporary file, close it, then rename it into the absent destination. Never
modify a published request. A missing next request waits without taking pixels;
an inaccessible, malformed or concurrently written request fails observation.
The supervisor must stop on `[GPU FRAMEBUFFER] request_failed=1`; this is a
local observation failure and produces neither a normal capture record nor a
simulation guard. Do not wait for a record that will no longer be produced.
The next eligible original Swap return supplies that numbered capture. Requests
can select moments during title, gameplay, exit and reentry; they do not assert
that those phases occurred. Classify the actual output and original state.

Sparse progress lines retain caller, device and driver counts while awaiting
requests. A caller change is evidence of changed execution, not automatic scene
classification. Decode the existing FBD1/GPD1 records using their maintained
tools. Their digital endpoint and precision boundaries remain those in
[NV2A-DIGITAL-INPUT](NV2A-DIGITAL-INPUT.md).

## Start replay only after actual gameplay

Use `XLLE_PILOT_INPUT=replay` and an ordinary fixed replay file containing:

```
0 000000000000000000000000000000000000
20 100000000000000000000000000000000000
220 000000000000000000000000000000000000
```

Set `XLLE_PILOT_INPUT_REPLAY_TRIGGER` to an absolute absent marker path in an
existing private directory. The connected virtual controller stays neutral
until the first actual host poll observes exactly `start-v1` followed by LF.
Publish that immutable marker atomically after the required gameplay observations.
The loaded replay starts on the existing selected reference clock; its times
are relative to this actual arming poll, not process launch or old HLE wall time.
Preserve the consumed-trigger timestamp and delivered press/release rows.
Verify those rows reached the original input consumer; a scheduled row that was
never delivered cannot prove a button transition. Marker removal does not rewind
or retrigger replay. Stale, inaccessible or malformed markers are errors.

These are elective host observation/input controls. They neither seed game state
nor classify phases, and they do not change default HLE execution. Xbox recovery
and enabled-noise hardware evidence remain separate prerequisites.

## Passive content-lifetime evidence

For the connected run, also set `XLLE_PILOT_GPU_LIFETIME=1` with the capture
directory above. One fresh call table observes original Swap and the source-hashed
resource destruction helper at `136B00` (156 bytes, stdcall4). This covers final
unbinding as well as public Release. It retains paired original entry/return
identity, the resource header on entry only, and separate driver completion and
submission counts in `gpu-lifetime.jsonl`. No freed header is read on return.

Each requested picture also records the four original texture-binding slots at
`device+B38` in its `.owners.json`. Associate relevant owners with scene output
and matching normal destruction returns; a binding alone does not prove sampling.
Resource views or shared backing can survive an object's destruction, so do not
interpret every return as freeing its backing allocation. The accepted G01
components supply unchanged lower-level ownership contracts.

The observer copies existing RAM and writes host evidence. It adds no guest
services, clock advances, GPU drains or fabricated completions. Failed reads,
failed writes or the 65536-event bound stop observation; unmatched entries cannot
establish retirement. Seal the files after process exit. These observations and
the unchanged G01 evidence support the normal content transition, not exhaustive
allocation or cancellation coverage.
