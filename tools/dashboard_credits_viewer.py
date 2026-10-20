"""Independently authored scrolling XAP Credits reader; no dashboard code copied."""
from __future__ import annotations

import json

VISIBLE_LINES = 14
CHUNK_LINES = 8  # Retail XAP limits individual string literals to 1023 characters.


def document_lines(pages: list[str], sections: list[int]) -> tuple[list[str], list[int]]:
    if not pages or len(pages) > 5000 or not sections or sections[0] != 0 or sections != sorted(set(sections)):
        raise ValueError("invalid Credits document sections")
    if any(type(i) is not int or not 0 <= i < len(pages) for i in sections):
        raise ValueError("Credits section outside document")
    lines, starts = [], []
    for index, page in enumerate(pages):
        rows = page.splitlines() or [""]
        if index in sections:
            if lines:
                lines.append("")
            starts.append(len(lines))
            lines.extend((rows[0], ""))
        elif len(rows) == 1:
            lines.append(rows[0])
        lines.extend(rows[1:])
    return lines, starts


def viewer(pages: list[str], sections: list[int]) -> str:
    lines, starts = document_lines(pages, sections)
    chunks = ["\n".join(lines[i:i + CHUNK_LINES]) + "\n" for i in range(0, len(lines), CHUNK_LINES)]
    limit = max(0, len(lines) - VISIBLE_LINES)
    out = ["\n// Independently authored XboxLLR scrolling Credits reader.\n",
           "var xllrCreditsActive;\nvar xllrCreditsLine;\n"]
    for start in range(0, len(chunks), 48):
        out.append(f"function XllrCreditsChunk{start // 48}(n) {{\n")
        for index in range(start, min(start + 48, len(chunks))):
            out.append(f"if (n == {index}) return {json.dumps(chunks[index], ensure_ascii=True)};\n")
        out.append('return "";\n}\n')
    out.append("function XllrCreditsChunk(n) {\n")
    for start in range(0, len(chunks), 48):
        out.append(f"if (n < {start + 48}) return XllrCreditsChunk{start // 48}(n);\n")
    out.append('return "";\n}\n')
    out.append(f"""
function XllrCreditsText() {{
    var chunk = Math.floor(xllrCreditsLine / {CHUNK_LINES});
    var text = XllrCreditsChunk(chunk) + XllrCreditsChunk(chunk + 1) + XllrCreditsChunk(chunk + 2);
    var start = 0;
    for (var i = 0; i < xllrCreditsLine - chunk * {CHUNK_LINES}; i = i + 1)
        start = text.indexOf("\\n", start) + 1;
    var end = start;
    for (i = 0; i < {VISIBLE_LINES}; i = i + 1) {{
        end = text.indexOf("\\n", end);
        if (end < 0) return text.substring(start, 1000000);
        end = end + 1;
    }}
    return text.substring(start, end - 1);
}}
function XllrCreditsRefresh() {{
    xllrCreditsBody.text = XllrCreditsText();
    xllrCreditsPosition.text = Math.round(xllrCreditsLine * 100 / {max(1, limit)}) + "%";
}}
function XllrCreditsOpen() {{
    xllrCreditsLine = 0;
    xllrCreditsActive = true;
    TellUser("", "");
    theMessageLayer.transparency = 1;
    XllrCreditsRefresh();
    xllrCreditsLayer.transparency = 0;
}}
function XllrCreditsClose() {{
    xllrCreditsActive = false;
    xllrCreditsLayer.transparency = 1;
    CloseMessage();
}}
function XllrCreditsPrevious() {{
    if (xllrCreditsLine > 0) xllrCreditsLine = xllrCreditsLine - 1;
    XllrCreditsRefresh();
}}
function XllrCreditsNext() {{
    if (xllrCreditsLine < {limit}) xllrCreditsLine = xllrCreditsLine + 1;
    XllrCreditsRefresh();
}}
function XllrCreditsPageDown() {{
    xllrCreditsLine = xllrCreditsLine + {VISIBLE_LINES - 1};
    if (xllrCreditsLine > {limit}) xllrCreditsLine = {limit};
    XllrCreditsRefresh();
}}
""")
    for name, candidates, comparison in (("NextSection", starts[1:], "<"),
                                          ("PreviousSection", list(reversed(starts)), ">")):
        out.append(f"function XllrCredits{name}() {{\n")
        for index in candidates:
            bounded = min(index, limit)
            out.append(f"if (xllrCreditsLine {comparison} {bounded}) {{ xllrCreditsLine = {bounded}; "
                       "XllrCreditsRefresh(); return; }\n")
        out.append("}\n")
    return "".join(out)


def viewer_geometry() -> str:
    # Layer owns its opaque fade quad and a separate camera. Text contains
    # only the current line window, avoiding a whole-license glyph mesh.
    return r'''
DEF xllrCreditsLayer Layer {
    fade 0.08
    transparency 1
    viewpoint Viewpoint { fieldOfView 0.6024 position 0 0 6 }
    children [
        Transform {
            translation -2.15 1.45 0
            scale 0.16 0.16 0.16
            children [ Shape {
                appearance Appearance { material MaxMaterial { name "CellEgg/Partsz" } }
                geometry Text { font "Body" translate false text "Credits and licenses" }
            } ]
        }
        Transform {
            translation -2.15 1.1 0
            scale 0.12 0.12 0.12
            children [ Shape {
                appearance Appearance { material MaxMaterial { name "CellEgg/Partsz" } }
                geometry DEF xllrCreditsBody Text { font "Body" translate false width 36 height 21 }
            } ]
        }
        Transform {
            translation -2.15 -1.56 0
            scale 0.085 0.085 0.085
            children [ Shape {
                appearance Appearance { material MaxMaterial { name "CellEgg/Partsz" } }
                geometry Text { font "Body" translate false text "Up/Down: scroll   Left/Right: section   A: page   B: back" }
            } ]
        }
        Transform {
            translation 1.8 -1.56 0
            scale 0.085 0.085 0.085
            children [ Shape {
                appearance Appearance { material MaxMaterial { name "CellEgg/Partsz" } }
                geometry DEF xllrCreditsPosition Text { font "Body" translate false text "0%" }
            } ]
        }
    ]
}
'''
