# Bounded offline random-byte service

Tracking: `beads-xbox.1.137`. The actual native dashboard reached the original
two-argument random-byte entry after graphics initialization and `default.xip`
loading. This extension implements that operation under the existing real
offline lifecycle owner. It does not introduce tasks, users, notifications,
link state, sockets or raw storage access. Original seeded stream identity and
deterministic replay remain [deferred, NTD-06](../runtime/NATIVE-TITLE-DEFERRALS.md).

The exact original wrapper reads the network owner and tails a RET8 method.
Without an owner it returns raw Winsock error `0x276d` before accessing output
or length. With an owner it calls kernel `XcRC4Crypt` under its original IRQL
guard, XORing successive stream bytes into the existing buffer, then returns
zero. It does not update guest LastError. Zero length permits NULL output and
does not advance the stream. The reached original constructor requests four
bytes into a stack DWORD, ignores the status and uses its low signed remainder
to initialize a scheduling field; a constant zero response would change that
behavior.

Native contract:

- Require a coherent live offline owner first. Missing owner returns `276d`,
  including for invalid pointers, oversized counts and missing callbacks.
- Admit **0 through 64 bytes**. This is a bounded implementation cap, not a
  claim about the full SDK API. The reached request is four bytes; neighboring
  original consumers request eight. Greater counts guard without output or
  backend mutation. Fixed 64-byte staging arrays avoid unbounded allocation.
- Zero length returns zero without pointer preflight, entropy or output access.
- For nonzero requests, snapshot both incoming arguments, preflight the entire
  readable and writable output, and stage the existing bytes. Obtain real OS
  entropy into separate private staging. Only after all fallible work succeeds,
  XOR and publish exactly the requested byte count. No tail bytes change.
- Hold the lifecycle lease across preflight, entropy and commit, excluding final
  cleanup. Every guard releases the lease. Invalid ranges and failed entropy
  remain explicit guards; they do not invent an original error or alter output
  or guest LastError.

The portable core exposes injected stage, entropy and non-failing commit seams.
The Windows adapter uses `BCryptGenRandom` with the system-preferred provider;
it exposes no Windows types through the common header. Guest range preflight
uses the existing native mapped-memory contract. There is no guest handoff
between staging and commit; as with the original caller, concurrent mutation
or unmapping of the same output is outside this admitted contract. The native
entropy sequence is not the original boot-seeded RC4 sequence. No seed, random
payload or private key bytes are logged or persisted.

The profile proves the exact original wrapper, query/generator/crypt bodies,
RET widths, owner operand, PRNG field role and RC4 import. Direct or stored
private-query bypasses are rejected. The existing 83 wrappers remain intact:
four operations are supported and 79 remain guarded. Exact earlier lifecycle
or cached-query records can upgrade only when all other record/header/body and
wrapper identities match. Repeat binding is idempotent. `ORIGINAL` routing and
profiles without this optional proof retain prior behavior.

Source lookahead does not justify fake asynchronous work. The reached original
constructor initializes task and notification handles to zero. Its periodic
method skips task pumps, results and notifications while those handles and
logged-on flags remain zero. Later original script/UI actions can create work;
that is a conditional source observation, not proof those paths never run.
Task pump/close dereference real task vtables, result queries read task state,
and logged-on-user queries expose original object memory. Notifications read
configuration and mutate last-seen counters. Storage-backed user enumeration
cannot infer an empty list from offline policy. Those operations remain guarded.

Validation:

- Nine unchanged-original component cases cover missing-owner precedence,
  zero-length NULL output, exact 4/8/64-byte extents, XOR of existing bytes,
  RET8/nonvolatile registers and split-call stream continuity. Initialized
  owner and RC4 seed are authored inputs; this is not original cold network
  initialization or boot-seed qualification.
- `test_native_network_random.c` checks injected core failures and exact XOR.
  Its Windows section includes the production dispatcher and tests NULL,
  overflow, cross-page/no-access and read-only rejection, both arguments aliased
  by output, exact extent, entropy failure and real cleanup contention. Strict
  MSVC `/O2 /W4 /WX` passes; the portable core also passes strict GCC.
- The existing real host fixture checks OS entropy success and exact extent,
  along with real Winsock reference ownership; it opens no sockets. Twelve
  authored binder tests pass, including proof drift and migration. An isolated
  copy of retained actual title bindings upgrades to four supported operations
  without changing generated CPU bodies or wrapper bytes, then repeats
  idempotently. The scratch tree remains explicitly nonlaunchable.

Private receipts under `build/native-profile-checks-20261019`:
`xnet-random-frontier-assessment.aot-private.json`,
`original-xnet-random-component.aot-private.json`,
`network-random-retained-binding.aot-private.json`, and
`network-random-contract.aot-private.json`. This batch performs no actual title
launch, generated owner-output mutation or menu/frame qualification.
