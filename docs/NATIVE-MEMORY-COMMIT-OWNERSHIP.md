# Complete native commitment ownership candidate

Tracking: `beads-xbox.1.206`, architecture burndown M01. Built-game loading,
startup, memory and steady-play measurements remain pending.

The earlier allocation-ledger implementation removed host queries for native
service arenas. Other address ranges and totals reconstructed from allocation
records remained in every total-budget query. This candidate gives all admitted
native mappings one commitment authority, covering image sections, virtual
memory, stacks/context scratch, pool, contiguous and system allocations.

Initialization requires a wholly reserved, uncommitted address space with
4096-byte host pages, before any workers or commits. The bitmap occupies 128 KiB
for 4 GiB of virtual addresses. Successful Win32 commit/decommit operations and
the direct NT operations used by VM/section services publish their actual
extents. Recommit/decommit is idempotent; failed operations do not publish.
Protection changes preserve commitment. Whole-space total queries are constant
time; partial queries count the actual byte intersection with committed pages.

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
