# Native named file metadata

`media_kernel(210)` implements `NtQueryFullAttributesFile` for the existing
installed-content and writable-partition namespaces. It returns NTSTATUS in
EAX and consumes two arguments plus the return address (`RET 8`). No guest file
handle or asynchronous request escapes this service.

The original export at `0x80019dec` in the qualified flat kernel
`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`
and nxdk establish the object-attributes/output ABI. A bounded original-kernel
check on the retained read-only HDD/COW floor confirms that lower/uppercase
existing names succeed, missing names return `STATUS_OBJECT_NAME_NOT_FOUND`,
and failure preserves the output. Success writes exactly 56 bytes, with zero
padding at offsets 52 through 55. FATX returns EOF in both size fields, matching
the existing native class-34 information policy.

| Offset | Field | Bytes |
| --- | --- | --- |
| 0 | CreationTime | 8 |
| 8 | LastAccessTime | 8 |
| 16 | LastWriteTime | 8 |
| 24 | ChangeTime | 8 |
| 32 | AllocationSize (original FATX: EOF) | 8 |
| 40 | EndOfFile | 8 |
| 48 | FileAttributes | 4 |
| 52 | ABI padding | 4 |

The implementation reuses `media_path`, mounted roots and the existing
case-insensitive/no-reparse host open policy. It requests only metadata read
access and synchronization, queries an aligned host buffer, closes the
temporary handle and publishes the guest result only after success. Timestamps,
EOF and attributes are actual host metadata; no substitute clock or guessed
file size is used. Original installed content remains read-only. Relative
directory handles and the DOS object-directory pseudo handle follow the same
resolution policy as ordinary opens. Unsupported raw devices, character
mapping and object attributes retain the resolver's existing guards.

The existing `media` runtime contract includes the focused metadata checks:
four-byte guest alignment, exact host metadata/ABI bounds, name case and root
forms, directories, missing/traversal/stream/invalid-root failures, unchanged
failure output (including a host query that partially writes its private
staging buffer), no guest handle allocation, and repeated leak-free queries.

Private evidence is retained under `build/native-profile-checks-20261019/`:
`original-full-attributes.aot-private.json` and
`native-metadata-contract.aot-private.json`. These establish the bounded
service contracts; they do not claim that a native dashboard run reached or
passed this API.

`NtDeleteFile` (195) remains unimplemented in this batch. It needs a separate
review of deletion, open-handle/in-flight request ownership and cache
invalidation, while preserving installed-content write protection.
