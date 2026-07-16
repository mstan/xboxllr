# DESIGN — xboxlle

Cold-readable architecture document. Self-contained: an engineer or LLM with no prior
context should understand what we're building, why, and how it differs from both the recomp
siblings and the existing Xbox emulators. It encodes two adversarial review rounds and the
maintainer's subsequent direction (harvest over from-scratch; arc structure).

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

## 2. Why this is `xboxlle`, not `xboxrecomp`

Every "recomp" sibling (psxrecomp MIPS→C, gbarecomp ARM→C, gcnrecomp PPC→C) is a *cross-ISA
static recompiler*: it lifts guest code to C ahead of time because translation is the only
path to native speed on a foreign ISA. Two facts kill that model for Xbox:

1. **The guest ISA is the host ISA** (x86-on-x86-64). A guest→C→native pipeline throws away
   native x86 semantics only to have a C compiler rebuild the same ISA — huge artifacts,
   solving none of the hard hardware problems.
2. **The boot chain is self-decrypting / self-modifying.** Static AOT recompilation cannot
   lift code that does not exist until runtime (the 2BL and kernel are ciphertext until
   decrypted; games patch/generate code). You'd need a dynamic executor anyway.

So Xbox needs a **dynamic** engine. Following the family's own precedent (`n64lle` is the
low-level-emulation counterpart to `n64recomp`), this project takes the `lle` name. It is
still in the family and still faithful; it simply respects what x86-on-x86 makes different.

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
| **Machine wiring, observability, cosim/hardware harness, LLE→HLE framework, packaging** | **Build ourselves** | — | GPL-2 whole |

The permissive borrows fold into a GPL-2 whole alongside the GPL-2 harvests. The maintainer
has accepted GPL-2.

**Why harvest instead of fork xemu wholesale?** Forking makes xemu's coupled internals and
its approximations our foundation, and leaves us spelunking a huge foreign tree rather than
owning a clean, instrumentable one. Harvesting takes the *knowledge and logic* (years of
reverse engineering) and re-expresses it where we can instrument, cosim, and hardware-correct
it. That is more complete than a fork, not a shortcut.

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
this). The block-hook layer is also our **mod-injection** point long-term (replace a block's
translation), which is why a dynamic engine is a *better* modding substrate than a static one.

If the spike selects the from-scratch interpreter instead, the same picture holds with our
interpreter in the CPU-core slot and a later hand-written x86→x86-64 JIT as the fast tier.

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

- A **software rasterizer** computes NV2A output in software — the **byte-exact reference /
  oracle** for "what pixels are correct."
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

The dashboard (Arc 1) is a real 3D UI and exercises nearly all of these — which is the point:
Arc 1 proves the platform faithful across the board before any game.

## 10. Project identity & deliverable

**xboxlle is a faithful, LLE-first Xbox execution platform.** Its products are per-target:
**Arc 1** ships the faithful navigable dashboard (in this repo, as the firmware product, like
gcnrecomp's menu). **Arc 2** ships **Blinx** in a sibling `BlinxLLE` repo that pins the
framework and takes BIOS + ISO → faithful, enhanced Blinx. No static x86→C export
(that's `n64recomp`'s lane, not `n64lle`'s). Any per-title speed artifact would be **cached
native block packs** (validated JIT output), not C source.

## 11. Observability & tooling (built by us)

Always-on typed rings (retired-PC/block, MMIO/PIO, IRQ/device-event, dirty-page, per-frame
state, undefined-flag-read) + a TCP debug server speaking **one JSONL schema** shared by the
engine, the xemu reference bridge, and the hardware probe agent. Probes query rings for a
window; never arm-then-run; never pause/step to synchronize. Hierarchical epoch-hash cosim
with checkpoint + binary-search to first divergence, behind the four validation gates.

## 12. Build & host

Windows x64 host; CMake + Ninja; MSVC or MinGW/MSYS2. The engine builds and tests **without
any copyrighted firmware** (CPU-floor crucibles, decode corpora, fixtures). Firmware-dependent
boot and the dashboard require the maintainer's git-ignored, hash-gated dumps
(`docs/INPUTS.md`), including the HDD image (the dashboard lives there).
