# Native resource write tracking

Tracking: beads-xbox.1.7.17. This implements the resource-ownership opportunity
identified in the [Blinx 2-1 assessment](BLINX-STAGE21-PERFORMANCE-ASSESSMENT-2026-10-22.md).
The measured implementation is retained after the owner's entrance playtest.
The reported severe 2-1 slowdown is substantially improved; occasional hitches
remain a bounded follow-up (beads-xbox.1.7.18). No Blinx addresses or special-case
scene substitutions are present in the tracker or shared renderer.

## Contract and implementation

Ordinary CPU-owned textures of at least4KiB can retain a prepared native image
while their CPU backing remains unchanged. Windows pages are temporarily made
read-only; an actual CPU write relinquishes that protection, invalidates the
snapshot, and retries the original instruction. Stores are not decoded or
emulated. Real guest read-only pages are never admitted. Internal host queries
report logical RW for internally protected pages; original VM query extents
first relinquish internal protection. Protection, allocation and decommit APIs
relinquish tracker ownership before changing actual policy.

The cache is two-phase: arm writable pages, run the existing complete
preparation/copy, then accept the snapshot only if the covered pages remained
unchanged. A global epoch provides the cheap reuse check; changed epochs inspect
per-page versions. Overlapping resources therefore invalidate together. Hot
pages stop using protection after64faults and retain byte validation. Unsupported
hosts, absent tracker, uncommitted/readonly/hot ranges and in-flight native writes
also retain byte checks through the same API. No save-state migration is needed.

A native-writer lease keeps direct OS read destinations writable through actual
asynchronous completion. This matters because kernel writes need not raise a
user-mode store exception. Synchronous uncached reads use the same lease. Private
IO status blocks remain unchanged, and actual guest IOSB/event/APC publication
keeps its existing order.

Reuse also requires the same prepared native image-content revision with all
mips uploaded and no outstanding GPU-written state. The revision belongs to the
shared image owner, separately from read/completion serials: writes through an
alias invalidate prepared reuse even after that alias wrapper is retired.
Native-only uploads therefore invalidate CPU-based reuse without a guest write.
Live GPU aliases and render-target/depth textures retain their existing
preparation path.

The optimization is enabled by default for the native-memory runtime when host
support is available. XLLE_PILOT_RESOURCE_WRITE_WATCH=0 selects the diagnostic
byte-check baseline. Original IO-device/LLE profiles currently do not initialize
the tracker: their full publication/alias/protection integration remains to be
qualified. They retain the existing byte-check behavior. Portable builds expose
the same snapshot API and always request byte validation.

## Validation so far

The initial candidate passed a focused ten-check bundle: native tracker, portable fallback, real GPU
resource-cache images, backend rendering, strip batching, deferred-state cache,
alias/completion, native memory, media and file cache. Total CTest time3.78s.
The tracker fixture checks direct stores, memcpy, overlaps, concurrent native
writers, capture-time changes, genuine RO policy, decommit/recommit, reload and
native OS reads under nested writer leases. The GPU fixture checks unchanged
reuse, retained-pointer edits, native-only texture mutation, exact real GPU
images and retirement over three device lifetimes.

All automated work uses ignored G: builds/profiles/logs and copied saves. The
owner package and persistent saves are unchanged. The build succeeds, with
pre-existing fixture macro-redefinition warnings and a pre-existing generated
shift warning; no broad warning-clean or full campaign claim is made.

## Source review after the initial bundle

Further review found cases requiring explicit protection. The following source
changes are present; the ten-check GPU/runtime result above predates them:

- Rearming a page advances its generation, so an older overlapping capture cannot
  become valid after another capture has observed a write and rearmed that page.
- A separate arming state prevents delayed write faults from racing with the
  installation of read-only protection.
- The exception handler preserves the interrupted thread's last-error value.
- VM query/protection output buffers hold native-writer leases too: those output
  pointers may themselves belong to watched guest pages.
- Forced-portable Windows builds retain linkable passthrough VM wrappers.
- Shared image-write revisions survive alias retirement. A GPU regression case
  compares the unchanged CPU-shadow path with an intervening native alias write.

The unit fixture now includes the overlapping unpublished-capture case, guest
VM-output buffers and two simultaneous writer threads. An isolated current-source
native/forced-portable build and test pass succeeds: 2/2 tests, 4.63 seconds CTest,
under nine seconds including compilation. It uses one logical CPU and BelowNormal
priority, with all outputs on G:. Headroom was checked first (about 23 GiB free
RAM, 29% host CPU, zero sampled F:/G: disk queues); TF2 remained running.

The current full runtime and native graphics/contract test binaries build
successfully, serially on one BelowNormal logical CPU (run044). The build itself
launches no GPU work or game capture. The later bounded GPU checks below cover
the new alias-image case. The owner package and persistent saves remain unchanged.
Private unit evidence:
G:/Temp/Codex/xboxllr/stage-entry-20261022/tracker-units-light-043.json and
tracker-units-light-043.log.

## Current-source CPU integration checks

Run045 passes the native memory (0.66s) and file-cache (0.11s) contracts on one
BelowNormal logical CPU. The media fixture fails its exact process-handle-count
check during APC cleanup. A failure-only diagnostic retains the existing
assertion; run046 identifies cycle2, expected163, actual164. The tracker is not
initialized by this nongraphics fixture, so this is not evidence of tracked-page
invalidation failing. Its cause has not been established.

With two BelowNormal logical CPUs available, run047 passes the same media
fixture in0.04s (0.05s total CTest). Do not describe the single-core bundle as
passing or infer absence of a lifetime bug from that rerun. Follow-up is recorded
as NTD-18 in the native-title deferral ledger and in Beads. Keep this supporting
validation question separate from measuring the actual stage2-1 performance
candidate; no production APC or handle-count assertion was weakened.

Evidence: tracker-runtime-light-044.json/.log,
tracker-cpu-integration-045.log, tracker-media-diagnostic-detail-046.log and
tracker-media-two-core-detail-047.log under the private stage-entry directory.

## Current-source GPU checks

Run048 passes the resource write-watch fixture in0.65s, including exact GPU
readback after a write through an alias and that alias's retirement, over three
device lifetimes. Run049 passes all four remaining focused renderer regressions:
backend, alias/completion, strip batching and deferred-state cache (2.99s CTest
total). These tests use hidden64x64 surfaces and32x32 texture data, two
BelowNormal logical CPUs, serial execution and G: output; no title is launched.
The sampled TF2 GPU engine utilization before this bounded pass is10%.

All ten selected contracts now have passing current-source results across
runs043/045/047/048/049. This is not a passing single-core bundle: retain the
explicit NTD-18 media/APC discrepancy above. No actual gameplay performance claim
follows from these small-surface correctness checks.

Private GPU evidence: tracker-gpu-resource-detail-048.log and
tracker-gpu-regressions-detail-049.log in the stage-entry directory.

## Performance and outstanding work

The intentionally incorrect cost-removal experiment039 suggested about21%
process CPU headroom from eliminating repeated ordinary-texture checks. It was
removed. It is not this implementation or a valid performance promise.

Candidate042 reaches Round2Stage1 at43:18 /5160x2160 internal rendering and
begins a balanced same-process comparison of checked preparation and tracked
reuse. Early windows are promising, but the host becomes heavily contended as
the owner starts TF2; the last windows fall to about6and1updates/s on both paths.
This run cannot establish a robust performance gain. The game has exited and
heavy builds and game captures are deferred to protect TF2 responsiveness;
continue light review while the owner is playing.

## Measured result and owner acceptance

Actual Round2 Stage1 / guest4 at adaptive43:18 uses the unchanged1080p,
2x2 SSAA, FXAA,8x AF and60Hz interpolation settings (5160x2160 internal).
Shipping inherited scheduling is used; the diagnostic guest-core pin is absent.
The selector and final scene screenshots were inspected against the unchanged
owner build; no obvious new visual corruption was observed.

Balanced same-process run050 alternates checked/tracked modes0,1,1,0,0,1,1,0
over eight3s windows. Frame-weighted process CPU cost falls from35.575 to
29.770ms/update (16.3% less), with27.995 versus29.914 updates/s. Mean sampled
host CPU is61.58% versus61.01% in the respective timed windows. This establishes
a bounded CPU saving, not a universal FPS improvement.

Cold-process runs051-054 switch tracker initialization fully off/on/on/off, so
they include initialization, native-writer and protection overhead. Host CPU
varies substantially between runs (means29.30/65.69/45.24/67.72%). Their4-16s
CPU costs are28.204/33.304/28.602/36.999ms/update, with observed update rates
29.931/29.595/29.940/26.886Hz. Largest observed first4s update gaps are
0.658/0.906/0.780/0.953s. Retain the load context; these separate launches are not
an isolated cold-load or universal zero-hitch result. The in-process selector
comparison leaves tracking infrastructure installed and must not be presented
as measuring its entire net overhead.

The owner received exact native executable SHA256
2d499f17d462f2c255596f5e828aebb37a4cb766f2534f8e7cfadd4e3d65f33b
and its matching PDB. The old runtime/manifest were backed up;322 existing
settings, identity and native-save files were hash-identical before launch.
The owner reports it is "much much smoother" and "vastly improved," with a minor
hitch or two possibly caused by background load. This confirms the intended
user-visible improvement; the cause of the remaining hitches is not established.
The two-minute owner session finishes normally (exit0) in retained diagnostics.

Retain this shared optimization and update Blinx's framework pin. Do not reduce
effects, quality, original simulation timing or callback correctness to obtain
the result. The deliberately incorrect texture-check omission remains absent.

Deferred follow-up:

- Occasional residual hitches and the earlier localized1-2 report:
  beads-xbox.1.7.18. Reproduce and attribute only meaningful remaining cases.
- Single-core media/APC process-handle stabilization: NTD-18 remains explicit.
- Original-IO/LLE write tracking stays disabled. Qualify its aliases,
  publication and native OS output paths before enabling it in that profile;
  existing byte validation remains the production behavior there.
- The optimized private PDB's tracker-state diagnostic cannot be sampled by the
  current helper; runtime initialization is verified by the available=1 log.
  The public-mode selector, actual pixels and CPU measurements do not depend
  on that optional statistic.

Private performance evidence: tracked-current-ab-050, tracked-cold-off-051,
tracked-cold-on-052, tracked-cold-on-053, tracked-cold-off-054 and
tracking-cold-abba-051-054.json under the stage-entry directory. The original
owner runtime remains in owner-before-tracking-055. Framework integration hashes
and issue disposition are recorded in Beads.

Private evidence: G:/Temp/Codex/xboxllr/stage-entry-20261022/tracked-resource-ab-042.
The matching-PDB runtime selector is only for owned QA children, not launcher UI.
