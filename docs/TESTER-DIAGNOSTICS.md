# Tester diagnostics

Each packaged native title retains its own game log and exit report beneath
`data/diagnostics/<UTC time>-<session>/`. Starting the game again creates a new
session; it does not overwrite the previous crash. `data/diagnostics/last-run.json`
points to the newest session. Reports record the actual executable/package hashes,
framework revision, selected mods/controller/options, operating system, process
exit code and a terminal native stop reason when one was printed. A session left
`running` means its supervisor did not finish reporting; it is not proof of a crash.

## Recording a problem

1. Open **Start with Diagnostics.cmd** instead of Start Game.cmd. This enables
   recording for this launch only; choose your controller/mods normally.
2. Reproduce the problem, then close the game if it is still running.
3. Send the session's **report.json** and describe the stage/action. Keep the
   matching private directory locally so a developer can request the specific
   evidence needed to reproduce it. There is no automatic upload.

**Do not upload `PRIVATE-DO-NOT-SHARE` wholesale.** It can contain the EEPROM,
console signing keys, private saves and raw logs with original file paths. No
original game XBE, assets, kernel dump or HDD image is copied into the session.
Do not include your original game files in bug reports.

Diagnostic mode makes a bounded copy of the initial native save/profile state,
records the resolved mod plan and runtime settings, and captures each state,
capability and rumble result at the actual native controller boundary. Changes
made by the game continue to use its normal saves; recording does not replace
or restore the user's persistent progress. Initial snapshots have a 128 MiB
budget each. A selected HDD image is **not** cloned automatically: that session
retains inputs/logs but reports that initial disk state is unavailable. Linked
or oversized save trees likewise report that reproduction is unavailable.
The state copy includes persistent partition1 (`E:`, including UDATA/TDATA).
Partition2 original Dashboard content, utility cache partitions3–5 and the
utility-cache manifest are omitted. Reproduction rebuilds caches from the
owned game files; it starts with cold caches and cannot reproduce a cache
corruption or cache-timing issue exactly from this recording alone.

The input writer has a fixed 256 KiB queue, drains every 100 ms (or 256 records),
and stops capture at 128 MiB. A full queue or I/O failure stops recording with an
explicit warning while gameplay continues. An abrupt exit can lose the queued
tail, normally up to 100 ms. Existing sessions are never silently deleted;
remove unwanted diagnostic session directories yourself when finished with them.
An intentional native stop drains the queue for up to two seconds before
exiting. `input_delivery_complete` is true only when the native writer confirms
that every queued record was flushed and the final count matches the file.
Hard process termination and native hardware exceptions can still leave an
explicitly unconfirmed tail; the recorder never drains locks in an exception
handler.

## Offline reproduction

With the **identical extracted release package** and your own matching game:

```powershell
python tools/replay_native_diagnostic.py G:/Reports/session --check
python tools/replay_native_diagnostic.py G:/Reports/session --root G:/Builds/Blinx --image G:/Owned/Blinx/default.xbe --work-dir G:/Temp/Blinx-reproduction --visible
```

`--work-dir` must be a new directory outside the package/session/game assets.
Reproduction copies only the verified initial snapshot entries and writes to
that disposable storage. It never writes to the original saves. The recording's
binary, package, game, snapshot and mod hashes are checked before starting.
Reproductions are muted, have a five-minute limit by default (`--seconds`), and
never enable a debug listener or reuse an original HDD path from the recording.
The packaged Python executable can also run the helper: replace `python` with
`python/python.exe` from the extracted release.

**This is input-boundary reproduction, not deterministic full-machine replay.**
Unlike the SNES/PSX checkpoint recorders that informed this design, the native
Xbox host does not yet restore a complete CPU/device/scheduler checkpoint here.
Host clocks, thread scheduling and external content can diverge. A changed call
kind/port/rumble argument stops with exit 3; exhausted recorded input stops with
exit 4 rather than claiming successful reproduction. The private reproduction
directory retains its own `game.log` and `reproduction.json`. A matching input
stream alone does not prove that the original crash was reproduced. Full native
checkpoint/replay and an explicitly prepared initial HDD clone remain separate
extensions; old save-state compatibility is not required.

Developers can enable the same session mode through `XBOXLLR_DIAGNOSTICS=1`.
Small authored tests exercise the real C input recorder/replayer, queue overflow,
changed call order, malformed input, snapshot ownership and separate save storage
without starting a game. This code is shared by Blinx, JSRF and Dashboard.
