# Xbox emulation and portable native execution: decision review

Date: 2026-09-06. Owner request: a faithful Xbox foundation that can support portable
native ports, ultrawide rendering, higher resolution, and higher frame rates, in the
spirit of psxrecomp. Tracking: `beads-xbox.1.24`.

## Finding and recommendation

xboxlle does not currently deliver that native-port product. It is a developing
hardware emulator using a dynamic CPU translator. We have not demonstrated that it
is more accurate or faster than xemu. Our regression tests are useful engineering
assets; matching our own reference images does not establish superiority over xemu
or correctness against a real Xbox.

There is a meaningful candidate product here: generate portable code for selected
Xbox software, retain a faithful device runtime and execution fallback, and expose
tested places to change camera, rendering, and timing. That could justify this work
without having to win every general-emulation comparison. It is a proposal, not a
capability we have already built.

**Recommendation:** retain xboxlle as the reference and measurement environment;
qualify one representative portable native region and one useful enhancement before
committing to a new compiler architecture. Compare the cost of keeping our device
runtime with extending xemu. Do not spend another open-ended performance campaign
assuming it automatically produces a native-port framework.

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

## Bounded next milestone and decision gates

The architecture qualification is the next milestone. A full CPU rewrite, complete
game port, and universal enhancement system are not authorized by this comparison.

1. **Qualify the reference comparison.** Pin a common firmware/disk profile and
   repeatable input route. Observe the same dashboard states in both engines. Use
   the same host, driver, scale, VSync policy, guest settings, and controlled disk,
   RTC, network, and audio configuration. Measure guest progress, distinct displayed
   images, and latency; do not compare instruction-budget labels across engines.
   Separate cold boot because startup boundaries differ. Use interleaved 3+3 or
   more legs, quiet host, and report median and minimum with failures included.
   Existing xemu qualification issue: `beads-xbox.1.2.4`.
2. **Specify the execution boundary, then test plumbing.** Define complete state
   transfer, memory access, interrupts, faults, and code invalidation. Translate a
   small synthetic region and compare it with Unicorn, including changed code and
   fault exits. Passing an arithmetic loop is only a plumbing result.
3. **Prove useful portable execution.** Select a representative region from real
   guest software based on traces, including realistic calls and memory/device
   interaction. Generate C or portable IR, integrate fallback, and execute it on
   x86-64 and an actual ARM64 host. Cross-compiling alone does not pass. Compare
   full observable state and event ordering; record native coverage, fallback
   frequency, handoff overhead, and total workload time. Keep proprietary inputs
   and generated proprietary code private. No hardware-console access is needed.
4. **Prove one enhancement seam.** In a selected scene, expose a deliberate camera
   or presentation hook and demonstrate true wider view with correct culling and
   UI placement. State exactly which title/scene it supports. Evaluate higher-rate
   simulation separately with timing, input, and audio checks. This is a small
   qualification scene, not a new full-game completion arc.
5. **Choose the runtime from evidence.** Estimate the work to maintain these
   contracts on xboxlle versus integrate with xemu. Continue a separate native
   framework only if it supplies useful portable artifacts or enhancement control
   with acceptable correctness and cost. If it cannot, reuse the research in xemu
   or narrow the project. Work already spent is not a reason to retain an inferior
   architecture.

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
claim must wait for gate 1.
