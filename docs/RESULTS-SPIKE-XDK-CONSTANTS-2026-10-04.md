# Complete XDK constant-upload APIs: parked

Tracking: `beads-xbox.1.56`. Branch: `spike/xdk-constants-20261004`.
Base: `4bcad36`.

This native API slice follows the donor runtimes' complete service approach:
the one-vector and four-vector vertex-constant setters run as native functions.
The functions' complete caller-visible effects include both the SDK's readable
constant bank and ordered GPU commands. Arguments are ECX/EDX, the return pops
only the return address, and callee-saved registers remain intact. The four-
vector operation preserves the empty x87-stack convention of its MMX/EMMS body.

The real guest MakeSpace allocator is called when the ring is full. A native
continuation retains the original arguments across that call, then performs the
upload after capacity exists. No capacity, GPU completion or API result is
fabricated. Private profiles verify both complete functions, including their
allocation branches. LLE builds do not install these native services.

This replaces two entire API functions, not the full XDK graphics subsystem.
Guest rendering preparation, other APIs and NV2A command consumption remain.
The broader full-native-XDK proposition therefore remains open.

## Result

| Measurement | Retained runtime | Native constant APIs |
| --- | ---: | ---: |
| Full route interval sum | 101.5446 s | 101.7939 s |
| Last four intervals | 18.3579 s | 18.6013 s |
| Final 123-vblank interval | 4.77457 s | 5.01213 s |
| Native single-vector calls | 0 | 990,597 |
| Native four-vector calls | 0 | 1,127,106 |
| Genuine MakeSpace calls from native adapter | 0 | 4,523 |

All 80 checks pass, including 1,000 comparisons against the original guest
functions covering constant bit patterns, GPU command contents, CPU bank,
return convention, preserved registers and write extents. The full real
dashboard route completes all 14 inputs, with empty stderr and no pending
native allocation continuation at the end.

25/28 captures are identical. The three changed clock/transition captures were
visually reviewed side by side: menu text, clock settings and loading display
remain present; animation/raster differences are small. Final capture matches.
The candidate executes 580,683 draws versus 580,540; native services remove
guest clock work and can move device scheduling relative to animation. Exact
instruction histories are not the HLE contract, and this is not a byte-identical
whole-route claim.

Both timing legs contain foreign compiler/runner processes, so this is a
directional screen. It demonstrates no substantial wall-time benefit from this
API slice. The register/Unicorn entry adapter still has host cost; two million
replaced API calls are not two million cost-free calls. No distinct displayed
FPS or causal small-percentage speed claim is made.

Private evidence: main checkout `build/r3-xdk-constants-leg00-A-r1*`,
`build/r3-xdk-constants-leg01-B-r1*`, `build/r3-constant-review.png`; worktree
`build/ctest.log` and generated `build/xdk_constant_profile.hpp`.

Configure `XLLE_RENDERER=HLE` and point `XLLE_XDK_CONSTANT_PROFILE` at a header
produced by `tools/xdk_constant_profile.py --xbe <private dashboard XBE>
--output build/xdk_constant_profile.hpp`. Leave the setting empty to omit the
native API implementation. Disposition: parked, not integrated.
