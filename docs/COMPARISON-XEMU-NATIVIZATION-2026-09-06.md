# Xbox emulation and portable native execution: decision review

Date: 2026-09-06, amended after owner clarification. Portable static/native output is
required, with psxrecomp as the architectural model. Actual ARM setup and enhancement
implementation are deferred; prepare their interfaces now. Decision tracking:
`beads-xbox.1.24`; first native implementation: `beads-xbox.1.25`.

## Finding and recommendation

Implementation update: the [AOT burndown](PLAN-AOT-BURNDOWN-2026-09-06.md) now has a
synthetic generated-C target and differential tests. The source comparison below
describes the audited baseline; production dashboard execution still uses Unicorn.

xboxlle does not currently deliver that native-port product. It is a developing
hardware emulator using a dynamic CPU translator. We have not demonstrated that it
is more accurate or faster than xemu. Our regression tests are useful engineering
assets; matching our own reference images does not establish superiority over xemu
or correctness against a real Xbox.

The required product direction is to generate portable code for selected
Xbox software, retain a faithful device runtime and execution fallback, and expose
tested places to change camera, rendering, and timing. This defines a product beyond
general emulation. It is the owner's decision, not a capability we have already built.

**Implementation direction:** retain xboxlle as the reference and device runtime;
build the first generated C/native target on the existing Windows x64 host. Keep
portable artifacts, guarded fallback, and explicit modification interfaces central
to the architecture. A matched xemu comparison remains useful independent work;
neither it nor ARM hardware setup blocks this deliverable. Runtime choices can
change with evidence, but dropping native output is not the current plan.

## What the terms mean

The **guest** is the Xbox program. The **host** is the computer running it. Xbox
instructions are 32-bit x86, also called IA-32. The host might be x86-64 or ARM64.

- **Dynamic recompilation / JIT:** translate instructions while the program runs.
  xboxlle already does this through Unicorn's QEMU-derived translator.
- **Static recompilation / AOT:** translate instructions before running the resulting
  program. Generated C or another intermediate representation can be compiled for
  different host CPUs. Each host needs its own build.
- **LLE:** reproduce the hardware interface that original software expects.
- **HLE:** replace selected guest services with implementations on the host.

These are separate choices. AOT can preserve hardware behavior. A JIT can implement
LLE. Both produce native host instructions; the useful distinction for this project
is whether we also produce reusable, editable per-title artifacts and a stable
runtime. [QEMU describes its translation machinery here](https://www.qemu.org/docs/master/devel/tcg.html),
and [Unicorn documents its JIT foundation](https://www.unicorn-engine.org/).

Xbox being x86 does **not** prevent static recompilation or an ARM64 port. It also
does not make an Xbox executable an ordinary Windows application: privileged
instructions, guest addresses, kernel behavior, devices, exceptions, and timing
still need support. The previous design's categorical rejection of x86-to-C was
incorrect. Self-decrypting code complicates coverage; locally preparing decrypted
code and guarding it at execution time are possible strategies. Code we cannot
prepare or validate must fall back safely.

## Comparison of implemented capabilities

This is an architecture and source comparison, not a matched performance ranking.

| Question | xemu | xboxlle today | psxrecomp today |
|---|---|---|---|
| Execution approach | QEMU-based hardware emulation with runtime CPU translation | Unicorn dynamic translation plus our machine/device runtime | Generated C for recovered guest code, with runtime validation and fallback |
| Faithfulness | Hardware emulation is its stated approach | Hardware emulation is our approach; dashboard-focused validation | Native CPU code still runs with emulated PSX devices and timing |
| Stock Xbox dashboard | Supported setup documented | Renders with our tested firmware/disk profile; broad image support unproven | Not an Xbox project |
| Host portability | Official release includes ARM64 builds as well as x86-64 | Windows x64 validated; portable foundations, other host builds unqualified | Portable generated source and runtime paths; this review did not verify an ARM64 executable |
| Per-title portable C/native build pipeline | No such exporter identified in the inspected project | Not implemented | Implemented generated C, dispatch, and runtime target pipeline |
| Higher internal resolution | Implemented | No user-facing enhancement framework yet | Supersampling paths implemented |
| Ultrawide and higher frame rates | Widescreen setting depends on guest support; no universal 21:9 promise | No tested 21:9 or higher-rate game framework | Aspect controls and per-title hooks; interpolation is not proof of faster game simulation |

xemu describes hardware emulation, rather than replacement of the Xbox kernel, in
its [project overview](https://xemu.app/docs/about/). It documents the
[Microsoft dashboard](https://xemu.app/docs/dashboard/), and its
[FAQ](https://xemu.app/docs/faq/) explains internal resolution scaling and the need
for game support for widescreen. Its [v0.8.136 release](https://github.com/xemu-project/xemu/releases/tag/v0.8.136)
includes Windows ARM64, Linux aarch64, and universal macOS packages. Cross-CPU
portability alone would not distinguish xboxlle from xemu.

Source pins used in this review:

- xboxlle: `400b91d334e45875a43322fa5c5604823138903b`.
- psxrecomp: `bf61b1054313e1c80d5c2bf1b14a1e50f2e54b8a`, sibling source checkout.
- xemu source: `482088490c2d5983ed5e1eafbd09906157c1eca0`, local reference checkout.
- xemu smoke executable: official v0.8.136, reporting
  `fc24584ce88f0915ad7f04775bb7712c2e3f49ee`. The downloaded archive matched the
  release API's SHA-256. This binary and the inspected source are different pins;
  source observations are not an assertion that every detail was tested in it.

## Cxbx-Reloaded: direct execution is a different native path

Follow-up review: `beads-xbox.1.26`. The inspected Cxbx-Reloaded checkout is
`585c49a50af1255ab155099e06f24505f9c5a800`, also the upstream master returned by the
GitHub API during this review. This is source inspection, not a performance test.

Cxbx executes the original Xbox x86 instructions directly on an x86 host. In
[`CxbxKrnl.cpp`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/init/CxbxKrnl.cpp),
`CxbxLaunchXbe` calls the guest entry pointer. Startup remaps guest kernel imports
through `CxbxKrnl_KernelThunkTable`, which supplies the replacement kernel services.
This is not the portable generated-C pipeline planned here.

Its boundary is hybrid. The
[`patch installer`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/Patches.cpp)
skips corresponding D3D, sound, or USB HLE patches when the respective LLE mode is
selected. That is evidence of selectable paths, not proof that all LLE paths are
complete or equally compatible. Calling Cxbx exclusively HLE would be inaccurate.

Consequently, **LLE plus native execution is not a unique combination**. Cxbx already
combines direct CPU execution with some LLE devices, and QEMU's dynamic translator
also emits native host instructions. The planned distinguishing deliverable is
portable ahead-of-time artifacts with a faithful machine runtime, guarded fallback,
and explicit modification interfaces. That is a useful product direction relative
to the inspected projects; this review does not establish worldwide uniqueness,
superior fidelity, or a speed advantage. Production native integration remains pending.

The most useful borrowing candidates are:

| Candidate | Application here | Required boundary |
|---|---|---|
| XbSymbolDatabase function recognition | Annotate known Xbox library functions; suggest translation entry points and named future hooks | Treat signature matches as candidates. Verify code bytes, decoding, control flow, and execution context independently. A symbol match neither translates code nor authorizes an HLE replacement. |
| Patch classification and selective installation | Keep faithful execution and opt-in replacements explicit, with per-profile eligibility | Use our versioned generated-code/runtime interface; do not inherit the direct-execution detours or kernel replacements as the baseline. |
| Timing-instruction containment | Lower guest RDTSC to our deterministic guest-clock API in generated code | Cxbx's `src/core/kernel/support/PatchRdtsc.cpp` intercepts selected guest instructions by patching them. Borrow the requirement to control timing, not its heuristic byte patterns or direct host-memory writes. |

The [symbol database](https://github.com/Cxbx-Reloaded/XbSymbolDatabase) was inspected
at `20eced544726f5558c5a408458f38a086cc4e543`; its separate
[license is MIT](https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/20eced544726f5558c5a408458f38a086cc4e543/LICENSE).
Cxbx ships GPL v2 text and inspected source headers permit GPL v2 or later. Preserve
each component's applicable terms and provenance;
the owner's PolyForm grant does not replace them. No source was imported in this
review. See [LICENSE-SCOPE](../LICENSE-SCOPE.md) for the existing release restriction.

First evaluate symbol recognition as optional preparation metadata for
`beads-xbox.1.25`, after the synthetic generated-code path works. It must not become
a prerequisite for translating every region: unknown title code still needs the
decoder, trace evidence, and safe fallback. Xbox format handling in
`src/core/hle/D3D8/XbConvert.cpp` and CPU edge handling in `src/devices/x86/EmuX86.cpp`
can inform independent test cases. Treat graphics helpers as later measured candidates
rather than importing the HLE graphics pipeline wholesale. Cxbx is not hardware truth.

## What we should borrow from psxrecomp

The useful pattern is **generated code plus a hardware runtime plus guarded
fallback**. It does not require pretending the original machine has disappeared.

In the inspected psxrecomp source, `recompiler/src/main_psx.cpp` emits full C,
shards, dispatch, and code-range manifests. `runtime/runtime.cmake` builds native
targets from them. `runtime/src/memory.c` tracks dirty RAM;
`runtime/src/overlay_loader.c` and the generated dispatcher check live code before
using compiled paths. Overlay capture and a compiler cache cover additional code;
misses can use the interpreter. The former in-process SLJIT path is not the current
design. GPU, DMA, timers, CD, MDEC, SIO, SPU, GTE, and interrupt machinery remain.

Its aspect, supersampling, interpolation, and per-title hooks are separate runtime
features. CPU translation alone does not create them. OS/CPU cache namespaces and
POSIX/NEON paths support a portability design, but some compilation tooling is
Windows-first. Those source paths are not evidence of a tested ARM64 release.

Borrow these contracts and validation ideas first. The MIPS instruction translator
itself cannot become an x86 translator by changing its input decoder.

## What xboxlle would need

Unicorn is already a potential cross-CPU foundation. `UNICORN_ARCH=x86` in our
[CMake configuration](../CMakeLists.txt) selects the **guest** architecture; the
dependency has an AArch64 host backend. Our conditional `-march=native` is a host
CPU tuning choice, not a requirement that the guest and host architectures match.

However, the product has not been ported and validated. Audit the Windows AMD64
profiler context handling in `src/host/sample_profiler.hpp`, compiler-specific
intrinsics, host build scripts, and GL extension requirements. The existing Linux
hygiene workflow does not compile or run the emulator on Linux or ARM64. Floating
point behavior and the rendering backend need cross-host qualification too.

There is also no ready-made hybrid executor boundary:

- `src/machine/machine.cpp` constructs `UnicornFloor` directly.
- [CpuFloor](../include/xlle/cpu_floor.hpp) exposes GPR/EIP/EFLAGS snapshots and only
  a subset of other registers. It is not a complete CPU-state interchange format.
- Switching between AOT and Unicorn needs complete relevant x87/MMX/SSE,
  segment/control/paging, exception, and interrupt state, with explicit ownership
  and precise exits. Private Unicorn metrics integration needs attention too.
- Generated code must preserve x86 flags, faults, memory ordering, x87 precision,
  guest memory access, and device-event boundaries. Ordinary C arithmetic is not
  automatically equivalent to x86 semantics.
- Variable-length decoding, indirect branches, changed code, and aliasing require
  verified entry points, live-code guards, invalidation, and safe fallback.

Changing the CPU translator does not automatically remove GPU work, MMIO traffic,
or driver overhead. The measured WHPX rejection in [the CPU tier study](DESIGN-CPU-TIER.md)
remains evidence about that implementation and workload. It does not rule out a
portable AOT design with different costs. Equally, AOT has no guaranteed speedup.

## Enhancements need their own correctness checks

Higher resolution means more rendering detail. True **21:9** also needs a wider
camera view, correct culling, and readable HUD/menu placement; stretching the final
image is insufficient. These changes can require title-specific knowledge.

Higher frame rates can require separating simulation steps, rendering, input, and
audio. Raising a frame limit can make an old game run too fast. Interpolating images
may look smoother without advancing the simulation more frequently. Report those
separately. Neither change requires AOT in principle; AOT artifacts may make the
relevant guest functions easier to replace and maintain.

Keep a faithful default profile. Optional HLE must be checked against that profile.
Enhancement profiles deliberately change declared behavior, such as aspect ratio;
validate their intended differences and preserve unrelated behavior. A single
pixel-equality gate cannot express both modes.

## Required native pipeline and first milestone

The initial pipeline is **Xbox IA-32 bytes → portable generated C → host machine
code**. On the current PC the final target is x86-64. A future ARM target compiles
the portable source for ARM; it does not execute the x64 binary. Generated code is
built before execution, not just wrapped around a call that runs everything through
Unicorn. Runtime-discovered code may be captured/prepared locally for later builds;
remaining dynamic paths use fallback. Full static coverage is not the first milestone.

The intended steady-state design is predominantly AOT, with Unicorn as fallback and
reference. Expand verified AOT coverage beyond the first region; retain strict guards
when code or execution context changes. Report guest-instruction execution shares
for AOT and fallback within each workload phase, alongside fallback time, reasons,
and transition overhead. Separate cold boot from steady dashboard use. Generated
function counts or source-file size do not establish how much execution is AOT.

Carry these contracts from psxrecomp, adapting the CPU semantics to x86:

| Contract | First Xbox implementation requirement |
|---|---|
| Generated artifacts | Emit C, shared declarations, dispatch data, and a versioned manifest. Identify guest code ranges, instruction boundaries, expected bytes/identity, address context, supported CPU state, exits, and generator/runtime versions. Provide a reproducible native build target. |
| Native dispatch | Validate live code and execution context before entering a compiled region. Code-page writes, mapping changes, and physical aliases must invalidate the relevant lookup; stale or unknown code returns to Unicorn. |
| Runtime ABI | Use explicit fixed-width guest values and a versioned state/event interface. Guest addresses are not host pointers. Return an explicit exit reason and guest continuation location; preserve flags, faults, event deadlines, and state owned by fallback. |
| Memory and devices | Route accesses through the machine contract, including segmentation, paging, permissions, aliases, and MMIO. Any optimized RAM access needs proven preconditions. The first subset can decline operations it cannot model precisely. |
| Modification points | Give generated regions stable, versioned identities and guarded replacement hooks. Keep renderer and scheduler interfaces separate from generated CPU code. Declare each hook's behavior profile; hooks are absent or disabled in faithful mode. |

These are planned interfaces, not assertions that the current CpuFloor implements
them. For a small GPR/EFLAGS-only first subset, other CPU state may stay owned by
Unicorn if it is demonstrably preserved and never implicitly modified. Unsupported
x87/MMX/SSE, privileged, segment/control, string, or fault-sensitive operations must
decline before side effects. Rejecting an instruction after partial execution is
not safe fallback. Broader coverage needs an expanded handoff implementation.

The implementation sequence is:

1. **Artifact and execution contract.** Specify the manifest, ABI, dispatch, memory,
   and event exits above. Emit and compile a bounded synthetic IA-32 region to C
   using the existing toolchain. Assert that the generated path actually executes;
   a JIT cache or all-fallback executable does not satisfy the deliverable.
2. **Semantic and fallback validation.** Compare defined CPU state, memory effects,
   faults, and event ordering with Unicorn. Include partial-register/flag cases,
   changed code, invalid entries, unsupported state, and memory/device boundaries.
   Verify a disabled identity hook preserves behavior and a deliberately enabled
   synthetic hook is selected only for the matching code/profile. This tests the
   modification interface without implementing widescreen or higher-rate simulation.
3. **Dashboard integration.** Select a representative real-software region from
   traces, including realistic control flow and memory interaction. Generate it
   locally, compile it into a target, and execute it with guarded fallback on x64.
   Record native coverage, fallback reasons, handoff cost, and total workload time.
   Preserve the dashboard regression workload. Keep private inputs and generated
   proprietary code outside the source repository. Synthetic-only success is a
   foundation result, not proof of a nativized dashboard.
4. **Cheap portability checks as available.** Keep generated code independent of
   x86 host assembly, host pointer width assumptions, and compiler-specific integer
   behavior. Compile without host-native CPU tuning, compare optimization levels
   or an available second compiler, and use cross-target compilation or emulated
   execution only if existing tools make it inexpensive. These catch classes of
   bugs; they do not prove ARM runtime correctness or performance. Do not stand up
   ARM hardware or a new platform environment for this milestone.

The required outcome is a real, reproducible native artifact with demonstrated
generated execution and safe integration. It does not require an immediate speedup
or completed enhancements. Speed claims still need measurements; later optimization
must include the runtime boundary cost. A full game port and a replacement for every
CPU operation remain beyond the first milestone.

## Independent comparison and later work

The xemu comparison remains unqualified (`beads-xbox.1.2.4`). Before making a ranking,
pin a common firmware/disk profile and repeatable input route, observe the same
dashboard states, and equalize host, driver, scale, VSync policy, guest settings,
disk, RTC, network, and audio. Measure guest progress, distinct displayed images,
and latency; instruction-budget labels are not comparable across engines. Separate
cold boot because startup boundaries differ. Use quiet-host interleaved 3+3 or more
legs and report median and minimum with failures included. This work is not a
prerequisite for building the native path.

Later work includes actual ARM platform qualification, true wider-view camera/UI
changes, and higher-rate simulation with input/audio checks. The first milestone
prepares the interfaces for them. Assess the cost of maintaining our device runtime
versus integrating with xemu as evidence develops; either implementation choice must
continue to serve the required portable-native product.

The faithful 60 FPS goal remains. Use [the performance methodology](PERF-METHODOLOGY.md)
for optimization claims; include boundary costs, not just isolated generated loops.
The working headroom proposal is 20% throughput margin (72 FPS capacity), not yet
a measured result or an owner-approved change to the target. Baseline regressions
still gate implementation; explore enhancements under separate profiles.

Extending xemu or incorporating its code also requires respecting its existing
license. Our original-source choice remains PolyForm Noncommercial. The current
Unicorn combined-build restriction is already recorded in
[LICENSE-SCOPE](../LICENSE-SCOPE.md); this comparison does not resolve it or change
any grant. Licensing/provenance is a release dependency when selecting a runtime.

## What was actually run in this review

The official xemu Windows executable was started locally using private firmware,
the same source HDD behind a QEMU snapshot overlay, and an isolated EEPROM copy.
QMP confirmed the overlay and running state; guest register inspection showed
kernel execution. Network was disabled. No physical console was accessed.

**This did not qualify a dashboard comparison.** The release did not expose the
attempted QMP screenshot command, and the hidden-window screenshot shortcut did
not yield captures. We did not establish an equivalent visible dashboard route,
deterministic epochs, FPS, or comparative accuracy. All instances started for this
review were stopped. Private assets, raw state, and logs remain outside the source
repository. Source findings above remain useful, but a numerical xemu-versus-xboxlle
claim must wait for the independent comparison to be qualified.
