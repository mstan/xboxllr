# Goal: faithful AOT dashboard at original-speed 60 FPS

Owner pivot, 2026-10-02. Active goal and central tracker: `beads-xbox.1.25.5`.
Work solo. **The dashboard is the primary deliverable; booting Blinx is the
stretch goal.** JSRF remains a later validation target. This supersedes the
Blinx-first ordering in earlier planning records.

## Acceptance

Compile the pinned dashboard and the kernel code it executes before launch.
Complete a declared post-boot animation/navigation route with zero guest CPU
JIT/interpreter fallback and no runtime guest-code translation. Initial boot on
the existing CPU floor is a separately disclosed first-stage boundary; whole-boot
AOT is an additional coverage milestone, never implied by a post-boot result.

Retain the real kernel, guest memory and implemented hardware contracts.
No stubs, forced acknowledgements, skipped initialization or captured-frame
playback. HLE is opt-in only after its implemented LLE counterpart and full
guest-visible contract are qualified. The explicit unlocked HDD policy is the
owner's narrow documented exception; provisioned locked startup also works.

The product route must include sustained animated main-menu idle, selection
changes, entry/return through Memory, Music, Settings and the locally available
Xbox Live flow. Real guest offline/error behavior is valid; invented service
responses are not. Pin the input identities and check actual reached screens.
Script labels alone do not prove navigation. UI audio, input and transitions
must be qualified; the current absence of host audio remains an open gap.

The performance target is original-speed guest execution with sustained 60 FPS
where the dashboard produces 60 Hz content, counting distinct frames and input
latency separately from host presents and guest vblanks. Preserve the native
baseline and measure each optimization against it on a quiet host. Additional
headroom remains desirable; 20% is a planning proposal, not an agreed gate.

## Renderer reuse

Use the host GPU to execute the real NV2A workload. Existing OpenGL rendering is
the working baseline. Prioritize qualified reuse over a new renderer project.
xemu's OpenGL/Vulkan NV2A backends, shader translation and surface/texture logic
are the primary donor candidates. Existing `nv2a_vsh_cpu` reuse remains in place.
"Modern renderer" means host-GPU execution of the actual NV2A workload;
changing graphics APIs is not an acceptance requirement.
Evaluate xboxrecomp renderer pieces individually; do not import their kernel HLE,
stubbed PCI/IRQ/memory interfaces or title-specific replay paths.

Reuse can include substantial backend code when its real dependencies are wired
and behavior passes qualification. Preserve upstream licenses and provenance.
The existing combined-executable distribution restriction remains tracked in
`beads-xbox.1.21`; it does not prevent local qualification work.
See [renderer reuse assessment](RENDERER-REUSE-2026-10-02.md).

## Burndown

| Gate | Status | Required evidence |
| --- | --- | --- |
| D0: dashboard baseline and scope | In progress | Pinned inputs; validated animation/navigation captures; guest-anchored phase counts; reproducible host-GPU baseline command |
| D1: useful general AOT execution | In progress | Integer/control flow, cold memory, kernel/device operations and required extended state; independent and differential tests |
| D2: scalable native artifacts | In progress | 257-region indexed dispatch passes O0/O2; launcher cap removed; build partitioning and wider subset selection remain |
| D3: fully AOT dashboard route | Open | Strict execution completes each declared menu phase; no CPU fallback or hidden guest execution in helpers; output/state checks pass |
| D4: renderer/device completion | In progress | Reused renderer components qualify against LLE behavior; required surfaces, synchronization, UI audio and input work |
| D5: original-speed 60 FPS | Open | Sustained guest speed, distinct displayed frames and input latency across the route; quiet native-versus-native measurements |
| Stretch: Blinx boot | Open | Real media, kernel loader and device path reach title execution or a precisely recorded failure |

Six dashboard gates remain. These are unequal work packages, not percentages or
a calendar forecast. The detailed compiler stages remain in the
[coverage board](PLAN-AOT-COVERAGE-BURNDOWN.md).

Current evidence: 74 public tests pass. The
[MMX checkpoint](RESULTS-AOT-MMX-2026-10-03.md) adds actual native
menu execution beyond the startup loops. The
[corrected 27B route](RESULTS-CPU-DEADLINES-2026-10-03.md) reaches Settings and
returns, with identical 270-row execution/device/DMA histories and all ten
same-clock images across floor/O0/O2. The observed digest gap is resolved.
[All eight idle phases](RESULTS-IDLE-PHASES-2026-10-03.md) now qualify against
the fully executed 27B route: all ten images and full device/DMA histories
match. This removes another 4.294B executed idle instructions. Native menu work
is unchanged; its share of remaining executed main-menu idle instructions is
26.04%, versus 10.02% with four phases. The subsequent
[general x87 checkpoint](RESULTS-AOT-X87-2026-10-03.md) raises this to **39.33%**
through additional compiled guest work, with idle skips unchanged. The
[expanded x87 checkpoint](RESULTS-AOT-X87-EXPANSION-2026-10-03.md) now raises
main-menu idle to **47.92%**, with five selected regions and 872,134,242 native
instructions over the full route. Final floor/native O0/O2 retain all 270
checkpoint rows and ten images, including the previous floor, with identical
native counters. The subsequent
[kernel integer checkpoint](RESULTS-AOT-KERNEL-INTEGER-2026-10-03.md) adds five
regions using qualified ROL/ROR, OR and BSWAP. Settings-entry native share rises
from **29.33% to 61.32%**, with main-idle unchanged at **47.92%**. Native work over
the route reaches **1,379,683,118** instructions; floor/native O0/O2 retain every
checkpoint and image. The
[scalar checkpoint](RESULTS-AOT-SCALAR-2026-10-03.md) expands to 246 real regions
with qualified shifts, branches, exact narrow reads and immediate stores.
Main-menu idle reaches **70.53%** native execution and Settings entry **76.72%**;
full-route native work reaches **1,815,586,408** instructions. Floor/native O0/O2
again retain all 270 checkpoints and ten images. The larger selection also
increases handoffs substantially, so admission/dispatch cost needs native
performance qualification. Six census intervals reconcile with zero overflow. These are observed
comparisons, not full machine or hardware equivalence. Full menu AOT and
60 FPS are unproven.

The earlier [22B menu census](RESULTS-DASHBOARD-PHASES-2026-10-02.md)
qualified six idle/navigation intervals under the previous CPU reference.
The two startup regions executed nothing in those intervals. MMX transfers,
x87 math and kernel operations are prominent first-code gaps. General LEA
lowering is qualified. Production MMX and bounded x87 arithmetic/transfer
services now exist. Broader x87 operations, exception/stack semantics and
extended-state qualification remain open.

Next: measure native admission/dispatch costs and expand useful post-boot
coverage with the measured x87 and kernel/memory gaps, and scale generated compilation units
without weakening code identity, and qualify general compiler/runtime operations
from the actual dashboard gaps. Renderer reuse proceeds against the same pinned
LLE workload. Blinx media work resumes after dashboard progress or when shared
infrastructure makes the stretch attempt practical.
