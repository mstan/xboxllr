# Kernel services and reduced-input console setup

Owner direction: October 21, 2026. Tracking: `beads-xbox.1.187.6`,
`beads-xbox.1.22` and release `beads-xbox.1.187`.
[Setup instructions and input matrix](SETUP.md).

## Decision

Keep the native kernel-service layer shared between games and Dashboard.
The target for a fresh supported game installation is the owner's game ISO,
with a locally created virtual console, settings and lightweight persistent save folders. Original
Xbox files remain an optional profile import. Features that actually require
genuine credentials must validate those inputs when selected. This specifies
the intended setup, not a claim that ISO-only launch already works.

The Microsoft Dashboard program and its assets must be supplied to run that
original UI. They do not inherently require booting Microsoft's firmware when
launched as a native title. Full original-software boot/reference execution is
a separate supported direction with its own additional inputs.

## What the current kernel dependency really is

| Boundary | Current implementation | Remaining setup work |
| --- | --- | --- |
| Game/Dashboard CPU | Ahead-of-time native title code; unresolved guest targets stop | Package the admitted revision and verify user content; no guest interpreter or BIOS boot is needed for this selection |
| Kernel services | Shared native exports, scheduling, memory, file, settings, graphics/audio/input services with retained reference contracts | Complete distribution review and package the common services; do not start a second kernel implementation per title |
| Save key data exports | `save_crypto.inc` loads a 360-byte `XLLEKEY1` artifact bound to XBE/EEPROM; exports HDD/title/alternate keys | Separate profile provisioning from the source of original signing material |
| Signing preparation | `native_save_keys.py` reads private certificate material from one admitted owned kernel; derives 17 title keys with standard-library HMAC | A fresh virtual signing profile and a general reviewed original-material import path, without bundling a Microsoft root or changing existing native saves |
| Console settings | Generated/imported EEPROM and authenticated `XLLESET1` preparation | Shared persistent profile selection and settings lifecycle |
| Storage | Persistent native host files; separate LLE raw-image path | One formatted FATX image used through both backends; do not substitute unrelated saves plus a dummy image |

The kernel is not a normal native runtime input. It is currently needed once
to prepare **original-compatible** signing data, or not needed again when a
matching prepared profile exists. New local virtual profiles now have a
separate generated identity and need no kernel. They do not claim retail-key
or original LLE-kernel compatibility. See [preview setup](NATIVE-FIRST-RUN-PREVIEW.md).

## Save identities are the important qualification boundary

An ISO's title certificate contains title-specific material. Today's HMAC
producer also needs the separate original certificate root; implementing HMAC
in native code does not manufacture that root. Original HDD-bound saves can
additionally depend on the console's EEPROM/HDD identity. An EEPROM alone does
not replace every original signing input.

There are two explicit profile cases:

- **Imported original profile:** retain the existing EEPROM, original-derived
  signing material and real saves. Preserve the currently qualified signing
  outputs. Missing inputs are actionable setup failures, not grounds for
  silently generating a different identity.
- **Fresh virtual profile:** generated, persistent signing/console material is
  a candidate way to remove the kernel dump requirement for new local play.
  It must be identified as virtual and qualified using actual new save,
  restart and load. It cannot be called stock-key-equivalent, and stock-save
  import/System Link compatibility must not be promised. The private preview
  now implements this profile and the once-only optional setup UI; component
  persistence and shared-folder identity checks pass. Actual new-game save,
  restart and load remains title qualification before release.

Do not weaken verification, use zero exports or accept every signature to
simulate compatibility. Both profile types must perform real cryptography
under their declared identity. Preserve imported profiles and existing native
saves; an import later selects a compatible profile or creates a separate one.
Do not silently rotate identity, re-sign old saves or introduce a conversion
project as a prerequisite for these setup changes.

An unmodified retail LLE kernel has its original signing identity. Merely using
the same C interface or file format does not make it honor a generated virtual
root. Cross-route compatibility claims require matching actual key material
and storage. A future open-kernel backend may implement the virtual profile
contract, but is not presently qualified. Keep original-profile equivalence
and synthetic-profile self-consistency separate in evidence and UI.

## HLE/LLE interface and persistence requirements

Use a shared console-profile/storage owner. Title metadata supplies title ID,
revision and admitted capabilities; games must not define separate fake
kernels or save formats. Profile identity and backend selection are settled
before guest execution, with no dual production execution or live HLE/LLE
switching requirement.

Both implementations must retain the caller-visible ABI and observable
results: file bytes, directory/title ownership, capacity and failure statuses,
completion ordering, flush/reopen behavior, settings and signing results for
the selected identity. HLE may batch operations without repeating device-level
steps. LLE accesses the actual FATX bytes through the existing disk device;
HLE accesses those same on-disk structures through shared image services.

Owner revision: never create a per-game HDD image automatically. An existing
raw/QCOW2 image is the optional first-priority storage choice; without one,
use the selected save folder or automatic private per-game folders. Validate
an imported disk and its selected original signing/EEPROM profile before use.
Image admission failures must not silently fall back to folder saves.
Import/select an existing disk without overwrite. Protect a shared image with
exclusive writer ownership initially. Close/flush/reopen and two-direction
HLE/LLE readback are the acceptance checks, using matching profile identities.
Do not require a Dashboard to provide games with persistent storage.

## First-use notice and feature checks

The setup choice is **Use a virtual console** / **Import my Xbox files**.
Missing optional inputs are informational. The explanation must use the title's
admitted capabilities, not a global warning about everything Xbox could do.
For Blinx, say the files generally do not matter for normal single-player play
with new local saves; mention existing Xbox save imports as the relevant
limitation. Do not raise irrelevant online errors for a single-player title.

Offer **OK, continue** and an import action; remember acknowledgement by
profile/notice version and expose it again in Settings. Only offer Continue
for a qualified virtual-profile path. The current required-signing-input error
must not be hidden behind an informational dialog before that path exists.

If an enabled Xbox networking feature needs authentic credentials, require
and validate them at that feature boundary. Acknowledging a dialog does not
authorize fake sign-in or completion. Which files are required is
feature-specific; kernel/EEPROM imports alone do not establish an online
implementation. Local multiplayer and a future host-native netplay transport
must not automatically acquire a BIOS requirement. Current Dashboard network
behavior remains offline.

## Existing open-source alternatives

Primary project sources checked October 21, 2026. These are references/candidates,
not new dependencies or proof that our three titles work under them.

| Project | What it offers | Relevance and current limit |
| --- | --- | --- |
| [nxdk](https://github.com/XboxDev/nxdk) | Open SDK, hardware libraries and kernel headers/import library | Useful interfaces/homebrew foundations; its `lib/xboxkrnl` is not a replacement runtime kernel |
| [nboxkrnl](https://github.com/ergo720/nboxkrnl) | Work-in-progress kernel reimplementation for the nxbx Windows launcher | GPL-2.0; its documented target is not real hardware or xemu. Not a qualified drop-in for our native service layer or retained LLE route |
| [OXOS](https://github.com/kooscode/oxos) | GPL-2.0 open kernel/BIOS with reported xemu title support | Worth an independent reference assessment later. Its own documentation explicitly says the missing stock certificate key changes save signatures/System Link compatibility; it does not solve original identity simply by being open source |
| [xemu-dashboard](https://github.com/xemu-project/xemu-dashboard) | Small MIT-licensed homebrew dashboard | Candidate optional default after dependency/asset review and title-service qualification. It supplies neither the Microsoft Dashboard nor a retail kernel |
| [Theseus](https://github.com/MrMilenko/Theseus) | Dashboard/UI project, GPL-3.0-or-later per its current README | Potential UI/reference interest, with separate code/asset terms. Not a kernel or permission to bundle original Microsoft software |

Our existing non-GPL shipping policy means GPL kernel code is not an automatic
implementation donor. Keep attribution, actual dependency scope and independent
design evidence explicit. The framework's owner-original PolyForm license is
not OSI open source; a permissively licensed shared service component would
need an explicit compatible licensing decision, not an accidental relicensing
through documentation.

## Bounded implementation sequence

1. Define profile identity/import metadata and title-aware capabilities in the
   shared first-run tooling. Keep existing imported/prepared profiles working.
2. Implement fresh virtual signing and first-use/import choices. Batch a new
   save/restart/load smoke for Blinx and JSRF; reject wrong-identity saves and
   retain the accepted original-profile checks. Qualify native Dashboard use
   separately rather than assuming it follows from the games.
3. Use the common image-backed storage owner for an existing imported disk;
   do not generate or format a per-game image. Prove durable bidirectional HLE/LLE access with
   the same original-compatible profile, including writer exclusion and errors.
4. Connect ISO extraction/revision checks, runtime files, profile setup and the
   launcher into one shared package flow. Preserve optional original Dashboard
   and full-system input choices. Verify the final archive contains no owned
   media, firmware, keys, profiles or saves.

No new bootable OS or open-dashboard port is required to complete the native
game setup path. Broader kernel/online/title compatibility and original cold
boot remain separately scoped work. This design does not mark their evidence
complete or restart the deferred exhaustive gameplay audit.
