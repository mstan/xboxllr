# Immutable preload ownership (L02)

Issue `beads-xbox.1.206`, campaign `beads-2wy8.1`. MSVC uses prefaulted
read-only file views by default. `XLLE_PILOT_RESIDENT_FILE_VIEWS=0` retains the
private-copy constructor for comparisons. Other compilers retain private
copies by default and under the same interface when views are requested.

The retained USA smoke preloads 976,975,369 bytes in 2,518 files and reports
813.093 ms elapsed startup work. That is an actual workload observation, not
inclusive CPU time. Blinx has no resident preload configuration; its installed
media path does not call the HOST preload preparation. Independent review
confirmed that its file-service execution path is unchanged by L02.

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
SEH syntax. A mapping or prefault admission failure retains the private-copy
constructor, and a later source fault retains original file I/O.

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
The isolated USA title uses the unchanged original CPU preparation, without
the other unfinished prototypes. Its actual resident and full media contracts
pass with views off/on, including async/APC/event/EOF behavior. The same
engine builds rev4 and passes native-save load, assisted original area loading,
movement and re-entry in both revisions, with actual DSP and muted output.

Independent review found that a mapped-copy fault could advance a synchronous
NULL/-2 read's host cursor before the original I/O fallback. Copy now precedes
the cursor commit; the resident file contract injects a real inaccessible view
and checks fallback bytes, IOSB and position for NULL, -2 and explicit offsets.
SEH only handles source-owned read faults. The primitive fixture exercises
mapping rejection, prefault failure and private-copy recovery; unrelated and
destination faults retain their original propagation. No production fault
injection switch is introduced.

A capacity-reserved USA copy/view/view/copy block observed gameplay private
commit reductions of 980,566,016 and 984,981,504 bytes (935–939 MiB), and startup
peak private commit reductions of 988,098,560 and 988,438,528 bytes. Working
sets remained similar. Actual views covered all 976,975,369 bytes in 2,517
nonempty files; the remaining admitted file was empty. Preload elapsed times
were 1295.019/347.881 ms for copies and 267.753/249.727 ms for views. These
were fresh processes with uncontrolled OS file-cache state; cold-storage
startup remains unmeasured, and these are not preload-only CPU measurements.

Gameplay CPU changed +1.57%/+3.57% versus 3.73% copy A/A variation; source-swap
arrival tails were comparable. Runtime is neutral/inconclusive at this sample
size, with no gameplay CPU or uncapped throughput win claimed. Three resident
reads served 35,992 bytes before the timing window. Most game asset opens are
nonbuffered and retain original I/O; assisted handovers added no resident hits.
Their success establishes loading behavior, not broader mapped-read coverage.
The narrower memory ownership benefit does not close related L01/B01 work.

Private raw evidence:
`private-audit-20261027-architecture/prototype-cohort-20261028/server-resident-return-001`.
All exported files were individually hash-verified. The deterministic 256 MiB
fixture was removed after its recipe and SHA256 were recorded; source, binary,
samples, compiler identity and reservation telemetry remain.
