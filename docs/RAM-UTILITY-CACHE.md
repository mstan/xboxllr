# RAM-backed utility cache

Native file/cache builds can set `XLLE_PILOT_RAM_CACHE=1` before launch.
The shared default remains disk-backed; a qualified title selects the RAM
policy in its runtime profile. `0` retains the persistent cache reference.
Selection is fixed for the process and does not change while playing.

The mounted utility partition uses a fresh, process-local 750 MiB FATX volume.
Its 16 KiB data pages are allocated as written, through a bounded direct page
index. Existing FATX file, directory, sharing, capacity and NT adapter contracts
are reused, including the native asynchronous completion path. The RAM
allocator resumes its free-cluster scan instead of rescanning the volume from
the beginning for every new cluster. Full volumes return the existing disk-full
status; allocation failure remains an error rather than silently writing to disk.

Only utility partitions 3–5 are admitted by the RAM API; one active utility
volume is owned by a title process. Mount refuses replacement while files in
that partition are open. Both folder-backed and native image-backed storage
retain persistent save partitions, console identity and installed content.
The utility mount ledger is also process-local. Existing disk cache files and
their ledger are left intact, and the RAM volume is discarded at process exit.
No cache file is imported from or written back to disk. Original/LLE file and
cache execution retain their existing storage.

This trades host disk writes for additional host RAM. The volume limit is
750 MiB of data pages, plus a page index of at most 384 KiB and filesystem/file
metadata. It is outside the guest's 64 MiB RAM. It is not a frame-rate mod or a
change to original REW/REC logic, and persistent native saves remain on disk.

Credit: [apexJCL's Blinx project](https://github.com/apexJCL/blinx1-recomp)
inspired the approach. The implementation is independently authored using
the existing first-party FATX service; exact reference identities are in
[the attribution inventory](../THIRD_PARTY_ATTRIBUTION.md).

The standalone storage CMake build provides `storage_ram_cache` and, on
Windows, `storage_ram_cache_bridge`. They exercise actual FATX bytes, sharing,
EOF/allocation/full-volume behavior, deletion, enumeration, reset, lazy memory
and save-partition rejection. The RAM API is portable C++17; the Windows
adapter uses the existing native file ABI.
