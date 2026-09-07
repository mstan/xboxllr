# DESIGN — xboxlle

Cold-readable architecture document. Self-contained: an engineer or LLM with no prior
context should understand what we're building, why, and how it differs from both the recomp
siblings and the existing Xbox emulators. It encodes two adversarial review rounds and the
maintainer's subsequent direction (harvest over from-scratch; arc structure).

**Status, 2026-09-06:** this document includes original design intentions, not just
implemented features. The owner's clarified goal includes portable native ports;
[NORTH-STAR](../NORTH-STAR.md) and the
[architecture comparison](COMPARISON-XEMU-NATIVIZATION-2026-09-06.md) govern that
implementation plan. Static/native output is required; ARM hardware qualification
is deferred. The current build uses Unicorn and SDL3/OpenGL. Proposed alternative
backends and validation oracles below must not be read as completed work.

---

## 1. The target

The original Microsoft Xbox (2001):

- **CPU:** Intel "Coppermine-128" Pentium III, 733 MHz, IA-32, 32-bit protected mode with
  paging, x87 / MMX / SSE (SSE1, *not* SSE2). Single core.
- **Chipset:** NVIDIA NV2A north/southbridge. **MCPX** southbridge holds a 512-byte secret
  boot ROM.
- **RAM:** 64 MB unified; 26-bit address bus (aliases wrap mod 64 MB).
- **GPU:** NV2A (GeForce3/4-class): a **pushbuffer** (PFIFO) feeding a programmable pipeline
  (PGRAPH) with vertex/pixel shaders and register combiners.
- **APU:** MCPX audio (AC'97-style + a hardware DSP / voice processor).
- **Storage:** IDE HDD (locked with a per-console key, FATX filesystem) + DVD-ROM. Software
  is XBEs (a PE variant); the **dashboard** (`xboxdash.xbe`) lives on the HDD.
- **Boot chain:** reset → **MCPX 1BL** runs *X-codes* (memory-controller / chipset init),
  then RC4-decrypts the 2BL → **2BL** initializes the platform, then RC4-decrypts +
  LZX-decompresses the kernel and relocates it → **kernel** launches the dashboard (or a
  game). The chain is self-decrypting / self-modifying by construction.

## 2. Current LLE runtime and required portable native tier

xboxlle currently uses Unicorn's dynamic translator to execute guest IA-32 code.
The guest ISA does not constrain the host to x86: Unicorn has an AArch64 host
backend, although xboxlle itself has only been validated on Windows x64.

The earlier claim that x86-to-C has no purpose was too broad. Portable AOT output
can support other host CPUs and maintainable per-title modifications even on an
x86 host. Self-decryption and changed code complicate static coverage; they do not
rule it out. Local code preparation, execution-time guards, and a dynamic fallback
are candidate tools. AOT does not by itself solve Xbox devices, timing, or speed.

The owner requires portable native ports following psxrecomp's architecture. The
initial pipeline is IA-32 bytes to portable generated C to host machine code, first
on Windows x64. Keep this runtime as the faithful baseline and device implementation.
Full CPU state preservation, faults, interrupts, memory/device boundaries, and code
invalidation must have explicit contracts, even when the first translated subset is
small. Preserve unsupported state and fall back before unsupported operations.
ARM hardware setup and actual enhancements are deferred; design their interfaces now.
See the [implementation milestones](COMPARISON-XEMU-NATIVIZATION-2026-09-06.md).

## 3. Strategy: harvest, borrow, build

We reach the goal fast **without shortcuts** by partitioning the system by *who should
author each part*:

| Part | Strategy | Source | License |
|---|---|---|---|
| **CPU core** | Harvest as embeddable, hookable library | **Unicorn Engine** (QEMU x86 core) | GPL-2 |
| **NV2A, APU, device models, boot logic** | Harvest domain logic, **rewrite modular + hardware-validate** | **xemu** | GPL-2 |
| **Host shell** (window, input plumbing, audio out, GPU context) | Borrow outright | **SDL3** (incl. SDL_gpu) | zlib |
| **Host GPU helpers** | Borrow | VMA, glslang/SPIRV-Tools | MIT/BSD |
| **Decode tables / FP / formats / crypto primitives** | Borrow | Zydis, SoftFloat, ISO9660/XBE/FATX/LZX/RC4/SHA-1 libs | MIT/BSD |
| **Machine wiring, observability, cosim/hardware harness, LLE→HLE framework, packaging** | **Build ourselves** | — | Original contributions: see LICENSE-SCOPE |

This table records a sourcing strategy, not a completed attribution audit. The earlier
statement that the maintainer accepted GPL for the whole project is superseded:
original work is offered under PolyForm Noncommercial, while third-party terms remain.
The combined Unicorn executable has an unresolved distribution restriction. See
[LICENSE-SCOPE](../LICENSE-SCOPE.md) and
[THIRD_PARTY_ATTRIBUTION](../THIRD_PARTY_ATTRIBUTION.md); this document grants no exception.

**Why maintain a separate runtime?** The original motivation was control over
instrumentation and device boundaries. That is not evidence of greater accuracy or
lower maintenance cost. Begin the native path against this runtime and assess its
cost as implementation proceeds. Comparing xemu integration remains useful but does
not gate native output or replace that required deliverable.

**Why harvest the CPU as a library (Unicorn) rather than write our own?** Unicorn exposes
QEMU's mature x86 core — softfloat x87, correct flags, `REP`/fault semantics, and a dynamic
translator (the JIT "fast tier" we concluded we need) — as an *embeddable* engine with hooks
for per-instruction/block/memory/interrupt events. That is exactly our need. A from-scratch
checked interpreter remains the fallback/truth-anchor if an early spike finds Unicorn's
fidelity or hookability insufficient.

## 4. The execution engine

```
                 guest x86 bytes
                       |
        Harvested CPU core (Unicorn / QEMU x86)  ← dynamic translation + hooks
                       |
     our hooks:  ring instrumentation · MMIO/PIO dispatch · deterministic clock
                 · block versioning for SMC/decrypt · cosim checkpoints
                       |
        our machine model: memory (64MB + 26-bit mirror), devices, scheduler
```

Self-modifying / self-decrypting code is handled by **page identity/versioning**: a code
page's generation bumps on write, invalidating dependent translated blocks; runtime
decryption is then ordinary memory mutation. To stop at an exact instruction boundary (fault,
interrupt, cosim checkpoint), the core runs under an instruction budget (QEMU already supports
this). Block hooks are a possible long-term **mod-injection** point. Whether runtime
hooks or generated artifacts are easier to maintain depends on the modification;
neither execution strategy is inherently the better modding substrate.

An alternative interpreter would occupy the CPU-core slot; any generated fast tier
would need the same state and event contract. Host backend and artifact choices
must preserve portable source artifacts, not assume an x86-64-only JIT. The first
source backend is C; lower-level IR may be added if implementation evidence warrants it.

## 5. Memory & machine model (built by us)

Flat 64 MB guest RAM with the 26-bit-bus mirror-aliasing modeled; MMIO windows (NV2A at
`0xFD00_0000`, etc.) dispatched via our hooks; MCPX ROM overlay and ROM-disable modeled as
real device state. v1's `xbox_memory.h` (mirror views) and `xbe_parser.cpp` are clean
references. The machine is defined by an explicit **tagged machine manifest** (every register/
RAM range provenance-tagged: derived-from-disassembly / hardware-doc-specified /
from-controlled-execution-awaiting-derivation / intentionally-unknown-blocker) — never a
silent whole-machine snapshot.

## 6. Boot seed: defer only the crypto, run the real machine init

Seeding at "decrypted kernel entry" would silently fake everything the earlier stages set up
(memory-controller/chipset config, RAM/scratch, ROM overlay, page tables, PCI, security
registers, relocation, CPU descriptor state) — the v1 failure at a lower level. Instead we run
the **real MCPX X-codes from the reset vector** and defer **only the RC4/RSA op** — inject the
decrypted 2BL bytes at that one boundary (tagged in the manifest); the real 2BL then does
machine setup, decompression, relocation, and kernel transfer as real guest code. Authentic
MCPX cold-boot crypto is a later faithfulness pass (P2 profile), validated on hardware.

## 7. Rendering: software reference is truth, host GPU is delivery

The NV2A is harvested from xemu but split by faithfulness:

- A **software rasterizer** computes NV2A output in software — a regression reference,
  not independent proof of what the real hardware produces.
- A **hardware-accelerated backend** (NV2A → Vulkan/GL via SDL3/SDL_gpu) draws on the host
  GPU for speed, **validated to match the software reference** by frame-hash parity.

SDL3/Vulkan are pure delivery (borrow freely). The NV2A→host translation is mildly
faithfulness-relevant and gets validated; the raw API binding is not. Build the pushbuffer/
PFIFO **recorder ring first** (observe-before-implement) and implement only the method subset
the dashboard actually emits.

## 8. Oracles & firmware profiles

**Real Xbox hardware is the authority** (homebrew nxdk probe agent emitting our JSONL schema),
a first-class tier from A0/A1 — because we harvest xemu's code, xemu can share our bugs and so
cannot be the independent oracle for harvested subsystems. **Pinned xemu-TCG** is a fast
*reference* oracle (control flow, device sequencing, regression) behind a determinism gate
(pinned commit, TCG-not-KVM, single vCPU, fixed firmware hashes, virtualized RTC, fixed
input, repeated boots reproduce identical epoch hashes). Cxbx-Reloaded is reference-only.

Three firmware profiles run the same engine:

| Profile | Purpose | Firmware |
|---|---|---|
| **P0 development parity** | Arc 1 daily dev + cosim | MCPX 1.0 + one xemu-bootable modified-retail BIOS ("COMPLEX 4627"-class); byte-identical in engine and reference |
| **P1 open diagnostic** | transparent bring-up | Cromwell, external fixture (GPL; can't load games, isn't the MS kernel) |
| **P2 authentic retail** | final secure boot + hardware validation | the maintainer's own pristine MCPX / flash / HDD / EEPROM |

## 9. Subsystem risk stack

Ranked by expected pain (x86 *decoding* is not near the top, and is mostly harvested):

1. **NV2A** (pushbuffer + programmable pipeline + shaders + combiners).
2. **Kernel ↔ hardware synchronization** (interrupts, DPCs, timing).
3. **Chipset & PCI behavior** (reset values, config space, W1C, ROM overlay).
4. **Interrupt & timing model.**
5. **APU / DSP / audio.**
6. **IDE / DVD + HDD security (FATX, lock key).**
7. **CPU edge semantics** (mostly the harvested core; we validate the edges).

The dashboard (Arc 1) is a real 3D UI and exercises many of these systems. Passing it
establishes behavior for that workload; it does not prove all hardware paths or games.

## 10. Project identity & deliverable

**xboxlle aims to provide a faithful foundation for portable native ports.** The
dashboard is the first validation workload; Blinx remains a later full-title arc.
Generated portable code, a device runtime, guarded fallback, and explicit enhancement
hooks are the required product architecture. Native output is not implemented.
NORTH-STAR scopes the first generated C target on the existing host. Host-specific
JIT caches can complement this pipeline but cannot replace portable source artifacts.
Build the modification interfaces now; actual enhancements and ARM qualification follow
later. This does not require translating every boot or kernel path in the first milestone.

## 11. Observability & tooling (built by us)

Always-on typed rings (retired-PC/block, MMIO/PIO, IRQ/device-event, dirty-page, per-frame
state, undefined-flag-read) + a TCP debug server speaking **one JSONL schema** shared by the
engine, the xemu reference bridge, and the hardware probe agent. Probes query rings for a
window; never arm-then-run; never pause/step to synchronize. Hierarchical epoch-hash cosim
with checkpoint + binary-search to first divergence, behind the four validation gates.

## 12. Build & host

The validated setup is Windows x64 with CMake, Ninja, and MinGW/MSYS2; other hosts and
toolchains require qualification. The engine builds and tests **without
any copyrighted firmware** (CPU-floor crucibles, decode corpora, fixtures). Firmware-dependent
boot and the dashboard require the maintainer's git-ignored, hash-gated dumps
(`docs/INPUTS.md`), including the HDD image (the dashboard lives there).
