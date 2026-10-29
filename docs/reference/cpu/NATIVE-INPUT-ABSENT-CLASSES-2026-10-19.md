# Proved absent native input classes

The native host exposes gamepads through its existing transport. Additional
original USB device classes require explicit profile admission; it does not
turn a host keyboard into an original USB keyboard or invent keyboard opens,
reports or input handles.

An optional input-profile `known_absent_classes` list records each exact XAPI
symbol, original address, 12 stored bytes, SHA256 and the policy
`native-host-absent-v1`. The binder verifies unique SDK variable identity,
alignment, complete stored extent, original zero state, exact digest and
nonoverlap with core or other admitted objects. The existing complete body,
RET and layout checks for the input family remain mandatory. Unknown policy,
changed object/body or inconsistent prior binding fail before generated edits.

The generated header declares `PILOT_INPUT_ABSENT_CLASS_ABI_VERSION=1`, a count
and `pilot_input_absent_classes[]` of address/name records. Legacy headers
default to count zero. The runtime contains no title address or title-name
branch. An explicit bind can upgrade an otherwise identical prior input
admission to the newly proved table, without renaming/retranslating CPU bodies
or changing the wrapper; repeated binding is idempotent. Removing or changing
previously recorded proof is not silently accepted by that upgrade.
The incremental upgrade currently starts from an admission without an absent
table; expanding an already recorded table rejects conservatively and requires
a later reviewed migration or a fresh preparation.

Only `XGetDevices` and `XGetDeviceChanges` admit these additional objects.
Initialization verifies their original zero masks before publishing native
input state. Unexpected current, changed or previous bits fault before output
writes. `XGetDevices` returns current zero and performs the original changed
clear/previous acknowledgement. `XGetDeviceChanges` takes the original
changed-zero fast path: two zero output stores, FALSE and no class-object
writes. Both preserve original stack cleanup and output alias order. These
absent queries do not poll/acknowledge gamepads. Opens and preallocation for an
additional absent class remain guarded; existing gamepad/MU/IR paths retain
their existing behavior. No new class operations or transport are implemented.

## Reached original keyboard caller

The optimized native run `20261019-112619-ylzmvq7u`, executable
`7a3a0545a1ed535db22ef931ea760f9d18a16dac88d51b534638225ff762b71c`,
reached `XGetDevices` with `1592a8` at 1590 entries. The original XDK5849
variable inventory identifies this as `g_DeviceType_Keyboard`, independently
of a generic zero-data scan. Its 12 original bytes are zero, digest
`15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b`.
The dashboard profile admits exactly this additional class.

Original caller `40380` pushes that object to `15a83b` (`XGetDevices`), stores
the mask and skips its eight-slot keyboard-open loop when zero. Immediate
polling caller `403d0` passes the same object plus two local DWORD outputs to
`15a85d` (`XGetDeviceChanges`); FALSE skips keyboard close/open loops. Its
following `40520` helper only reads existing keyboard handles; the empty
initial masks never created one. No other unhandled class occurs in these
immediate callers. MU and IR are already owned as absent in the existing host
profile; other uncalled classes are not inferred.

The device-type pointer is a three-DWORD object, not a class-number index:
current mask at0, changed at4, previous at8. Original `XGetDevices` reads
current, clears changed and copies current to previous under original IRQL
helpers. Original `XGetDeviceChanges` returns two zero outputs immediately
when changed is zero; otherwise it derives insertions/removals from current
and previous, includes remove/reinsert bits from their intersection with
changed, acknowledges the masks and returns a Boolean. These are the same
already qualified gamepad change semantics, not a new keyboard descriptor ABI.

## Bounded validation

`build/native-profile-checks-20261019/original-input-absent-classes.aot-private.json`
records unchanged original XAPI query bodies, linked to the real original
kernel import implementations in the qualified snapshot. Three repeated
zero-keyboard queries and aliased change outputs returned zero, retained zero
class state and preserved output canaries/RET cleanup. This is a software
query oracle on the original static object; it does not qualify original USB
initialization, a physical keyboard or keyboard open/read transport.

The focused profile tests cover exact object emission, drift/overlap/policy
rejection before mutation and explicit binding upgrade/idempotence. The
standalone authored runtime fixture includes production `input.inc` with both
the new subset header and a pre-profile legacy header. It checks empty query
ABI/mutations/canaries, no host poll side effect, unexpected nonzero state,
unknown class, range, open, preallocation and initialization guards, plus
existing gamepad disconnect/reconnect events. The prior dashboard binding/header
upgrade was first checked in a partial scratch copy, followed by complete
selected-family rebinding and the actual optimized native build. Both current
and legacy-header integrated CTest cases pass. The retained actual Blinx
admission also passes a header-only, record-preserving, idempotent compatibility
check in separate scratch; that check does not establish Blinx gameplay under
the new framework revision.

Private receipts `native-input-absent-contract.aot-private.json` and
`native-input-binding-migration.aot-private.json` retain commands, hashes and
results. The actual native run `20261019-114209-s4zk7ror`, executable
`8f1c622f030200377206a4ba69fb74989b6daa42f9c442dd855b996de241f5b0`,
passes the keyboard query and reaches 1,604 entries before the next guard at
EEPROM setting `104`. It uses a frozen neutral controller replay. No menu frame
was captured; passing enumeration does not establish menu completion.
