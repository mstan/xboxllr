# Native recording ownership

Two independent build options default OFF:

- `PILOT_INPUT_REPLAY_OWNER` reads an immutable input replay through one64KiB
  buffer. `XLLE_PILOT_INPUT_REPLAY_OWNER=0` selects the original reader at
  startup. The existing input lock, parser order, pending timed row, EOF and
  divergence behavior remain. Recording still uses its existing bounded
  asynchronous writer. Input playback does not restore machine determinism.
- `PILOT_SESSION_PAGE_OWNER` lets full-Session frame observation use current
  watcher ownership for aligned ARMED pages. The caller already owns CPU and
  device eligibility. The page therefore has the same committed/logical-RW
  one-page description as the existing query wrapper. Other states/ranges
  and unsupported hosts retain that wrapper. Set
  `XLLE_PILOT_SESSION_PAGE_OWNER=0` for the original observation path.

The Session optimization preserves every sampled byte, rotating phase, map
description field, FNV operation and CPU/frame evidence value. It adds no
persistent map cache and does not skip memory evidence. The proof is specific
to the Session observer; it must not replace arbitrary guest/host VM queries
or be used while mapping writers can run. Normal play skips Session frame
observation as before. Journals and actual audio/screenshot capture keep their
existing ownership and batching.

The input fixture checks strict/timed streams spanning many buffers, exact
payload/order, EOF and malformed rows at refill boundaries, alongside the
existing recording/overflow cases. `pilot_contract_test session-memory`
compares the actual original/owned frame-evidence function across all256
phases and mapping/write transitions. Native and portable watcher tests cover
the conservative fallback. Built-title measurements must distinguish normal
play, ordinary input recording and full-Session recording; component speed
or an unfinalized journal prefix alone is not title/replay qualification.
