"""Authored menu and controller checks; retail XAP/VM validation is separate."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dashboard_credits_mod import block, credit_pages, main_script, native_panel_script, viewer
from dashboard_credits_viewer import document_lines, VISIBLE_LINES


SCRIPT = '''
function UpdateMainMenu() { originalMenuUpdate(); }
DEF theMainMenu Level {
  function OnADown() { originalSelect(); }
  function OnMoveDown() { if (nCurMainMenuItem < 3) nCurMainMenuItem = nCurMainMenuItem + 1; }
  function onLoad() { var c = theMainMenu.children[0].children[0]; UpdateMainMenu(); }
}
'''
PANEL_SCRIPT = '''
function StartSettingsMenu() {
  if ((strCurMenu == "SETTINGS") & (strPrevMenu == "SETTINGS")) {
    nCurSettingsMenuItem = 0; nCurSettingsMenuButton = 0; nSettingsScroll = 0;
  }
  var sSettingsMenu = ["CLOCK", "LANGUAGE"];
  if (avPack != "RFU") sSettingsMenu.push("AUDIO");
  sSettingsMenu.push("VIDEO", "NETWORK", "LIVE TODAY");
  if (region != "EU") sSettingsMenu.push("PARENTAL CONTROL");
  sSettingsMenu.push("AUTO OFF");
  var index = sSettingsMenu.length;
  sSettingsMenu[index] = theTranslator.Translate("SYSTEM INFO");
  index = index + 1;
  nSettingsItemCount = index; sSettingsElements = sSettingsMenu;
  originalSettings();
}
function MenuIcon() { originalMenuIcon(); }
function StartPanelMenu() { originalPanel(); }
DEF theSettingsMenu Level {
  function OnADown() { originalSelect(); }
  function OnMoveUp() {
    if (nCurSettingsMenuItem == 0) return;
    nCurSettingsMenuItem = nCurSettingsMenuItem - 1;
    if (nCurSettingsMenuButton == 0) nSettingsScroll = nSettingsScroll - 1;
    else nCurSettingsMenuButton = nCurSettingsMenuButton - 1;
  }
  function OnMoveDown() {
    if (nCurSettingsMenuItem == nSettingsItemCount - 1) return;
    nCurSettingsMenuItem = nCurSettingsMenuItem + 1;
    if (nCurSettingsMenuButton == 5) nSettingsScroll = nSettingsScroll + 1;
    else nCurSettingsMenuButton = nCurSettingsMenuButton + 1;
  }
}
control DEF thePanelJoystick Joystick {
  function OnADown() { originalMessage(); }
  function OnBDown() { originalMessage(); }
  function OnMoveLeft() { originalMessage(); }
  function OnMoveRight() { originalMessage(); }
  function OnMoveUp() { originalMessage(); }
  function OnMoveDown() { originalMessage(); }
}
'''


def function(source, marker):
    begin, _, end = block(source, marker)
    return source[begin:end]


class CreditsTests(unittest.TestCase):
    def test_modal_state_uses_onload_not_class_member_initializers(self):
        generated = main_script(SCRIPT, ["Credits"], [0])
        self.assertIn("var xllrCreditsActive;", generated)
        self.assertIn("var xllrCreditsLine;", generated)
        self.assertNotIn("var xllrCreditsActive =", generated)
        self.assertNotIn("var xllrCreditsLine =", generated)
        load = function(generated, "function onLoad()")
        self.assertLess(load.index("xllrCreditsActive = false;"), load.index("UpdateMainMenu();"))
        self.assertLess(load.index("xllrCreditsLine = 0;"), load.index("UpdateMainMenu();"))

    def test_home_selection_navigation_and_highlights_remain_original(self):
        generated = main_script(SCRIPT, ["Credits"], [0])
        self.assertEqual(function(generated, "function UpdateMainMenu()"),
                         function(SCRIPT, "function UpdateMainMenu()"))
        for marker in ("function OnADown()", "function OnMoveDown()"):
            self.assertEqual(function(generated, marker), function(SCRIPT, marker))
        self.assertNotIn("xllrCredits_", generated)
        self.assertNotIn("GoToSettings();", generated)

    def test_settings_insertion_rejects_missing_or_duplicate_original_anchor(self):
        anchor = 'sSettingsMenu[index] = theTranslator.Translate("SYSTEM INFO");\n  index = index + 1;'
        with self.assertRaises(ValueError):
            native_panel_script(PANEL_SCRIPT.replace(anchor, ""))
        with self.assertRaises(ValueError):
            native_panel_script(PANEL_SCRIPT.replace(anchor, anchor + "\n  " + anchor))

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
            self.assertEqual(len(sections), 3)
            first = notices[0]["viewer_page"] - 1
            visible = " ".join(" ".join(page.splitlines()[1:]) for page in pages[first:sections[-1]])
            self.assertEqual("".join(visible.split()), "".join(text.split()))
            self.assertIn("second-LICENSE", "".join(pages[-1].split()))

    @unittest.skipUnless(shutil.which("node"), "Node is optional for generated-script logic checks")
    def test_main_document_owner_can_open_without_child_settings_fields(self):
        program = '"use strict";\n' + viewer(["Credits\nMatthew Stanley"], [0])
        program += '\nXllrCreditsOpen(); if (!xllrCreditsActive || xllrCreditsLine !== 0) throw Error("modal state");'
        result = subprocess.run([shutil.which("node"), "-"], input=program, text=True,
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(shutil.which("node"), "Node is optional for generated-script logic checks")
    def test_settings_routes_scroll_boundaries_and_original_panel_restoration(self):
        pages = ["Credits\nMatthew Stanley\n" + "\n".join(f"First credit line {i}" for i in range(40)),
                 "Credits\nOther credits\n" + "\n".join(f"Second credit line {i}" for i in range(40)),
                 "License one\n" + "\n".join(f"License A line {i}" for i in range(40)),
                 "License one\ncontinued", "License two\n" + "\n".join(f"License B line {i}" for i in range(40))]
        sections = [0, 1, 2, 4]
        panel = native_panel_script(PANEL_SCRIPT)
        program = r'''
const assert = require('node:assert/strict');
function node() { return {name:'', visible:true, SetScale(...v){this.scale=v;}, SetTranslation(...v){this.position=v;}}; }
var panelNodes=new Proxy({}, {get(t,k){return t[k] ??= node();}});
var settingsNodes={ConsoleIcon:{}};
var homeReturns=0, settingsReturns=0, panelCalls=0, originalSelections=0, originalMessages=0;
var subIn=0, subOut=0, shownIcon=null;
var theMainMenu={GoBackTo(){homeReturns++;}};
var theSettingsMenu={children:[{children:[settingsNodes]}], GoBackTo(){settingsReturns++; StartSettingsMenu();}};
var theSetPanelMenu={children:[{children:[panelNodes]}], GoTo(){StartPanelMenu();}};
var theSettingsSubMenuIn={Play(){subIn++;}}, theSettingsSubMenuOut={Play(){subOut++;}};
var strCurMenu='SETTINGS', strPrevMenu='SETTINGS', avPack='COMPOSITE', region='NA';
var nCurSettingsMenuItem=0, nCurSettingsMenuButton=0, nSettingsScroll=0, nSettingsItemCount=0, sSettingsElements=[];
var theTranslator={Translate(s){return s;}};
function originalSettings(){}
function originalMenuIcon(){shownIcon='original';}
function ShowIcon(icon){shownIcon=icon;}
function PlaySoundA(){}
function PlaySoundB(){}
function originalMessage(){originalMessages++;}
function originalSelect(){
  originalSelections++;
  if(sSettingsElements[nCurSettingsMenuItem]=='SYSTEM INFO'){
    strPrevMenu='SETTINGS'; strCurMenu='SYSTEM INFO'; theSetPanelMenu.GoTo();
  }
}
function originalPanel(){
  panelCalls++;
  panelNodes.QuestionText.text='Original System Info';
  panelNodes.PanelHeading.text='SYSTEM INFO';
}
'''
        lines, starts = document_lines(pages, sections)
        program += "var expectedLines=" + json.dumps(lines) + ";\n"
        program += "var expectedStarts=" + json.dumps(starts) + ";\n"
        program += "var visibleLines=" + str(VISIBLE_LINES) + ";\n"
        program += viewer(pages, sections)
        for name in ("StartSettingsMenu", "MenuIcon", "StartPanelMenu"):
            program += function(panel, f"function {name}()") + "\n"
        settings = function(panel, "DEF theSettingsMenu Level")
        panel_joystick = function(panel, "control DEF thePanelJoystick Joystick")
        program += "\nfunction settingsControl(event){\n"
        program += "\n".join(function(settings, f"function {name}()")
                             for name in ("OnADown", "OnMoveUp", "OnMoveDown"))
        program += "\neval(event+'()');}\nfunction controller(event){\n"
        program += "\n".join(function(panel_joystick, f"function {name}()")
                             for name in ("OnADown", "OnBDown", "OnMoveUp", "OnMoveDown",
                                          "OnMoveLeft", "OnMoveRight"))
        program += "\neval(event+'()');}\n"
        program += r'''
for (var pack of ['COMPOSITE','RFU']) for (var gameRegion of ['NA','JAPAN','EU']) {
  avPack=pack; region=gameRegion; strCurMenu='SETTINGS'; strPrevMenu='SETTINGS'; StartSettingsMenu();
  assert.deepEqual(sSettingsElements.slice(-2),['SYSTEM INFO','CREDITS']);
  assert.equal(sSettingsElements.filter(s=>s=='CREDITS').length,1);
  assert.equal(nSettingsItemCount,10-(pack=='RFU')-(gameRegion=='EU'));
  for(var i=0;i<nSettingsItemCount+3;i++) settingsControl('OnMoveDown');
  assert.equal(nCurSettingsMenuItem,nSettingsItemCount-1);
  assert.equal(nCurSettingsMenuButton,5);
  assert.equal(nSettingsScroll,nSettingsItemCount-6);
  MenuIcon(); assert.equal(shownIcon,settingsNodes.ConsoleIcon);
  settingsControl('OnADown');
  assert.equal(strCurMenu,'XLLR CREDITS'); assert.equal(strPrevMenu,'SETTINGS');
  assert.equal(xllrCreditsActive,true); assert.equal(xllrCreditsLine,0);
  assert.ok(xllrCreditsBody.text.includes('Matthew Stanley'));
  assert.equal(panelNodes.ConsoleIcon.visible,true); assert.equal(panelNodes.ButtonGroup.visible,false);
  controller('OnMoveUp'); assert.equal(xllrCreditsLine,0);
  controller('OnMoveDown'); assert.equal(xllrCreditsLine,1);
  controller('OnMoveUp'); assert.equal(xllrCreditsLine,0);
  controller('OnMoveRight'); assert.equal(xllrCreditsLine,expectedStarts[1]);
  controller('OnMoveLeft'); assert.equal(xllrCreditsLine,0);
  controller('OnADown'); assert.equal(xllrCreditsLine,visibleLines-1);
  for(var i=0;i<=expectedLines.length-visibleLines;i++){
    xllrCreditsLine=i; XllrCreditsRefresh();
    assert.equal(xllrCreditsBody.text,expectedLines.slice(i,i+visibleLines).join('\r'));
  }
  controller('OnMoveDown'); controller('OnADown');
  assert.equal(xllrCreditsLine,expectedLines.length-visibleLines);
  assert.equal(xllrCreditsPosition.text,'CREDITS 100%');
  controller('OnBDown');
  assert.equal(xllrCreditsActive,false); assert.equal(strCurMenu,'SETTINGS'); assert.equal(strPrevMenu,'XLLR CREDITS');
  assert.equal(nCurSettingsMenuItem,nSettingsItemCount-1); // Returning does not jump to Clock/Home.
  settingsControl('OnADown'); assert.equal(xllrCreditsLine,0);
  controller('OnBDown');
  settingsControl('OnMoveUp'); assert.equal(sSettingsElements[nCurSettingsMenuItem],'SYSTEM INFO');
  settingsControl('OnADown'); assert.equal(strCurMenu,'SYSTEM INFO');
  assert.equal(panelNodes.QuestionText.text,'Original System Info');
  assert.deepEqual(panelNodes.S_Home_text_games01.scale,[1,1,1]);
  assert.equal(panelNodes.QuestionText.width,10); assert.equal(panelNodes.QuestionText.scrollRate,0.1);
}
assert.equal(homeReturns,0); assert.equal(settingsReturns,12);
assert.equal(subIn,12); assert.equal(subOut,12); assert.equal(originalSelections,6);
assert.equal(originalMessages,0);
for(var entry of ['NETWORK','LIVE TODAY']){
  sSettingsElements=[entry]; nCurSettingsMenuItem=0; strCurMenu='SETTINGS';
  settingsControl('OnADown'); assert.equal(strCurMenu,'XLLR OFFLINE');
  assert.equal(panelNodes.PanelHeading.text,'OFFLINE');
  assert.ok(panelNodes.QuestionText.text.includes('Xbox Live is unavailable'));
  controller('OnADown'); controller('OnMoveRight');
}
assert.equal(originalMessages,0);
'''
        result = subprocess.run([shutil.which("node"), "-"], input=program, text=True,
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
