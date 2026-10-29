# Debugging a black or stalled dashboard

Credit to **sp00nznet**, whose
[xboxdashboard investigation](https://github.com/sp00nznet/xboxdashboard/blob/34c2dda6b444af88dc22d404846db206a630c7fc/README.md)
motivated this separation of guest progress, command submission, rendering and
presentation. This is original xlle guidance incorporating those diagnostic
lessons; no external emulator code, generated guest code or assets were imported.
The [source audit](https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/PERF-EXTERNAL-REVIEW-2026-09-06.md) records what was checked.

## Establish which machine and executable you are investigating

Keep the flash, decrypted 2BL, EEPROM, full HDD image, executable identity and
input route together in a private run record. Use the [input guide](../../INPUTS.md) to
check the format and paths. A dashboard XBE alone does not contain all its assets.
Do not replace the HDD with an extracted host directory in the LLE path.

The external project's
[XBE analysis](https://github.com/sp00nznet/xboxdashboard/blob/34c2dda6b444af88dc22d404846db206a630c7fc/docs/xbe_analysis.md)
is specifically for build 3944. Its function addresses, section boundaries and
signatures are leads, not portable labels. Verify your own XBE's build and layout
before using them. Classifier names alone do not prove function boundaries or
justify replacing a guest routine.

## Capture a bounded observation

From the repository root, after preparing a Release build and private inputs:

```powershell
./tools/run_stockdash_replay.ps1 -Label dashboard-triage -ExpectedFinalSha ''
py -3 tools/summarize_xbox_report.py build/dashboard-triage.report.json
```

This records one diagnostic replay with frame-series checkpoints. It is not a
performance acceptance set. Clearing the maintainer-specific final-SHA gate lets
you inspect another private fixture; it does not establish that fixture as
correct. The harness can still fail on a machine fault or other required checks;
retain its stdout/stderr and partial artifacts when that happens.

Keep reports, logs and frames private. If another interactive run is active,
inspect an existing report first; a concurrent run contaminates timing evidence.

## Find the first layer whose evidence stops advancing

| Observation | Next evidence to inspect |
|---|---|
| Guest faults or fails before useful NV2A work | Stop reason, interrupt context and `[ring] block-pc` history. Check the actual firmware/assets and guest path before changing rendering. |
| Surface clears occur but no geometry appears | Compare PFIFO progress, method/batch counts, generic skip census and every relevant raster family. A successful clear alone establishes no UI geometry. |
| Geometry is submitted but the captured frame is wrong | Check admission/rejection reasons, color and depth writes, texture source/resolves, active surface, clipping and destination blending. Zero counters for one specialized family do not imply zero draws globally. |
| Guest scanout captures look correct but the live window does not | Check scanout selection and interactive capture/present counters, cadence and presentation errors. Repeated successful presents are not proof of distinct guest frames. |
| Captures or guest PCs stop changing | Distinguish an expected idle/wait state from a deadlock using IRQ/input/device progress and the selected input script. One repeated PC or image is insufficient. |

The executable prints `NV2A raster`, per-family raster statistics and the generic
skip census at shutdown. Block-PC history is a bounded ring of translation-block
entries, not a recovered guest call stack. Missing postmortem output after abrupt
termination is missing evidence; it does not prove the guest made no progress.
For presentation measurements, see [the counter definitions](https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/PERF-PRESENT-2026-09-06.md).

If command order around a particular clear is the unresolved question, append
`--pfifo-clear-trace <color-address-from-this-run>` to a direct boot invocation.
The address must come from that run's observed surface. This bounded trace arms
on the matching clear and records through the subsequent selected BEGIN/END;
it does not establish GPU completion or flip timing. Avoid transplanting an
address from another dashboard build or fixture.

## Follow failures without changing their meaning

Use source findings about asset loading, integrity checks and thread waits to
select observations. Preserve the guest's response to a failed check. A bypass
that reaches the next screen is not evidence that the original failure was
fixed. Any future opt-in HLE implementation must first demonstrate the same
observable behavior as the LLE floor, including failure paths.

Return to the [performance methodology](../testing/PERF-METHODOLOGY.md) only after the
behavioral issue is explained. A single diagnostic run cannot qualify an
optimization or demonstrate 60 FPS with headroom.
