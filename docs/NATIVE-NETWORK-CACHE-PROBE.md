# Native mounted cache probe

`network_cache_probe.inc` is included after `media.inc`. Its callback is
`uint32_t network_cache_probe(void *, unsigned partition, const char *relative)`.
The network query owner supplies its reviewed locator (partition 1,
`CACHE\LocalCache08.bin`); this adapter does not interpret update metadata.

The adapter holds `media_lock` shared while using `storage_roots`, walking
components and closing every temporary handle. It never takes a network lock,
creates guest objects, reads payloads, creates files or grants write access.
The caller must not already hold the exclusive media lock. Every component is
opened relative to the retained mounted handle with the existing no-reparse,
case-insensitive NT transport, read attributes and synchronization only.

Before each lookup, real FileStandardInformation proves the parent is a
directory, and FileCaseSensitiveInformation (71) must succeed with zero flags.
This deliberately requires a Windows filesystem supporting that query.
Unsupported metadata, case-sensitive directories, unknown flags, access errors,
reparse traversal, malformed names and missing owners remain errors. They are
never converted to absence. The final object must be an ordinary file.
Only the caller may interpret genuine NAME_NOT_FOUND/PATH_NOT_FOUND statuses.
Those codes are reserved for actual component lookup failures. If a metadata
query returns either code, the adapter returns an internal unsupported-state
status instead, so the query owner cannot mistake a metadata failure for an
absent cache. This status is not projected as a guest HRESULT.
Directory handles remain retained across lookup; this is not an atomic snapshot
against external filesystem renames or replacement, and does not claim one.

The standalone authored fixture `tests/test_native_network_cache_probe.c`
includes the production callback and the exact existing NT open transport.
It exercises real scoped NTFS roots, absent cache/file, mixed case, existing
empty file, directory/non-directory components and a real junction. Injected
metadata errors test early and final-query failure, case-sensitive flags and a
malformed success/null handle. Repeated calls and failure paths preserve host
handle count. No title, hardware or owner storage is accessed.

The initial fixture used `CreateFileW` with the production requested access
mask. That did not establish transport or granted-access parity: the Win32 API
can add attribute-read permission. Actual native execution instead obtained
`ACCESS_DENIED` for class 71 on the NT-created partition handles. A direct NT
comparison reproduced that failure with access `00100021` and succeeded with
`001000a1`. Mounted partition roots now explicitly request
`FILE_READ_ATTRIBUTES`; no payload-write or other mutation permission is added.
The corrected fixture opens its root through the same `NtCreateFile`
attributes/share/disposition/options as production and checks granted access
with FileAccessInformation (8). It reproduces the old-rights failure and verifies
the new rights before the full path-probe suite. Those observed statuses are
host evidence, not an original Xbox or universal filesystem claim.

MSVC `/std:c11 /O2 /W4 /WX`, one Normal-priority logical CPU 8, passed the
fixture on 2026-10-19. Private executable/object are under
`build/dashboard-native-20261019/network-cache-probe.*`; test fixtures use
the system temporary directory (`TEMP`/`TMP`), scoped as `cache-probe-<pid>`.
