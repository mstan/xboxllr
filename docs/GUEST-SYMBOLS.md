# Private guest symbols and type metadata

`tools/native_guest_symbols.py` imports locally supplied debug metadata and
resolves guest addresses in runtime diagnostics. It accepts original Xbox XBEs;
PE tool addresses and host exception PCs are separate address spaces.

PDB imports require one matching CodeView identity in the executable: an RSDS
GUID and age, or an NB10 signature and age. Functions use file-backed executable
section offsets and PDB lengths. Folded functions retain every name at a shared
address. Zero-filled static globals are retained as mapped objects; thread-local
locations are not interpreted as static addresses.

MAP imports validate the converted entry point and executable section layout,
including truncated section names and discarded COMDAT offsets. This is
structural corroboration, weaker than PDB identity: a same-layout patched binary
cannot be distinguished from these checks alone. MAP function flags provide
entry names, not lengths, and can include compiler-local assembly labels.
Consequently MAP lookup matches exact entries only.
It never guesses which function contains an arbitrary following address.

Both forms produce JSON bound to the complete XBE SHA-256. Keep original files,
DIA dumps and derived manifests in private research storage outside Git. The
`.aot-private.json` suffix is ignored by the repository. Nothing automatically
adds metadata to a game's shareable crash ZIP or changes console-service/HLE
bindings.

## Read a local PDB

`tools/pdb_inspect.cpp` is an optional Windows frontend using the installed
Visual Studio DIA SDK. It reads both legacy PDB 2.0 and PDB 7.0 without registering
a COM server. Build it from a Visual Studio 2022 x64 developer PowerShell, with
an existing output directory outside the checkout:

```powershell
$dia = 'C:/Program Files/Microsoft Visual Studio/2022/Community/DIA SDK'
$env:INCLUDE = "$dia/include;$env:INCLUDE"
cl /nologo /O2 /EHsc /std:c++17 /MD tools/pdb_inspect.cpp `
  /FeG:/Research/pdb_inspect.exe /FoG:/Research/pdb_inspect.obj `
  /link ole32.lib oleaut32.lib
```

Capture native UTF-8 output as bytes. For example, run this with Python; it also
avoids older PowerShell versions transcoding redirected native output:

```python
import subprocess
with open('G:/Research/title.dia.jsonl', 'xb') as output:
    subprocess.run([
        'G:/Research/pdb_inspect.exe',
        'C:/Program Files/Microsoft Visual Studio/2022/Community/DIA SDK/bin/amd64/msdia140.dll',
        'G:/Research/title.pdb',
    ], stdout=output, check=True)
```

Then import and query the result:

```powershell
python tools/native_guest_symbols.py import-dia --xbe G:/Research/title.xbe `
  --metadata G:/Research/title.dia.jsonl `
  --output G:/Research/title-symbols.aot-private.json `
  --types-output G:/Research/title-types.aot-private.json
python tools/native_guest_symbols.py lookup `
  --symbols G:/Research/title-symbols.aot-private.json `
  --xbe-sha256 <complete-XBE-SHA256> --address 0x00011001
```

Use `import-map` with `--metadata title.map` and the same `--xbe`/`--output`
arguments for a MAP. Lookup also accepts `--log runtime.log` instead of
`--address`. It annotates recognized guest stop/recent-entry markers and leaves
host frames alone. Lookup runs with standard Python on Windows or Linux;
the original PDB, DIA and a compiler are unnecessary.

The separate type manifest retains type IDs, UDT/member ownership, offsets,
array counts, pointer and function type links, calling conventions and addressed
static objects. IDs are local to one source PDB. This is layout metadata for
research and the bounded live inspector described below. Enum constants retain their exact signed
or unsigned value as a decimal string, including 64-bit values. Bitfield members
retain storage byte offset, bit position and bit width only when DIA supplies
them. A real bit position of zero differs from missing metadata. DIA reports a
bitfield's length in bits; other type lengths are bytes. See Microsoft's
[location properties](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/symbol-locations)
and [length units](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/idiasymbol-get-length).
Do not treat every member location as a byte offset.
Compiler-only PDBs can contain useful types without any linked addresses.

Query an exact type name, type ID, or static global name with `describe`:

```powershell
python tools/native_guest_symbols.py describe `
  --types G:/Research/title-types.aot-private.json `
  --xbe-sha256 <complete-XBE-SHA256> --type-name Actor
```

Use `--type-id 1234` or `--object-name g_actor` in place of `--type-name`.
The result preserves every matching definition, its owned members and referenced
type records. Globals include their guest addresses and mapped span; that span
is not an object-size claim. `byte_offset` is supplied only for a regular
instance-relative member. Bitfields, constants, static members and virtual
bases require their own location interpretation. Missing names return an empty
list; the reader does not substitute a similarly named type or another build.

`native_guest_types.TypeDecoder(document, xbe_sha256)` decodes supplied known
bytes through `scalar(type_id, raw)` and `bitfield(member_id, instance_bytes)`.
Integer results use decimal strings to preserve precision. Enum results retain
all matching names; unknown values retain their number. Signed bitfields use
their declared integer storage type and width, including fields crossing byte
boundaries. Wrong-image documents are rejected; missing layouts, short reads and
invalid bit ranges return explicit unavailable results. These APIs interpret
bytes provided by the caller; live memory transport and scoped-variable location
resolution are separate features.

## Scoped variables and live inspection

The DIA frontend retains lexical/function scopes, function debug boundaries,
register locations, signed byte offsets, available liveness flags/ranges and
frame programs. `native_guest_variables.Variables` consumes this JSON with an
explicit guest PC/register context and read callback. It distinguishes
out-of-scope, optimized-away and unavailable locations. Register IDs follow
CodeView x86 numbering; host exception registers are never substituted.
Stack/virtual-frame bases need certified guest context or an evaluable DIA
program. Return-address search input must be certified by the caller; it is
never inferred by scanning pointer-looking stack words. TLS additionally needs
the matched image template and the paused guest thread's TLS base.

An opt-in native TCP endpoint provides `pause [decimal-entry-address]`,
`guest_snapshot`, `read_guest <snapshot> <decimal-address> <bytes>` and `resume`.
Pause waits at an original function entry. The execution owner remains stopped
until resume. Reads require the current snapshot token and are limited to 4096
bytes of committed readable guest RAM per request; device reads are rejected.
Resume invalidates the token. The runtime verifies the loaded XBE hash before
binding its provider.

The initial runtime provider can certify only the pure CPU profile before any
guest kernel service or guest-thread creation. Device/audio/graphics owners,
generated service bindings and title mods prevent certification. These profiles
return an explicit unavailable response; ordinary running game profiles still
need a qualified quiescence owner. EBP and flags are omitted because this CPU
lowering can keep them local to a generated function. Missing context stays
unavailable. A function-entry pause does not make a source variable live before
its PDB debug range begins.

With a compatible runtime already paused, query an exact static object or
in-scope variable through the same endpoint:

```powershell
python tools/native_debug.py --port 8080 pause --address 0x11020
python tools/native_guest_inspect.py --port 8080 `
  --types G:/Research/title-types.aot-private.json --object g_actor --path 'items[2].mode'
python tools/native_guest_inspect.py --port 8080 `
  --types G:/Research/title-types.aot-private.json --variable 1234
python tools/native_debug.py --port 8080 resume
```

Member paths support declared array indices and `pointer->member` traversal.
`--follow 1` expands pointer targets, with an explicit limit of eight levels;
cycles, null pointers and unreadable targets have distinct results. Queries
bound total reads to 64 KiB, aggregate/array expansion to 64 members/elements,
nesting to eight and visited nodes to 128. Select a member/index when a complete
object exceeds an expansion limit. Missing offsets or array bounds are never
guessed. The client checks image identity before reads and checks that the same
snapshot remains paused after the query. It sends no guest-memory writes and
does not resume execution automatically. Python consumption remains portable;
only PDB ingestion requires Windows DIA.

## Discovery use

Discovery treats Bink's `BINKDATA` section as data even when the XBE marks it
executable. Its callable tables feed the existing guarded pointer pass; the
other Bink and SDK code sections retain their code classification. Coefficient
words that resemble pointers into an existing body need a separately qualified
entry or padded function boundary before they can supply an alias. This analysis
view preserves the image bytes.

Discovery and normal CPU lowering read instructions from the loaded-image view:
file bytes followed by the section's declared zero-fill, bounded by its virtual
extent. They never borrow file padding or a neighbouring raw section. The sweep
materializes at most 15 tail bytes to complete an instruction; it does not sweep
an arbitrary BSS allocation. A PDB entry must still begin in file-backed code,
but its declared body can include this bounded zero tail. Callee cleanup queries
use the same instruction bytes as the generated body. Cache identity includes
the shared image reader.

Discovery reads a matching-CodeView PDB manifest only when explicitly supplied
with `--discovery-symbols` to `native_pilot.py` (or the private title path key
`discovery_symbols`). MAP entry-only manifests are rejected for body discovery.
PDB bounds constrain body formation without letting interior entries truncate
their parent. Branch and switch-table handling still determines executable body
ends; a declared extent can also contain trailing data. Independently callable
aliases remain separate entries. Entries manufactured by decoding proven trailing
switch/index data are removed before following their apparent branches.
The metadata identity is retained in generation
proofs and cache keys; changed or removed metadata cannot reuse an old discovery.
This path is under corpus and built-runtime qualification in the symbol burndown;
it does not establish that every supplied body can execute natively.

The earlier private experiment supplying
6,729 PDB roots through the existing seed interface recovers 185 additional MAP
entries in ATV3 without losing a MAP entry. It also changes 47 old extents and
removes four unlabelled aliases. Those changes have not been qualified as correct
generated bodies, so bare-address bulk seed export is excluded. Exact
symbol identity does not prove that the current discovery/lifting pipeline
handles every supplied body correctly. Names remain diagnostic metadata.

The November 2026 full archive survey and its qualification evidence are
described in [symbol reference qualification](SYMBOL-REFERENCE-QUALIFICATION.md).
