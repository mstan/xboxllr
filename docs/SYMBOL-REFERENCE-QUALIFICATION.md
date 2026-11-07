# Symbol-reference discovery qualification

The initial bounded November 5, 2026 pass compares unaided discovery with independent
MSVC MAP references from [debugging.games/Xbox](https://debugging.games/Xbox).
Names and addresses are withheld from discovery. Original archives, MAP files,
executables, the comparison runner and raw results remain in private research
storage. Source-only identities and counts are in
[`accuracy/symbol_reference_results.json`](../accuracy/symbol_reference_results.json).

The [archive-wide survey](XBOX-SYMBOL-CORPUS.md) records all 47 sources and their
metadata dispositions, separately from the initial two-pair experiment below.

## Accepted correction

A referenced, 16-byte-aligned entry can follow a decoded return directly when
the preceding body already ends on the alignment boundary. Requiring an
additional padding byte misses legitimate object factories and callbacks.

`tools/native_discovery.py` extends the pinned discovery tools for normal
`native_pilot.py` preparation, without modifying the donor checkout or adding
symbol seeds. Existing pointer provenance and gap checks remain. The new path
also requires a decoded terminal boundary, no overlapping instruction stream,
no unsaved register restoration, and a complete body which does not cross an
existing entry. A tail jump must name an existing function start.

A further correction recognizes decoded multi-byte alignment after a terminal,
bounded to 15 bytes. Bink assembly code sections may also use unreachable zero
alignment or start a kernel at the section boundary. These cases still require
a supplied code reference, a decoded instruction boundary, a complete body,
saved-register consistency and no intervening known entry. Bink data sections
do not receive the assembly-section exception.

A short return path in an otherwise unclaimed gap also needs a consistent
saved frame. Numeric table words cannot establish a standalone entry that
restores registers it never saved. This check preserves previously qualified
aliases, entries within known bodies, actual branch or jump-table targets, and
decoded entry boundaries. Authored regression fixtures exercise both rejection
and preservation cases; original image bytes remain unchanged.

On Getting Up, this recovers 15 independently MAP-labelled Bink kernels:
46,125 entries become 46,140, with no removed starts or changed existing records.
All 15 complete bodies return with the original four-byte argument cleanup.
Their generated shared-helper C matches actual original x86 execution for 300
constructed input/state cases and 2,467,200 output/state bytes, including stack
and preserved-register checks. This is body correctness evidence for that input;
the final corpus, gameplay and runtime-performance gates remain separate.

Unaided discovery also reuses the PDB path's trailing-switch-table check, with
an additional decoded terminal/alignment requirement. All switch cases must
precede the table; a direct or unresolved computed continuation prevents the
cut. Interleaved tables retain their later code. On Getting Up this corrects
73 extents, recovers one independently labelled callback and removes one false
alias inside a bounded byte-index array. Call-graph changes are confined to the
corrected, added and removed bodies. The 15 executed Bink kernel sources remain
identical; the new callback lowers without unsupported operations, with its
gameplay/runtime check still separate.

A bounded audit of the retained MAP references also ties 43,413 historical
per-build missing labels to validated C++ unwind records and explicit accesses
through the enclosing frame. These are compiler-local cleanup labels, not
ordinary new-function gains or unique functions across titles. Their exact
identities and source evidence remain private; the broader gap audit is open.

A separately qualified private MAP seed set uses the existing explicit seed
interface for Getting Up's 22 remaining ordinary entries. All are retained;
two derived shared jump entries and three changed ranges have original-byte
proof, with no unexplained call-graph changes. All 24 bodies lower completely.
A representative recovered callback passes 20 original-x86/shared-helper
executions, including argument values, both global stores and native stack/
saved-register checks. Its unchanged original instructions have no PC-relative
operands and execute at a host-selected address. The remaining 8,229 MAP labels
are exactly 8,228 proved unwind actions and one registered exception-context
handler. This closes this image's supplied-entry gap classification; the wider
corpus and archived-game gameplay remain unqualified.

| Independent reference | Named entries | Baseline matched | Corrected matched |
| --- | ---: | ---: | ---: |
| Sphinx and the Cursed Mummy | 7,689 | 7,668 | 7,671 |
| Halo Combat Evolved, cache build | 10,781 | 8,166 | 8,169 |

Neither input loses a previously discovered entry or changes an existing
function extent. Starts without MAP function labels remain 159 and 1,232,
respectively. Those counts do not establish false-positive rates: MAP files do
not necessarily label every callable entry, and they do not prove body extents.
The new named entries include two object factories, a method whose first load
precedes stack allocation, and three Halo callbacks/helpers.

Matched current-title discovery checks preserve every existing entry and
extent: Blinx NTSC-U changes from 8,857 to 8,865 entries with its normal seeds
and data-section exclusions; JSRF revision 4 changes from 8,847 to 8,875. All
36 added bodies decode completely through a return or a direct tail jump.
These inputs have no independent MAP reference in this pass, so their counts
establish neither named-entry recall nor whole-game correctness. These are
static checks, with no game launch or runtime performance comparison.

MAP coordinates refer to the pre-conversion PE image. The comparison validates
segment bases, file-backed executable offsets and the converted entry point
before scoring. Both selected pairs use a PE-to-XBE delta of `0x3ef400`;
that value is an observation about these inputs, not a universal conversion.
Sphinx also has retained named direct-call pairing samples from the initial
survey. Exact input identities are recorded in the results file.

## XPP USB registration follow-up

XPP embeds USB driver registration records before its first strongly detected
code entry. Ordinary data-section pointer scans cannot see them. The follow-up
correction examines that prefix only, requiring a consecutive pointer run with
an already known callback. A new target must lie beyond the metadata in XPP,
follow a decoded terminal/padding instruction, avoid existing bodies and
overlapping instruction streams, preserve the stack, and decode through a
complete terminator without borrowing another entry. Tail destinations must
already be known. New entries are aliases, preserving existing function extents.

Five full-pipeline comparisons qualify this correction against the accepted
aligned-boundary baseline:

| Input | Additional entries | Independent names | Lost starts / changed old extents |
| --- | ---: | ---: | ---: |
| Sphinx | 5 | 5 MAP entries | 0 / 0 |
| Halo cache | 3 | 3 MAP entries | 0 / 0 |
| ATV3 | 2 | 2 MAP entries | 0 / 0 |
| Blinx NTSC-U, normal seeds/exclusions | 5 | No independent reference | 0 / 0 |
| JSRF revision 4 | 3 | No independent reference | 0 / 0 |

Sphinx now matches 7,676 of 7,689 MAP entries; Halo matches 8,172 of 10,781.
Starts without MAP function labels remain unchanged. Blinx changes from 8,865
to 8,870 discovered entries, and JSRF revision 4 from 8,875 to 8,878. These are
static discovery checks; title gameplay and runtime performance are unmeasured.
Source identities and counts are in
[`accuracy/xpp_registration_results.json`](../accuracy/xpp_registration_results.json).
The full archive survey separately screens the XPP rule across the corpus;
that screen is distinct from these full-pipeline comparisons.

The 82 proposals without exact symbol references have since passed a separate
static callability check across their 20 affected images. Original initializer
loops directly call 20 proposed entries; original class lookup, device-field
assignment and add/remove dispatch support the other 62. Independent matched
PDB declarations corroborate the callback ABI, and all 82 proposed bodies decode
through a `RET 4` matching the caller's four-byte argument. Checked libraries and
wide zero-padded pointer lists are included. The admission function is unchanged
from the merged base. This establishes callable entries; it does not transfer
target names/types or establish execution, performance or final-bundle corpus
qualification. The aggregate result is retained in the same qualification file.

## Rejected broad rule and remaining inputs

An initial rule accepted any complete-looking terminator after an aligned,
referenced boundary. It proposed two additional, unlabelled Halo fragments
whose alias extents ended before their shared bodies. That version was rejected.
The accepted rule rejects those fragments and tails to non-start destinations.

After the first correction, Sphinx retained 18 unmatched named entries: nine CRT/internal entries with no
direct, immediate or static-pointer references found in the input, two
unaligned lone CRT initializer pointers, and seven USB callbacks in embedded
XPP registration metadata. The follow-up recovers five of those callbacks.
Two isolated XID callback pointers and the eleven CRT/internal entries remain
rejected by its provenance and boundary guards. A symbol name alone is not a seed.
Halo retained 2,612 unmatched named entries after the first correction; that
bounded pass did not classify all of them or qualify the archive collection.

The [private guest-symbol reader](GUEST-SYMBOLS.md) now supports PDB 2.0 and
PDB 7.0 metadata, exact-build diagnostic names and type layouts. Cross-build
library attribution remains separate. There is no runtime performance claim or title release change.
The correction takes effect when a title is regenerated.

## Regression checks

`tests/test_native_discovery.py` contains 32 authored positive and rejection
cases and is registered as `native_discovery` in CTest. Set `XLLE_CPU_DONOR` to
the clean donor revision recorded in `tools/native_pilot.py`, then run:

```powershell
python tests/test_native_discovery.py
python tests/test_native_function_extent.py
```

The donor's 68 unittest cases, 50 standalone function checks and additional
immediate-reference script pass with the extension.
One standalone expectation is deliberately updated: its aligned referenced
target immediately after a return is now accepted. The original fallthrough,
unaligned target and tail-to-non-start rejection cases remain in force.
