# Tester diagnostics

Each packaged native title retains its game log and exit report beneath
`data/diagnostics/<UTC time>-<session>/`. A new launch creates a new session;
it does not overwrite the previous crash. `data/diagnostics/last-run.json`
points to the newest session. Reports identify the actual executable, package,
game revision, framework, mods, controller settings and terminal stop.

## Recording a problem

Blinx and both supported JSRF revisions enable **Replay diagnostics** in the
Mods menu by default. Its saved setting applies on the next launch. Start the
game normally, reproduce the problem and close the game if it remains open.
Send **report.json** and describe the stage/action. Keep the matching private
folder locally so a developer can request the specific evidence needed.
**Start with Diagnostics.cmd** also enables recording for one launch.

There is no automatic upload. **Do not upload `PRIVATE-DO-NOT-SHARE` wholesale.**
It contains private starting saves, console identity/signing secrets, runtime
journals and logs with local paths. Cache files can contain original game data;
a selected HDD image is also copied privately. Original game files must not be
included in public reports or release archives.

Recording takes an independent copy of the complete initial writable state,
including empty directories and utility caches, plus the console profile and
any selected HDD image. The game continues to write its normal persistent
progress. Replay uses a new disposable copy and never restores or replaces the
owner's saves. Linked storage, a changing snapshot or insufficient space makes
replay explicitly unavailable instead of creating a partial baseline.

## Verified session replay

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

Older input-only recordings retain their separate helper,
`tools/replay_native_diagnostic.py`. They are not full session recordings and
cannot establish deterministic simulation or terminal failure reproduction.
Developer launches can request diagnostics with `XBOXLLR_DIAGNOSTICS=1`.

## Focused renderer captures

Developer runs using the existing `XLLE_PILOT_DRAW_CAPTURE` directory can set
`XLLE_PILOT_DRAW_CAPTURE_AFTER_SWAP=1` to start their draw budget at the next
swap after the trigger. `XLLE_PILOT_DRAW_CAPTURE_FORMAT=0x0e` restricts capture
to the stage-zero allocation format, and
`XLLE_PILOT_DRAW_CAPTURE_UNIQUE_TEXTURES=1` retains one representative draw
per stage-zero header. These filters help inspect transparent materials without
capturing hundreds of repeated particle or fur draws. The existing 256-draw
maximum and source-serialization guards still apply. A representative texture
capture does not establish all uses of that texture or complete-scene coverage.
