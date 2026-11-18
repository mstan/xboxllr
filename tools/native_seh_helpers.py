"""Collect the SEH frame helpers generated code exchanges EBP with.

The donor lifter brackets every call to an MSVC __SEH_prolog/__EH_prolog/
__SEH_epilog with a publish of the caller's frame (g_seh_ebp = ebp) before the
call and a read-back (ebp = g_seh_ebp) after it, because EBP is a C local in
generated code.  The guest interpreter bridge must run those helpers as guest
code (they rewrite their caller's EBP and stack frame directly), so it needs
exactly the set generated code treats this way.  Derive it from the generated
C rather than re-detecting it: then the two cannot disagree.
"""
import re
from pathlib import Path

_BRACKET = re.compile(
    r"publish frame to SEH helper \*/(.*?)read back frame from SEH helper", re.S)
_CALL = re.compile(r"/\* call 0x([0-9A-Fa-f]{8}) \*/")


def collect(gen_dir):
    helpers = set()
    for path in sorted(Path(gen_dir).glob("*.c")):
        text = path.read_text(encoding="utf-8")
        for match in _BRACKET.finditer(text):
            calls = _CALL.findall(match.group(1))
            if len(calls) != 1:
                raise ValueError(
                    f"{path.name}: SEH helper bracket holds {len(calls)} calls, expected one")
            helpers.add(int(calls[0], 16))
    return sorted(helpers)


def c_table(helpers):
    """C declaration for pilot_image.h (a 0 sentinel keeps the array non-empty)."""
    body = ", ".join(f"0x{va:08x}u" for va in helpers)
    return ["/* SEH frame helpers the guest interpreter must run as guest code. */",
            f"static const unsigned pilot_seh_helpers[] = {{ {body + ', ' if body else ''}0u }};",
            f"#define PILOT_SEH_HELPER_COUNT {len(helpers)}u"]


if __name__ == "__main__":
    import sys
    found = collect(sys.argv[1])
    print(" ".join(f"0x{va:08X}" for va in found))
