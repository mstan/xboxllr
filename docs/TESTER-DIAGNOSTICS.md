# Tester diagnostics

Each packaged native title retains its game log and exit report beneath
`data/diagnostics/<UTC time>-<session>/`. A new launch creates a new session;
it does not overwrite the previous crash. `data/diagnostics/last-run.json`
points to the newest session. Reports identify the actual executable, package,
game revision, framework, mods, controller settings and terminal stop.

## Recording a problem

An unexpected game exit opens a crash report window with the recorded failure
reason and an optional description field. **Copy diagnostic ZIP** prepares a
verified ZIP and copies the actual file to the Windows clipboard. Paste into
a chat that accepts files, or use **Open report folder** to attach it. This
export includes the public failure report, selected diagnostic markers and
validated input records. It excludes console identities, signing keys, native
saves, raw logs and private replay environments. There is no automatic upload.
It is a diagnostic attachment, not a complete replay snapshot. Normal game
closure and unattended automated tests do not open this window.

Blinx and both supported JSRF revisions enable **Crash and input logs** in the
Mods menu by default. Its saved setting applies on the next launch. Play
normally and reproduce the problem. Each launch retains its game log, exit
report, controller input timeline and private starting saves/settings.
**Start with Diagnostics.cmd** enables input recording for one launch.

Renderer stops include target dimensions, guest registers and recent translated
calls, and drain the input writer before exiting. Hardware faults or forced
termination may leave an incomplete final input tail; the report distinguishes
that from a confirmed final flush. Recording uses a bounded 256 KiB writer queue
and a 128 MiB input-file budget. A writer failure is explicit and gameplay
continues. Full scheduler, clock and DSP recording is disabled in normal play.

Send **report.json** and describe the stage/action. Keep the matching private
folder locally. There is no automatic upload. **Do not upload
`PRIVATE-DO-NOT-SHARE` wholesale:** it contains starting saves, console identity
and signing secrets, inputs and logs with local paths.

## Lightweight input playback

Use the identical package, your own matching game and a new disposable work
directory. The helper restores a copy of the starting persistent partition and
console profile, uses cold utility caches and keeps the owner's saves untouched.

```powershell
python tools/replay_native_diagnostic.py G:/Reports/session --check
python tools/replay_native_diagnostic.py G:/Reports/session --root G:/Builds/JSRF --image G:/Owned/JSRF/default.xbe --work-dir G:/Temp/JSRF-input-playback --detail
```

Playback is hidden and muted by default. `--visible` shows it with muted audio;
`--detail` enables verbose diagnostic traces during playback. Recorded controller
states follow their elapsed-time timeline independently of host polling frequency.
`--strict-input-calls` retains the older exact input-call checker.
Input playback repeats controls; scheduling, clocks and device timing are not
restored, so it cannot claim deterministic simulation or the same crash outcome.
The report explicitly records that limitation. The bundled `python/python.exe`
can run either helper in place of system Python.

For interactive investigation, use an opt-in `XLLE_PILOT_DEBUG_PORT` launch
with the lightweight input recorder enabled. The TCP controller operates as
Player 1 and keeps a neutral virtual connection when the selected physical
controller disconnects, including battery loss. Physical input resumes on
reconnection; leased TCP commands take priority while active. `clear_input`
releases automated buttons without disconnecting Player 1. Other ports and
explicit `XLLE_PILOT_INPUT=none` absence fixtures retain their original behavior.
For multiplayer QA, `connect_input 3` explicitly connects virtual Players1/2;
`set_input_port 1 <36 hex digits> <lease_ms>` drives Player2. The Python helper
accepts `press a --player 2`. Masks include Player1 and are limited to four
ports; other virtual ports remain absent until requested. `connect_input 1`
disconnects the additional virtual ports. Leases remain bounded to five seconds
and the interface still requires an explicit localhost debug-port launch.

Treat a recording as a navigation route: observe screenshots at checkpoints,
correct drift with TCP input, and resume from the useful checkpoint. Do not
require a complete deterministic replay before investigating a menu. JSRF's
`tools/qa_roboy.py` retains screenshots and actions for its endgame-save route,
including an explicit position-only shortcut to Roboy. Diagnostic replay mode
itself consumes recorded API results and does not accept live steering; use
the live input source/TCP route for takeover.

## Full session replay development mode

The heavier full recorder is separate development tooling, enabled explicitly
with `XBOXLLR_FULL_REPLAY=1`. It is not the default play-session logger.
Replay starts at the recorded launch using the identical binary, dependencies,
owned game revision and mod plan. Normal translated execution rebuilds native
call stacks. The journal captures CPU/device admission, input and rumble,
clocks, entropy, window observations, file metadata and asynchronous completion
observations. CPU/register/call-flow checks at each turn and rotating memory
checks at frames stop at the first divergence. Actual disk/GPU/audio work must
still complete; a recorded result never bypasses required device work.

This is a replay from launch, not an arbitrary-PC checkpoint or a portable save
state. Native C call stacks are reconstructed by execution. A matching terminal
boundary, every recorded observation and the original exit code are required
before the tool reports `verified: true`. A recorded intentional renderer/native
stop can therefore be a successful reproduction even when its exit code is 2.
A mismatch exits 3 and retains its first divergence in the private replay log.
Hard termination or a hardware exception can leave an unsealed journal; its
logs remain useful but it cannot claim a verified terminal reproduction.

The asynchronous writer has an 8 MiB queue, flushes at most every 100 ms and
compresses journals as gzip with zlib 1.3.2. Journal storage is limited to
8 GiB compressed and 64 GiB expanded per session. Initial state is limited to 2 GiB,
console profiles to 128 MiB and selected HDD images to 32 GiB. Local history
reserves at most 32 GiB and retains at least 4 GiB free space. Existing sessions
are never silently deleted. Remove unwanted sessions after preserving any
needed evidence. A queue/storage/write failure stops recording with an explicit
incomplete warning while gameplay continues. Intentional stops drain accepted
records for up to two seconds. `journal_delivery_complete` requires confirmation
that all accepted records reached the journal; a valid file alone is insufficient.

With the **identical extracted release package** and your own matching game:

```powershell
python tools/replay_native_session.py G:/Reports/session --check
python tools/replay_native_session.py G:/Reports/session --root G:/Builds/JSRF --image G:/Owned/JSRF/default.xbe --work-dir G:/Temp/JSRF-reproduction
```

`--work-dir` must be new and outside the package, recording and game assets.
Replay verifies the package, executable, game, baseline and journal hashes before
starting. It uses isolated saves, settings, caches, temporary paths and any HDD
copy. It is hidden and muted by default, has a one-hour timeout (`--seconds`)
and enables no debug listener. `--visible` shows the game while keeping it muted.
The new directory retains `game.log` and `reproduction.json`.
The bundled `python/python.exe` can run the helper in place of system `python`.

`XBOXLLR_DIAGNOSTICS=1` requests the lightweight input logger.
Full-session development captures use their separate schema and helper.

## Focused renderer captures

The draw-state observer accepts indexed draws without invoking the
full frame-input serializer. `XLLE_PILOT_DRAW_STATE_TEXTURES=1` optionally adds
bounded GPU texture snapshots (32 unique headers, 8 MiB total) and raster state
per observed draw. It does not adopt or modify guest texture bytes. This was
used to distinguish a complete source atlas from destination regions erased by
whole-view clears. It is diagnostic evidence, not a complete scene replay.

Touching `XLLE_PILOT_DRAW_STATE_TRIGGER` captures the next complete swap;
updating its timestamp captures another, retaining distinct record numbers.
All swaps share a 2048-record limit. Optional fixed inputs default to 16 vertices
per draw. `XLLE_PILOT_DRAW_STATE_MAX_VERTICES` accepts 1 through 1024 for small
world/decal meshes; submitted input data across all captures is capped at 32 MiB.
Programmable inputs use separate `program-N.bin` records containing the actual
NV2A code, streams and indices. Their constants remain in the paired
`draw-state-N.bin`. The developer `pilot_shader_test --submitted-program`
observer accepts those two files and an output path to retain raw GPU vertex
results. This does not make a complete scene replay or an expected-image oracle.

Developer runs using the existing `XLLE_PILOT_DRAW_CAPTURE` directory can set
`XLLE_PILOT_DRAW_CAPTURE_AFTER_SWAP=1` to start their draw budget at the next
swap after the trigger. `XLLE_PILOT_DRAW_CAPTURE_FORMAT=0x0e` restricts capture
to the stage-zero allocation format, and
`XLLE_PILOT_DRAW_CAPTURE_UNIQUE_TEXTURES=1` retains one representative draw
per stage-zero header. These filters help inspect transparent materials without
capturing hundreds of repeated particle or fur draws. The existing 256-draw
maximum and source-serialization guards still apply. A representative texture
capture does not establish all uses of that texture or complete-scene coverage.
