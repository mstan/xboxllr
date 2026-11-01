# Complete native commitment ownership candidate

Tracking: `beads-xbox.1.206`, architecture burndown M01. Built-game loading,
startup, memory and steady-play measurements remain pending.

The earlier allocation-ledger implementation removed host queries for native
service arenas. Other address ranges and totals reconstructed from allocation
records remained in every total-budget query. This candidate gives all admitted
native mappings one commitment authority, covering image sections, virtual
memory, stacks/context scratch, pool, contiguous and system allocations.

Initialization requires a wholly reserved, uncommitted address space with
4096-byte host pages, before any workers or commits. A page-state table occupies
1 MiB for 4 GiB of virtual addresses (896 KiB more than the initial commitment-only
bitmap). Successful Win32 commit/decommit/protection operations and
the direct NT operations used by VM/section services publish their actual
extents. Recommit/decommit is idempotent; failed operations do not publish.
Protection changes preserve commitment. Whole-space total queries are constant
time; partial queries count the actual byte intersection with committed pages.

The same authority now publishes logical read/write permissions. After an actual
successful mapping/protection change it queries the affected regions once; hot
native consumers validate spans against that ownership. This also handles actual
OS recommit behavior without assuming protection changes or stays unchanged.
Title-mod read/write and native wait/service validation retain their individual
permission rules and bounds. Readonly image overrides publish both the temporary
change and restoration. Real NT protection changes relinquish write watches
through the same operation owner rather than leaving that duty to each caller.

Internal write-watch readonly arming preserves logical RW access. One-shot guard
pages always require the host-query fallback because guard consumption can change
their host state without another protection service. Invalid/unsupported ownership
also falls back; a known uncommitted/noaccess span is rejected. No payload byte is
cached and no write notification, protection or dirty tracking is skipped.

Selection evidence: the combined candidate's 227 private main-thread stack
snapshots included seven `title_mod_read` / host VirtualQuery paths during draw
preparation. These were wall snapshots under contention, including waits, with
226 caller walks; they select an experiment and establish no exact CPU percentage
or speedup. Raw evidence is retained with campaign issue `beads-xbox.1.206`.

Only a runtime with owned mapping writers admits this authority. Original
device/kernel, USB, file and scheduling owners retain their previous host-query
path. Unsupported hosts, non-4096-byte pages, unavailable metadata or unproved
mapping extents also retain that path. Guest allocation placement, protection,
the 64-MiB budget and separately charged native service bytes are preserved.

`XLLE_PILOT_NATIVE_MEMORY_ACCOUNTING=0` selects the previous accounting path at
startup for matched diagnostics. There is no live ownership/backend switch.
The source-level fallback remains functioning under the same query interface.

The focused native check uses actual OS VirtualQuery results as its oracle for
Win32/NT operations, unaligned ranges, repeated commits, failed operations,
partial decommit, protection and reservation retirement. The portable check
requires the caller fallback without returning an invented count. Existing VM,
system-memory, statistics, section, section-mod and readonly-recommit fixtures
exercise the integrated runtime with accounting enabled and disabled. Test
duration differences are not game performance results.
