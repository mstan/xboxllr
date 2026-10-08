# Calendar conversion: maintained LLE and native HLE

This is the first serial delivery slice of `beads-xbox.1.102`, tracked in
`.103`. It covers kernel ordinals 304 (`RtlTimeFieldsToTime`) and 305
(`RtlTimeToTimeFields`). It does **not** qualify clock generation, RTC state,
`KeQuerySystemTime`, interrupts, timers or scheduling.

## Contract and implementations

Both builds enter through the same kernel dispatch and guest stdcall interface:
two 32-bit guest pointers, a 12-byte stack return including return address,
and preserved EBX/ESI/EDI/EBP. Time is a signed 64-bit count of 100-ns units
since 1601-01-01. Fields are eight signed 16-bit values: year, month, day,
hour, minute, second, millisecond, weekday (Sunday = 0).

Ordinal 305 writes 16 field bytes and truncates sub-millisecond time. Ordinal
304 returns BOOLEAN in AL, writes eight time bytes on success and preserves
the destination on invalid fields. Input/output may be unaligned or overlap.
No asynchronous completion, handles, persistent device state or callbacks are
involved. Guest buffers must be mapped/readable and the destination writable;
fault recovery and arbitrary negative/overflowing timestamps are not qualified
by this slice. Successful sampled dates cover the epoch, 1970 and leap/century
boundaries in 2000, 2100 and 2400. This is bounded evidence, not exhaustive
proof over every representable timestamp.

- **HLE (default):** `system_time.inc` delegates to the host NT calendar APIs,
  copying the same input/output layout. No interpreter or kernel image is linked.
- **LLE:** `calendar_lle.c` executes the owner's hash-bound original kernel
  routines and their real callees. There are no instruction or callee replacement
  hooks. Each call gets an isolated x86 engine, original read-only kernel image,
  stack and buffers. Scratch relocation retains alignment and overlapping
  address relationships. Only a real return with the expected stack/preserved
  registers publishes output. A failed execution stops visibly.

The LLE mode intentionally favors a simple reference over performance. Its
Unicorn dependency and private original kernel bytes are confined to this
already private, GPL-dependent pilot. They are not a non-GPL shipping solution.
The original image and generated header stay in ignored build output.

## Build and run

Use the normal native-title configuration from [README.md](README.md), in a
separate build directory, with these additional CMake settings:

```text
-DPILOT_CALENDAR=LLE
-DPILOT_CALENDAR_KERNEL=<owned flat live kernel image>
-DPILOT_CALENDAR_UNICORN=<Unicorn 2.1.4 Windows package root>
-DPILOT_CALENDAR_UNICORN_NOTICES=<directory containing original 2.1.4 COPYING>
```

The package root contains `include/unicorn/unicorn.h`, `lib/unicorn.lib` (the static library). `calendar.cmake` rejects unqualified kernel/engine hashes or
a changed license file. It links the static library, copies its licenses and records input
identities in `calendar-private/provenance.txt`. The qualified flat image has
SHA-256 `827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`;
the compressed BIOS extraction is not that image. Requalify a new revision
explicitly instead of removing the hash check.

Build `xlle_native_title` and `pilot_contract_test`, then run
`ctest --test-dir <build> -C Release -R pilot_system_clock --output-on-failure`.
Run the executable with the normal owned XBE, EEPROM, media and state arguments.
The startup log records build selection; a separate bounded first-call message
proves each ordinal actually returned from original kernel execution. Merely
seeing the startup selection is not execution evidence.

For HLE, configure `-DPILOT_CALENDAR=HLE` and run the same contract test in that
build. No runtime fidelity flag, live fallback or duplicate production
execution exists.

## Independent qualification

`tools/native_clock_reference.py` executes the original routine at its real
export address in a separate oracle, checks independently calculated Gregorian
results, and verifies invalid-output preservation and the stdcall ABI. Its 99
overlapping/page-crossing cases establish the buffer behavior before the
maintained LLE adapter uses scratch relocation. The native contract test then
checks the same expected effects in either selected build. The oracle also
has system-clock snapshot tests; those are not evidence that this change
implements a complete LLE clock service.

Both selected builds and the original oracle pass these cases. The first
actual-title test exposed a host integration error: the pilot's first-chance
fault guard ran before Unicorn's legitimate translation-buffer commit handler.
The LLE configuration now installs the terminal guard as an unhandled-exception
filter. The calendar test installs the actual production handler, checks high
guest-stack addresses, and a separate child test proves a genuine unmapped
access still stops with exit code 2. No exception is fabricated or resumed by
our adapter. The HLE build retains its existing first-chance guard.

All **38 native tests pass** with calendar LLE selected. The qualified
190.17-second Blinx run executes **both** ordinals through this maintained LLE
route, renders Demo play, accepts Start interruption at 90 seconds, returns to
the title menu and renders the next attract gameplay entry. Nineteen frames
were captured; there are no terminal guard/fault messages. The harness ends
the child at its explicit deadline (recorded exit 1), not a title crash.
This is an integration regression, not a new complete-loop endurance or speed
qualification. HLE rendering/audio and other service families remain selected.

Private evidence (not committed):

- `build/blinx/calendar-original-reference.aot-private.json`: original kernel
  oracle, known calendar results and 99 buffer alias cases.
- `build/blinx/calendar-final-qualification-ctest.log`: complete LLE-selected
  native test suite, including actual fault-handler integration.
- `build/blinx/calendar-hle-fixed-tests.log`: same calendar and fault-guard
  tests with the HLE implementation selected.
- `build/blinx/calendar-lle-qualified-20261008/run.aot-private.json` and
  `run-calendar-lle-qualified-20261008.log`: configuration, runtime call evidence,
  captures and resource samples. Executable SHA-256:
  `1ccbc220f862f707b79370e69de9dd5b96ad124d4b45f5dcf648e1d3831a415a`.
- Captures 9/12/16 show first Demo play, returned menu and second Demo play.

The preceding failed actual-title runs and debugger stack are retained under
`calendar-lle-attract-20261008`, `calendar-lle-fault-site-20261008` and
`calendar-fault-debug.aot-private.log`; they explain why the production-handler
contract test was added. This closes only calendar conversion, not its parent
clock/scheduling family.
