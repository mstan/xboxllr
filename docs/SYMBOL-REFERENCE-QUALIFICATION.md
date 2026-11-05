# Symbol-reference discovery qualification

The bounded November 5, 2026 pass compares unaided discovery with independent
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

## Rejected broad rule and remaining inputs

An initial rule accepted any complete-looking terminator after an aligned,
referenced boundary. It proposed two additional, unlabelled Halo fragments
whose alias extents ended before their shared bodies. That version was rejected.
The accepted rule rejects those fragments and tails to non-start destinations.

Sphinx retains 18 unmatched named entries: nine CRT/internal entries with no
direct, immediate or static-pointer references found in the input, two
unaligned lone CRT initializer pointers, and seven USB callbacks in embedded
XPP registration metadata. Promoting these requires stronger evidence about
entry usage or the registration layout. A symbol name alone is not a seed.
Halo retains 2,612 unmatched named entries; this bounded pass does not classify
all of them or qualify the rest of the archive collection.

PDB 2.0 ingestion, cross-build library matching and live runtime naming are
separate work. There is no runtime performance claim or title release change.
The correction takes effect when a title is regenerated.

## Regression checks

`tests/test_native_discovery.py` contains 13 authored positive and rejection
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
