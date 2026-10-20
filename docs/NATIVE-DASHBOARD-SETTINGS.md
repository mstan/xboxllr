# Native Dashboard settings

Tracked by `beads-xbox.1.155` (settings), `beads-xbox.1.156` (Credits) and
`beads-xbox.1.159` (console system time).
The owner preview is an immutable executable snapshot. Investigations use an
independent state directory, a private copy of the input EEPROM, muted output
and TCP controller/screenshot port 4384. No real Xbox operations are involved.

## Reproduced failures and fixes

Selecting Video / Wide Screen reached the previously unsupported kernel export
`ExSaveNonVolatileSetting` (ordinal 29). Another owner interaction reached an
undiscovered original input-property callback at `0x402D0`.

The shared kernel adapter now implements user-setting writes with the actual
four-argument stdcall boundary. It ignores the unused Type argument, accepts
short writes with zero fill, rebuilds the user checksum, updates both the raw
software EEPROM owner and its API cache, and returns real storage failures.
Unknown, factory and encrypted indices cannot modify those regions.

Only the 96-byte user section is persisted in a source-bound record under the
selected native state directory. An atomic, flushed replacement completes
before the in-memory owners publish success. A failed write retains the old
file and both old memory owners. Startup verifies the input identity, exact
record length, record digest and user checksum. The supplied EEPROM file and
its factory/encrypted fields remain unchanged. This native user-settings
storage does not substitute for original-kernel SMBus qualification.

The callback is compiled from its complete original 77-byte body, with its
three existing interior discovery fragments retained. Original constructor
and destructor stores prove its vtable at `0x25758`, slot 2; all 23 table
entries were audited. Only metadata and original-body hashes are committed.
No success stub or interpreter path is added.

The existing user query also now reports DATE and DVD-region fields as DWORDs;
the two four-byte timezone names and whole-user section remain binary.

Time/Date Apply independently reached unsupported `NtSetSystemTime` (228).
The shared implementation sets a process-local console wall-clock offset;
it never calls the host's time-setting service. System-time queries and
absolute dispatcher timers/waits share that clock domain. Changing it rebases
their host deadlines under the dispatcher lock, while QPC/interrupt time,
relative timers and relative waits retain their duration clocks. An alarm
racing a backward clock change is checked against the console deadline before
reporting a timeout. Native NT absolute waits translate their deadlines before
entering the host service; changing time while one is in flight remains an
explicit pre-mutation guard. The two-pointer ABI and accepted high-part bounds
were cross-checked against the locally pinned Cxbx implementation; no Cxbx
implementation body was copied.

## Validation ledger

| Check | Status | Evidence |
| --- | --- | --- |
| All 19 user fields and typed query results | Pass | Actual runtime export calls in `pilot_settings_user` |
| Short, zero-length and whole-section writes | Pass | Actual runtime; checksum and unchanged protected regions |
| File replacement and startup reload | Pass | Real private directory/NT file operations; 0.90 s focused test |
| Rejected/refused write isolation | Pass | Unknown/factory/encrypted indices, oversize input and unavailable storage; injected NT write/rename failures not tested |
| Video / Wide Screen and return | Pass | Original native Dashboard; visible Wide Screen status and persisted record |
| Video / Normal and return | Pass | Original native Dashboard; visible Normal status |
| Video / Letterbox | Pass | Original native Dashboard; explicit Normal/Letterbox/Wide Screen cycle and return |
| Timezone and daylight savings | Pass | Original native menu; Pacific to Mountain and DST Yes saved |
| Time/Date Apply | Pass for session clock | Original native Apply changes time and returns; host clock remains isolated |
| Language | Pass | English to German and back through original native menu |
| Audio settings | Pass | Mono, Stereo and Dolby Surround each selected, saved and visibly reported; unattended output muted, no listening claim |
| Parental movie/game ratings | Pass | Original menus changed and displayed saved rating values |
| Parental passcode | Pass | Original set/verify saves code; a fresh process requires and accepts it; cancel also exercised |
| Auto Off | Pass for setting | Original Yes/No panel saves Disabled and Enabled; actual six-hour shutdown not exercised |
| System Info | Pass | Original floating green panel opens and returns |
| Credits document/controller | Pass, synthetic | Complete notices and controller boundaries in `test_dashboard_credits.py` |
| Credits native presentation | Pass | Actual original panel, Matthew first, scroll/section/page and Back; ordinary System Info restored afterward |
| Network / Auto Sign-In offline route | Pass within offline scope | Each opens native offline panel and returns; separate original XoDash remains deferred |
| Console-time/timer/wait regression batch | Pass | Seven actual runtime tests, 1.12 s; new time/guard/refurb checks plus existing timers/events/delay/worker waits |

## Explicit deferrals

The original Network Settings option launches the separate XoDash executable.
Its actual path reaches display persistence followed by guarded
`HalReturnToFirmware` (ordinal 49). Original XoDash execution and its network
configuration are deferred. The selected offline Dashboard mod instead shows
the native floating Xbox panel saying the Dashboard is offline and Xbox Live
is unavailable. It preserves normal Back behavior. This is an **offline-route
check**, not original network-configuration parity or a successful connection.
The firmware-return guard remains intact for other callers.

Auto Off setting persistence is covered, but its six-hour shutdown is deferred.
Tests never wait six hours or power off the host. The persisted timer flag is
not evidence that the eventual firmware-return path is qualified.

Other AV pack / digital-audio / high-resolution capability branches are not
established by the current composite-pack fixture. They must not be advertised
as qualified from the three visible aspect-mode choices.

Physical RTC behavior and clock persistence across process/power restarts are
deferred; the new clock offset is session-only. Changes concurrent with opaque
native NT absolute waits stop before changing time. Original absolute delay
qualification retains its existing guard. These limits do not prevent the
Dashboard's ordinary Time/Date Apply path or alter relative timer contracts.

The same Apply path reads refurbishment metadata with ordinal 25. This native
storage profile has no raw partition-zero refurbishment-sector owner, so the
adapter returns `STATUS_NOT_SUPPORTED` for valid requests and preserves the
buffer for both reads and writes. The original caller checks the negative
status at `0x3178E` and skips its optional first-set bookkeeping at `0x317B7`.
The factory refurbishment record, power-cycle count and first-set persistence
remain deferred; no invented record or successful disk write is reported.

## Credits

The authored scrolling document controller uses the existing System Info
Level, camera, meshes, green materials, console icon, heading and body Text.
The body contains only the visible document window; Up/Down scroll, Left/Right
move between sections, A advances a page and B returns to the main menu.
Normal System Info geometry/text settings are restored on its next use.
Matthew Stanley remains the first credited person. Complete third-party
notices remain copied verbatim and reachable through the document/index.
The original proportional Text node clips at the native panel edge; line
scrolling exposes wrapped continuation lines. Runtime checks sample document
navigation and do not claim that a person read every notice. Authored controller
tests check every viewport boundary and complete, non-elided notice payloads.

All transformed original XAP/XIP data and the original-body generated C stay
private. The checked-in tools contain authored transformations and assertions.
