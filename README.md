# xboxlle

A **faithful, LLE-first low-level emulation platform for the original Xbox** (2001,
IA-32 "Coppermine" Pentium III + NV2A). It boots the **real** MCPX boot ROM, 2BL, and
Microsoft kernel — no hand-written kernel, no HLE shell. Its first product goal is the
**real Xbox dashboard, fully navigable**, running on that real firmware; navigation is not
yet demonstrated.

It belongs to the recomp family (nesrecomp / psxrecomp / gbarecomp / gcnrecomp /
n64lle) and inherits its doctrine: **LLE-first, run the real firmware, validate against
ground truth, always-on ring buffers, no faked state, no stubs, artifacts are evidence
not authority.** Like its closest sibling **`n64lle`**, it takes the *low-level-emulation*
name rather than "recomp" on purpose: the Xbox guest is x86-on-x86-64, so this is a
**dynamic execution platform** (a CPU floor + JIT + harvested device models), **not** a
guest→C static recompiler. That distinction is why it is `xboxlle`, not `xboxrecomp`.

It is a from-scratch successor to `../xboxrecomp` (v1), whose HLE kernel (119 hand-written
thunks) failed the same way the PSX predecessors did: faked kernel state drifted from
real RAM → "thousands of swaps, black framebuffer, never a title screen." v1's own
`ARCHITECTURE-REVIEW.md` is this project's founding rationale.

## Strategy in one paragraph

Don't reinvent proven wheels; don't take shortcuts either. **Harvest** the hard,
already-solved parts and re-express them in our own modular, observable, hardware-correctable
architecture: the CPU from **Unicorn Engine** (embeddable QEMU x86 core, GPL-2), the
**NV2A / APU / device models** from **xemu** (GPL-2, rewritten modular + validated against
real hardware). **Borrow** the host layer outright (SDL3, Vulkan/OpenGL — pure presentation,
not faithfulness-critical). **Build ourselves** the parts that are the actual value: the
deterministic machine wiring, the observability/cosim/hardware-conformance harness, the
LLE-floor → opt-in-HLE framework, and the per-title packaging. Real **Xbox hardware** (via
the companion [`xboxlle-probe`](https://github.com/mstan/xboxlle-probe) homebrew agent) is
the ground-truth oracle; xemu is a reference we harvest from and check against.

## The two arcs

- **Arc 1 — the real Xbox dashboard, fully navigable and functional**, booted LLE. The
  platform's first product and current goal; no game involved. (Mirrors gcnrecomp's GameCube
  main menu and psxrecomp's BIOS shell.)
- **Arc 2 — Blinx: The Time Sweeper**, the first game, on the proven Arc-1 platform, as its
  own per-title repo (`BlinxLLE`).

## Companion hardware probe

[`xboxlle-probe`](https://github.com/mstan/xboxlle-probe) is the independently built nxdk
homebrew and host client used to collect ground truth from a physical original Xbox. It is
kept as a separate repository because it does not participate in the emulator build and has
its own hardware-safety, deployment, protocol, and AI-agent documentation.

> [!CAUTION]
> `xboxlle-probe` provides dangerous low-level hardware access. Incorrect use can crash,
> corrupt, or brick a console. Read its warning and security documentation before using it.

The original `agent/` and `tools/xbox_agent.py` files in this repository are retained as
historical provenance. The standalone repository is the maintained, authoritative version.

## Where to read

| Question | File |
|---|---|
| What is this, in 10 lines? | this file |
| The ONE current goal + next milestone | `NORTH-STAR.md` |
| How we work (the law) | `PRINCIPLES.md` |
| The architecture, cold-readable | `docs/DESIGN.md` |
| The arc/milestone ladder | `docs/ROADMAP.md` |
| What is true *right now* | `STATUS.md` |
| What the maintainer must supply (BIOS/dumps/hardware) | `docs/INPUTS.md` |
| How real-hardware measurements are collected | [`xboxlle-probe`](https://github.com/mstan/xboxlle-probe) |

## Status

**Arc 1 / A4 — real firmware is producing visible LLE boot output, not yet a dashboard
menu.** The retail boot chain reaches the dashboard XBE through the modeled CPU and Xbox
devices. The guest's PFIFO/PGRAPH stream rasterizes into unified RAM, the retail kernel's
flip queue drives PCRTC, and SDL3 presents the resulting scanout. A deterministic 4.5B
instruction replay reaches a recognizable Xbox X logo; this is guest-generated output with
no replacement kernel, D3D interception, or host-rendered substitute. The later 4.75–6.0B
window is black, 6.25B begins a mode transition with noisy lower scanlines, and the 6.4B
endpoint is near-black 720x480 scanout at `0x03c8c000`, not a visible dashboard/menu.

The hardware floor is not complete: remaining low-frequency NV2A states, later boot/dashboard
frames, observed guest XID polling/menu navigation, timing refinements, and broader device
accuracy are still A4 work. See `STATUS.md` for exact evidence and `PROGRESS.md` for the
current burndown.

An opt-in disconnected-link replay now reaches `evoxdash` and proves its first mode-8
immediate quad stream. The modeled front end submits and assembles those vertices, but mode 8
remains deliberately recorder-only while post-VSH and texture evidence is gathered. The late
alternating buffers are still near-black; this is not a visible menu or navigation claim.

For the opt-in live Controller S keyboard path during an LLE replay, add `--interactive` to a
`--boot ... --run2bl ...` invocation. It opens an SDL window before emulation and presents
guest scanout at a host-wall-clock 30 Hz maximum; arrows map
to the D-pad, `Z`/`X` to A/B, and Enter/Backspace to Start/Back. This feeds the modeled OHCI/XID
endpoint 2 on the emulation thread. The retail replay has not yet been proven to poll endpoint 2
at the dashboard menu: it configures the device but never requests the XID class descriptor
or publishes an endpoint ED. This is input plumbing rather than a navigation-complete claim.

For reproducible visual checkpoints, `--frame-series <path-prefix> <instruction-interval>`
writes scanouts at guest-instruction intervals. The interval is a positive integer of at least
1,000,000 instructions; captures are named with their actual sampled instruction count.

Two bounded diagnostics support late-boot investigation. `--net-disconnected` changes the
modeled PHY link state before guest execution; it is an experimental pre-boot link transition,
not a verified cold-reset cable-out model. It cannot be combined with a UDP Ethernet peer.
`--pfifo-clear-trace <32-bit-color-address>` arms at the first successful clear whose first
resolved pixel address matches the target. It then selects the first subsequent Kelvin BEGIN,
retains the 32 decoded methods ending at that BEGIN, and records through its matching END with
a hard cap of 4,096 records. Entries include the active object class and parser-time GET/PUT
snapshots. The trace describes command order only; it does not by itself prove PGRAPH
completion, notification, semaphore, or flip timing.

## Legal / distribution posture

No copyrighted bytes ever enter this repo. MCPX ROM, flash BIOS, kernel, EEPROM, HDD
images, and game discs are **maintainer-supplied, git-ignored, and hash-gated**, and come
from the maintainer's own console. Harvested code (Unicorn, xemu) is GPL-2; the whole is
GPL-2. Borrowed host libs (SDL3 zlib, etc.) are permissive and compose in.
