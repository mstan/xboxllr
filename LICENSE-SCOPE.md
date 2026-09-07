# License scope and distribution status

The owner has selected **PolyForm Noncommercial License 1.0.0** for his original
work. [LICENSE](LICENSE) contains the unmodified official license text;
[NOTICE](NOTICE) identifies the licensor. This follows the noncommercial policy
used by psxrecomp. Its local license file contains edited wording, so xboxlle
uses the official PolyForm text rather than presenting those edits as standard
PolyForm. Nothing here changes psxrecomp's files or terms.

This grant applies only to original contributions for which Matthew Stan holds
the necessary rights. It does not relicense third-party code, derivatives of
third-party code, dependency source, or license documents. Their existing terms
continue to govern. In particular, the EEPROM-generation port remains LGPL,
including modifications to that port. See [the attribution inventory](THIRD_PARTY_ATTRIBUTION.md).

## Unresolved combined-executable restriction

The current build statically links **Unicorn**, whose upstream license is GPL.
PolyForm's noncommercial restriction is not GPL-compatible. Retaining a GPL
notice or putting dependency files in a separate directory does not resolve
that conflict for a combined executable. No GPL linking exception or alternate
Unicorn license has been obtained, and this document grants neither.

**Do not distribute the current combined executable as a PolyForm-only product.**
The owner's original-source licensing choice is recorded here; compatibility
and incorporated-source provenance remain an open release task, tracked in
`beads-xbox.1.21`. This is not a claim that every existing source file is entirely
original or that the full dependency graph has been cleared.

Before distributing a combined build, resolve this through applicable
permissions or a reviewed licensing/architecture change. Changing a static link
to a DLL alone is not a presumed solution. Any alternative must also respect
the measured CPU floor and the owner's scope decisions. Do not silently offer
the owner's work under GPL, apply noncommercial restrictions to third-party
GPL code, or label PolyForm as an OSI open-source license.

The standalone LGPL EEPROM tool is distributed as editable Python source with
its notices and license. Its generated machine files do not become copies of
the generator's source merely because it produced them; keep them private for
their identity/key content regardless.

References: [official PolyForm terms](https://polyformproject.org/licenses/noncommercial/1.0.0),
[Unicorn 2.1.1 COPYING](https://github.com/unicorn-engine/unicorn/blob/2.1.1/COPYING),
and [GNU's compatibility guidance](https://www.gnu.org/licenses/gpl-faq.en.html#WhatDoesCompatMean).
