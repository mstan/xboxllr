# Native title binding profiles

`native-title-profile.v1` selects reviewed native service adapters by the exact
original XBE SHA256. Each profile has `schema`, `id`, `xbe_sha256` and a
`bindings` object. A family contains an explicit `adapter` and its original
body/ABI/layout proofs, or a data-only `include` relative to the profile.
Includes must resolve within the framework checkout or the selected profile's
own directory, allowing private title repositories to own their fragments;
they cannot import code.
The binder chooses its supported adapter IDs and verifies original bytes,
generated source identity and identified ABI before emitting wrappers.

All service binders accept `--profile PATH` and
`bind(xbe, output, symbols, profile=None)`. Without a selection, only the bundled
Blinx profile is considered, still matched by exact image hash. An unknown
image, missing requested family, unsupported adapter or mismatched proof fails
closed. `has_profile_binding` is for an elective generation step such as the
original display bridge; it never qualifies an absent required service.

Profiles are reviewed metadata, not scanner-name allowlists. A scanner label
with the wrong ABI cannot authorize a wrapper: the input adapter records an
explicit `symbol_stack_bytes` separately when it differs from the checked
original RET cleanup. Actual profile admission still requires exact body bytes.
Profile files contain no original code or generated title bodies.

## Current adapters

| Family | Adapter | Boundary |
| --- | --- | --- |
| CPU | `x86-mmx-cdecl-v1` | Whole cdecl MMX capability query |
| Cache | `xapi-utility-cache-v1` | XMountUtilityDrive and its original NT error helper |
| Media | `xapi-installed-media-v1` | Reviewed no-argument optical verifier called by XapiInitProcess |
| Input | `xapi-gamepad-v1` | Reviewed API subset, state 22; optional caps 25 and feedback 70/motors 66 |
| Audio | `dsound-objects-v1` | Reviewed public API subset and whole AddRef; device 8/buffer 28 interfaces, descriptor 24 |
| CRT | `crt-native-transfer-v1` | Reviewed original setjmp/longjmp plus exact generated lowering counts |

The input and audio adapters emit only the title's reviewed API table rows.
Missing shared-runtime semantic names receive distinct enum values beyond
`PILOT_*_COUNT`, so the runtime's range check rejects them. Input emits feature
macros and omits capabilities/feedback layout constants when those APIs are
absent. Audio emits `PILOT_AUDIO_HAS_DSP`; no seed is invented when the complete
DSP helper family is absent. Different object or packed structure layouts need
a separately qualified adapter and runtime allocation contract.

The Blinx profile preserves existing body selections and binding records.
Rebinding an already bound private tree can upgrade only its input/audio
headers and recorded file hashes, then is idempotent. Owner build products are
not upgraded implicitly.

## Dashboard 5960 evidence and limits

`dashboard-5960.json` matches original image
`832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d`.
Its SDK libraries are revision 5849 despite the dashboard's 5960 title revision.
The MMX query at 0x000e5adf is byte-identical to the previously reviewed cdecl
query. Cache and input proofs were recovered from original identified bodies.
The gamepad device-info table points to an 18-byte report and polling
`[1,8,0,0]`; the state includes a 4-byte packet counter. Capabilities and feedback
APIs were not identified, so their bindings and layout fields are absent.

`XGetDeviceEnumerationStatus` at 0x0015ab78 returns 0 for idle and 1 for pending,
RET 0. `XInputGetDeviceDescription` at 0x00161aad uses a single handle argument
and RET 4, unlike the scanner's two-argument prototype. It returns a byte from a
real USB class-control response or 0 after setting LastError. Its actual caller
opens the IR device type and configures a remote. The native host has no IR
device; a bound Description call must remain an explicit unsupported operation
until that separate hardware-response contract exists. It must not synthesize
a description from an SDL controller subtype.

Audio interface offsets are proved by the public create routines. Constructors
at 0x000f50f0 and0x000f604f select the original device/buffer vtables; both whole
AddRef slots point to0x000f53f2..0x000f5439 (71 bytes, RET 4). This entry must be
included in generated CPU discovery before binding. No DownloadEffectsImage
API or previously reviewed DSP helper family was found; DSP metadata is empty.
Optical media, CRT transfer and original-display families are absent until
qualified. Profile admission and generated wrappers do not establish runtime
boot, rendering, sound or input qualification.

Run the authored, binary-free checks with
`py -3 tests/test_native_binding_profiles.py`.
