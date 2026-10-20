"""Authored scripts exercise mod navigation; retail XAP/VM validation is separate."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dashboard_credits_mod import block, credit_pages, menu_geometry, menu_script, native_panel_script, viewer
from dashboard_credits_viewer import document_lines, VISIBLE_LINES


GEOMETRY = '''
DEF Main_memory_ringpin_4 Transform {
  children [
    DEF theSettingsItem Transform { }
    DEF SettingsPanelMaterial Material { }
    DEF SettingsTextMaterial Material { }
    DEF game_select_pod_inner03 Shape { geometry USE theSettingsItem }
    HeadingText { translate true text "SETTINGS" }
    Mesh { url "theSettingsItem.xm" }
  ]
}
'''
SCRIPT = '''
function UpdateMainMenu() { originalMenuUpdate(); }
DEF theMainMenu Level {
  function OnADown() { originalSelect(); }
  function OnMoveDown() { if (nCurMainMenuItem < 3) nCurMainMenuItem = nCurMainMenuItem + 1; }
  function onLoad() { var c = theMainMenu.children[0].children[0]; UpdateMainMenu(); }
}
DEF theMessageJoystick Joystick {
  function OnADown() { originalMessageA(); }
  function OnBDown() { originalMessageB(); }
  function OnMoveLeft() { originalMessageLeft(); }
  function OnMoveRight() { originalMessageRight(); }
}
'''
PANEL_SCRIPT = '''
function StartSettingsMenu() { originalSettings(); }
function StartPanelMenu() { originalPanel(); }
DEF theSettingsMenu Level {
    function OnADown() { originalSelect(); }
}
control DEF thePanelJoystick Joystick {
  function OnADown() { originalMessageA(); }
  function OnBDown() { originalMessageB(); }
  function OnMoveLeft() { originalMessageLeft(); }
  function OnMoveRight() { originalMessageRight(); }
  function OnMoveUp() { originalMessageUp(); }
  function OnMoveDown() { originalMessageDown(); }
}
'''


def function(source, marker):
    begin, _, end = block(source, marker)
    return source[begin:end]


class CreditsTests(unittest.TestCase):
    def test_modal_state_uses_onload_not_class_member_initializers(self):
        _, names = menu_geometry(GEOMETRY)
        generated = menu_script(SCRIPT, names, ['Credits'], [0])
        self.assertIn('var xllrCreditsActive;', generated)
        self.assertIn('var xllrCreditsLine;', generated)
        self.assertNotIn('var xllrCreditsActive =', generated)
        self.assertNotIn('var xllrCreditsLine =', generated)
        load = function(generated, 'function onLoad()')
        self.assertLess(load.index('xllrCreditsActive = false;'), load.index('UpdateMainMenu();'))
        self.assertLess(load.index('xllrCreditsLine = 0;'), load.index('UpdateMainMenu();'))

    def test_geometry_keeps_original_and_mesh_urls(self):
        modified, names = menu_geometry(GEOMETRY)
        self.assertTrue(modified.startswith(GEOMETRY.rstrip()))
        self.assertEqual(modified.count('url "theSettingsItem.xm"'), 2)
        self.assertIn("USE " + names["theSettingsItem"], modified)
        self.assertEqual(modified.count('text "CREDITS"'), 1)
        self.assertEqual(modified.count('text "SETTINGS"'), 1)
        with self.assertRaises(ValueError):
            menu_geometry(GEOMETRY + GEOMETRY)

    def test_complete_notices_deduplicate_without_losing_components(self):
        with tempfile.TemporaryDirectory() as folder:
            paths = [Path(folder) / name for name in ("first-LICENSE", "second-LICENSE")]
            text = "Copyright Test Author\n\n" + "All-original-notice-text " * 40
            for path in paths:
                path.write_text(text, encoding="utf-8")
            spec = dict(title="Credits", people=[dict(name="Matthew Stanley", work="Development",
                                                    url="https://example.invalid/project")],
                        legal=[], notices=[])
            pages, notices, sections = credit_pages(spec, paths)
            self.assertIn("Matthew Stanley", pages[0])
            self.assertIn("https://example.invalid/project", "".join(pages[0].split()))
            self.assertEqual(len(notices), 2)
            self.assertEqual(notices[0]["viewer_page"], notices[1]["viewer_page"])
            self.assertEqual(len(sections), 3)  # person, shared notice, complete index
            first = notices[0]["viewer_page"] - 1
            visible = " ".join(" ".join(page.splitlines()[1:]) for page in pages[first:sections[-1]])
            self.assertEqual("".join(visible.split()), "".join(text.split()))
            self.assertIn("second-LICENSE", "".join(pages[-1].split()))

    @unittest.skipUnless(shutil.which("node"), "Node is optional for generated-script logic checks")
    def test_generated_controller_logic_and_menu_highlights(self):
        _, names = menu_geometry(GEOMETRY)
        pages = ["Credits\nMatthew Stanley\n" + "\n".join(f"First credit line {i}" for i in range(40)),
                 "Credits\nOther credits\n" + "\n".join(f"Second credit line {i}" for i in range(40)),
                 "License one\n" + "\n".join(f"License A line {i}" for i in range(40)),
                 "License one\ncontinued", "License two\n" + "\n".join(f"License B line {i}" for i in range(40))]
        generated = menu_script(SCRIPT, names, pages, [0, 1, 2, 4])
        menu = function(generated, "function UpdateMainMenu()")
        load = function(generated, "function onLoad()")
        self.assertLess(load.index(".SetTranslation("), load.index("UpdateMainMenu();"))
        begin, _, end = block(generated, "DEF theMainMenu Level")
        select = function(generated[begin:end], "function OnADown()")
        panel = native_panel_script(PANEL_SCRIPT)
        begin, _, end = block(panel, "control DEF thePanelJoystick Joystick")
        message = panel[begin:end]
        handlers = "\n".join(function(message, "function " + event + "()")
                             for event in ("OnADown", "OnBDown", "OnMoveLeft", "OnMoveRight",
                                           "OnMoveUp", "OnMoveDown"))
        program = r'''
const assert = require('node:assert/strict');
function node() { return { name: '', visible: true, SetRotation(...value) { this.rotation = value; } }; }
var menuNodes = new Proxy({}, { get(target, key) { return target[key] ??= node(); } });
var closes = 0;
var theMainMenu = { children: [{ children: [menuNodes] }], GoBackTo() { closes++; } };
var theMessageInline = { children: [{ MessageText: { text: '' } }] };
var panelNodes = new Proxy({}, { get(target, key) { return target[key] ??= {...node(), SetScale(...s) {this.scale=s;}, SetTranslation(...t) {this.position=t;}}; } });
var theSetPanelMenu = {children:[{children:[panelNodes]}], GoTo() {StartPanelMenu();}};
var theSettingsMenuOut = {Play() {}};
var strCurMenu='SETTINGS', strPrevMenu='SETTINGS';
var nCurMainMenuItem = 0, g_bXOnDashExists = true;
var originalUpdates = 0, originalSelections = 0, originalMessages = 0, settingsCalls=0, panelCalls=0;
function originalMenuUpdate() { originalUpdates++; }
function originalSelect() { originalSelections++; }
function originalMessageA() { originalMessages++; }
function originalMessageB() { originalMessages++; }
function originalMessageLeft() { originalMessages++; }
function originalMessageRight() { originalMessages++; }
function originalMessageUp() { originalMessages++; }
function originalMessageDown() { originalMessages++; }
function originalSettings() {settingsCalls++;}
function originalPanel() {panelCalls++;}
function GoToSettings() {StartSettingsMenu();}
function PlaySoundA() {}
function TellUser(text) { theMessageInline.children[0].MessageText.text = text; }
function CloseMessage() { closes++; }
'''
        lines, starts = document_lines(pages, [0, 1, 2, 4])
        program += 'var expectedLines = ' + json.dumps(lines) + ';\n'
        program += 'var expectedStarts = ' + json.dumps(starts) + ';\n'
        program += 'var visibleLines = ' + str(VISIBLE_LINES) + ';\n'
        program += viewer(pages, [0, 1, 2, 4]) + menu + "\nfunction select() {\n" + select + "; OnADown();\n}\n"
        program += function(panel, 'function StartSettingsMenu()') + '\n' + function(panel, 'function StartPanelMenu()') + '\n'
        program += "\nfunction controller(event) {\n" + handlers + "; eval(event + '()');\n}\n"
        program += r'''
for (var online of [true, false]) {
    g_bXOnDashExists = online;
    nCurMainMenuItem = 4; UpdateMainMenu();
    assert.equal(menuNodes.xllrCredits_game_select_pod_inner03.visible, true);
    assert.equal(menuNodes.xllrCredits_SettingsPanelMaterial.name, 'GameHilite');
    assert.equal(menuNodes.game_select_pod_inner.visible, false);
    assert.equal(menuNodes.theMenuItems.rotation[3], online ? 0.75 : 0.9);
    select(); assert.equal(xllrCreditsLine, 0);
    assert.ok(xllrCreditsBody.text.includes('Matthew Stanley'));
    assert.equal(strCurMenu, 'XLLR CREDITS');
    assert.equal(xllrCreditsBody, panelNodes.QuestionText);
    assert.equal(xllrCreditsPosition, panelNodes.PanelHeading);
    assert.equal(panelNodes.ConsoleIcon.visible, true);
    assert.equal(panelNodes.ButtonGroup.visible, false);
    controller('OnMoveUp'); assert.equal(xllrCreditsLine, 0);
    controller('OnMoveDown'); assert.equal(xllrCreditsLine, 1);
    controller('OnMoveUp'); assert.equal(xllrCreditsLine, 0);
    controller('OnMoveRight'); assert.equal(xllrCreditsLine, expectedStarts[1]);
    controller('OnMoveLeft'); assert.equal(xllrCreditsLine, 0);
    controller('OnADown'); assert.equal(xllrCreditsLine, visibleLines - 1);
    for (var i = 0; i <= expectedLines.length - visibleLines; i++) {
        xllrCreditsLine = i;
        XllrCreditsRefresh();
        assert.equal(xllrCreditsBody.text, expectedLines.slice(i, i + visibleLines).join('\r'));
    }
    controller('OnMoveDown'); assert.equal(xllrCreditsLine, expectedLines.length - visibleLines);
    controller('OnADown'); assert.equal(xllrCreditsLine, expectedLines.length - visibleLines);
    assert.equal(xllrCreditsPosition.text, 'CREDITS 100%');
    controller('OnBDown'); assert.equal(xllrCreditsActive, false); assert.equal(strCurMenu, 'SETTINGS');
    StartPanelMenu(); assert.deepEqual(panelNodes.S_Home_text_games01.scale,[1,1,1]);
    assert.equal(panelNodes.QuestionText.width,10); assert.equal(panelNodes.QuestionText.scrollRate,0.1);
    assert.equal(nCurMainMenuItem, 4);
    nCurMainMenuItem = 0; UpdateMainMenu(); select();
    assert.equal(menuNodes.xllrCredits_game_select_pod_inner03.visible, false);
    controller('OnADown'); controller('OnBDown');
}
assert.equal(originalUpdates, 2); assert.equal(originalSelections, 2);
assert.equal(originalMessages, 4); assert.equal(closes, 2);
assert.equal(panelCalls,4); assert.equal(settingsCalls,0);
strCurMenu='XLLR OFFLINE'; StartPanelMenu();
assert.equal(panelNodes.PanelHeading.text,'OFFLINE');
assert.ok(panelNodes.QuestionText.text.includes('running offline'));
assert.ok(panelNodes.QuestionText.text.includes('Xbox Live is unavailable'));
assert.equal(panelNodes.ButtonGroup.visible,false);
controller('OnADown'); controller('OnMoveRight');
assert.equal(originalMessages,4); // Offline screen does not launch or select anything.
'''
        result = subprocess.run([shutil.which("node"), "-"], input=program, text=True,
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
