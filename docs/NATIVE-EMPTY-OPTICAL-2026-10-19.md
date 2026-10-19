# Native immutable empty optical owner

`PILOT_OPTICAL=EMPTY` selects a virtual board with no optical device object,
no mounted content and no tray transitions. The default `ORIGINAL` route is
unchanged. This is an explicit native policy, not a physical-device probe or
a successful dismount stub. Original device/scheduler/USB owners cannot be
combined with this policy.

`optical_empty.inc` is initialized once before guest entry and implements
only ordinal 9, `HalReadSMCTrayState`: NTSTATUS success, RET8, required DWORD
state output `0x10`, optional DWORD change-count output zero. The stores retain
original order, so aliased state/count pointers finish with zero. The fixed
count belongs to this immutable owner; no guest transitions are simulated.
Invalid ranges and use before initialization fault before guest mutation.

With HOST content on a non-optical device, ordinal 91,
`IoDismountVolumeByName` (RET4), resolves the existing counted-ANSI namespace
under `media_lock`. Only the exact resolved `\Device\CdRom0` or unresolved
`\??\CdRom0:` absence is admitted, returning
`STATUS_OBJECT_NAME_NOT_FOUND` (`0xc0000034`). It never
closes handles or dismounts HDD/title content. Optical title ownership,
remapping to another volume and unknown names retain explicit guards.
Symbolic links and case folding use the shared namespace resolver. EMPTY
also guards optical file paths before the legacy CdRom0-to-content fallback,
so installed HDD content cannot appear through an absent optical device.
The generic `namespace_resolve_checked` inspector checks the initial name
and every replacement, before a remap can erase an optical ancestor. The
ordinary resolver supplies no inspector and preserves existing behavior.
`media_path.inc` contains the production path function shared by media and
the focused fixture; no duplicated path implementation is used for tests.
Ordinary optical file probes remain unqualified rather than receiving an
invented error status. Mounted media, motor control, dynamic changes,
DVD/authentication and actual dismount behavior are outside this contract.

## Original execution evidence

The private original-kernel oracle used initialized capture SHA256
`31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`
and flat kernel SHA256
`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`.
The HDD backing was read-only with a volatile COW overlay, never physical
hardware. Ordinal 91 returned `0xc0000034` for `\Device\CdRom0`, its lower-case
variant and two absent names. The latter observations do not qualify arbitrary
native namespace absence: unknown native volumes still fault.

The subsequent native run reached ordinal 91 with the actual counted name
`\??\CdRom0:`. Additional original offline calls returned `0xc0000034` for
that name and lower/upper-case variants. Original
`NtOpenSymbolicLinkObject` (203) also returned `0xc0000034` for all three,
establishing that this DOS name genuinely has no link in the captured
namespace. EMPTY admits that exact missing name without registering an alias.
Existing links resolve first: a CdRom0 DOS name remapped to HDD or another
unknown target still faults. A logical alias into the proven absent name
retains the actual absence result. This is not a successful dismount.

The storage-only reference initially stopped ordinal 9 at unsupported SMBus
port C004. A subsequent repeated call on that failed instance is invalid
evidence and is not used. A fresh reference then reused the existing
`Amd756Smbus`, `XboxSmc` and `DualPic` models, with IDE tray telemetry from
the existing owner: raw zero, no mounted optical content. The original
ordinal 9 body at `80017e5e`, SMBus service and captured IRQ handler all ran;
one completion interrupt delivered success, state `0x10`, count zero and
cached state `0x10`. Repeated cached calls confirmed the optional null count
and aliased-pointer behavior. This qualifies the selected model's empty
board path, not silicon, eject transitions or other SMC services. The original
code masks raw state with `0x70`, retains `0x40`/`0x60`, and otherwise emits
`0x10`; the selected policy uses only the observed raw-zero case.

Private records under `build/native-profile-checks-20261019/`:

- `original-optical-empty.aot-private.json`: original 91 cases and the
  storage-only SMBus boundary.
- `original-optical-board.aot-private.json`: completed original 9 outputs,
  ports, interrupt count and board-source hashes.
- `original-optical-caller-name.aot-private.json`: exact native caller's DOS
  name/case and original symbolic-link absence checks.
- `native-optical-empty-contract.aot-private.json`: authored checks/build.

`tests/test_native_optical_empty.c` exercises the actual shared namespace and
empty-owner implementation: repeated/optional/aliased outputs and RET8,
initialization and pointer guards, exact/case/linked absence, cycles,
remapped/HDD/optical-owner guards, and optical file-path confinement.
Production `media_path` rejects direct CdRom0-to-HDD remaps and one/two-hop
logical aliases through CdRom0, while a logical alias directly to HDD remains
accepted. This closes the initially reviewed post-resolution-only guard gap.
These authored checks are separate from the original execution oracle.
