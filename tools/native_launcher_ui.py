#!/usr/bin/env python3
"""Prepare the pinned Xbox-themed shared launcher, with explicit host limits.

This host consumes mod settings, audio volume and the shared controller source.
Its prepared Xbox profile offers those controls without advertising remaps,
fullscreen, filtering or window-size settings that the host does not consume.
The original clean UI revision plus every prepared file hash are recorded.
"""
import argparse
import hashlib
import io
import json
import re
from pathlib import Path
import subprocess
import zipfile

REVISION = 'ccc186e097b362555292b157ffd303abcea26fbf'


def prepare(source, output, git='git'):
    source, output = Path(source).resolve(), Path(output).resolve()
    head = subprocess.check_output([git, '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != REVISION:
        raise ValueError('Unreviewed recomp-ui revision')
    subprocess.run([git, '-C', str(source), 'diff', '--quiet', 'HEAD', '--'], check=True)
    archive = subprocess.check_output([git, '-C', str(source), 'archive', '--format=zip', REVISION])
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        for item in files.infolist():
            path = (output / item.filename).resolve()
            if not path.is_relative_to(output):
                raise ValueError('Unsafe dependency archive path')
            if item.is_dir():
                path.mkdir(parents=True, exist_ok=True)
            else:
                if item.filename in ('src/consoles/xbox/xbox_profile.h', 'src/common/backends/imgui/launcher_imgui.cpp'):
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                data = files.read(item)
                if not path.exists() or path.read_bytes() != data:
                    path.write_bytes(data)
    profile = output / 'src/consoles/xbox/xbox_profile.h'
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        code = files.read('src/consoles/xbox/xbox_profile.h').decode('utf-8')
    old = '    kPanelsDashboardCommon, kPanelsSettingsStub, kPanelsControllerCommon,'
    new = '    kPanelsXboxNativeDashboard, kPanelsXboxNativeSettings, kPanelsXboxNativeController,'
    if code.count(old) != 1:
        raise ValueError('Unreviewed Xbox launcher panel boundary')
    code = code.replace('static const SystemProfile kSystemProfileXbox = {',
        '/* xboxlle host adapter: only consumed controls are shown. */\n'
        'static const char* const kPanelsXboxNativeDashboard[]={"game","controller",NULL};\n'
        'static const char* const kPanelsXboxNativeSettings[]={"audio",NULL};\n'
        'static const char* const kPanelsXboxNativeController[]={"controller_config",NULL};\n'
        'static const SystemProfile kSystemProfileXbox = {').replace(old, new)
    if code.count('    NULL, 0, NULL, NULL,') != 1:
        raise ValueError('Unreviewed Xbox sample-rate control boundary')
    code = code.replace('    NULL, 0, NULL, NULL,', '    NULL, 1, "", NULL,')
    data = code.encode('utf-8')
    if not profile.exists() or profile.read_bytes() != data:
        profile.write_bytes(data)
    view = output / 'src/common/backends/imgui/launcher_imgui.cpp'
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        code = files.read('src/common/backends/imgui/launcher_imgui.cpp').decode('utf-8')
    old = '    if (ImGui::Button(install_label)) {'
    if code.count(old) != 2:
        raise ValueError('Unreviewed bundled-only provider toolbar')
    code = code.replace(old, '    if (mods->install_archive && ImGui::Button(install_label)) {')
    code, count = re.subn(
        r'(    if \(mods->install_archive && ImGui::Button\(install_label\)\) \{.*?\n    \})([^{}]*?)    ImGui::SameLine\(\);',
        lambda match: match[1] + match[2] + '    if(mods->install_archive)ImGui::SameLine();', code, flags=re.S)
    if count != 2:
        raise ValueError('Unreviewed mod toolbar spacing boundary')
    old = '        const bool on = m->s.player_src[p] != 0;\n        const char* st = ui_text(on ? "connected" : "not assigned");'
    new = '''        bool on = m->s.player_src[p] != 0;
        const SystemProfile* input_profile = (const SystemProfile*)m->profile;
        if(input_profile && input_profile->id && !strcmp(input_profile->id,"xbox") && m->s.player_src[p]==2) {
            on=false;
            for(int i=0;i<g_pad_count;++i)if(!strcmp(g_pads[i].guid,m->s.player_gamepad_guid[p])){on=true;break;}
        }
        const char* st = ui_text(on ? "connected" : m->s.player_src[p] ? "disconnected" : "not assigned");'''
    if code.count(old) != 1:
        raise ValueError('Unreviewed shared player connection-status boundary')
    code = code.replace(old, new)
    old = '        row_label("Deadzone", th);\n        {'
    if code.count(old) != 1:
        raise ValueError('Unreviewed controller deadzone boundary')
    code = code.replace(old, '        if (!cfg_prof || !cfg_prof->id || strcmp(cfg_prof->id,"xbox")) {\n            row_label("Deadzone", th);')
    # Original code-drawn pad silhouette; no foreign console's pad artwork.
    old = '        image_fit_centered(art, 120, 78, inner);'
    if code.count(old) != 1:
        raise ValueError('Unreviewed player controller-art boundary')
    code = code.replace(old, '''        if(aprof && aprof->id && !strcmp(aprof->id,"xbox") && !art.id) {
            ImDrawList* draw=ImGui::GetWindowDrawList();
            ImVec2 origin=ImGui::GetCursorScreenPos();origin.x+=(inner-px(120))*0.5f;
            auto point=[&](float x,float y){return ImVec2(origin.x+px(x),origin.y+px(y));};
            const ImU32 body=IM_COL32(34,43,31,255),edge=IM_COL32(97,133,57,255);
            draw->AddCircleFilled(point(28,48),px(21),edge);
            draw->AddCircleFilled(point(92,48),px(21),edge);
            draw->AddRectFilled(point(17,12),point(103,52),edge,px(19));
            draw->AddCircleFilled(point(28,48),px(19),body);
            draw->AddCircleFilled(point(92,48),px(19),body);
            draw->AddRectFilled(point(19,14),point(101,51),body,px(17));
            draw->AddCircleFilled(point(31,29),px(10),IM_COL32(12,17,11,255));
            draw->AddCircle(point(31,29),px(8),edge,0,px(1.3f));
            draw->AddCircleFilled(point(74,48),px(8),IM_COL32(12,17,11,255));
            draw->AddCircle(point(74,48),px(6),edge,0,px(1.3f));
            draw->AddRectFilled(point(43,40),point(49,56),edge,px(1));
            draw->AddRectFilled(point(38,45),point(54,51),edge,px(1));
            draw->AddCircleFilled(point(60,25),px(8),IM_COL32(138,204,19,255));
            draw->AddCircleFilled(point(95,25),px(3),IM_COL32(236,204,48,255));
            draw->AddCircleFilled(point(89,31),px(3),IM_COL32(54,139,220,255));
            draw->AddCircleFilled(point(101,31),px(3),IM_COL32(218,69,65,255));
            draw->AddCircleFilled(point(95,37),px(3),IM_COL32(85,191,62,255));
            draw->AddCircleFilled(point(89,45),px(2),IM_COL32(14,16,13,255));
            draw->AddCircleFilled(point(96,45),px(2),IM_COL32(214,220,204,255));
            draw->AddCircleFilled(point(57,38),px(2),edge);
            draw->AddCircleFilled(point(64,38),px(2),edge);
            ImGui::Dummy(ImVec2(0,px(78)));
        } else image_fit_centered(art, 120, 78, inner);''')
    data = code.encode('utf-8')
    if not view.exists() or view.read_bytes() != data:
        view.write_bytes(data)
    inventory = {str(p.relative_to(output)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in output.rglob('*') if p.is_file() and p.name != 'xboxlle-ui.json'}
    manifest = dict(version=1, adapter_version=3, revision=REVISION, archive_sha256=hashlib.sha256(archive).hexdigest(),
                    adapter='Xbox native host: mod provider, audio volume and shared Player 1 source', files=inventory)
    (output / 'xboxlle-ui.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify(output):
    output = Path(output).resolve()
    manifest = json.loads((output / 'xboxlle-ui.json').read_text(encoding='utf-8'))
    if manifest.get('revision') != REVISION or manifest.get('adapter_version') != 3:
        raise ValueError('Unreviewed prepared UI profile')
    for name, digest in manifest['files'].items():
        path = (output / name).resolve()
        if not path.is_relative_to(output) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Changed prepared UI dependency: ' + name)
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--git', default='git')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    if not args.verify and args.source is None:
        parser.error('--source is required for preparation')
    result = verify(args.output) if args.verify else prepare(args.source, args.output, args.git)
    print(json.dumps({k: v for k, v in result.items() if k != 'files'}))
