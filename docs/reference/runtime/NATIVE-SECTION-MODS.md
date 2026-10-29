# Native section data mods

Tracking: `beads-xbox.1.137`. Dashboard Credits uses the shared native-title
loader with a separate, title-owned static data registry. The supplied XBE
and generated CPU identity stay original. This interface does not patch CPU
instructions or accept executable plugins from a downloaded package.

`PILOT_SECTION_MOD_SOURCE` selects a reviewed registry implementing
`pilot_title_section_mods()`. The native driver selects it through the private
`section_mod_source` path, hashes the complete self-contained C source into
configuration/build/run identity, and clears stale CMake selection when absent.
Changing or removing the registry requires configuring and building again.
The existing graphics mod ABI remains unchanged.

The portable validator checks exact original section descriptors, the admitted
image identity, replacement SHA256, non-executable flags and exclusion of
generated CPU entries. It appends replacement data to an owned loader view,
relocates only selected sections onto isolated pages and updates their
length-prefixed SHA1 digest, extent and boundary-counter cells. Original raw
bytes and all unrelated header fields/descriptors remain unchanged. The view
stays alive for the process. Initial preloads and deferred section reloads use
that same view; existing section references, protection, page ownership and
the guest memory budget remain responsible for residency. Protection follows
the retained boundary flags: actual XIPS flags `0x0a` leave its aligned first
page writable, as the original loader does; relocation does not silently add
protection bits.

Each reservation of two header counter cells is explicit title-owned proof.
The validator rejects occupied/aliased cells; zero bytes alone do not establish
that arbitrary header padding is free. The title author must also establish
that consumers resolve the selected data through section metadata; relocating
data referenced by fixed guest pointers is unsuitable. The dashboard recipe uses the reviewed
5960 reservation at `0x1ffc/0x1ffe`. Registry admission is currently limited to
native files, memory and scheduling. It does not claim that direct self-file
reads or original-kernel file loading see a transformed on-disk executable.

## Private generation

`tools/native_section_mod.py` generates self-contained C and a receipt from
owned XBE sections and replacement data. `tools/xbe_image.py` supplies the
shared immutable parser; the existing integrity tool reuses it.

Dashboard generation can produce the matching registry together with both
modified archives:

```powershell
py -3 tools/dashboard_credits_mod.py --assets <owned-dashboard-assets> `
  --native-xbe <original-xboxdash.xbe> --output <new-private-mod-directory>
```

Use its `sections.aot-private.c` as `section_mod_source`. Mount a separate
content directory containing the original XBE and generated archives; never
replace the owner's original assets. Archive block checks remain enabled and
use the new table. These locally derived data files require private storage;
this tooling does not establish redistribution rights.

## Qualification

Authored generation checks cover source identity, descriptors, code exclusion
and reservations. The runtime contract fixture exercises the real preload and
deferred section loader with enlarged data, isolated counters, read-only
protection, nested references, repeated unload/reload, unrelated section bytes
and preservation of the original source. Actual native Credits placement,
paging, B return, ordinary menu navigation and performance remain unverified
until the native dashboard baseline is usable.
