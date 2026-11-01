# Native nonencrypted factory settings

`settings_factory_query(index,type,value,length,result)` handles only
`ExQueryNonVolatileSetting` (ordinal 24) indices `0x100..0x103`. It returns
one when handled, sets `g_eax` to NTSTATUS and leaves `g_esp` untouched;
the existing dispatcher retains RET20 argument cleanup. It returns zero for
every other index so the existing user-settings owner and unsupported guards
remain in control. There is no encrypted game-region/key service, aggregate
EEPROM query, setting write or persistence in this module.

The factory layout is independently present in nxdk's `XC_FACTORY_*` indices,
Cxbx's `XBOX_FACTORY_SETTINGS`/EEPROM field metadata, and the original kernel's
five-entry factory descriptor table at `800109c8`. Only four descriptors are
admitted:

| Index | Field | EEPROM offset | Bytes | Registry type |
| --- | --- | --- | --- | --- |
| 100 | Serial number | 34 | 12 | 3 (binary) |
| 101 | Ethernet address | 40 | 6 | 3 (binary) |
| 102 | Online key | 48 | 16 | 3 (binary) |
| 103 | AV region | 58 | 4 | 4 (DWORD) |

Offsets and indices above are hexadecimal. The nonencrypted section occupies
`30..5f`, including its DWORD checksum at `30` and reserved bytes. Little-endian
DWORD addition with end-around carry must equal `ffffffff`. This checksum
validates the factory section; it is not encrypted-section authentication.
No payload or key bytes are logged by the implementation or test fixture.

Factory and user queries share the existing `settings_cache[256]` first-read
snapshot and `settings_cache_valid`, under `eeprom_lock`. The input is the real
file-backed EEPROM already supplied to the runtime. An all-FF initial image
returns `c0000184`, retaining the ability to retry initialization. A populated
cache remains populated even when its factory checksum fails; later raw input
changes cannot silently replace the original snapshot. A user-created cache
is reused by factory reads, and a factory-created cache retains user bytes
unchanged. The existing user-query implementation is untouched.

Original checksum validation precedes the size check. A bad checksum returns
`c000009c`, even for a short buffer; a valid but short query returns
`c0000023`. Both leave type, result length and value entirely unchanged.
Success writes type, then optional result length, then zeroes the complete
supplied value buffer and copies exactly the field bytes. Overlapping outputs
retain this order. Required pointers and complete output ranges are checked
before any successful guest write; failure-only outputs are never accessed.
Payload is staged while holding the EEPROM owner lock and guest stores occur
after release.

## Actual caller and original evidence

The original dashboard caller at `6c56b` pushes result=NULL, length=4, a value
local, a type local and index `103`, then calls import thunk `7c010`/ordinal24.
On a negative status it returns zero; on success it returns byte1 of the value.
The following caller tests values3/4 before querying user VIDEO index8 and
optionally clearing `80000` through ordinal29. The captured original cache's
factory query returned byte1=1. The implementation returns actual input bytes;
it does not force that branch value or implement the optional setting write.
The native run subsequently reached this exact query after optical91/9,
confirming the scope, rather than relying on an API-name prediction alone.

`original-settings-factory.aot-private.json` under
`build/native-profile-checks-20261019/` records unchanged original ordinal24
execution on the qualified initialized RAM capture
`31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1`
and kernel `827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`.
It includes actual caller disassembly, hashes rather than identity/key payloads,
successful field/type/tail comparisons, actual null-result query, short-buffer
preservation, output alias order, and corrupt-checksum precedence. Explicitly
provisioned authored cache variants test the original query/cache-reader and
checksum code; they do not qualify SMBus EEPROM initialization. The all-FF
initialization failure is supported by the decoded original initialization
loop and authored checks, not a completed physical or SMBus initialization run.

`tests/test_native_settings_factory.c` uses authored EEPROM bytes only. Its
checks cover all four fields, full buffer zeroing and canaries, shared cache
epochs, raw mutation, type/result/value aliases, checksum and short precedence,
unused invalid failure pointers, success preflight, missing input, all-FF
retry and lock release. `native-settings-factory-contract.aot-private.json`
retains the isolated MSVC command/results and source identities. Native-title
integration and the next reached-service result are separate root checks.
