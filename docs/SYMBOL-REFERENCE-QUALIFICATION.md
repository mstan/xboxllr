# Symbol-reference discovery qualification

The initial bounded November 5, 2026 pass compares unaided discovery with independent
MSVC MAP references from [debugging.games/Xbox](https://debugging.games/Xbox).
Names and addresses are withheld from discovery. Original archives, MAP files,
executables, the comparison runner and raw results remain in private research
storage. Source-only identities and counts are in
[`accuracy/symbol_reference_results.json`](../accuracy/symbol_reference_results.json).

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

`tests/test_native_discovery.py` contains 19 authored positive and rejection
cases and is registered as `native_discovery` in CTest. Set `XLLE_CPU_DONOR` to
the clean donor revision recorded in `tools/native_pilot.py`, then run:

```powershell
python tests/test_native_discovery.py
python tests/test_native_function_extent.py
```

The donor's 68 unittest cases and 49 standalone checks pass with the extension.
One standalone expectation is deliberately updated: its aligned referenced
target immediately after a return is now accepted. The original fallthrough,
unaligned target and tail-to-non-start rejection cases remain in force.
