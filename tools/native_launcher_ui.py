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
import urllib.request
import zipfile

REVISION = 'ccc186e097b362555292b157ffd303abcea26fbf'
CONTROLLER_PHOTO_URL = 'https://upload.wikimedia.org/wikipedia/commons/thumb/7/7f/Xbox-s-controller.jpg/330px-Xbox-s-controller.jpg'
CONTROLLER_PHOTO_SHA256 = '0af066ce9d07e507be223a9bd9801146b2dc2ef625a7d8032f17b0634d51cb8b'


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
    if code.count('"pad_xbox.tga"') != 1:
        raise ValueError('Unreviewed Xbox controller artwork path')
    code = code.replace('"pad_xbox.tga"', '"pad_xbox.jpg"')
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
    # Xbox uses the real Controller S photo staged by the native launcher.
    old = '        image_fit_centered(art, 120, 78, inner);'
    if code.count(old) != 1:
        raise ValueError('Unreviewed player controller-art boundary')
    code = code.replace(old, '''        if(aprof && aprof->id && !strcmp(aprof->id,"xbox"))
            image_fit_centered(art, 180, 124, inner);
        else image_fit_centered(art, 120, 78, inner);''')
    data = code.encode('utf-8')
    if not view.exists() or view.read_bytes() != data:
        view.write_bytes(data)
    photo = output / 'assets/consoles/xbox/img/pad_xbox.jpg'
    if not photo.is_file():
        request = urllib.request.Request(CONTROLLER_PHOTO_URL, headers={'User-Agent': 'xboxlle-launcher-artwork/1.0'})
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read(1024 * 1024)
        if hashlib.sha256(data).hexdigest() != CONTROLLER_PHOTO_SHA256:
            raise ValueError('Unreviewed controller photograph download')
        photo.parent.mkdir(parents=True, exist_ok=True)
        photo.write_bytes(data)
    if hashlib.sha256(photo.read_bytes()).hexdigest() != CONTROLLER_PHOTO_SHA256:
        raise ValueError('Changed controller photograph')
    inventory = {str(p.relative_to(output)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in output.rglob('*') if p.is_file() and p.name != 'xboxlle-ui.json'}
    manifest = dict(version=1, adapter_version=4, revision=REVISION, archive_sha256=hashlib.sha256(archive).hexdigest(),
                    adapter='Xbox native host: mod provider, audio volume and shared Player 1 source',
                    controller_photo=dict(url=CONTROLLER_PHOTO_URL, sha256=CONTROLLER_PHOTO_SHA256), files=inventory)
    (output / 'xboxlle-ui.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify(output):
    output = Path(output).resolve()
    manifest = json.loads((output / 'xboxlle-ui.json').read_text(encoding='utf-8'))
    if manifest.get('revision') != REVISION or manifest.get('adapter_version') != 4:
        raise ValueError('Unreviewed prepared UI profile')
    if manifest.get('controller_photo') != dict(url=CONTROLLER_PHOTO_URL, sha256=CONTROLLER_PHOTO_SHA256):
        raise ValueError('Unreviewed controller photograph provenance')
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
