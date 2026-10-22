# Setup: games, console profiles and the original Dashboard

Status: October 21, 2026. Current previews run on Windows x64. Standalone
Windows packages and the first-run interface below are still being built;
Linux releases are deferred. Tracking: `beads-xbox.1.187.6`, release
`beads-xbox.1.187`, console/storage provisioning `beads-xbox.1.22`.

## Choose what you want to run

| Experience | What you supply today | Intended release setup |
| --- | --- | --- |
| Blinx or JSRF, direct native launch | Matching extracted game files and a prepared title/console profile, or the reviewed owned kernel plus an EEPROM to prepare signing inputs; developer build inputs are also needed | Select your own supported game ISO; setup extracts/verifies it and creates a virtual console and writable storage. Original console files are an optional import. This ISO-only path is not implemented/qualified yet. |
| Original Microsoft Dashboard, native launch | Matching Dashboard 5960 executable **and its assets**, plus the same signing/profile preparation used by games | Import your own matching Dashboard files. Use the shared native console services; a full original firmware boot is optional. Reduced-input profile setup still needs qualification here too. |
| Original-software full-system/reference route | Compatible owned flash, decrypted 2BL, a readable raw FATX HDD image and an EEPROM; encrypted bootloader extraction additionally needs matching MCPX/key material | An explicit advanced option with its own compatible input set; it is separate from direct native launch. |
| Open-source default dashboard | No implementation has been selected for xboxllr | Optional redistributable shell/dashboard with its own qualified services and asset licenses; never a prerequisite for launching a game. |

**Native games already use replacements for kernel services.** They do not boot
the Microsoft kernel. Today's kernel-file requirement is an offline source of
signing material, rather than execution of a complete operating system. The
native Dashboard is another title using those services; its original artwork
and program are separate from boot firmware. See the
[kernel-service and profile design](KERNEL-SERVICES-SETUP.md).

## 1. Obtain your own content

Use game and console backups you are entitled to make and use. This project
does not supply Microsoft firmware, retail Dashboard assets, games or private
console keys. Owning hardware is not a blanket permission to redistribute its
files; backup and circumvention rules depend on where you live.

- **Games:** dump your own disc, or copy its files using your existing console
  backup setup. The Xbox game-data filesystem is important: a normal PC DVD
  copy is not necessarily a usable Xbox dump. See
  [xemu's disc-dumping and image guide](https://xemu.app/docs/disc-images/).
  Keep the original dump untouched. For the current developer previews, extract
  an Xbox XISO into a new directory using
  [XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso):

  ```powershell
  & 'G:\Tools\extract-xiso.exe' -x 'G:\XboxBackups\MyGame.iso' -d 'G:\XboxProfiles\game-content'
  Get-FileHash -LiteralPath 'G:\XboxProfiles\game-content\default.xbe' -Algorithm SHA256
  ```

  These are example locations; use your own tool and storage paths. Preserve
  the complete extracted tree and its relative names. Extraction is not the
  same as rewriting/patching an image. Do not patch an unsupported XBE to pass
  revision checks.
- **Original Dashboard:** using an already available backup/file-transfer
  method on your own console, copy the stock Dashboard executable and its
  complete related asset tree from the console's C: partition. Preserve
  names/layout, including revision-specific data directories. A homebrew
  dashboard or `xboxdash.xbe` alone is not the complete retail Dashboard.
  Only 5960 is currently qualified by this project. Copy files to a new PC
  directory; native Dashboard use does not require replacing anything on the
  physical Xbox.
- **Console identity:** preserve your EEPROM backup when you want an existing
  console identity. It is 256 bytes and contains private material. A newly
  generated EEPROM is suitable for a new virtual profile, but is not the
  identity of your physical Xbox and may not unlock its console-bound saves.
- **Firmware/kernel for the original route:** obtain backups from your own
  console through your existing backup tools. Flash/BIOS, a decompressed
  kernel, a loaded kernel memory capture, MCPX ROM and EEPROM are distinct
  files; renaming one does not produce another. For extraction from an owned
  BIOS, [XboxBiosTool](https://github.com/tommojphillips/XboxBiosTool) documents
  its extraction options and matching MCPX/key inputs. Extraction output can
  include private keys: keep the entire output private. No flashing, hardware
  write or boot modification is needed merely to use already made backups.

The current signing preparer accepts only the reviewed loaded-kernel SHA256
listed in [signing setup](NATIVE-SIGNING-SETUP.md). A kernel from another BIOS,
or a decompressed file instead of that loaded capture, is **not automatically
accepted**. General firmware/key import is a remaining setup task, not a reason
to search for someone else's matching dump or disable the identity check.

### Supported game identities

Check the extracted **XBE**, not the ISO's hash:

| Title/revision | `default.xbe` SHA256 |
| --- | --- |
| Blinx: The Time Sweeper, NTSC-U | `e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249` |
| Jet Set Radio Future, NTSC-U | `f4441696fdd007c31a712815cdde967836fbc3ab97fcb2f58e1db56974644219` |

These identities come from the game projects' revision metadata. Other regions,
releases or modified executables need their own admission; the game title on
the disc label alone is insufficient.

## 2. Set up or import a console profile

The intended first-run choice is **Use a virtual console** or **Import my Xbox
files**. Both choices must keep identity and persistent storage together. The
first-run UI is not available yet; the current developer steps follow below.

For a new virtual console, generate settings/identity and format writable
storage locally. The game ISO should be the only required user content for
the default supported game route. An original Dashboard import is optional.
Do not ask a player for a BIOS, boot ROM, existing HDD image or Dashboard simply
to start a game once this route is qualified.

Missing optional files warrant an informative, title-aware first-use notice,
not an error or a warning before every launch. Intended Blinx wording, **after
the fresh-profile path is qualified**:

> No original Xbox console files were provided. Blinx can use a virtual console
> for normal single-player play and new local saves. These files generally
> won't matter for that. Importing existing Xbox saves may require original
> console files. You can add them later in Settings.

Offer **OK, continue** and **Import Xbox files**. Remember acknowledgement for
that console profile and the notice version; keep the explanation accessible
in Settings. A single-player title should not receive irrelevant Xbox online
warnings. For a title with supported Xbox network features, explain only its
relevant missing capability. Local multiplayer is not automatically an Xbox
network feature.

If a future feature requires genuine console credentials, selecting that
feature must require and validate the relevant imports before connecting.
A notice acknowledgement cannot replace required credentials. Imported files
do not promise online support: current Dashboard Xbox Live behavior is offline,
and authentication/network qualification remains separate. Host-native netplay
need not inherit a firmware requirement merely because it uses the internet.

Adding imports later must not silently change the identity used by existing
saves. Select an existing compatible profile, or create a separate imported
profile while retaining the old one. Save conversion is separate work; setup
must not quietly re-sign, discard or replace native saves.

## 3. Current developer preparation

These commands prepare inputs, **not a complete end-user installation**. The
current build also requires the matching title metadata, CPU preparation,
renderer/audio dependencies and build tools in the linked developer guides.
The framework's [developer setup](DEVELOPMENT.md) covers a clean checkout,
toolchain/dependency preparation and native versus full-system builds.

Use Python 3 and run from a current xboxllr checkout. Example private locations
below are on G:; choose a writable data drive with sufficient space. Direct
temporary output away from the system drive before running preparation tools:

```powershell
$profileRoot = 'G:\XboxProfiles\MyConsole'
$env:TEMP = 'G:\Temp\xboxllr-setup'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force -Path $profileRoot,$env:TEMP | Out-Null
python tools/create_eeprom.py "$profileRoot\eeprom.bin" --revision r2
```

For an imported profile, use its existing EEPROM instead. Never regenerate it
over an existing identity. The generator validates an existing file and leaves
it untouched. Choose the revision compatible with the imported/reference
profile; `r2` is the currently tested default, not a universal hardware claim.

Once you have the admitted owned kernel and matching game/Dashboard XBE:

```powershell
python tools/native_save_keys.py --xbe 'G:\XboxProfiles\game-content\default.xbe' --kernel 'G:\XboxBackups\reviewed-loaded-kernel.bin' --eeprom "$profileRoot\eeprom.bin" --output "$profileRoot\game-save-keys.aot-private.bin"
python tools/native_console_settings.py --eeprom "$profileRoot\eeprom.bin" --output "$profileRoot\console-settings.aot-private.bin"
```

Signing files bind to an exact XBE and EEPROM. Prepare one per supported title;
an existing file/receipt is preserved, not overwritten. Reuse existing valid
settings output; these preparation commands are not a command to run before
every launch. For the native Dashboard, substitute its matching `xboxdash.xbe`.
Normal signing preparation uses standard-library HMAC and does not execute the
kernel or need Unicorn. Optional `--verify-original` is a developer reference
check, not player setup.

Configure the title's private paths to use this EEPROM, its extracted media and
its persistent state directory. Runtime overrides
`XLLE_PILOT_SAVE_KEYS_FILE` and `XLLE_PILOT_CONSOLE_SETTINGS_FILE` can select
these prepared files instead of compiled development paths. Keep the selected
state directory stable across launches and builds. Current state is backed by
real native host files; it is not yet the common FATX image described below.

Continue with the relevant guide for build and launcher commands:

- [Blinx developer setup](https://github.com/mstan/BlinxRecomp/blob/main/docs/DEVELOPMENT.md)
- [JSRF developer setup](https://github.com/mstan/JSRFRecomp/blob/main/docs/DEVELOPMENT.md)
- [Shared native title driver](NATIVE-TITLE-DRIVER.md) and
  [native pilot](../pilots/native-title/README.md)
- [Native Dashboard status and title work](NATIVE-DASHBOARD-BURNDOWN.md)

Do not advance a game's pinned framework merely because its setup guide links
to newer framework documentation. That requires its own compatible build check.

## 4. HDDs and the original-system route

A supplied physical HDD image is not conceptually necessary for a new console.
Setup should create a real partitioned, formatted FATX image and required empty
directories. No Microsoft files are needed to make empty writable storage.
Both native services and the LLE device path must use that same backing store,
identity and file/flush/error contracts. An unrelated empty image alongside
native folder saves does not establish interoperability.

Today, the native previews use persistent host directories, while the
full-system route attaches a raw HDD image. An external FATX-based installer
already exists for reference preparations (`tools/native_hdd_install.py`), but
is not a packaged first-run formatter or a shared native image backend. Shared
image provisioning, existing-save import and cross-route save checks remain
open under `beads-xbox.1.22`. Do not treat a zero-filled file as a formatted HDD.

For the original-system/reference route, follow the separate
[private input guide](INPUTS.md), [legacy setup](LEGACY-FULL-SYSTEM-SETUP.md)
and [HDD security guide](HDD-SECURITY.md). They specify compatible flash,
decrypted 2BL, raw FATX HDD and EEPROM inputs, including external BIOS
extraction. The current entry is a supplied 2BL handoff; it is not qualified
authentic MCPX cold boot. Keep source backups untouched and use working copies.
Test overlays discarded on exit are not persistent user storage.

[xemu's required-file guide](https://xemu.app/docs/required-files/) also explains
firmware versus HDD contents and offers a disk without the Microsoft Dashboard.
Its QCOW2 download is **not** directly accepted by xboxllr's raw-image path;
format conversion and compatibility need separate handling. A blank/generated
disk still needs your own Dashboard assets if you want the Microsoft Dashboard.

## 5. Preserve saves and resolve setup failures

Back up the entire console profile: persistent state/HDD, EEPROM, any imported
identity material and per-title signing inputs. Share one stable profile between
compatible recomps rather than inventing a new identity per game. Exclusive
access to a shared image is an acceptable initial policy; two uncoordinated
writers are not. Save states are disposable build-specific snapshots; ordinary
in-game saves are persistent progress and must survive upgrades.

| Setup message | What it means / next step |
| --- | --- |
| Unsupported game revision | Compare the extracted XBE hash with the title metadata; use the supported original revision. |
| Unreviewed original kernel | Today's importer only admits its recorded capture. Keep your backup; broader import/virtual-profile support is unfinished. |
| Missing private signing inputs | Today's profile has not completed signing preparation; this is not evidence that the game needs a running Microsoft kernel. |
| Signing input belongs to another XBE/EEPROM | Select the correct prepared title/profile. Do not change identity or erase saves to bypass it. |
| Dashboard files missing | Supply its complete matching asset tree, or launch a game directly. |
| HDD/profile busy | Close the other writer and retry; never clone a live image as an implicit second writable save store. This message is a required future shared-image behavior, not an implemented UI claim. |

Packaging must ship its own runtime dependencies and usable setup UI without
workstation paths, private files or a mandatory developer installation. A clean
first-run game/save/restart/load check is still required before calling these
packages ready. This document distinguishes that release contract from what
the current developer previews can do.
