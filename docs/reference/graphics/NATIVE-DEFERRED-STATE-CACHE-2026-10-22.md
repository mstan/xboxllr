# Native deferred graphics-state cache

Tracking: beads-xbox.1.7.17. Shared native renderer optimization, first exercised
with Blinx NTSC Round 2 Stage 1 (guest stage 4, rd21 assets).

The entrance submits approximately 20,000 short strips per game frame. Every
draw previously converted and republished all 44 deferred render fields and
22 fields in each of four texture stages, even when their values were unchanged.
This is graphics service work in the statically compiled game profile, not an
x86 interpreter fallback.

The draw boundary still reads the actual guest words, including inline writes
that bypass named API entry points. It compares five independent categories:
deferred render fields and texture stages 0–3. A category is reused only when
both its guest bytes and its native mutation revision match. Named/packet
changes to the renderer therefore cannot hide behind unchanged guest bytes.
Device close/reset invalidates every category; initialization drops all cached
snapshots. No guest dirty flag is consumed or rewritten.

Native revision ownership follows the shared render/texture setter boundary.
Only the nine host render fields republished by the deferred converter affect
the render revision. Texture revisions are independent per stage. The prepared
Phobos device stores these fields through those setter calls; its state-block
API is not used by this runtime. Runtime-owned state-block restoration uses the
same maintained setters. Backends without this audited ownership return
untracked revisions and retain full synchronization.

The packed-YUV consumption guard remains outside the cache. Changed unsupported
or invalid fields still reach the existing validators. The texture sampling
path also walks mip offsets once instead of re-summing all preceding levels.
It retains every level, every actual CPU-byte comparison and the existing
exposure/alias/lifetime rules.

Equally pitched native/guest texture levels compare and copy the identical
contiguous range in one call. Padded rows and converted X1R5G5B5/G8B8 retain
their existing specialized comparisons. No texture data is trusted without
checking the actual CPU bytes. Set XLLE_PILOT_DEFERRED_STATE_CACHE=0 for the
uncached diagnostic baseline; ordinary launches enable the cache.

## Evidence and limits

Private evidence: G:/Temp/Codex/xboxllr/stage-entry-20261022.
QA uses independent copied campaign saves and identity, muted output, a hidden
window, four logical CPUs at normal priority and one game process at a time.
The original owner's playtest was closed only after explicit permission.

Unprofiled isolated baseline baseline-solo-011 measured 17.1 / 19.3 / 17.4
game updates per second in the entrance's 0–4 / 4–10 / 10–16 second windows.
The first cache candidate cache-solo-012 measured 18.9 / 22.0 / 22.7, at the
same adaptive 1080p, 2x2 SSAA, FXAA and interpolation settings. This is a useful
bounded observation, not a claim of a stable 30 Hz game rate.

The refined category cache's profiled deferred-state phase cost was 0.225
microseconds per draw versus 0.817 in the original baseline's complete
30-frame windows (over 500,000 calls/window). Other phases became slower under
concurrent CPU/driver load. Profiling has material overhead and those wall-time
phase measurements are attribution evidence, not a shipping FPS benchmark.
Later system-load samples showed all logical CPUs saturated by independent
work; those runs cannot establish a final user-facing frame-rate improvement.

Same-process comparison same-run-017 alternated the cache off/on in six
three-second samples, after entering stage 4. Both paths used the same build,
assets, save, visual settings and process. The QA harness changed only the
owned child's diagnostic cache selector, resolved through its matching PDB;
no original guest state or owner process was modified. Mean update rates were
18.9 uncached and 22.2 cached; mean process CPU cost was 52.9 versus 41.7 ms per
update (21% lower). This isolates the state cache, not the later contiguous
texture comparison change.

With the contiguous comparison included, contiguous-final-018 measured
21.0 / 24.4 / 24.8 updates per second and 40.4 / 37.1 / 38.2 process CPU ms per
update. This is improved, but still does not establish a steady 30 Hz entrance.
Actual stage screenshots were retained and inspected. Seven selected GPU tests
pass, including 1555/X8 storage and null-texture alpha. All four rejection
subprocesses pass independently with the exact exit code and reason. The
combined CTest guard wrapper also encountered its startup timeout under heavy
load; that timeout is not represented as a passing test.

The focused GPU fixture checks actual red/green pixels after native-only
texture changes and raw guest writes, including stage 3 with active intervening
combiners. Existing depth, alpha, blending and culling cases run in the same
fixture over three device lifetimes. Invalid deferred lighting/point writes
are tested after priming the cache. The broader lifecycle fixture has an
independent unqualified original-allocator retirement path and is not counted
as passing evidence for this optimization.
