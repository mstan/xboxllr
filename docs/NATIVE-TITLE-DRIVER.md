# Native title driver receipts

`tools/native_title.py` accepts definitions and private path JSON outside the
framework checkout. Relative profile/seed paths resolve against the definition;
private paths resolve against their JSON file. The binding profile must select
the exact XBE SHA256. The dashboard definition selects owned retail 5960, not an
upstream dashboard substitute.

The optional private `console_settings` path selects an authenticated,
EEPROM-bound boot-settings artifact prepared by `native_console_settings.py`.
It contains no signing keys, does not change the existing signing-input format,
and is shared console input rather than title-specific behavior. A title that
never requests the encrypted game-region setting does not require this file.

`PILOT_NETWORK = "OFFLINE"` selects a shared native network-service owner through
an explicit `network` binding family. It requires native scheduling and IO/USB
OFF, exact original entry proofs and a complete guarded boundary around
unadmitted private-object consumers. `ORIGINAL` remains the default for existing
titles. Offline initialization and cleanup are separate from cable state,
signed-in users and online tasks; selecting this owner does not qualify live
services or admit raw disk/NIC accesses.

Content mounts and guest DOS drive links have separate owners. The content
device always resolves to the verified read-only image directory. With
`content.drive_owner = "HOST"` (the default), startup also creates the declared
`content.drive` link. `"TITLE"` leaves guest links for original title code to
create. The dashboard selects TITLE because it maps C: to writable partition 1
and Y: to its read-only content on partition 2; pre-seeding C: for content would
make its original C: creation collide. Older generated headers retain HOST.

Use `prepare`, `configure`, `build`, then `run`. Preparation records the selected
profile hash, selected binding families, canonical generation-proof fingerprint
and complete generated-file hashes. Selected families must match nonempty actual
admissions in the generation manifest. Configure refuses an older
preparation under a changed profile. Configuration and build receipts bind the
definition, profile, generation manifest, generated directory, and final binary.
A manually generated/built tree has no implicit receipt adoption: run it through
the driver workflow before using driver launch. These are local consistency
checks, not signed attestations or a security boundary against an actor editing
both artifacts and receipts.

`generation.unit_max_functions` and `unit_max_bytes` bound generated compilation
units without changing function bodies. `partition` applies these budgets to a
verified existing preparation and records the transform, retaining the previous
generation/preparation receipts. It does not retranslate code or change service
proofs. Configure and build again after changing the partition. Fresh `prepare`
uses the same policy automatically; oversized single functions remain intact.

`bind` adds or refreshes reviewed whole-service bindings in a verified existing
preparation. It reruns every previously admitted family against the original
XBE and selected profile, then refreshes the preparation receipt. Removing an
existing admission requires a fresh preparation. Previous generation/preparation
receipts are retained in `binding-history`. Configure/build follow the refresh;
CPU translation is reused. A failed refresh cannot be launched as a valid new
preparation. An incomplete-bind marker blocks configuration, build and launch
even if a failed binder changed no artifacts. A successful full bind clears the
marker; a fresh preparation clears it only after all selected admissions pass.

Each launch copies the verified executable and adjacent DLLs into a unique run
stage and verifies the staged executable hash before creating the process.
Driver mutations into a marked stage or its descendants are rejected. Future
ordinary builds therefore do not replace a running owner's executable.

`run --input-replay path.txt` copies a native controller replay into the same
immutable stage and records its SHA256 in the stage and launch/result receipts.
This selects the existing replay input backend for that run, without changing
the title definition or requiring a rebuild. Lines use milliseconds since first
poll and 36 hex digits of Xbox pad state, or `disconnected`; the backend retains
its existing parser and connection handling. Source-file edits cannot alter an
already staged replay. Other launches keep their selected normal input source.

`run --capture` uses the existing direct GPU present readback once per second,
saving images in the run's `captures` directory. Captures are diagnostic I/O and
must be disabled for frame-rate measurements. Both options configure only this
launch; the build and selected native contracts remain unchanged.

Owner launches are visible, have normal audio, and have no deadline. `--detach`
leaves the process open. Automated launches require `--automated --seconds N`
with a finite positive duration; they remain owned by the driver, hidden and
muted, and cannot detach. Only that created process is stopped at its deadline.
The authored tests mock process creation: they do not validate dashboard boot,
rendering, input, audio output, or native performance.

`--link-profile ITERATIVE` selects MSVC incremental linking for development.
Pass the same selection to configure, build and run; the build receipt binds
it so a different selection cannot silently launch an older artifact. It keeps
the selected CPU optimization (including `/O2`) and existing generated objects,
but disables linker removal/folding and can increase executable/disk size. The
first link creates the incremental database and is still a full link. `FINAL`
is the default and explicitly resets an existing iterative cache to full linking
when configured without the flag. Other supported toolchains keep FINAL.
Switching link profiles does not retranslate the guest CPU. Do final acceptance
and delivery with FINAL; this option does not establish runtime performance.

For the normal edit/build/run loop, configure once with the chosen link profile,
then repeat only `build` and `run` while editing shared runtime sources. Those
edits do not require prepare, bind, partition or a fresh build directory. The
build system recompiles affected objects and reuses the rest. Configure again
when changing the title definition, generated admissions, private build inputs
or link policy. CMake handles ordinary build-script dependency regeneration;
explicit configuration is useful after adding notice source files (below).

`build --with-checks` builds the shared `pilot_native_iteration` target and
writes the normal executable-bound receipt in one invocation. This target
includes the title, runtime contract fixture and, for XAudio2 builds, the
independent audio reference/DirectSound fixtures. It builds check executables;
run the selected CTests separately. A failed build does not refresh the launch
receipt. This keeps a single dependency traversal for compatible targets and
avoids a second receipt-only application build. Ordinary `build` continues to
build just the title. Use the same four-job and ITERATIVE policy throughout
the development loop; retain FINAL acceptance for delivery.

Notice trees remain part of each native build. Their individual output files
and source dependencies now drive copying, including builds with no relink.
Unchanged inputs need no copy command or content comparison. Changed files and
missing staged notices rebuild their affected group; content is copied
byte-for-byte. Configure again when introducing new nested notice files so
Visual Studio loads the refreshed dependency inventory before building.
Removed source notices may leave surplus old
notice files in an existing build, so release packaging should use a clean stage.

If the child exits before the deadline, the command propagates its exit status.
A planned deadline stop is recorded explicitly; it establishes only that the
bounded observation finished, not that the title passed a gameplay check.
