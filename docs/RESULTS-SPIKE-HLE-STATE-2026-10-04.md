# Redundant graphics state: correct, but parked for lack of a clear speed gain

Branch `spike/hle-state-20261004`, parent `54c102a`; tracking
`beads-xbox.1.52`. Independent of matrix math and the render worker.

The source model is phobos's `src/d3d/rhi_vulkan.c` at `f01b20d`, notably
`DYN_CHANGED`/`DYN_SET` and command-recording invalidation. Remote `7884426`
adds a fog-coordinate change, not a different state strategy. We adapt the
principle to our existing OpenGL renderer, rather than copy Vulkan state calls.

The implementation stores uniform values per linked shader program, uploads
only the enclosing changed vec4 range, and respects arrays shortened by shader
optimization. Scalar/layout uniforms skip unchanged values. Raster state is
cached with target dimensions/depth presence and invalidated after clears.
Sampler state is cached per sampler object; texture-object settings remain
applied to their own objects. Every actual draw, dependency and output remains.
No new arithmetic approximation is introduced by this cache.

## Applied results

79/79 tests pass. New real-renderer checks cover clear/mask restoration, returning
to a program with changed constants and changes to optimized-away constant rows.
An initial test fixture assumed a clean surface must produce a GPU readback;
the fixture was corrected to establish dirty GPU ownership before asserting
readback bytes. The final full suite is `build/r2-state-tests-final.log`.

The real 27B interactive Settings/return A/B completes all 14 events in both
arms, empty stderr. **All 28 captures are byte-identical.** Renderer draw,
vertex, upload-byte and resolve counts also match.

The candidate performs 878,565 uniform updates and suppresses 5,447,584;
uniform uploads total 162,528,984 bytes. It suppresses 481,681 of 580,540 raster
state applications and 432,680 of 438,926 sampler state applications.

| Observed wall time | Baseline | Candidate |
| --- | ---: | ---: |
| All frame windows | 122.2557 s | 120.2490 s |
| Last four windows, 491 vblanks | 21.6567 s | 22.5070 s |
| Final window, 123 vblanks | 5.8232 s | 5.7589 s |

Foreign processes contaminate both arms. Total time improves 1.6%, the last four
windows regress 3.9%, and the final window improves 1.1%. These mixed, small
changes do not establish a worthwhile dashboard speed improvement. Eliminating
millions of driver calls is a mechanism result, not proof of a major application
gain; comparisons and bookkeeping also cost CPU time.

**Park remotely; do not integrate this standalone optimization.** Revisit if
host profiling demonstrates a state-upload bottleneck on another workload or
renderer configuration. No repeated noisy benchmark search is warranted now.
LLE is untouched. Private evidence: main `build/r2-state-r1-*`; no captures or
proprietary bytes are committed.
