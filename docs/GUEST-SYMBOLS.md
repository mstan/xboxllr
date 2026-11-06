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
research, not a live memory inspector. Bit positions, enum values and location
expressions are not decoded; do not treat every member location as a byte offset.
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

## Discovery use

Discovery reads a matching-CodeView PDB manifest only when explicitly supplied
with `--discovery-symbols` to `native_pilot.py` (or the private title path key
`discovery_symbols`). MAP entry-only manifests are rejected for body discovery.
PDB bounds constrain body formation without letting interior entries truncate
their parent. Branch and switch-table handling still determines executable body
ends; a declared extent can also contain trailing data. Independently callable
aliases remain separate entries. The metadata identity is retained in generation
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
