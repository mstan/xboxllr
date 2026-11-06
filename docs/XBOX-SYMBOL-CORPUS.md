# Xbox debug-symbol corpus survey

Status: all archives inspected, all distinct XBEs analyzed, and discovery/diagnostic candidates qualified or rejected as described below.

The November 5, 2026 survey inspected all 47 archives listed at [debugging.games/Xbox](https://debugging.games/Xbox). Original archives and all 316 extracted files were hashed in private research storage on G:. Original executables, MAP/PDB contents, type layouts and bulk address tables stay outside the source repository.

## Extracted value

The [guest-symbol reader](GUEST-SYMBOLS.md) imports exact-build diagnostic names, function ranges, static globals and type layouts. Its optional Windows DIA frontend reads legacy PDB 2.0 as well as PDB 7.0 without COM registration; the resulting JSON lookups run with standard Python. All 108 distinct qualified XBE symbol imports pass manifest loading and range lookup. Build identity and address backing are checked before import.

The [XPP registration correction](SYMBOL-REFERENCE-QUALIFICATION.md#xpp-usb-registration-follow-up) recovers USB callbacks referenced inside an executable section. Five full-pipeline comparisons preserve every old start and extent, adding ten independently named callbacks across Sphinx, Halo and ATV3, plus five static entries in Blinx and three in JSRF revision 4. These are discovery results, not gameplay or runtime-performance measurements.

A private bulk PDB-seeding experiment recovered 185 additional ATV3 MAP entries, but changed 47 old extents and removed four unlabelled aliases. Those generated-body changes remain unqualified, so bulk seed export was rejected for the shipped reader. The reader supplies diagnostic metadata without automatically changing generation or service bindings.

## Coverage and identity rules

The inputs contain 98 MAPs, 68 PDBs, 108 XBEs (103 distinct hashes), 29 PEs, two ELF files, three IDBs, three ILKs and five text notes. The PDBs include seven PDB2 and 61 PDB7 files; two PDB copies are duplicates. DIA successfully read all 69 unique PDB/IDB databases.

MAP entry and section-layout checks qualify 76 XBE/MAP pairs and 19 PE/MAP pairs. Eighty of the 98 MAP files have a qualifying supplied executable. CodeView GUID/signature plus age qualifies 58 image/PDB pairs, covering 36 distinct XBEs and 20 PEs. Together the MAP and PDB references cover 78 of the 103 distinct XBEs. A filename match is insufficient evidence for binding addresses.

Discarded COMDAT records with wrapped offsets are excluded from entry scoring. Truncated section names are normalized with section-index and size corroboration. MAP function flags yield entry-only labels; they do not establish body lengths. PDB functions require executable file backing; static objects may occupy mapped zero-fill. Type IDs belong to one PDB, and thread-local locations are not treated as static guest addresses.

## Every archive

The final two numeric columns count distinct qualified image hashes within each archive. Zero means no automatic address binding passed the identity/layout checks. PE columns include Xbox pre-conversion images and Windows tools; their address spaces remain separate.

| # | Archive title and recorded date | MAP / PDB files | Qualified XBE / PE hashes | Disposition |
| ---: | --- | ---: | ---: | --- |
| 1 | ATV3 Lawless [2005-03-07] | 1 / 1 | 1 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 2 | Amped 2 (Beta) [2003-09-12] | 0 / 2 | 0 / 2 | PDB identity matches PE files; supplied XBEs remain unqualified. |
| 3 | Blitz The League [2005-08-26] | 1 / 1 | 1 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 4 | Buffy the Vampire Slayer (Beta) [2002-03-04] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 5 | Buffy the Vampire Slayer (Beta) [2002-06-25] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 6 | Buffy the Vampire Slayer [2002-08-07] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 7 | Call of Duty 3 [2006-10-06] | 3 / 6 | 2 / 0 | Two exact XBE/PDB matches; MAP structural guards reject alternate section/layout details. |
| 8 | Counter-Strike (Beta) [2003-09-05] | 3 / 0 | 0 / 0 | Linux i386 symbols and STABS; no supplied Xbox executable. |
| 9 | Crusty Demons - Freestyle Moto X [2005-08-23] | 1 / 1 | 0 / 1 | MAP/PDB match the PE; supplied XBE revisions remain unqualified. |
| 10 | Crusty Demons - Freestyle Moto X [2006-05-26] | 1 / 1 | 0 / 1 | MAP/PDB match the PE; supplied XBE revision remains unqualified. |
| 11 | FIFA Soccer 2003 (Beta) [2002-09-16] | 2 / 0 | 1 / 0 | One XBE/MAP pair; extra debug MAP is unmatched; MIPS object contains data only. |
| 12 | Freaky Flyers [2003-05-28] | 3 / 0 | 3 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 13 | Full Spectrum Warrior - Ten Hammers [2005-12-14] | 1 / 1 | 1 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 14 | Gauntlet Dark Legacy [2002-03-25] | 0 / 1 | 0 / 0 | Legacy PDB reads successfully; XBE lacks a qualifying identity. |
| 15 | Hail to the Chimp (Beta) [2006-11-15] | 1 / 0 | 1 / 0 | MAP/XBE qualified; attached 2001 runtime logs are historical references. |
| 16 | Halo 2 (Prototype & Dev Tools) [2003-05-03] | 8 / 0 | 4 / 4 | Four MAP/XBE and four Windows-tool MAP/PE pairs; two other XBEs lack a qualified reference. |
| 17 | Halo Combat Evolved [2001-08-15] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 18 | Halo Combat Evolved [2001-09-25] | 2 / 0 | 2 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 19 | Halo Combat Evolved [2002-01-14] | 0 / 1 | 1 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 20 | James Bond 007 - From Russia With Love [2005-09-11] | 4 / 4 | 4 / 0 | Three IDBs expose no useful linked records; ILK parsing was not needed for the accepted MAP/PDB-backed decisions. |
| 21 | James Bond 007 - From Russia With Love [2005-09-30] | 4 / 4 | 4 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 22 | James Bond 007 - From Russia With Love [2005-12-05] | 4 / 4 | 3 / 0 | Three MAP/XBE layouts match; release revision does not. |
| 23 | Kelly Slater's Pro Surfer (Beta) [2002-07-14] | 0 / 1 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 24 | Marc Ecko's Getting Up [2005-12-13] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 25 | Miami Vice [2004-11-18] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 26 | Operation Flashpoint - Elite (Beta) [2005-08-12] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 27 | Pirates [2005-07-05] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 28 | Power Drome [2004-04-19] | 1 / 1 | 3 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 29 | Predator - Concrete Jungle (Beta) [2005-03-31] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 30 | Predator - Concrete Jungle [2005-01-17] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 31 | Project Snowblind (Beta) [2004-10-13] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 32 | Project Snowblind [2004-11-17] | 3 / 0 | 2 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 33 | Sphinx and the Cursed Mummy [2003-10-09] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 34 | Starcraft Ghosts [2004-05-08] | 2 / 4 | 2 / 3 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 35 | The Da Vinci Code [2006-03-17] | 2 / 0 | 2 / 0 | Two distinct qualified XBE hashes, including identical-file aliases. |
| 36 | The Godfather [2006-01-26] | 4 / 1 | 4 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 37 | The Godfather [2006-02-05] | 4 / 4 | 4 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 38 | The Godfather [2006-04-10] | 4 / 9 | 4 / 0 | Two PDB files duplicate earlier copies; compiler PDBs provide types. |
| 39 | The Godfather [2006-05-09] | 4 / 4 | 4 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 40 | The Red Star (Beta) [2004-07-17] | 2 / 0 | 1 / 0 | Current MAP qualifies; old MAP does not. |
| 41 | The Red Star [2004-08-12] | 1 / 0 | 1 / 0 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 42 | The Simpsons [2004-03-08] | 8 / 0 | 6 / 0 | Six qualified XBE hashes; two Bond MAPs do not match supplied revisions. |
| 43 | Tiger Woods PGA Tour 06 (Beta) [2005-07-12] | 0 / 1 | 1 / 1 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 44 | Tom Clancy's Ghost Recon 2 [2004-07-30] | 5 / 5 | 5 / 5 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 45 | TopSpin (Beta) [2003-07-16] | 6 / 7 | 0 / 0 | All six MAPs and seven PDBs fail automatic binding to supplied XBEs. |
| 46 | Voodoo Vince (Beta) [2003-07-15] | 0 / 3 | 1 / 2 | Qualified linked reference; retain as held-out discovery/diagnostic evidence. |
| 47 | Xyanide (Beta) [2003-05-07] | 1 / 1 | 0 / 1 | Xbox PE/MAP reference; compiler PDB has types but no linked functions. |

## Other-platform and auxiliary records

Counter-Strike includes an i386 Linux shared object with 3,007 function symbols and 1,645 STABS records, including 433 type-declaration records and 141 named STABS functions. This is useful semantic reference material. GCC/Linux layouts and addresses do not establish MSVC/Xbox layouts or addresses. FIFA includes a MIPS object with 85 data symbols and no function symbols; it supplies no Xbox code-entry reference.

Compiler-only PDBs retain type information without linked function addresses. The three IDBs expose no useful linked records through the reader, while the ILKs are incremental-link caches alongside already available MAP/PDB data. Their cache internals were not reverse-engineered. No additional cache-format parser is needed for the accepted decisions.

The Hail to the Chimp executable identifies that title and XDK5849. Its attached logs are dated 2001 and include game-state sizes and initialization diagnostics. They are historical research evidence; they are not bound to that executable merely by sharing an archive. Two source notes document archive provenance.

## Completed validation

The frozen 85b73e0 discovery baseline completed without symbol seeds on all 103 distinct XBEs. The separate XPP-only screen also completed on all 103. Three Xbox PE builds lacking a qualifying XBE/MAP pair (both Crusty builds and Xyanide) completed static discovery at their original PE addresses. The [source-only result manifest](../accuracy/xbox_symbol_corpus_results.json) records every archive/member identity, disposition and per-image reference count.

The following counts use each image hash once. MAP function flags are entry labels and may include compiler-local labels; PDB function records have independently declared extents. Entries missing inside a discovered range are distinct from linked bodies outside those ranges. These counts measure reference-entry recall, not precision.

| Reference | Distinct XBEs | Entries | Matching unaided starts | Missing inside discovered ranges | Missing outside discovered ranges |
| --- | ---: | ---: | ---: | ---: | ---: |
| MAP | 72 | 2,294,753 | 2,175,578 | 1,205 | 117,970 |
| Exact PDB | 36 | 1,171,824 | 1,140,501 | 405 | 30,918 |

The XPP-only screen proposes 300 entries across the 78 independently referenced images; 300 have an independent reference label. The other 25 images have 82 proposals across 20 affected images without exact symbol references. All 82 now have independent original-code callback and ABI evidence: 20 through initializer consumers and 62 through class lookup, assignment and add/remove dispatch. Each proposed range decodes completely and returns with four-byte argument cleanup, matching its caller. These are static callability decisions; no target names or full types transfer with them. The five full-pipeline representative comparisons remain separate evidence for preserving previous starts and extents. The screen and callback audit are not 103 final-bundle pipeline comparisons or runtime execution checks.

A separate raw E8 gap check covers all 78 referenced images. Of 17 byte-pattern hits, 4 lie inside other decoded instructions and 13 lack a verified containing body/reachable call. No raw-opcode seeding rule was accepted. This does not prove that unclaimed code is invalid or exhaust indirect calls.

The guest-symbol reader passes 18 authored tests; the discovery corrections pass 19 synthetic tests. Existing extent/crash-report checks and the pinned donor gate (68 unittest cases, 50 standalone checks, plus its immediate-reference script) pass. All 108 qualified diagnostic imports and 36 concrete UDT/static-object manifest queries pass. The optional DIA frontend was built and exercised against both PDB2 and PDB7.

Analysis used Windows and Linux with the same frozen donor/extension. The Capstone bindings report 5.0.7 on both hosts. The Windows runtime files and original frozen SERVER vendor bundle match the released toolkit's 5.0.9 wheel byte for byte; Linux used the 5.0.7 wheel. Both wheel identities are recorded. A portable qualification reproduced all 7,472 function records, extents and call links identically for its held-out input before dispatching the Linux batch. Host counts and the exact qualification identity are retained in the manifest. These are static-analysis runs, not cross-host performance comparisons.

Reference recall does not establish complete precision, callable body extents, gameplay correctness or runtime speed. Unreferenced linked code, internal/shared entries and callbacks without sufficient corroboration can remain undiscovered. Cross-build library attribution and interpreting live memory through debug types require separate evidence.
