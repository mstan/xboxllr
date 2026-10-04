# Native XDK indexed command construction: retain as an opt-in candidate

Tracking: `beads-xbox.1.47`. Branch: `spike/hle-xdk-graphics-20261003`.
Independent baseline: `4d2370a`. This branch does not include the other three
experiments. The bounded experiment replaces index copying and command building;
it does **not** yet replace the whole XDK graphics service or bypass NV2A command
decoding. Those remain larger follow-up opportunities.

## Donor lesson and caller contract

NoRain's DOAXVB adapter at `aa1d3c8afcd8d063171d819e27307070b63f056b`,
`recomp-runtime/d3d_draw_adapter.c:1406`, runs the original guest draw before the
host draw. That sequence alone would retain our guest driver cost. This branch
replaces the costly indexed command-construction loop with native work while
retaining real guest preparation, ring allocation/refill and completion/kick.

The phobos/XbSymbolDatabase scanner identifies the draw, but its reported ABI
does not match this dashboard: the actual implementation takes three stack
arguments and returns with RET 12. The private profile generator verifies the
audited function hash, boundaries, globals and helpers. The machine also compares
the complete function bytes before enabling the service. Other revisions require
a fresh ABI audit; a name or signature match is insufficient.

The native service preserves index order, primitive commands, ring bounds and
guest completion effects. It snapshots the supplied indices, uses genuine guest
allocation on ring exhaustion, and resumes the genuine callee epilogue. Guest
memory transfers validate translations and RAM bounds. Native work is counted
separately rather than inventing retired guest instructions. No required draw is
dropped and no successful placeholder is introduced. Selection is build-time;
without the private profile the existing execution path remains active.

## Applied validation

- Optimized and unoptimized CPU machine tests pass, including the new native
  service redirect test: genuine guest instruction accounting and continuation
  survive repeated replacement calls.
- The complete 79-test suite passes (`build/spike-xdk-all-tests.log`, 65.90 s).
- The real 27B clock/menu/Settings/return route completes all 14 inputs with
  empty stderr. Settings and the returned dashboard were visually reviewed.
- 142,113 native calls handle 610,376,646 indices and make 29,814 real ring
  refills. One call is suspended at the requested global instruction stop;
  this is an in-progress guest call, not a claimed completion.
- 18 of 28 captures are byte-identical to the baseline. The others show small
  timing differences: the returned menu differs at 1,722 pixels, maximum channel
  difference 4/255, mean 0.00252/255. Settings-open has maximum 2/255. Clock-post-A
  has a localized transition difference up to 49/255. Exact instruction history
  is intentionally not an HLE acceptance requirement.
- Final interval native instructions fall from 144,274,436 to 91,925,180;
  fallback instructions are 40,306,151 versus 41,679,287. Renderer work remains:
  580,540 draws versus 580,683 and 175,461,689 versus 175,545,669 vertices.
- The final 123-vblank interval takes **7.3793892 s**; all frame windows total
  128.3618402 s. This is a busy-host, single-candidate observation, not an isolated
  speedup estimate or original-speed qualification. The earlier baseline's
  11.4993427 s was measured in another run and is not a controlled pair.

Private evidence: main checkout `build/spike-xdk-r1*`, machine-test log
`build/spike-xdk-machine-tests.log`. No private profile, firmware or generated
guest bytes belongs in the repository.

## Reproduction and decision

Generate `build/xdk_draw_profile.hpp` with `tools/xdk_draw_profile.py --xbe ...
--symbols ... --output build/xdk_draw_profile.hpp`. Configure `XLLE_RENDERER=HLE`
and `XLLE_XDK_DRAW_PROFILE` to that absolute header path. Use the existing stock
dashboard replay, 27B instructions, 1.5B frame interval, the Settings input
script, native AOT and idle fast-forward, provisioned HDD security, public
register transfers and decoded-DXT cache disabled.

Retain as a title-profile opt-in candidate. Full native XDK routing is not
finished, and this result does not establish original-speed displayed cadence.
