"""Authored scrolling document controller for the owner's native System Info panel."""
from __future__ import annotations

import json

VISIBLE_LINES = 8  # Leave two native text rows for the stable controller hint.
ROWS_PER_FUNCTION = 48  # Bound each retail script dispatch function.


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


def viewer(pages: list[str], sections: list[int], licenses_page: int | None = None) -> str:
    lines, starts = document_lines(pages, sections)
    if licenses_page is not None and (licenses_page not in sections or licenses_page == 0):
        raise ValueError("Licenses must start at a separate, nonempty document section")
    license_line = starts[sections.index(licenses_page)] if licenses_page is not None else len(lines)
    credits_limit = max(0, license_line - VISIBLE_LINES)
    licenses_limit = max(license_line, len(lines) - VISIBLE_LINES)
    # The native rendered string window diverged from the standalone JS check.
    # Select bounded literal rows instead of depending on that string-method
    # behavior. CR is the native Text node's line break.
    out = ["\n// Independently authored xboxlle native Credits and Licenses reader.\n",
           "var xllrCreditsActive;\nvar xllrCreditsLine;\nvar xllrCreditsLegal;\n"
           "var xllrCreditsBody;\nvar xllrCreditsPosition;\n"]
    for start in range(0, len(lines), ROWS_PER_FUNCTION):
        out.append(f"function XllrCreditsRows{start // ROWS_PER_FUNCTION}(n) {{\n")
        for index in range(start, min(start + ROWS_PER_FUNCTION, len(lines))):
            literal = json.dumps(lines[index], ensure_ascii=True)
            if len(literal) > 1023:
                raise ValueError("Credits row exceeds the retail string-literal limit")
            out.append(f"if (n == {index}) return {literal};\n")
        out.append('return "";\n}\n')
    out.append("function XllrCreditsRow(n) {\n")
    for start in range(0, len(lines), ROWS_PER_FUNCTION):
        out.append(f"if (n < {start + ROWS_PER_FUNCTION}) return XllrCreditsRows{start // ROWS_PER_FUNCTION}(n);\n")
    out.append('return "";\n}\n')
    toggle = "XllrCreditsPageDown();" if licenses_page is None else f"""
    if (xllrCreditsLegal) {{ xllrCreditsLegal = false; xllrCreditsLine = 0; }}
    else {{ xllrCreditsLegal = true; xllrCreditsLine = {license_line}; }}
    XllrCreditsRefresh();
"""
    hint = "A  Licenses   B  Back" if licenses_page is not None else "A  Page   B  Back"
    out.append(f"""
function XllrCreditsFirst() {{
    if (xllrCreditsLegal) return {license_line};
    return 0;
}}
function XllrCreditsEnd() {{
    if (xllrCreditsLegal) return {len(lines)};
    return {license_line};
}}
function XllrCreditsLimit() {{
    if (xllrCreditsLegal) return {licenses_limit};
    return {credits_limit};
}}
""")
    out.append("function XllrCreditsText() {\nvar at = xllrCreditsLine;\n"
               "var last = XllrCreditsEnd();\nvar text = XllrCreditsRow(at);\n")
    for offset in range(1, VISIBLE_LINES):
        out.append(f'if (at + {offset} < last) text = text + "\\r" + XllrCreditsRow(at + {offset});\n')
    out.append("return text;\n}\n")
    out.append(f"""
function XllrCreditsRefresh() {{
    var hint = {json.dumps(hint)};
    if (xllrCreditsLegal) hint = "A  Credits   B  Back";
    xllrCreditsBody.text = XllrCreditsText() + "\\r\\r" + hint;
    xllrCreditsPosition.text = "Credits";
    if (xllrCreditsLegal) xllrCreditsPosition.text = "Licenses";
}}
function XllrCreditsOpen() {{
    xllrCreditsLine = 0;
    xllrCreditsLegal = false;
    xllrCreditsActive = true;
}}
function XllrCreditsClose() {{
    xllrCreditsActive = false;
}}
function XllrCreditsPrevious() {{
    if (xllrCreditsLine > XllrCreditsFirst()) xllrCreditsLine = xllrCreditsLine - 1;
    XllrCreditsRefresh();
}}
function XllrCreditsNext() {{
    if (xllrCreditsLine < XllrCreditsLimit()) xllrCreditsLine = xllrCreditsLine + 1;
    XllrCreditsRefresh();
}}
function XllrCreditsPageDown() {{
    xllrCreditsLine = xllrCreditsLine + {VISIBLE_LINES - 1};
    if (xllrCreditsLine > XllrCreditsLimit()) xllrCreditsLine = XllrCreditsLimit();
    XllrCreditsRefresh();
}}
function XllrCreditsToggle() {{
    {toggle}
}}
""")
    for name, candidates, comparison in (("NextSection", starts[1:], "<"),
                                          ("PreviousSection", list(reversed(starts)), ">")):
        out.append(f"function XllrCredits{name}() {{\n"
                   "var first = XllrCreditsFirst();\nvar last = XllrCreditsEnd();\n"
                   "var limit = XllrCreditsLimit();\nvar target;\n")
        for index in candidates:
            out.append(f"if ({index} >= first) {{ if ({index} < last) {{\n"
                       f"target = {index}; if (target > limit) target = limit;\n"
                       f"if (xllrCreditsLine {comparison} target) {{ xllrCreditsLine = target; "
                       "XllrCreditsRefresh(); return; }\n} }\n")
        out.append("}\n")
    return "".join(out)
