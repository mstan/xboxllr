# Direct indexed submissions: parked

Branch `spike/xdk-direct-20261004`, parent `54c102a`; Beads `beads-xbox.1.50`.

The experiment removes index-packet construction and repeated ring refills.
It owns an index snapshot keyed to the physical address of the real BEGIN
parameter, consumes that snapshot only when the ordered graphics queue reaches
BEGIN, and draws at END. Guest preparation, initial reservation, cleanup and
submission remain real. `XLLE_XDK_DIRECT_DRAW=ON` selects it at build time;
the normal native command builder and the separate LLE renderer remain available.

This follows the native API direction in NoRain's DOAXVB adapter and phobos's
HLE D3D routing. Both donors also contain shadow paths that run guest work
before host drawing; this experiment deliberately replaces the indexed payload
work. It is one operation, not a complete XDK graphics replacement.

Pinned donor review: sp00nznet `1409a7d`, NoRain compiler `7adaf21`, DOAXVB
`aa1d3c8`, phobos `7884426`. The latter's new fog-coordinate fix does not change
this experiment's design.

## Results and decision

`r2-xdk-direct-r1`: sequential interactive A/B, 27 billion guest-clock units,
1.5-billion intervals, 14-event Settings/return route; baseline `54c102a`.
Both processes exit 0, empty stderr, all 14 input events completed. Foreign
processes contaminate timing: these are screening observations, not controlled
speed estimates or fresh-frame rates.

| Final interval / route counter | Baseline | Candidate |
| --- | ---: | ---: |
| Final 123 vblanks, seconds | 5.7800 | 6.4961 |
| Previous 123 vblanks, seconds | 5.7278 | 7.0725 |
| Intra-draw ring refills | 29,814 | 0 |
| Final interval native instructions | 91,925,180 | 89,807,421 |
| Final interval fallback instructions | 40,306,151 | 1,409,992,590 |
| Final interval fast-forwarded idle | 1,367,568,680 | 0 |
| Draws submitted to host renderer | 580,540 | 578,920 |

Candidate queued 140,677 native indexed payloads, consumed 140,447 and had 230
pending at the global instruction limit. No duplicate/reused pending slot fault.
28 captures per arm; 22 differ, with visually coherent Settings and dashboard
screens but changed animation phase. This does not establish full compatibility.
The runtime reports no idle-loop entries in the final interval; the displaced
execution replaces the saved work with fallback execution. Its precise cause
is unresolved. Do not describe the queued tail alone as dropped draws or assume
it proves a deadlock: the full interactive route completes.

**Park; do not integrate.** There is no demonstrated end-to-end gain. Revisit
only with an identified scheduling/submission mechanism that explains the lost
idle time, or with a broader native graphics contract that removes that cost.
Do not add fake waits, fake completion or dropped draws to improve a counter.

## Validation

80/80 CTest checks pass. The new real-renderer queue test compares ordinary
index packets and native payloads through both PFIFO paths, verifies a blocked
draw is not executed early, input ownership, duplicate-address rejection,
exactly-once consumption and matching rendered output. Existing renderer
qualification also passes. This tests the queue contract, not all title behavior.

Private logs and captures remain under main `build/r2-xdk-direct-r1-*`;
full suite log `build/r2-xdk-full-tests.log`. No title bytes or captures are
committed. The branch is a reproducible experiment, not a shipping default.
