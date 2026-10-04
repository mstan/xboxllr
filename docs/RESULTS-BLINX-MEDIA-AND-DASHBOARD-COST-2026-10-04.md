# Blinx launch probe and dashboard host-cost findings

Owner direction: Blinx exploration may proceed before dashboard original-speed
qualification. The dashboard remains an active objective and regression
workload. Parked optimizations remain candidates for later title-specific
requalification; their [retry conditions](RESULTS-REMAINING-DONOR-SPIKES-2026-10-04.md)
are recorded individually. Tracking: `beads-xbox.1.7.1` and `.17`.

Branch: `bringup/blinx-media-20261004`, based on `1fbed1d`. No parked optimization
was combined into this branch.

## Real optical transport, first observed launch blocker

`--dvd <raw image>` attaches read-only 2048-byte optical sectors to IDE drive 1.
The image is neither patched nor rebased, and attachment supplies no fabricated
authentication metadata. `--dvd-trace` records reached packet commands and reads.
The implementation extends the existing xemu-derived controller with:

- Actual READ(10)/READ(12), capacity and mounted-media status.
- PIO replies split at the host byte-count limit, including odd final-byte bus
  padding; scatter DMA through the real PRD/memory/IRQ path.
- Out-of-range SCSI errors, bounded transfer limits, and completion only after
  actual data delivery.
- Drive-backed SMC tray state, ATAPI media-removal locking and start/stop state.
  An unimplemented SMC motor command with mounted media stops explicitly.

Protocol source: xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
`hw/ide/atapi.c` (`cmd_read`, `cmd_read_cdvd_capacity`, reply phases,
`cmd_start_stop_unit`) and `hw/xbox/smbus_xbox_smc.c` (tray state).
The existing IDE/PIC implementation supplies interrupt and DMA delivery.

The actual kernel probe used the previously audited full Blinx Rev 1 image,
7,825,162,240 bytes, SHA-256
`0e7a024d3d9edf6c6394e9a2943a04d5c93b81bea95ce0137bb3cb459e74fd6b`.
Its XDVDFS base is `0x18300000`; that remains a filesystem location inside the
image, not a drive-offset hack. This revision uses XDK 5558 and is distinct
from the original XDK 4831 compiler input. Source inventory remains private at
main `build/aot-20261002/media.aot-private.json`.

The kernel saw mounted media, executed TEST UNIT READY, then requested
MODE SENSE(10), page `0x3e`, using DMA. It stopped after **53,516,892 guest
instructions**, at `0x8001857e`, with:

> Xbox DVD authentication mode page 0x3e: media attached, authentication contract unimplemented

Process exit was 2. **No Blinx entry or rendered game scene is claimed.** The
first blocker is now an observed optical authentication request. The HDD used
the existing provisioned locked configuration and copy-on-write storage; source
images and XBE certificates were unchanged. There were no guest optical sector
reads before this authentication request.

All 80 HLE-build tests pass. New authored tests cover multiple PIO phases,
odd replies, complete sector bytes, READ12 scatter DMA and write extents,
no premature completion, range/zero-length commands, tray state/locking,
unimplemented authentication/oversized transfers, and unchanged image bytes.
The final SMC motor guard has an additional passing focused check.
The separate LLE-renderer build also compiles, passes the optical transport
test and reaches the same authentication stop at the same instruction count
and PC, using the CPU floor without a private AOT input. Its evidence is
`build/blinx-rev1-lle.log`; this is a launch-path check, not a full LLE gameplay
or performance qualification.

Current optical scope is deliberately explicit: reads above 128 KiB stop until
streaming is implemented. Xbox authentication, physical DVD metadata commands,
SMC motor/event sequencing and arbitrary game-media coverage are incomplete.
Capacity and data transport do not qualify those behaviors.

## Why the donor authentication path was not copied

The inspected phobos `kernel_bridge.c` fills the authentication page with
partition selected, CDF valid and authenticated. sp00nznet sets the same success
bytes; Cxbx's `EmuKrnlNt.cpp` does likewise. These are post-load native-runtime
shortcuts, not a drive challenge/response implementation for our running kernel.
NoRain's reviewed bridge supplies no replacement drive authentication model.
The ordinary xemu ATAPI transport also supplies no Xbox authentication page.

These findings exclude those specific shortcuts from this port; they do not
invalidate donor renderer/compiler work. The next launch task is to establish
and implement the actual drive/media authentication contract, or qualify a
different real launch configuration under the owner's policy. Neither an ISO
directory listing nor three success bytes closes that dependency.

Private first-probe evidence: this worktree's `build/blinx-rev1-first.log`.

Reproduce the bounded kernel probe with either renderer build, using the owner's
unchanged local firmware/HDD/EEPROM and provisioned private credential file:

```powershell
./build/xlle.exe --nv2a-renderer opengl --boot <flash> --run2bl <bldr> `
  --hdd <stockdash-hdd> --hdd-security-file <private-key-file> --eeprom <eeprom> `
  --dvd <full-Blinx-Rev1-iso> --dvd-trace --net-disconnected `
  --test-synthetic-controller-hub --reduced-retire-diag `
  --machine-chunk-insns 4096 --max-host-memory-mb 768 --max-insns 2000000000
```

The HLE probe additionally enabled the existing guarded AOT profile and idle
fast-forward; the separate LLE probe above deliberately used the CPU floor.
Both stop on the authentic reached request, without setting authentication
success or claiming game initialization.

## Dashboard slowness: sampled host work instead of guest counts

The no-disc dashboard regression completes all 14 inputs and matches **28/28
captures** against the retained `54c102a` runtime control. Stderr is empty.
This run also uses the existing `--host-sample-profile 2` diagnostic. Its SHA-256
verified executable and logs were archived before subsequent builds:
`3f981810c2ae611e99d7f497961e6c496cb775007d1222343afc086aeee2a0c0`.

The last two menu/return windows, `[24B,25.5B)` and `[25.5B,27B)`, contain
**4,883 samples**, with complete leaf tables and no sampler overflow, failed
suspensions or failed context reads. Whole-run achieved sampling was 407.5 Hz,
mean period 2.454 ms. Disjoint groups from those two windows are:

| Main-thread leaf group | Samples | Share |
| --- | ---: | ---: |
| AOT executor, guards and RAM/x87/MMX adapters | 1,059 | 21.69% |
| Register APIs and CPU register snapshots | 539 | 11.04% |
| Guest address translation and translated-block invalidation | 359 | 7.35% |
| Generated guest function bodies | 459 | 9.40% |
| Emitted JIT code | 133 | 2.72% |
| OpenGL driver module | 100 | 2.05% |
| Remaining libraries, arithmetic helpers, devices and other code | 2,234 | 45.75% |

The first three groups total **40.08% of observed samples**. This is evidence
that the CPU execution interfaces and guest-memory machinery deserve attention.
It is not a claim that all their work is removable overhead. Arithmetic helpers,
including extended-precision operations, also consume time outside the small
matrix routine. Replacing one helper or reducing counted guest instructions
does not remove this surrounding work.

This is one busy-host diagnostic run, not a speedup estimate or an exclusive
wall-time partition. The sampler observes the main thread, including waits;
driver worker threads and actual GPU duration are not directly measured. The
2.05% driver-module figure therefore is not the entire cost of graphics.
Inlining charges work to its enclosing symbol. GDB could not launch because
of a DLL entry-point failure, so names were resolved from the verified binary's
nearest preceding defined PE text symbol using `nm -n -C`; the denominator and
module classification still come from the existing profiler.

These findings prioritize a bounded CPU-state/memory-interface investigation
over another renderer swap for the dashboard. Blinx may exercise the same
machinery and provide useful additional evidence. No incidental speedup from
Blinx bring-up has been demonstrated.

Private evidence in main `build/`: `blinx-media-dashboard-profile-leg00-A-r1*`,
`blinx-media-dashboard-profile-accounting.json/.txt`,
`symbolize_blinx_dashboard_nm.py`, and the SHA-keyed `profiles/` archive.
