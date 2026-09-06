# INPUTS - private machine material

A clean checkout builds and runs the unit/spike tests without private Xbox material.
Stock-dashboard replay and interactive dashboard work require a private sibling
`machine` directory. Keep it outside the repo tree:

```text
F:\Projects\xboxlle\
  xboxlle\        # this repository
  machine\        # private, local-only console material
```

The current repository ignores common private asset names if they are accidentally
placed under the repo, but `.gitignore` is only a safety net. Do not commit or publish
private console bytes, EEPROM contents, HDD images, extracted boot blobs, dashboard
files, game discs, generated replay logs/reports, screenshots, or local machine evidence
without a separate hygiene review. See [repository hygiene](REPOSITORY-HYGIENE.md)
and the [publication guard](../tools/check_repository_hygiene.py).

## Current stock-dashboard layout

These four files are the actual inputs used by `tools/run_stockdash_replay.ps1` and the
README interactive command:

| Path relative to repo root | What it is | Current use |
|---|---|---|
| `..\machine\flash-dump-ff000000.bin` | Flash image mapped by `--boot` | Mirrored into the 16 MiB flash window. |
| `..\machine\bios-extract\bldr.bin` | Already extracted/decrypted 2BL blob | Loaded at RAM `0x90000` by `--run2bl` and entered at the current approximate 2BL handoff. |
| `..\machine\xbox-hdd-stockdash.img` | Full sector-for-sector stock Xbox HDD image | Provides FATX partitions and dashboard files. |
| `..\machine\eeprom.bin` | 256-byte per-console EEPROM dump | Attached as the SMBus EEPROM and used by the guest/kernel path. |

The repo does not bundle a BIOS extractor; the current executable consumes
`bios-extract\bldr.bin` as a supplied artifact. See the external extraction step below.
Do not claim this path is authentic MCPX cold boot. Authentic MCPX -> 2BL cold boot is a
later faithfulness pass.

The HDD image must be the whole disk image, not a folder and not only `xboxdash.xbe`.
The dashboard target includes the C drive/FATX contents and related stock dashboard files
as they exist on the imaged disk. The default IDE attach path is copy-on-write: guest
writes are kept in memory and served back during the same run, but the image file is not
modified unless `--hdd-writeback` is explicitly passed.

The EEPROM dump is exactly 256 bytes. It contains per-console secrets, region/video
settings, and material used by the console software for HDD-key derivation. Never publish
it, decoded values, derived keys, or raw byte excerpts.

## Preparing the bootloader

Obtain the flash, EEPROM and full HDD image from your own console using your
existing dump process. A locked physical HDD must be unlocked for imaging; supply
readable raw sectors, not a compressed archive or a virtual disk container. Keep
the untouched originals and use working copies. The current boot setup is tested
with one machine/firmware profile; arbitrary retail BIOS revisions and modified
disk layouts are not claimed compatible.

An external [XboxBiosTool](https://github.com/tommojphillips/XboxBiosTool) checkout
is used in the development workspace to extract/decrypt BIOS components. Its
documented `/extr` command accepts the flash and either a matching private MCPX
dump (`/mcpx`) or a private 16-byte 2BL key file (`/key-bldr`). The MCPX revision
must match the BIOS; consult that tool's documentation. For example, adapting the
tool executable path to your installation, from this repository root:

```powershell
$biosToolExe = 'F:\Projects\xboxlle\refs\XboxBiosTool\xbiostool.exe'
& $biosToolExe /extr ..\machine\flash-dump-ff000000.bin `
  /mcpx ..\machine\mcpx.bin /dir ..\machine\bios-extract
```

This is the tool's documented interface, not a claim that every BIOS/MCPX pairing
has been validated here. Verify that extraction reports successful decryption and
produces `bldr.bin`; do not substitute the encrypted flash region or kernel blob.
Some already decrypted/modified BIOSes need different extraction options. The
current executable does not read MCPX itself, but extraction of an encrypted 2BL
needs the matching MCPX/key. All extraction output remains private, including
additional key files that the tool may emit. No download of firmware, dashboard
data, EEPROMs or keys is part of this setup.

## Optional/later machine material

These inputs are useful for hardware-oracle or later secure-boot work, but they are not
consumed by the current stock-dashboard replay command:

| File/category | What it is | Status |
|---|---|---|
| MCPX 1BL ROM dump | Secret MCPX boot ROM, typically 512 bytes | Needed for authentic cold boot/hardware validation work, not current replay. |
| Pristine retail flash BIOS | Unmodified retail flash image from the owner console | Later P2 authentic secure-boot profile. |
| `hardware.json` | Hand-written machine metadata: board revision, RAM size, video encoder, SMC, AV pack, HDD/DVD identity, dump methods | Evidence/provenance. Current executable does not consume it. |
| Game disc image (`.iso`/`.xiso`) | Owner-supplied game disc image | Arc 2 only. |

## Hardware oracle capability

The companion `xboxlle-probe` repo is the maintained homebrew/hardware path. It requires
homebrew execution on an original Xbox, an nxdk build environment, and a network or serial
channel. That capability is for collecting ground truth such as CPU flag edges, CPUID/MSR,
TSC, PCI config reset values, SMBus/SMC responses, NV2A register behavior, cache behavior,
and other hardware facts. It is not required for a clean build or for the current local
stock-dashboard replay.

## Log/report sensitivity

Replay artifacts can include exact argv, local paths, frame hashes, input script names,
input timing, endpoint report counts, device counters, and performance data. They should
be treated as review-before-publish material even when they contain no raw firmware,
EEPROM, or HDD bytes. Fixture-specific runs whose final frame is not the canonical 12B
frame should pass `-ExpectedFinalSha ''` and use their own evidence instead of relying on
the canonical final-SHA gate.
