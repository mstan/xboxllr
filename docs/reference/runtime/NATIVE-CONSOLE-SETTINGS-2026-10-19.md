# Boot-derived console region

Ordinal24/index`104` returns the original boot-derived game-region DWORD.
It is separate from the checksummed nonencrypted factory fields100..103 and
does not query or decrypt mutable EEPROM bytes on each call.

`tools/native_console_settings.py --eeprom <input> --output <new-file>` reuses
the existing EEPROM validator/decryptor. Authentication and factory/user
checksums must pass before any artifact is created. It reads only the region
from the decrypted security section (offset24 within the28-byte plaintext),
normalizes raw zero to `80000000`, and rejects multiple bits in the low31 bits.
The original producer permits a single low bit with the optional manufacturing
high bit; the preparer does not silently clamp it to a preferred territory.
The original unauthenticated/unencrypted manufacturing bypass is not admitted.
Input bytes and the original EEPROM file remain unchanged. Existing output
files are never replaced. Logs/receipts contain metadata and hashes, not
decrypted keys, identity fields or region payload values.

The separate private44-byte artifact is:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | ASCII `XLLESET1` |
| 8 | 32 | SHA256 of the complete startup EEPROM input |
| 40 | 4 | Normalized region, little-endian DWORD |

The associated `.bin.json` receipt records source identity and successful
authentication. This is a derived local input with the same integrity model
as existing private preparation artifacts, not a signature authenticating
arbitrary user modifications to the derived file. Runtime validates version,
exact length, startup EEPROM identity and the normalized region domain.
`XLLEKEY1` signing inputs and their360-byte ABI remain unchanged. The console
artifact is title independent; there is no dashboard or Blinx branch.

## Native owner and query contract

Root calls `settings_region_bind_boot()` once immediately after loading the
EEPROM, before any raw EEPROM owner can mutate it. It captures the immutable
startup SHA under the existing EEPROM lock and opens no artifact. Missing
EEPROM is recorded as unavailable rather than forcing a file requirement for
titles that never use this service. The identity phase is published atomically
and cannot be rebound.

`settings_region_query(index,type,value,length,result)` returns zero for all
indices except104. For104 it initializes its immutable artifact owner once
through `InitOnceExecuteOnce`, using `XLLE_PILOT_CONSOLE_SETTINGS_FILE` or
`PILOT_CONSOLE_SETTINGS_FILE`. It sets `g_eax` and leaves `g_esp` untouched;
the existing ordinal24 dispatcher retains RET20 cleanup. Missing input,
unbound identity, malformed artifact and invalid output ranges fault before
guest mutation. Later raw EEPROM/cache changes, including changes before
the first104 call, cannot change or invalidate the captured startup identity.
Once loaded, artifact deletion/replacement cannot alter the cached region.

Original output order is optional ResultLength=4, unsigned length check,
Type=4 and one valueDWORD. Short buffers return `c0000023` after the result
length store and leave unused type/value outputs untouched except where they
alias that result store. Success writes only four value bytes; it does not
zero a longer supplied buffer, even when length is`ffffffff`. Only four value
bytes require a writable range. Output aliases retain the original store
order. Success ranges are preflighted together before any store; a short
query checks only its used optional result output. Guest stores occur outside
the EEPROM lock. There are no writes to EEPROM, storage or signing inputs.

## Original and authored evidence

Actual native run `20261019-114209-s4zk7ror` reached104 after keyboard
enumeration at1604entries. Original caller`6cde8` pushes a null result pointer,
length4, value/type locals and104, calls`7c010`, and returns the value on success
or zero on negative status. This is the reached call, not an inferred API need.

`original-settings-region.aot-private.json` in
`build/native-profile-checks-20261019/` records the exact caller and unchanged
original query/boot-producer execution. It uses the hash-bound initialized
capture and qualified flat kernel from prior native-service proofs. The INIT
code page containing`80069adf` was restored from that exact flat source into
the post-initialization software reference; original crypto, query and locking
callees were not replaced. EEPROM cache inputs were explicitly authored,
authenticated software-test images. This does not qualify physical EEPROM
transport, a full original boot or the manufacturing bypass.

The original producer returns for zero, single and manufacturing-plus-single
authored regions with the expected normalization. Bad authentication and
multiple low bits reach its fatal path and do not return; the bounded oracle
stops at unsupported SMBus portC004 rather than executing physical effects.
These are boot failures, not invented query-time NTSTATUS values. The original
query matrix confirms short/result ordering, aliases, null result, no tail
zeroing and independence from later EEPROM cache corruption. No private
payload/key values are printed in the proof.

`test_native_console_settings.py` checks the existing supported auth revisions,
normalization/domain, exact source identity, rejection before artifact output
and refusal to replace an output. `test_native_settings_region.c` uses authored
EEPROM/artifacts with production owner code: startup binding, lazy admission,
raw mutation before/after the first query, deleted artifact after admission,
short/alias/no-tail behavior, lock-free guest stores and15 guarded child cases
for unavailable, malformed and invalid ownership/output states. The isolated
MSVC`/O2 /W4 /WX` and Python results/source hashes are retained privately in
`native-settings-region-contract.aot-private.json`. Root separately owns
runtime/CMake/driver wiring and artifact preparation. The integrated optimized
fixture passes in 0.88 seconds. Actual run `20261019-120231-_8rw7q61`, executable
`9c7d981d8fde952e7b1e49c09c3824f80236b039ee8755637f0bc0e0084b67fe`,
passes `104`, reads the original online-dashboard update header and reaches
1,710 entries before a raw partition-0 file-open guard inside network startup.
The next boundary is outside this region owner; menu rendering remains pending.
