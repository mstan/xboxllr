# Immutable preload ownership prototype (L02)

Issue `beads-xbox.1.206`, campaign `beads-2wy8.1`. Default off:
`XLLE_PILOT_RESIDENT_FILE_VIEWS=1` selects the experimental constructor.

The retained USA smoke preloads 976,975,369 bytes in 2,518 files and reports
813.093 ms elapsed startup work. That is an actual workload observation, not
inclusive CPU time. Blinx's accepted Stage 2-1 control logs have no resident
preload; neutrality there must still be checked in the combined package.

The prototype replaces private per-file preload copies with read-only file views,
prefaulting every page before publishing the existing complete cache. The same
admitted handles deny writers and deletion for the cache lifetime; identity
matching, exclusions, budget checks, atomic failure cleanup, original guest
copies, synchronous file position and completion remain under the existing
owner. It does not map file pages into guest RAM or bypass original parsers.

MSVC structured exception handling guards prefaulting and later view reads;
an in-page failure returns to the original I/O/error path. A failed mapping
attempt falls back to the original private preload. GCC uses that private-copy
path under the same interface. No new supported host is required to accept
SEH syntax. Whole-game stress and failure-path qualification remain pending.

A CPU-only SERVER reservation ran a 256 MiB deterministic warm-cache file in
four fresh processes, copy/view/view/copy, with one MSVC 14.29 binary. The
constructor sampled 394,601,777 and 379,283,726 execution cycles for private
copies, versus 232,750,108 and 231,731,877 for views: approximately 39–41%
lower in those pairs. Elapsed times were 129.180/113.628 ms versus
70.866/71.258 ms. Private commit increased by 268,959,744/268,967,936 bytes
for copies and 565,248/524,288 bytes for views. Working sets remained similar;
these figures are not a claim of a 256 MiB process working-set reduction.

Exact content, actual read-only mapped/private regions, bounds rejection,
retained writer denial, borrowed-handle lifetime and idempotent retirement
pass in that real Windows fixture. The Windows SDK 19041 C5105 warning with
MSVC 1929 is scoped to the SDK include only; authored code retains `/W4 /WX`.
Modern MSVC and Windows GCC checks and strict USA title compilation pass.
Per-game startup, loading, traversal/re-entry, audio and runtime measurements
remain pending; component evidence alone does not close L02.

Independent review found that a mapped-copy fault could advance a synchronous
NULL/-2 read's host cursor before the original I/O fallback. Copy now precedes
the cursor commit; the resident file contract injects a real inaccessible view
and checks fallback bytes, IOSB and position for NULL, -2 and explicit offsets.
SEH only handles source-owned read faults. The primitive fixture exercises
mapping rejection, prefault failure and private-copy recovery; unrelated and
destination faults retain their original propagation. No production fault
injection switch is introduced.

One large local file on SERVER does not predict the cost of 2,518 USA files
on its actual storage. Shared pages may be reclaimed and later faulted again;
the game evaluation must include reads and re-entry, not constructor timing
alone. The component result justifies testing this whole cache-owner
replacement. It closes neither L02 nor the related launch/loading entries.

Private raw evidence:
`private-audit-20261027-architecture/prototype-cohort-20261028/server-resident-return-001`.
All exported files were individually hash-verified. The deterministic 256 MiB
fixture was removed after its recipe and SHA256 were recorded; source, binary,
samples, compiler identity and reservation telemetry remain.
