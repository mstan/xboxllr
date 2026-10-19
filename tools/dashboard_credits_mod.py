"""Build a private 5960 Credits asset mod from hash-checked owned archives.

Original scripts and geometry are transformed locally, never bundled here.
Theseus supplied external format/behavior references, not implementation code.
The generated files require in-dashboard visual and navigation validation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import textwrap

from dashboard_xip import Xip

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    "default.xip": "b8611df758981d50ebce1bec835cc23388ac1ba277ca21cabea8c0eca1e47c4c",
    "mainmenu5.xip": "3c3a202cd935e7840cd7d806fee190b8d9a5ddc1d01922c5e15b97cf5d0e6812",
}
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|[{}]', re.S)


def block(source: str, marker: str) -> tuple[int, int, int]:
    if source.count(marker) != 1:
        raise ValueError(f"expected one reviewed script anchor: {marker}")
    begin = source.index(marker)
    opening = source.index("{", begin)
    depth = 0
    for token in TOKENS.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return begin, opening, token.end()
    raise ValueError(f"unbalanced script block: {marker}")


def inject(source: str, marker: str, prefix: str = "", suffix: str = "") -> str:
    _, opening, end = block(source, marker)
    return source[:opening + 1] + prefix + source[opening + 1:end - 1] + suffix + source[end - 1:]


def js_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def pages_from_text(title: str, text: str) -> list[str]:
    lines = []
    for line in text.splitlines():
        lines.extend(textwrap.wrap(line.expandtabs(4), width=43) or [""])
    return [title + "\n" + "\n".join(lines[i:i + 6]) for i in range(0, len(lines), 6)]


def credit_pages(spec: dict, extra_notices: list[Path]) -> tuple[list[str], list[dict], list[int]]:
    pages = []
    sections = []
    def append(title, text):
        sections.append(len(pages))
        pages.extend(pages_from_text(title, text))
    for person in spec["people"]:
        detail = person["name"] + "\n" + person["work"]
        if person.get("url"):
            detail += "\n" + person["url"]
        append(spec["title"], detail)
    for paragraph in spec["legal"]:
        append("Legal notices", paragraph)
    sources = [ROOT / path for path in spec["notices"]] + extra_notices
    records = []
    seen = set()
    shared_text = {}
    for source in sources:
        source = source.resolve(strict=True)
        if source in seen:
            continue
        seen.add(source)
        data = source.read_bytes()
        if len(data) > 2_000_000:
            raise ValueError("notice is too large for this bounded viewer")
        text = data.decode("utf-8-sig")
        # Keep the complete notice, including long unbroken URLs. Never elide
        # license text to fit the panel; navigation covers every generated page.
        canonical = text.replace("\r\n", "\n").strip()
        if canonical not in shared_text:
            shared_text[canonical] = len(pages)
            append(source.parent.name + "/" + source.name, text)
        records.append(dict(name=source.name, path=str(source), sha256=hashlib.sha256(data).hexdigest(),
                            viewer_page=shared_text[canonical] + 1))
    # Identical license texts appear once in the viewer, while every original
    # notice remains copied verbatim and listed with its applicable component.
    append("Notice index", "\n".join(f"{Path(item['path']).parent.name}/{item['name']}: page {item['viewer_page']}"
                                      for item in records))
    return pages, records, sections


def viewer(pages: list[str], sections: list[int]) -> str:
    if not pages or len(pages) > 5000:
        raise ValueError("unsupported number of Credits/legal pages")
    out = ["\n// Independently authored XboxLLR Credits mod.\n",
           "var xllrCreditsActive;\nvar xllrCreditsPage;\n"]
    for start in range(0, len(pages), 48):
        out.append(f"function XllrCreditsText{start // 48}() {{\n")
        for index in range(start, min(start + 48, len(pages))):
            value = pages[index] + f"\n{index + 1}/{len(pages)}  Left/Right: page  Up/Down: section\nA: next  B: back"
            out.append(f"if (xllrCreditsPage == {index}) return {js_string(value)};\n")
        out.append('return "";\n}\n')
    out.append("function XllrCreditsText() {\n")
    for start in range(0, len(pages), 48):
        out.append(f"if (xllrCreditsPage < {start + 48}) return XllrCreditsText{start // 48}();\n")
    out.append('return "";\n}\n')
    out.append("""
function XllrCreditsOpen() {
    xllrCreditsPage = 0;
    xllrCreditsActive = true;
    TellUser(XllrCreditsText(), "");
}
function XllrCreditsClose() {
    xllrCreditsActive = false;
    CloseMessage();
}
function XllrCreditsPrevious() {
    if (xllrCreditsPage > 0) xllrCreditsPage = xllrCreditsPage - 1;
    theMessageInline.children[0].MessageText.text = XllrCreditsText();
}
""")
    out.append(f"""
function XllrCreditsNext() {{
    if (xllrCreditsPage < {len(pages) - 1}) xllrCreditsPage = xllrCreditsPage + 1;
    theMessageInline.children[0].MessageText.text = XllrCreditsText();
}}
""")
    for name, candidates, comparison in (("NextSection", sections[1:], "<"),
                                          ("PreviousSection", list(reversed(sections)), ">")):
        out.append(f"function XllrCredits{name}() {{\n")
        for index in candidates:
            out.append(f"if (xllrCreditsPage {comparison} {index}) {{ xllrCreditsPage = {index}; "
                       "theMessageInline.children[0].MessageText.text = XllrCreditsText(); return; }\n")
        out.append("}\n")
    return "".join(out)


def menu_geometry(source: str) -> tuple[str, dict[str, str]]:
    begin, _, end = block(source, "DEF Main_memory_ringpin_4 Transform")
    original = source[begin:end]
    names = {name: "xllrCredits_" + name for name in re.findall(r"\bDEF\s+([\w-]+)", original)}
    # Rename only DEF/USE identifiers, preserving every quoted mesh URL.
    token = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|\b(DEF|USE)\s+([\w-]+)', re.S)
    clone = token.sub(lambda m: f"{m[1]} {names.get(m[2], m[2])}" if m[1] else m[0], original)
    anchor = 'translate true text "SETTINGS"'
    if clone.count(anchor) != 1:
        raise ValueError("reviewed Settings text geometry differs")
    clone = clone.replace(anchor, 'translate false text "CREDITS"')
    return source[:end] + "\n" + clone + source[end:], names


def menu_script(source: str, names: dict[str, str], pages: list[str], sections: list[int]) -> str:
    item = names["theSettingsItem"]
    panel = names["SettingsPanelMaterial"]
    text = names["SettingsTextMaterial"]
    glow = names["game_select_pod_inner03"]
    ring = names["Main_memory_ringpin_4"]
    reset = (f"\nvar xllrMenu = theMainMenu.children[0].children[0];\nvar xllrAngle = 0.0;\n"
             f'xllrMenu.{panel}.name = "FlatSurfaces2sided";\n'
             f'xllrMenu.{text}.name = "NavType";\n'
             f"xllrMenu.{glow}.visible = false;\n")
    selected = ["if (nCurMainMenuItem == 4) {\nxllrAngle = 0.9;\nif (g_bXOnDashExists) xllrAngle = 0.75;\n",
                "xllrMenu.theMenuItems.SetRotation(0, 0, 1, xllrAngle);\n"]
    for label, inner in (("Memory", "02"), ("Music", ""), ("Online", "04"), ("Settings", "03")):
        selected.append(f'xllrMenu.{label}PanelMaterial.name = "FlatSurfaces2sided";\n')
        selected.append(f'xllrMenu.{label}TextMaterial.name = "NavType";\n')
        selected.append(f"xllrMenu.the{label}Item.SetRotation(0, 1, 0, xllrAngle);\n")
        selected.append(f"xllrMenu.game_select_pod_inner{inner}.visible = false;\n")
    selected.extend((f'xllrMenu.{panel}.name = "GameHilite";\n',
                     f'xllrMenu.{text}.name = "HilightedType";\n',
                     f"xllrMenu.{glow}.visible = true;\n",
                     f"xllrMenu.{item}.SetRotation(0, 1, 0, xllrAngle);\nreturn;\n}}\n"))
    follow = f"""
xllrAngle = 0.0;
if (g_bXOnDashExists) xllrAngle = (nCurMainMenuItem - 1) * 0.25;
else {{
    if (nCurMainMenuItem == 0) xllrAngle = -0.45;
    if (nCurMainMenuItem == 3) xllrAngle = 0.45;
}}
xllrMenu.{item}.SetRotation(0, 1, 0, xllrAngle);
"""
    source = inject(source, "function UpdateMainMenu()", reset + "".join(selected), follow)
    begin, _, end = block(source, "DEF theMainMenu Level")
    level = source[begin:end]
    level = inject(level, "function OnADown()", "\nif (nCurMainMenuItem == 4) { PlaySoundA(); XllrCreditsOpen(); return; }\n")
    if level.count("nCurMainMenuItem < 3") != 1:
        raise ValueError("reviewed menu navigation bound differs")
    level = level.replace("nCurMainMenuItem < 3", "nCurMainMenuItem < 4")
    # Place the clone from the original onLoad owner of `c`, before the menu
    # selection/highlight update. Never add a global reference to that local.
    begin_load, _, end_load = block(level, "function onLoad()")
    load = level[begin_load:end_load]
    anchor = "UpdateMainMenu();"
    if load.count(anchor) != 1:
        raise ValueError("reviewed first-load menu update differs")
    # The retail class compiler accepts declarations here, but rejects class
    # member initializers. Initialize the modal state under the real onLoad.
    placement = ("xllrCreditsActive = false;\nxllrCreditsPage = 0;\n"
                 f"c.{ring}.SetTranslation(57.43, -43.38, 101.5);\n")
    load = load.replace(anchor, placement + anchor)
    level = level[:begin_load] + load + level[end_load:]
    source = source[:begin] + level + source[end:]
    begin, _, end = block(source, "DEF theMessageJoystick Joystick")
    joystick = source[begin:end]
    for event, action in (("OnADown", "Next"), ("OnBDown", "Close"), ("OnMoveLeft", "Previous"), ("OnMoveRight", "Next")):
        joystick = inject(joystick, f"function {event}()", f"\nif (xllrCreditsActive) {{ XllrCredits{action}(); return; }}\n")
    # These events are new in the message controller; dashboard navigation
    # remains handled by its original owner whenever this modal is absent.
    joystick = inject(joystick, "DEF theMessageJoystick Joystick", suffix="""
function OnMoveDown() { if (xllrCreditsActive) XllrCreditsNextSection(); }
function OnMoveUp() { if (xllrCreditsActive) XllrCreditsPreviousSection(); }
""")
    return source[:begin] + joystick + source[end:] + viewer(pages, sections)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--credits", type=Path, default=ROOT / "pilots/dashboard/credits.json")
    parser.add_argument("--notice", type=Path, action="append", default=[])
    parser.add_argument("--notice-tree", type=Path, action="append", default=[])
    parser.add_argument("--native-xbe", type=Path,
                        help="Generate a native section-data registry from the unchanged owned XBE")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("choose a new output directory; originals are never changed")
    inputs = {}
    for name, sha in PINS.items():
        data = (args.assets / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != sha:
            parser.error(f"{name} differs from the reviewed 5960 revision")
        inputs[name] = Xip(data)
    spec = json.loads(args.credits.read_text(encoding="utf-8"))
    extra_notices = list(args.notice)
    for directory in [ROOT / "LICENSES"] + args.notice_tree:
        if not directory.is_dir():
            parser.error(f"missing notice directory: {directory}")
        extra_notices.extend(path for path in sorted(directory.rglob("*"))
                             if path.is_file() and any(word in path.name.upper() for word in ("LICENSE", "COPYING", "NOTICE")))
    pages, notices, sections = credit_pages(spec, extra_notices)
    geometry, names = menu_geometry(inputs["mainmenu5.xip"].read("default.xap").decode("utf-8"))
    script = menu_script(inputs["default.xip"].read("default.xap").decode("utf-8"), names, pages, sections)
    outputs = {
        "default.xip": inputs["default.xip"].replace("default.xap", script.encode("utf-8")),
        "mainmenu5.xip": inputs["mainmenu5.xip"].replace("default.xap", geometry.encode("utf-8")),
    }
    native = None
    if args.native_xbe:
        from dashboard_integrity import XBE_SHA256, Xbe, replace_records
        from native_section_mod import section_registry
        original_xbe = args.native_xbe.read_bytes()
        if hashlib.sha256(original_xbe).hexdigest() != XBE_SHA256:
            parser.error("native XBE differs from the reviewed original dashboard")
        section = Xbe(original_xbe).named("XIPS")
        table = replace_records(original_xbe[section.raw:section.raw+section.length],
            {name: ((args.assets/name).read_bytes(), payload) for name,payload in outputs.items()})
        native = section_registry(original_xbe, [("XIPS", table, 0x1ffc)], "xboxllr.dashboard.credits")
    args.output.mkdir(parents=True)
    records = []
    for name, data in outputs.items():
        destination = args.output / name
        with destination.open("xb") as handle:
            handle.write(data)
        if destination.read_bytes() != data:
            raise ValueError("mod output readback differs")
        records.append(dict(path=name, source_sha256=PINS[name], sha256=hashlib.sha256(data).hexdigest()))
    for index, notice in enumerate(notices):
        raw = Path(notice["path"]).read_bytes()
        if hashlib.sha256(raw).hexdigest() != notice["sha256"]:
            raise ValueError("notice changed during generation")
        relative = Path("licenses") / f"{index:04d}-{notice['name']}"
        (args.output / relative).parent.mkdir(exist_ok=True)
        (args.output / relative).write_bytes(raw)
        notice["copy"] = relative.as_posix()
    for name, sha in PINS.items():
        if hashlib.sha256((args.assets / name).read_bytes()).hexdigest() != sha:
            raise ValueError("input archive changed during generation")
    manifest = dict(schema="xboxllr.dashboard.asset-mod.v1", private_guest_output=True,
                    id="xboxllr.dashboard.credits", dashboard_build=5960, credits_pages=len(pages),
                    validation="generated and read back; in-dashboard visual/input validation pending",
                    inputs_unchanged=True, files=records, notices=notices, sections=sections,
                    source_reference=dict(repository="https://github.com/MrMilenko/Theseus",
                                          revision="5fc7ddddd03b4b5a7e6233769ad6a8fe46009965",
                                          role="external behavior/reference; no implementation incorporated"))
    if native:
        source, receipt = native
        registry = args.output / "sections.aot-private.c"
        registry.write_text(source, encoding="utf-8", newline="\n")
        (args.output / "sections.aot-private.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
        manifest["native_section_mod"] = dict(registry=registry.name,sha256=receipt["registry_sha256"],
                                              original_xbe_sha256=receipt["source_xbe_sha256"])
    (args.output / "mod.aot-private.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated private Credits mod: {len(pages)} pages; dashboard validation pending")


if __name__ == "__main__":
    main()
