# INPUTS — what the maintainer must supply

Everything here is **copyrighted or per-console secret**. It is **git-ignored, hash-gated,
and never committed**. All of it should come from **your own console**, dumped with homebrew
you already run — the cleanest sourcing story, and the only way to get the P2 authentic
profile and the hardware-oracle tier.

## Nothing is needed to start A0

A0 (foundations + host shell + observability + CPU-floor spike) builds and tests with **no
firmware and no hardware** — CPU fixtures, decode corpora, ring plumbing. We can begin
immediately.

## Tier 1 — needed for A1 (oracle + hardware harness) and A2 (boot)

Assemble a `machine/` "machine evidence package":

| File | What it is | Notes |
|---|---|---|
| `mcpx.bin` | MCPX secret boot ROM (1BL) | 512 bytes. **MCPX v1.0** preferred (matches the recommended dev profile). |
| `flash.bin` (P0) | An **xemu-bootable modified/debug retail BIOS** | Engine and reference must boot **byte-identical** firmware, so the cosim is meaningful. "COMPLEX 4627"-class is the known-good pairing. |
| `eeprom.bin` | Per-console EEPROM (keys, region, video standard, settings) | 256 bytes. Fed identically to engine and reference. |
| `hardware.json` | Machine metadata (hand-written) | motherboard revision, MCPX revision, RAM bytes (67108864), video encoder (Conexant/Focus/Xcalibur), SMC version, AV pack, video standard (NTSC-M/PAL), HDD model+firmware, DVD model, and the **dump method** for each blob. Pins the machine profile. |

## Tier 1b — needed for A4/A5 (the dashboard *is* on the HDD)

| File | What it is | Notes |
|---|---|---|
| `hdd.raw` | Full **sector-for-sector** image of your unlocked HDD | Every partition + unused sectors (FATX). **Contains `xboxdash.xbe`** — Arc 1's whole target. Needed earlier than a game would be. |

## Tier 0 — the hardware-oracle capability (valuable from A0/A1, the highest-leverage thing)

- **Homebrew execution on your Xbox** to run our **nxdk probe-agent XBEs** (small binaries we
  write) that stream the same JSONL schema over the network. This is the ground-truth oracle
  for: defined/undefined flags, x87/MMX/SSE edges, CPUID/MSR, TSC, PCI config reset values,
  SMBus/SMC identities/responses, NV2A register reads, and cache/SMC behavior — none of which
  xemu can authoritatively provide.
- Practically: nxdk toolchain to build XBEs + a network (or serial) channel from the console
  to this machine. This is the "hook Claude up to a real Xbox" capability, pulled **forward**
  because it de-risks the hardest subsystems (NV2A, timing).

## Tier 3 — later (authentic retail + Arc 2)

| File | What it is | Notes |
|---|---|---|
| `flash-retail.bin` | Your **pristine, unmodified** retail flash BIOS | P2 authentic secure-boot profile, hardware-validated. Off the Arc-1 critical path. |
| Blinx disc image (`.iso`/`.xiso`) | Your own copy of **Blinx: The Time Sweeper** | Arc 2 only. |

## Legal note

We only transform **your own** dumped bytes, on your own hardware, and never redistribute
them. The framework repo stays clean and shareable. Harvested emulators (xemu, sp00nz,
Cxbx-Reloaded) are cloned and used as external references/harvest sources under their GPL
terms, kept **out of the framework tree** (in the workspace parent, not committed here).
