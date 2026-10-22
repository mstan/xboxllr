# Console launch and persistent storage

Owner clarification, 2026-10-08; provisioning work is tracked in
`beads-xbox.1.22`, with storage qualification in `.102`.

October 21 setup clarification: [Setup](SETUP.md) and
[kernel services/profile design](KERNEL-SERVICES-SETUP.md) distinguish current
inputs from the ISO-only target. Original Xbox identity imports remain optional
for the intended fresh local-play route. A title-aware first-use notice explains
only relevant limitations; required genuine credentials are checked when a
feature needs them. Existing native saves/identities must remain intact.
Native Dashboard launch is distinct from original-firmware boot.

**A supported real dashboard path is an architectural requirement. Supplying
dashboard files is optional for accelerated/direct-title use. Real persistent
console storage is required in both cases.** Dashboard performance and polish
remain continuing work; they do not block the current Blinx bring-up. Removing
the old whole-dashboard epic dependency does not waive dashboard support.

## User choice and the shared console contract

Users must be able to select an original dashboard route when they provide its
software, or use the accelerated/native launch route without those assets.
The intended launch choices are:

- Direct game launch, with no dashboard interaction required.
- An optional default open-source dashboard/native shell, if a suitable
  implementation is available. Prefer MIT or another compatible permissive
  license, retain attribution, and qualify the supported console-service
  contracts before adoption. No donor has been selected or qualified yet.
- The original retail boot experience with user-supplied compatible boot
  software and dashboard, including the original Xbox logo/boot sequence before
  dashboard or title entry. Preserve that original execution route; a dashboard
  file alone is not a substitute for the boot software this route requires.

An open-source default is a reduced-input setup option, not a mandatory launcher
step or a replacement for the supported retail route. It need not reproduce
retail branding or proprietary assets. Its dashboard scope must not be mistaken
for a firmware/kernel replacement.

A native shell may replace dashboard implementation details while providing its
supported functions through actual console services. Direct title launch must
establish the supported launch environment; it must not pretend absent dashboard
code ran or substitute successful boot/service results for required effects.

The implementation choice is fixed before guest execution, consistent with
build-selected service backends. Normal dashboard-to-game title transitions are
not live switching between LLE and HLE implementations. Both routes retain the
implemented LLE floor and observable I/O contracts; native implementations need
not repeat internal LLE instructions or hardware operations.

The default accelerated setup should require as little user-supplied material
as possible: owned title data, plus original software only when the selected
reference/boot route needs it. Do not bundle or invent proprietary dashboard,
kernel, firmware or game bytes to reduce setup requirements.

## Storage exists independently of a dashboard

When a user supplies no disk, provision a real persistent virtual HDD with valid
Xbox partition/FATX structures and required empty storage directories. A sparse
raw image is acceptable. Importing an existing compatible image/profile remains
supported; provisioning must never overwrite an existing original or silently
rotate its console identity. Reuse the existing persistent EEPROM generator
rather than requiring a physical-console EEPROM dump for every native setup.

The console profile owns the HDD and identity. Game repositories bind their
titles to that profile rather than each inventing an incompatible save store.
The console profile location and save-drive/image target must be configurable
before launch, rather than fixed to a game repository's local directory.
Provide a simple provisioned default and allow an existing compatible profile
to be selected. Never silently move, overwrite or recreate existing saves.

The longer-term ecosystem acceptance case is multiple standalone Xbox recomps
selecting one shared save drive/profile, with normal title-ID/directory isolation
and the same stable console identity where required by the save format. The
choice must refer to the actual backing store, not independent per-game copies.
Do not assume simultaneous writers to a raw image are safe: exclusive ownership
between processes is an acceptable first implementation, with a clear busy
result. Concurrent shared access would require separately qualified coordination.

Real file data, title IDs, directory ownership, capacity/errors, reopen behavior,
flush semantics and persistence across restart belong to the shared contract.
The original dashboard must be able to observe the resulting supported saves.
Image changes must survive orderly closure; failure paths must preserve the
documented integrity guarantees and expose actual errors.

HLE can batch or accelerate filesystem operations. Save operations must still
read/write actual storage and honor their completion/flush contract. A separate
empty HDD alongside unrelated host-folder saves does not meet the requirement.
The LLE and accelerated routes need coherent persistent state usable across
separate launches. Neither duplicate production execution nor live state
migration is required.

Tests that use copy-on-write images must distinguish discarded test overlays
from durable user storage. The documented explicit unlocked-HDD option remains
allowed; the real locked-drive path must also remain available.

## Current implementation and acceptance work

| Part | Existing basis | Remaining work |
| --- | --- | --- |
| Original dashboard route | Full-system LLE boots the owned dashboard | Maintain this route and expose an explicit supported launch choice alongside native title launch |
| Retail boot experience | Existing original-software full-system boot route | Qualify the requested logo/boot sequence with the selected compatible user-supplied software, followed by dashboard/title entry |
| Optional default dashboard | Architectural option only; no selected donor | Assess compatible permissive implementations when needed, preserve attribution and real service effects; direct game launch stays available |
| Reduced-input identity | `tools/create_eeprom.py`; qualified generated-profile boot and retained LGPL attribution | Integrate provisioning/import into console setup, preserve identity and persist relevant changes |
| Real HDD | Full-system IDE/image path with locked/unlocked policy and copy-on-write testing | Empty-image FATX formatter/importer, durable user mode and guided setup without dashboard assets |
| Native save operations | Native pilot performs real persistent host-directory file operations | Connect accelerated filesystem services to the shared virtual-HDD contract; host folders alone do not close this requirement |
| Configurable/shared save target | Private game launcher has local state configuration; shared console-profile contract is not implemented | Select/provision/import the actual profile and save drive; later qualify two recomp projects using one store, title isolation, identity continuity and exclusive image ownership |
| Cross-route compatibility | Existing kernel/filesystem fixtures and original dashboard | Write a save, close/restart, read through the LLE route/dashboard, then reverse the direction; qualify shared identity, errors and flushing |

Bundle formatter/layout, file lifetime and cross-route persistence checks using
one owned test console profile where possible. Reuse original kernel, existing
LLE devices and behavioral references. Dashboard access and image-backed native
storage remain explicit acceptance gates until exercised; neither the working
attract milestone nor a repository split completes them.
The optional default dashboard and cross-title shared-drive rollout are future
work. Preserve their interfaces now without starting another dashboard project
or interrupting the serial Blinx LLE audit. This document records requirements,
not completion of a launch selector, shared profile, formatter or retail-logo
qualification.
