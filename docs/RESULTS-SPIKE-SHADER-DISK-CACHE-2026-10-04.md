# Compiled shader disk cache: startup benefit, menu timing flat

Tracking: `beads-xbox.1.59`. Branch: `spike/shader-disk-cache-20261004`.
Base: `4bcad36`. Optional cache directory: `XLLE_HLE_SHADER_CACHE` environment
variable; unset leaves source compilation enabled without disk storage.

The design is borrowed from phobos `rhi_shader_cache.c`: versioned records,
full compile-key comparison even when the filename hash matches, temporary-file
publication and real compilation on a miss. OpenGL adds the complete vendor,
device, GL version and shading-language version to the vertex/fragment source
key. Binary corruption, truncation, driver rejection and unavailable binary
support fall through to actual source compilation. Cached binaries never stand
in for an unsupported shader. Windows replacement uses atomic MoveFileEx.

## Real dashboard result

| Measurement | First launch into empty cache | Warm launch |
| --- | ---: | ---: |
| Program binaries loaded | 9 | 42 |
| Source compilations | 33 | 0 |
| Measured source compile/link time | 1.77463 s | 0 s |
| Cache load/lookup time | 0.00987 s | 0.05023 s |
| Full route interval sum | 103.1356 s | 102.3881 s |
| Final 123-vblank interval | 4.87419 s | 5.01288 s |

Nine first-launch hits reuse source-identical binaries produced earlier in that
same launch. The in-memory state cache has 42 entries but only 33 distinct
generated program sources. No binary was rejected by the driver.

All 14 inputs complete, stderr is empty, and all 28 captures are byte-identical
between cold and warm. All 80 checks pass, including full-key mismatch, changed
source, binary corruption, truncation and replacement tests. Every warm shader
was used in real dashboard rendering, not just loaded in a synthetic test.

The useful result is removal of repeated compilation. Foreign compiler/runner
processes make both legs unusable for quiet-host speed claims, and the final
menu interval is not faster. Do not sell the 1.77-second compile saving as a
steady-state FPS improvement or interpret aggregate route time as a rigorous
startup latency measurement.

Private evidence: main checkout `build/r3-shader-cache-leg00-A-r1*` and
`build/r3-shader-cache-leg01-B-r1*`; cache contents under this worktree's
`build/dashboard-cache`; `build/ctest.log`.

Disposition: preserved as an optional startup/hitch candidate on its remote
branch; not integrated into the retained runtime. It is not a demonstrated
dashboard throughput win.
