# Native offline network lifecycle

The explicit `OFFLINE` policy replaces a reviewed SDK lifecycle boundary with
an owned host service. `ORIGINAL` remains the default and calls the retained
original bodies. This is a source-derived HLE contract for offline title use,
not qualification of Xbox Live, original network configuration repair, or a
complete cold original NIC initialization.

The current source-bound dashboard reaches original network initialization
after 1,710 generated entries, then attempts a raw partition-zero configuration
open. That reached frontier does not justify weakening raw disk or file-option
guards. The shared owner instead admits startup and cleanup and stops all other
reviewed network-object consumers before their original reads. The later
reached cached-update query has a separate bounded read-only empty-cache view;
see [the cached-query contract](NATIVE-CACHED-UPDATE-QUERY-2026-10-19.md).

## Admitted contract

`xonline-offline-lifecycle-v1` accepts only a NULL startup descriptor. The
original descriptor can select heap reservation on cold startup; that path is
unsupported. A non-NULL descriptor stops before backend or reference changes,
including on repeated startup.

| Operation | Guest ABI | Native behavior |
| --- | --- | --- |
| Startup | One DWORD argument, HRESULT in EAX, RET4 | Acquire owner lock; call real host `WSAStartup(MAKEWORD(2,0))` on every admitted call; create private offline owner on first success; acquire one reference only after success. |
| Cleanup | HRESULT in EAX, RET0 | Missing owner returns `0x80150005`; otherwise retire one reference, destroy final private owner, and balance one real `WSACleanup`. Original cleanup ignores its backend cleanup result. |
| All other protected entries | No admitted result | Explicit stop before original object access, output mutation, or backend operation. |

The original startup acquires its online gate before its WSA call. Native
lifetime operations are likewise serialized, including backend calls. Host
Winsock requests version 2.0 using its own `MAKEWORD` encoding; the original
private SDK call's literal `0x0200` is not passed through as a host version.
Neither private WSADATA buffer is exposed to guest callers. Host
startup returns an error code directly; it is not read from host
`WSAGetLastError` or stale guest TLS. Failure writes the guest thread LastError
through the exact proved original setter. Positive errors project to
`0x80070000 | (error & 0xffff)`, negative errors retain their bits, and a
nonnegative projected status becomes `0x80004005`. Successful startup leaves
guest LastError untouched. First private-owner allocation failure balances the
acquired backend reference and returns `0x8007000e` without publishing an owner.

The original WSA reference field is a WORD. Native startup stops before a
65,536th acquisition rather than losing ownership through wraparound. A state
inconsistency also stops before backend mutation. EBX/ESI/EDI remain unchanged;
the dispatcher applies the exact guest stack cleanup. It uses the existing
native binder register/flags interface and adds no new flags export channel.

No original CXo/CXn object, guest callback registration, raw disk handle, signed-in
user, socket, receiver thread, cache task or guest kernel success stub is
fabricated. The private owner records only the admitted immutable offline
policy. Supported lifecycle calls need a backend reference and owner lifetime;
they do not need a socket or actual network traffic. Unsupported link, address,
configuration, persisted update-record parsing, public task and Live APIs
remain explicit stops.

## Boundary proof

The title profile contains original addresses and exact body sizes/hashes,
decoded RET sets, global operand roles, callback-table data/publisher proof and
the thread LastError helper proof. Shared source contains no title addresses.
The current profile protects 83 generated entries: 81 original core/adjacent
global consumers and two generated entries from the original four-slot Xn
callback table. Two lifecycle entries and the reached empty-cache query are
supported; the other 80 stop. The query retains exact original helper/RET,
locator, certificate-suffix and private-method consumer proofs.

The original-byte inventory finds 74 direct CXo/CXn pointer consumers among
those 81 functions. All 90 stored core-pointer/gate address literals, including
unaligned occurrences, belong to inventoried functions. No other stored core
pointer aliases were found. An adjacent data candidate points to control state
outside the two core pointers and is retained in the private inventory; it is
not represented as a CXo/CXn alias.

There are 57 tail-called methods. In this original image, their decoded direct
callers are within XONLINE/XNET and no stored data pointers to those methods
were found. Original public wrappers remain guarded, and the native owner
never publishes their original private objects. Constructor final setup writes
only fields within the original object; its CXo registration is within CXn.
The four original Xn callback slots are published only by original XnInit,
which is guarded. The two callbacks without generated entries retain the
existing unknown-target stop. If either becomes generated or registered, bind
rejects it until an explicit guard admission is added.

This is bounded static evidence, not a proof of every computed alias or
arbitrary dynamic code path. The disassembler has one spurious `.data`
function covering a large data interval; data scans deliberately treat that
interval as data. All admitted bodies require full decoding. Other discovery
ranges can contain embedded data; their consumer scan uses skip-data decoding
and an independent all-byte core-literal check. Existing unresolved generated
targets still fail closed. A new reached path must extend the reviewed family
instead of bypassing a guard.

## Integration and validation

`network_offline.c/.h` is the portable lifecycle core with injected locking and
backend operations. `network_host.c/.h` supplies the existing Windows native
host's real Winsock lifetime and lock. Winsock types stay isolated from the
runtime's existing `windows.h` translation unit. The host adapter is built
only for that supported Windows native route; the core has no OS imports.

Root integration selects `PILOT_NETWORK_OFFLINE`, links the core/host files and
`ws2_32`, includes `network_offline.inc`, and calls
`pilot_network_initialize(cache_probe, context)` once before guest entry.
The injected probe owns actual mounted filesystem confinement; the lifecycle
core and Winsock adapter have no host path knowledge. Generated wrappers use
`pilot_network_dispatch(entry, original_SetLastError)`. OFFLINE requires the
complete reviewed generated header/binding; old titles under ORIGINAL require
none of these files or resources.

Passed focused checks:

- Nine authored binder tests: retained original bodies, repeat-bind byte
  idempotence, missing consumer rejection, changed body/RET/global operands,
  callback-table/publisher drift, newly generated callback guard requirement,
  stored global alias rejection, missing original and changed wrapper rejection.
- Cached-query production dispatcher/core: seven guards, exact156-byte/no-tail
  output, aliased argument snapshot, all-page writable preflight, missing-owner
  precedence, presence/error guards and cleanup waiting for an active view.
- MSVC `/O2 /W4 /WX`: production dispatcher and lifecycle, seven guards,
  guest RET and LastError callback ABI, successful LastError preservation,
  repeated references, backend failure projection, allocation-failure unwind,
  final retirement, and 1,600 concurrent startup/cleanup pairs.
- Separate MSVC `/O2 /W4 /WX` real backend test: two host Winsock startup and
  cleanup acquisitions, final owner retirement, no sockets or network traffic.
- Exact original dashboard binder in isolated scratch: all 83 entries admitted
  and repeat binding byte-identical; original generated source unchanged.

The isolated binder scratch is explicitly not launchable. No CPU regeneration,
owner title run, owner generated output, settings or binary was changed by
these tests. Original component evidence covers eight authored existing-owner
and absent-owner cases. Full original cold/final NIC behavior and actual title
menu/controller/audio/Credits remain separate validation.

Private source hashes, exact entry inventory, callback/caller closure and test
receipts are under `build/native-profile-checks-20261019/` in
`native-network-consumer-inventory.aot-private.json`,
`xonline-lifecycle-source-contract.aot-private.json`,
`original-xonline-lifecycle-paths.aot-private.json`,
`native-network-binding-contract.aot-private.json`, and
`native-network-contract.aot-private.json`. They contain no key payloads.
