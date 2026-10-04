# GPU-resident presentation: working, parked pending a menu speed benefit

Tracking: `beads-xbox.1.49`. Branch: `spike/hle-gpu-present-20261003`.
Independent baseline: `4d2370a`; the other three experiments are absent.

NoRain's DOAXVB D3D11 presenter at
`aa1d3c8afcd8d063171d819e27307070b63f056b` presents native GPU surfaces. This
branch applies that policy to our GL renderer: a resident dirty scanout target
blits directly to the visible window, with aspect preservation and correct
orientation. CPU-produced startup frames still use the genuine SDL upload path.
Native presentation retains the native context between frames, avoiding two GL
context switches per subsequent draw. Required draws are not dropped.

This bounded spike implements GPU-resident presentation, **not a separate
render-worker queue**. Existing GL submission is asynchronous, but no new
ownership/completion protocol for a worker thread is claimed. NoRain's worker
implementation that silently declines some draws was not copied.

## Validation

- The GPU test reads the actual window backbuffer and verifies orientation,
  colors and refusal of nonresident surfaces. Readback is an explicit test-only
  witness; normal native presentation does not capture pixels.
- Full checks initially passed 78/79. The dummy software presentation test
  caught an over-broad OpenGL-window default. Window capability is now explicit
  at the HLE application's call site; generic software presentation remains
  available. Both affected checks pass after the fix (GPU and presenter).
- The real interactive 27B clock/menu/Settings/return route completes all 14
  inputs in both arms, with empty stderr and **28/28 identical captures**.
  A screenshot of the actual candidate window also shows the correct menu.
- Both arms display 2,208 frames and report zero presentation failures. The
  candidate performs 1,933 resident presents; genuine CPU capture/upload is
  used for the other 275. Capture volume falls **2,587.5 MiB -> 322.3 MiB** and
  GPU resolves fall **2,350 -> 1,085**. Draw and vertex counts are identical.
- Average capture-to-present latency falls **1,429.9 us -> 393.8 us**. Fully
  resident intervals are around 0.3-0.35 ms per present with no CPU upload.
- However, the final 123-vblank menu window is **8.6846247 s -> 8.6605107 s**,
  only 0.28% less wall time. This does not establish a meaningful menu speedup.
  Total frame windows are 144.6126036 s -> 133.7524818 s, but both legs have
  foreign workloads and that aggregate cannot establish isolated causality.

Private evidence in the main checkout: `build/spike-present-r1*`,
`build/spike-present-tests.log`, `build/spike-present-fix-tests.log` and
`build/spike-present-window-review.png`. Binary identities, command lines,
process censuses and original captures are preserved by the replay harness.

## Decision and reproduction

**Park on the remote branch with a draft PR; do not integrate into the active
menu-performance build.** The implementation reduces presentation overhead,
but this screen confirms that overhead is not the dominant menu bottleneck.
Revisit when CPU/driver gains make it significant, or when a validated render
queue provides a larger benefit. Do not repeat noisy comparisons to manufacture
a positive result, and do not equate API present counts with distinct content
cadence or original Xbox speed.

Build with the baseline's HLE/private-AOT configuration. Run the stock replay
with `--interactive --interactive-present-stats-ms 5000`, 27B instructions, 1.5B
frame intervals, the Settings route, native AOT, idle fast-forward, provisioned
HDD security, public register transfers and decoded-DXT cache disabled. The
LLE renderer and execution clock are unchanged by this branch.
