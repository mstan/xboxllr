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

## Reducing user-supplied inputs

The following is a development plan, not an alternative setup command. Today,
omitting `--hdd` or `--eeprom` reports an absent device; it does not create one.
Only the built-in fixture demo and unit/probe tests need no console material.

| Input | Candidate replacement | Remaining requirement |
|---|---|---|
| Full physical HDD dump | Generate a sparse raw disk with the correct Xbox/FATX layout, then import a user-supplied dashboard directory. | The Microsoft dashboard's executable and supporting files still come from an authorized source. A zero-filled file alone is not a formatted Xbox disk. Import/layout compatibility needs validation. |
| Physical EEPROM dump | Generate a persistent virtual console profile with fresh identity, HDD-key material, selected region/video settings and valid checksums/authentication. | Match the supported firmware revision and validate boot/settings behavior. Do not clone another console's identity or assume 256 random bytes are valid. Any required firmware-specific cryptographic material needs its own sourcing review. |
| Separately supplied decrypted 2BL | Integrate local extraction from supplied firmware with any required private MCPX/key input. | This derives a private artifact; it does not turn the bootloader into redistributable content or remove the firmware requirement. |
| Microsoft dashboard | Optional independently authored or appropriately licensed homebrew dashboard. | Different guest software and a separate compatibility target; it cannot stand in for stock-dashboard fidelity validation. |
| Microsoft flash/kernel/MCPX | Independently implemented, appropriately licensed replacement firmware could serve a future homebrew profile. | No compatible replacement is integrated or validated here. It must not silently replace the stock LLE floor. |

There is practical precedent: [xemu generates a valid EEPROM when none is supplied](https://xemu.app/docs/eeprom/).
Its [required-files guide](https://xemu.app/docs/required-files/) also offers a disk
without the Microsoft dashboard. The separate
[xemu-dashboard project](https://github.com/xemu-project/xemu-dashboard) builds an
open-source guest dashboard and creates a raw disk before converting it to QCOW2.
That is a useful implementation reference, not proof that its image boots in
xboxlle. The current IDE backend requires raw sectors, not QCOW2.
[Cromwell](https://github.com/XboxDev/cromwell) is a free replacement BIOS designed
to load Linux; its maintainers explicitly say it cannot load original Xbox games.
It is therefore not a drop-in solution for this project's stock-software target.

The engineering recommendation is to start with a disk formatter/importer and a
virtual EEPROM generator. This could remove the requirement for a full HDD image
and a real console's EEPROM while retaining user-supplied firmware and dashboard
files. Keep generated profiles private and persistent; use fixed synthetic test
profiles only for reproducible tests, never as shared real-console identities.
Retain the existing dump-based route as the reference and validate each generated
profile against the same device behavior before promoting it.

### Legal boundary

For a U.S. baseline, copyright protects original expression, while procedures,
systems and methods of operation are excluded under
[17 USC 102(b)](https://www.copyright.gov/title17/92chap1.html#102).
An independently written formatter or original homebrew program can avoid
copying Microsoft's expressive content. Generating, decrypting, compressing or
embedding someone else's firmware/artwork does not itself grant distribution
rights. An open-source replacement still has license obligations.

Access-control circumvention is a separate question under
[17 USC 1201](https://www.copyright.gov/title17/92chap12.html#1201), including its
conditional interoperability provisions. Owning a console is not a blanket
permission to distribute its software or keys. These are design boundaries,
not jurisdiction-independent legal clearance for a particular extraction tool
or firmware package.

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
