# Dashboard annotations and Credits mod

Owner request, 2026-10-18; tracked in `beads-xbox.1.136`. Dashboard boot and
performance remain tracked separately in `beads-xbox.1.17`.

## Current result and limits

The private Dashboard 1.0.5960 input is hash selected. The annotation tool
finds 298 script-method address candidates from its actual metadata and
literal constructor stores. These are companion annotations beside the
dashboard build, not a replacement executable or a complete decompilation.

The Credits asset mod adds a selectable item after Settings, with Matthew
Stanley first as XboxLLR developer/integrator. It credits sp00nz/sp00nznet's
xboxrecomp, phobos665's fork, NoRain211's Dead or Alive Xtreme Beach Volleyball
work, Theseus/TeamUIX, xemu, Unicorn/QEMU, DSP56300, platform dependencies and
Evan-Amos's launcher photograph. This project role does not replace any
original author's copyright or license.

The private output preserves every original archive and the base HDD image.
Archive replacement/readback and generated controller/menu logic checks pass.
Actual Credits positioning, text readability, XAP compilation, controller
navigation and return to the main menu are **unverified**. The
2026-10-19 modern HLE cold-boot candidate passes tiled storage, loaded draws,
the reached nominal audio modes, SRCCOPY and semantic context SAVE, and displays
the Xbox boot logo. The subsequent idle PGRAPH reset is implemented and passes
focused HLE/strict-boundary checks. Boot047 exposed an older DSP dependency
that lacked the framework's maintained resident SEN stack support. Boot048
uses a fresh isolated dependency and passes both idle reset and SEN. After
the explicit opaque09F8 HLE omission, stock boot049 visually reaches the main
menu at 10,000,466,663 instructions. This establishes stock menu rendering,
not steady performance or Credits/input acceptance.

Credits image004 takes a fatal reboot before any input in050b. The generator
has two repaired incompatibilities: top-level class variables now use bare
declarations and initialize under the original menu onLoad; appended XIP data
keeps a full read-ahead block for the retail buffered reader. The005 functional
run captures the fatal entry and proves an earlier blocker: the original XBE
still contains the original archives' block hashes. The first modified block
fails the retail integrity comparison before XAP parsing. Neither generator
repair alone is claimed as the cause of the earlier reboot.

`tools/dashboard_integrity.py` independently regenerates the two affected
records in a new private XBE and preserves the integrity checks. The enlarged
table uses isolated pages with fresh header reference cells; adjacent sections
and every unrelated record remain unchanged. Authored digest/layout tests and
private readback pass. Functional boot and Credits acceptance remain pending.
Do not describe this prototype as installed or playable. It is not a public
release.

This full-machine route has partial CPU AOT with interpreter fallback and an
OpenGL HLE GPU. It does not yet use Blinx's complete native title CPU/service
route or its D3D11 backend. Boot-animation speed is not menu performance.
The full-machine codec currently retains a headless output sink; accepting an
APU signal model does not establish audible host output. These limits must
remain visible in any owner handoff.

## Theseus and licensing

Reference revision: [MrMilenko/Theseus
5fc7ddddd03b4b5a7e6233769ad6a8fe46009965](https://github.com/MrMilenko/Theseus/tree/5fc7ddddd03b4b5a7e6233769ad6a8fe46009965).
Its root declares GPL-3.0-or-later; components have their own notices.
The inspected checkout matches the remote revision reviewed for this work.

The tools here use format/interface facts as external references. They do not
copy or link Theseus implementation bodies or its address map. The dashboard
script/geometry transformations operate only on locally owned, hash-checked
original files. No Microsoft dashboard files or XDK headers are bundled in
this repository. Ownership and redistribution rights in those inputs remain
separate from the license of a reconstruction.

Incorporating the Theseus engine would be a separate licensing/architecture
decision. The owner's existing shipping policy excludes GPL implementation;
the framework's original-source PolyForm grant does not relicense GPL work.
The current combined build already has unresolved release licensing, recorded
in [LICENSE-SCOPE.md](../LICENSE-SCOPE.md) and `beads-xbox.1.21`. This work does
not clear those dependencies or authorize distribution. Reference credit is
recorded in [THIRD_PARTY_ATTRIBUTION.md](../THIRD_PARTY_ATTRIBUTION.md).

## Annotation evidence

`tools/dashboard_symbols.py` reads the selected XBE section map and the
24-byte FND descriptor shape documented by Theseus. It examines static data
and provable immediate/register constructor stores without executing the XBE.
Unknown register values, control-flow barriers and conflicting constant stores
cannot supply assumed descriptor values.

Each candidate retains its descriptor address, signature, constructor-store
sites and a hash of 16 actual code bytes. Names and pointers come from this
binary. The tool does **not** prove constructor execution, receiver class,
control-flow reachability, function extent, semantics or equivalence to a
Theseus implementation. Do not blindly import these as verified function
boundaries or carry addresses to another XBE revision.

```powershell
py -3 tools/dashboard_symbols.py `
  --xbe ../machine/hdd/C/xboxdash.xbe `
  --expected-sha256 832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d `
  --output build/dashboard-owner-20261018/dashboard-symbols-next.aot-private.json
```

The output is private and new-path-only. This tool requires Python and
Capstone; authored tests contain no retail code.

## Asset mod construction

The independently authored XIP reader bounds header, directory, name and
payload extents. Only ordinary file members are replaceable. It appends an
aligned replacement and changes that member's directory entry, preserving
other original payload locations, specialized mesh records and timestamps.
Original archives are never written. The mod builder accepts only the reviewed
5960 `default.xip` and `mainmenu5.xip` hashes.

The mod clones owned Settings geometry locally with unique DEF/USE names,
keeps mesh URLs intact, extends selection to index 4, and uses the dashboard's
existing modal owner for Credits. Its controller mapping is A/Right for the
next page, Left for the previous page, Up/Down for the previous/next section,
and B to return. Other message dialogs retain their original handlers.

```powershell
py -3 tools/dashboard_credits_mod.py `
  --assets ../machine/hdd/C/xboxdashdata.185ead00 `
  --notice-tree build/dashboard-owner-20261018/third_party `
  --output build/dashboard-owner-20261018/credits-private/next-mod
```

Use a new output directory each time. The manifest records archive and notice
hashes. Raw notices are copied verbatim and read back; identical decoded text
is shared in the paged viewer, with an index preserving applicable component
names. Long text is wrapped rather than omitted. Collected notice files are
an inventory, not proof that the entire dependency graph has been cleared.
Reconcile that inventory against the final executable and release contents
before any public release.

The checked-in mod source/spec contains no original asset payload. Generated
archives, extracted scripts and annotation files stay in ignored private
build storage. Installation should use a separate private image or a
qualified asset overlay; never patch the owned base HDD in place.

The private `dashboard-credits-005.aot-private.img` was prepared as a separate
8 GiB clone. The base was opened read-only, and the clone matched its complete
SHA-256 before the two XIP replacements. Reopening the clone verified both
replacement archives and the unchanged dashboard XBE. The original image's
size and modification time were unchanged. The clone is now marked read-only;
runtime writes must use copy-on-write. Its adjacent private JSON manifest
records the base/image/archive hashes and successful offline readback. This
preparation does not establish that the retail XAP compiler accepts the mod or
that its menu and notice pages render correctly; those checks remain pending.
The earlier004 clone is retained as failure evidence. Its replacement members
ended exactly at EOF;005 preserves a full64KiB read-ahead block beyond the
appended member and includes those bytes in the archive's declared extent.
The original loader can refill as soon as its current buffer is exhausted,
including at the end of a completed member read. Reference: Theseus
[`xip_archive.cpp`](https://github.com/MrMilenko/Theseus/blob/5fc7ddddd03b4b5a7e6233769ad6a8fe46009965/theseus/xbox/xip_archive.cpp).
Its class compiler also explicitly rejects class-member initializers; this
differs from local variables inside functions. Reference:
[`CClassCompiler::ParseMemberVar`](https://github.com/MrMilenko/Theseus/blob/5fc7ddddd03b4b5a7e6233769ad6a8fe46009965/theseus/engine/xap_compile.cpp).

### Matching private integrity metadata

The captured005 failure establishes the actual owned binary's hash contract:
each at-most-64KiB archive block has `SHA1(le32(actual_length) + block)` in its
XBE `XIPS` record. A short final block uses its actual length. The section
itself uses the same length-prefixed hash over its complete raw bytes. The
tool verifies all original XBE section digests and both original archives'
block tables before replacing their records; other records and the sentinel
are preserved byte for byte.

The two005 archives add twelve hashes (240 bytes). The original virtual gap
after `XIPS` is only sixteen bytes, so in-place growth would overlap the next
section. For the exact hash-selected5960 layout, the tool appends the enlarged
table at raw offset `0x1df000`, relocates it to isolated VA `0x1fe000`, rounds
its virtual extent to complete pages, and updates its digest and image extent.
It preserves flags `0xa` and reserves two zero16-bit reference cells in the
reviewed embedded-PE header padding at file offsets `0x1ffc/0x1ffe`. The owned
kernel increments/decrements both head and tail counters, so retaining the old
cells shared with adjacent sections would be incorrect. Existing adjacent
sections and their cells remain untouched. The consumer resolves `XIPS` by
name; the original table VA appears only in its section header.

This is deliberately a pinned-layout mod recipe, not an arbitrary XBE
rewriter. Changed header metadata invalidates the original RSA signature;
the output requires a boot environment that accepts private unsigned mods.
It does not remove any retail archive/section check, establish redistribution
rights, or include original dashboard payloads in the repository.

```powershell
py -3 tools/dashboard_integrity.py `
  --xbe ../machine/hdd/C/xboxdash.xbe `
  --assets ../machine/hdd/C/xboxdashdata.185ead00 `
  --mod build/dashboard-owner-20261018/credits-private/mod-005 `
  --output build/dashboard-owner-20261018/credits-private/next.aot-private.xbe `
  --manifest build/dashboard-owner-20261018/credits-private/next.aot-private.json
```

Use a separate new private image containing both the modified archives and
their matching XBE. A modified archive with the unchanged XBE fails integrity
before the script compiler can accept or reject it. Offline hashes and layout
checks do not establish a successful runtime menu or controller interaction.

## Focused validation

```powershell
py -3 tests/test_dashboard_xip.py
py -3 tests/test_dashboard_symbols.py
py -3 tests/test_dashboard_credits.py
py -3 tests/test_dashboard_integrity.py
```

The Credits test uses an authored menu and optional Node execution to check
selection/highlights, page/section bounds, original-handler delegation and
modal return. Host JavaScript logic success is not a retail-XAP-VM result.
The final dashboard pass must check the fifth item with Live present/absent,
all text/notice access, B return, normal Memory/Music/Settings navigation and
an unchanged original-image hash.
