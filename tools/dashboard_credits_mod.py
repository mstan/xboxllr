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
from dashboard_credits_viewer import viewer, document_lines

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
    for line in text.splitlines() or [""]:
        lines.extend(textwrap.wrap(line.expandtabs(4), width=24) or [""])
    return [title + "\n" + "\n".join(lines[i:i + 6]) for i in range(0, len(lines), 6)]


def notice_title(source: Path) -> str:
    names = {ROOT / "LICENSE": "xboxlle license", ROOT / "NOTICE": "xboxlle notices",
             ROOT / "LICENSE-SCOPE.md": "License scope",
             ROOT / "THIRD_PARTY_ATTRIBUTION.md": "Third-party attribution",
             ROOT / "pilots/native-launcher/assets/NOTICE.md": "Launcher artwork"}
    return names.get(source, source.parent.name.replace("-", " ").replace("_", " "))


def credit_pages(spec: dict, extra_notices: list[Path]) -> tuple[list[str], list[dict], list[int], int]:
    pages = []
    sections = []
    def append(title, text):
        sections.append(len(pages))
        pages.extend(pages_from_text(title, text))
    if not spec["people"]:
        raise ValueError("Credits requires its project lead")
    group = None
    links = []
    for index, person in enumerate(spec["people"]):
        detail = person["name"] + "\n" + person["work"]
        if person.get("url"):
            links.append(person["name"] + "\n" + person["url"])
        heading = spec["title"] if index == 0 else person.get("section", "Special thanks")
        if heading != group:
            append(heading, detail)
            group = heading
        else:
            pages.extend(pages_from_text(heading, "\n" + detail))
    licenses_page = len(pages)
    append("About this build", "\n\n".join(spec["legal"]) or "Full third-party notices follow.")
    if links:
        append("Project links", "\n\n".join(links))
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
            append(notice_title(source), text)
        records.append(dict(name=source.name, path=str(source), sha256=hashlib.sha256(data).hexdigest(),
                            title=notice_title(source), viewer_document="licenses",
                            viewer_page=shared_text[canonical] + 1))
    # Identical license texts appear once in the viewer, while every original
    # notice remains copied verbatim and listed with its applicable component.
    _, line_starts = document_lines(pages, sections)
    by_page = dict(zip(sections, line_starts))
    for item in records:
        item['viewer_line'] = by_page[item['viewer_page'] - 1] - by_page[licenses_page] + 1
    append("Notice index", "\n".join(f"{item['title']}: line {item['viewer_line']}"
                                      for item in records))
    return pages, records, sections, licenses_page


def main_script(source: str, pages: list[str], sections: list[int], licenses_page: int | None = None) -> str:
    """Add the document owner without changing Home choices or geometry."""
    begin, _, end = block(source, "DEF theMainMenu Level")
    level = source[begin:end]
    begin_load, _, end_load = block(level, "function onLoad()")
    load = level[begin_load:end_load]
    anchor = "UpdateMainMenu();"
    if load.count(anchor) != 1:
        raise ValueError("reviewed first-load menu update differs")
    # The retail class compiler accepts declarations here, but rejects class
    # member initializers. Initialize the modal state under the real onLoad.
    placement = "xllrCreditsActive = false;\nxllrCreditsLine = 0;\n"
    load = load.replace(anchor, placement + anchor)
    level = level[:begin_load] + load + level[end_load:]
    source = source[:begin] + level + source[end:]
    return source + viewer(pages, sections, licenses_page)


def native_panel_script(source: str) -> str:
    """Route Credits through the existing System Info Level, camera and meshes.

    Only authored guards/assignments are inserted in the private owned script.
    No original script or geometry is distributed by this tool.
    """
    begin, _, end = block(source, "function StartSettingsMenu()")
    settings = source[begin:end]
    anchor = re.compile(r'(sSettingsMenu\[index\] = theTranslator\.Translate\("SYSTEM INFO"\);'
                        r'\s*index = index \+ 1;)')
    settings, count = anchor.subn(r'\1\n    sSettingsMenu[index] = "Credits";\n    index = index + 1;', settings)
    if count != 1:
        raise ValueError("reviewed System Info Settings entry differs")
    source = source[:begin] + settings + source[end:]
    source = inject(source, "function MenuIcon()", """
if (sSettingsElements[nCurSettingsMenuItem] == "Credits") {
    ShowIcon(theSettingsMenu.children[0].children[0].ConsoleIcon);
    return;
}
""")
    source = inject(source, "function StartPanelMenu()", """
var xllrPanel = theSetPanelMenu.children[0].children[0];
xllrPanel.PanelHeading.font = "heading";
xllrPanel.S_Home_text_games01.SetScale(1, 1, 1);
xllrPanel.QuestionText.width = 10;
xllrPanel.QuestionText.height = 8;
xllrPanel.QuestionText.scrollRate = 0.1;
xllrPanel.QuestionText.scrollDelay = 5;
""", """
if (strCurMenu == "XLLR CREDITS") {
    xllrPanel.PanelHeading.font = "body";
    xllrPanel.ButtonGroup.visible = false;
    xllrPanel.ConsoleIcon.visible = true;
    xllrPanel.Group07.visible = false;
    xllrPanel.Group02_supports.SetTranslation(-6.15, 0.3773, 61.650002);
    xllrPanel.S_Home_text_games01.SetScale(0.72, 0.72, 0.72);
    xllrPanel.QuestionText.width = 13.8;
    xllrPanel.QuestionText.height = 11.1;
    xllrPanel.QuestionText.scrollRate = 0;
    xllrPanel.QuestionText.scrollDelay = 1000000;
    xllrCreditsBody = xllrPanel.QuestionText;
    xllrCreditsPosition = xllrPanel.PanelHeading;
    XllrCreditsRefresh();
}
if (strCurMenu == "XLLR OFFLINE") {
    xllrPanel.PanelHeading.text = "OFFLINE";
    xllrPanel.QuestionText.text = "This dashboard is\\rrunning offline.\\r\\rXbox Live is unavailable.";
    xllrPanel.ButtonGroup.visible = false;
    xllrPanel.ConsoleIcon.visible = true;
    xllrPanel.Group07.visible = false;
    xllrPanel.Group02_supports.SetTranslation(-6.15, 0.3773, 61.650002);
}
""")
    begin, _, end = block(source, "control DEF thePanelJoystick Joystick")
    joystick = source[begin:end]
    for event, action in (("OnADown", "Toggle"), ("OnMoveDown", "Next"), ("OnMoveUp", "Previous"),
                          ("OnMoveLeft", "PreviousSection"), ("OnMoveRight", "NextSection")):
        joystick = inject(joystick, f"function {event}()",
                          f'\nif (strCurMenu == "XLLR OFFLINE") return;\n'
                          f'if (strCurMenu == "XLLR CREDITS") {{ XllrCredits{action}(); return; }}\n')
    joystick = inject(joystick, "function OnBDown()", """
if (strCurMenu == "XLLR CREDITS") {
    XllrCreditsClose();
    PlaySoundB();
    strPrevMenu = "XLLR CREDITS";
    strCurMenu = "SETTINGS";
    theSettingsMenu.GoBackTo();
    theSettingsSubMenuOut.Play();
    return;
}
""")
    source = source[:begin] + joystick + source[end:]
    begin, _, end = block(source, "DEF theSettingsMenu Level")
    settings = inject(source[begin:end], "function OnADown()", """
if (sSettingsElements[nCurSettingsMenuItem] == "Credits") {
    PlaySoundA();
    strPrevMenu = "SETTINGS";
    XllrCreditsOpen();
    strCurMenu = "XLLR CREDITS";
    theSetPanelMenu.GoTo();
    theSettingsSubMenuIn.Play();
    return;
}
if ((sSettingsElements[nCurSettingsMenuItem] == theTranslator.Translate("NETWORK")) +
    (sSettingsElements[nCurSettingsMenuItem] == theTranslator.Translate("LIVE TODAY"))) {
    PlaySoundA();
    strCurMenu = "XLLR OFFLINE";
    theSetPanelMenu.GoTo();
    return;
}
""")
    return source[:begin] + settings + source[end:]


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
    pages, notices, sections, licenses_page = credit_pages(spec, extra_notices)
    script = main_script(inputs["default.xip"].read("default.xap").decode("utf-8"), pages, sections, licenses_page)
    main_archive = Xip(inputs["default.xip"].replace("default.xap", script.encode("utf-8")))
    settings = inputs["default.xip"].read("settings3.xap").decode("latin1")
    outputs = {
        "default.xip": main_archive.replace("settings3.xap", native_panel_script(settings).encode("latin1")),
        # Restore the complete original Home asset when overlaying an older
        # Credits mod; do not retain its cloned Home item or transformed mesh.
        "mainmenu5.xip": (args.assets / "mainmenu5.xip").read_bytes(),
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
                    credits_lines=len(document_lines(pages,sections)[0]), viewer="native-system-info-two-documents",
                    licenses_page=licenses_page, visible_title="Credits", project_name=spec["title"],
                    entry_point="settings-after-system-info", home_menu="original-unmodified",
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
    print(f"Generated private scrolling Credits mod: {manifest['credits_lines']} lines; dashboard validation pending")


if __name__ == "__main__":
    main()
