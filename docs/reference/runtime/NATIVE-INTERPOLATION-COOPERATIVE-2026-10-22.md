# Cooperative native interpolation presentation

Tracking: shared `beads-xbox.1.192`; Blinx entrance `beads-xbox.1.7.20`.

## Cause and contract

Blinx's Stage 2-2 entrance exposed a distinction between simulation progress
and visible presentation. With interpolation enabled, guest Swap captures
owned source images and the independent host worker presents them. That
worker tries the renderer lock without blocking. A busy graphics owner can
repeatedly win the lock, dropping presentation ticks even while the game
continues near its original 30 updates/s.

A missed timer deadline now leaves one coalesced request. At the end of a
normal graphics dispatch, the existing renderer owner can service that request
using the same interpolation consumer and completed image history. The worker
remains nonblocking: device close can still stop and join it while holding the
renderer lock. Close clears pending work. Native DPC context never services it.

Host presentation cannot run inside an active guest visibility query. The
request waits until End, so fullscreen presentation draws contribute no guest
samples. The cooperative service preserves the calling thread's complete FP
environment and uses normal host rounding only during presentation. Unsupported
backends retain a readiness result of zero. No game-specific behavior is added.

Simulation/vblank timing, effects, source assets, internal resolution, SSAA,
interpolation history and its existing one-source-frame latency are unchanged.
`XLLE_PILOT_INTERPOLATION_COOPERATIVE=0` is a diagnostic control for reproducing
the old worker-only behavior; the fix is enabled by default. It is not an
HLE/LLE switch. Read-only matching-PDB observations distinguish worker attempts,
lock misses and accepted presentations from game updates.

## Bounded actual-title comparison

Visible Stage 2-2 (`rd22`, stage 5) runs use copied owner saves, matching console
identity and the populated utility cache. Native scheduling is inherited;
the render profile retains adaptive output, 1080p/2x2 SSAA, FXAA and 8x filtering.
Background QA output is muted while audio emulation remains active. Each
window below covers the first two seconds after player allocation, which does
not by itself establish the exact player-control boundary.

| Run | Policy | Game updates/s | Accepted native presentations/s | Host CPU mean |
| --- | --- | ---: | ---: | ---: |
| 012 | Prior worker-only implementation with observation | 27.83 | 21.76 | 70.9% |
| 013 | Cooperative service enabled | 29.55 | 59.61 | 47.8% |
| 014 | Same executable as 013; diagnostic control disabled | 29.13 | 33.15 | 51.7% |

Runs 013/014 use identical executable SHA256
`8749f911f977ee249be0ac13376eb813df186b0cf96c599fb871f79906f5e844`.
Run 013 services 50 missed ticks cooperatively in that window. The following
2-4 and 4-8 second windows retain about 60 accepted presentations/s; the control
delivers about 35 and 38. These are accepted native presentation calls, not
physical scanout measurements. Host load varies, so no total CPU improvement
is claimed. This is smoother presentation at original simulation speed, not
60-update gameplay or a whole-game performance guarantee.

Entry-to-player allocation remains about 1.21 seconds in both same-executable
runs. An approximately 0.7-second pre-allocation swap gap remains; this change
does not claim to eliminate loading stalls. Owner review of the reported
entrance remains on the game issue. Minor hitches remain separately tracked.

Private evidence: `G:/Temp/Codex/xboxllr/blinx-stage22-20261022/`, particularly
`interpolation-comparison.json` and runs `stage22-visible-present-012`,
`stage22-cooperative-013`, `stage22-cooperative-off-014`.

## Focused validation and limits

The final bundle passes all six checks: `pilot_graphics_visual_mods`,
`pilot_graphics_backend`, `pilot_graphics_alias_completion`,
`pilot_graphics_strip_batch`, `pilot_graphics_deferred_cache` and
`pilot_graphics_interpolation_service` (4.51 seconds total).

The new actual-GPU fixture covers pending request consumption, unchanged source
pixels/count, preserved rounding and exception flags, diagnostic/DPC guards,
zero samples in an empty guest visibility query, and stop/join under renderer
ownership across three device lifetimes. Existing visual checks cover actual
GPU midpoint blending, stall/cut/reset at 60/120 Hz, unchanged guest front
buffer and guest submission count. Original image quality is retained.

The broader graphics ABI fixture has an unresolved vblank-event deadline
failure before drawing; it is not counted as passed (NTD-19). An earlier
state-batching prototype was rejected and reverted: its title performance
gain was not established. No batching prototype is part of this fix.
