# Read-only cached-update query owner

The reached query is a local cached-update lookup, not Ethernet status. The
existing initialized offline lifecycle owner now supports a transient empty
cache view only when its injected filesystem owner proves the selected mounted
path absent. It never infers absence from offline/link state, fabricates an
original CXo/task pointer, returns a fake update descriptor, changes the caller
or EEPROM, or creates/repairs persistent cache files.

The original caller's setting index `0x11` maps to EEPROM B8, whose bit8 is set
in the retained input. The earlier B4 check inspected a different field. After
this query, the unchanged caller treats every nonzero result as no launch,
returns AL1 and proceeds toward graphics. Only result0 selects its original
cached-update launch behavior. No further network API occurs in that bounded
successor before graphics.

## Exact admitted boundary

The profile records the original wrapper, private-method tail and RET4,
enumeration/open/constructor/layout helper bodies, fixed156-byte output, type8
table, stored path formats, and source certificate extension word. The binder
checks all against original bytes. It rejects another direct caller or stored
pointer to the private method. Unknown computed targets remain guarded by the
existing native route; this is bounded static evidence rather than arbitrary
alias qualification. Addresses and layout facts remain profile data.

The source's selected cache is on mounted P1, under `CACHE/LocalCache08.bin`
with an empty source-image suffix. The native runtime receives its partition
and relative name through generated metadata. The portable network core and
Winsock adapter contain no direct filesystem or host-path interpretation.

The original query first checks initialized owner. Missing owner returns
`0x80150005` before output validation/access or cache activity. An initialized
query clears exactly156 bytes, then creates/pumps/enumerates its private kind8
task/cache and closes every created handle. No record, wrong title or an
unsigned timestamp age of900s or more yields `S_FALSE=1`, with zero output;
future timestamps are rejected too. Only a matching recent record yields0
and a descriptor. Original cache failures can propagate negative HRESULTs
after the clear.

The bounded host implementation admits only actual absence. Existing files,
unknown formats and probe errors stop explicitly after the original output
clear; it makes no normal HRESULT claim for those unadmitted paths. Persisted
record parsing, timestamps, descriptors and public task APIs remain guarded.

## Callback and lifetime order

The runtime injects:

```c
uint32_t probe(void *context, unsigned partition, const char *relative);
pilot_network_initialize(probe, context);
```

The callback returns raw NTSTATUS. Only exact `C0000034` or `C000003A` from
genuine name/path lookup absence qualify an empty view. Status0 means presence
and stops. Every other status, including denied access, reparse/confinement,
ambiguous case or unsupported metadata, stops. The filesystem adapter owns
mount identity, component checks, case policy and closure of temporary handles;
see [its contract](../network/NATIVE-NETWORK-CACHE-PROBE.md).

The query acquires the lifecycle owner lock and checks owner consistency and
references. Only after a present owner is established does its output callback
preflight the complete156-byte mapped writable span and clear it once. It
snapshots the guest argument pointer before clearing, including an argument
alias. A transient view then borrows the real owner while the synchronous
filesystem callback runs. Every result and guard releases the lifetime lock
before dispatch can stop. Cleanup waits for the query and cannot retire its
owner mid-view. Callbacks must not reenter network lifecycle operations.

The mapped clear invokes no guest service or scheduler handoff. It does not
clear again after the probe, undo callback/concurrent writes, or zero bytes
beyond156. A proven absent view returns1. No original enum/task handle is
exposed, so no fake async task/event or guest private-object layout is needed.
There is no LastError preservation claim for original cache internals.

The original OPEN_IF path can create directories and initialize/repair a
persistent empty cache. This owner deliberately supplies a read-only view and
does not reproduce that persistence. Existing cache files must not be discarded
or interpreted as empty. This distinction is part of the admitted policy.

## Validation and limits

- Nine authored binder tests pass, including exact old-admission migration,
  preserved bodies/wrapper, repeat-bind identity, query body/RET/path/table and
  certificate drift, direct private consumer and stored alias rejection.
- MSVC `/O2 /W4 /WX` production query fixture passes exact156-byte/no-tail
  writes, missing-owner precedence, argument alias, mapped-page preflight with
  no partial writes, both absence statuses, presence/error guards, no post-probe
  overwrite, released locks and query/final-cleanup lifetime.
- The existing production lifecycle fixture passes its seven guards and
  1,600 concurrent startup/cleanup pairs after this extension.
- An isolated copy of the actual retained83-entry admission migrates without
  changing original generated bodies or wrapper bytes. Every prior record fact
  remains, except the explicitly admitted query action; repeat bind is
  byte-identical. No owner generated tree or title product is changed.

Five unchanged-original prefix/terminal component cases also passed with
explicitly authored non-NULL owner/continuation locals. They establish the
clear/no-owner/no-record/wrong-title/error terminal behavior, not successful
original cache/task IO. No full original initialized cache engine, persisted
valid/expired record, original persistent repair or full-title pass is claimed.
Root owns filesystem integration, candidate build and actual muted title smoke.

Private evidence is under `build/native-profile-checks-20261019/` in
`cached-update-query-assessment.aot-private.json`,
`original-cached-update-components.aot-private.json`,
`native-cached-update-binding-migration.aot-private.json` and
`native-cached-update-contract.aot-private.json`. No key or cache payload bytes
are logged.
