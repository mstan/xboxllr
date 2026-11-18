"""Qualification-only: make selected translated functions look undiscovered.

Each listed guest entry keeps its C symbol, so direct callers and the dispatch
table still reach it, but its body becomes exactly the stub generated code uses
for an undiscovered direct target (RECOMP_UNIMPL_FUNCTION).  A strict runtime
therefore stops there as it would for a real discovery miss, and a runtime with
the guest interpreter enabled runs the original function instead.  The list is
recorded in the generation manifest; it is never part of a release profile.
"""
import json
import re
from pathlib import Path

KIND_FORCED = 5


def load(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    entries = data["functions"] if isinstance(data, dict) else data
    out = sorted({int(str(v), 16) if isinstance(v, str) else int(v) for v in entries})
    if not out or any(not 0x10000 <= va < 0x80000000 for va in out):
        raise ValueError("forced interpretation list must name guest code addresses")
    return out


def _body_end(text, open_brace):
    """Index just past the brace matching text[open_brace], skipping C literals/comments."""
    depth, i, n = 0, open_brace, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("/*", i):
            i = text.index("*/", i + 2) + 2
            continue
        if c == "/" and text.startswith("//", i):
            i = text.index("\n", i)
            continue
        if c in "\"'":
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unterminated generated function body")


def apply(gen_dir, functions):
    gen_dir = Path(gen_dir)
    remaining = set(functions)
    replaced = []
    for path in sorted(gen_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        changed = False
        for va in sorted(remaining):
            m = re.search(rf"^void sub_{va:08X}\(void\)\s*\{{", text, re.M)
            if not m:
                continue
            start, brace = m.start(), m.end() - 1
            end = _body_end(text, brace)
            stub = (f"void sub_{va:08X}(void)\n{{\n"
                    f"    RECOMP_UNIMPL_FUNCTION(\"qualification: forced interpretation\", 0x{va:08X}u, {KIND_FORCED}u);\n}}")
            text = text[:start] + stub + text[end:]
            remaining.discard(va)
            replaced.append(dict(va=f"0x{va:08X}", unit=path.name))
            changed = True
        if changed:
            path.write_text(text, encoding="utf-8", newline="\n")
    if remaining:
        raise ValueError("forced interpretation names functions with no generated body: " +
                         ", ".join(f"0x{va:08X}" for va in sorted(remaining)))
    return replaced
