#!/usr/bin/env python3
"""Prepare the pinned Xbox-themed shared launcher, with explicit host limits.

This host currently consumes mod settings and audio volume. Its prepared Xbox
profile therefore offers those controls without advertising controller remaps,
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
        'static const char* const kPanelsXboxNativeDashboard[]={"game",NULL};\n'
        'static const char* const kPanelsXboxNativeSettings[]={"audio",NULL};\n'
        'static const char* const kPanelsXboxNativeController[]={NULL};\n'
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
    data = code.encode('utf-8')
    if not view.exists() or view.read_bytes() != data:
        view.write_bytes(data)
    inventory = {str(p.relative_to(output)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in output.rglob('*') if p.is_file() and p.name != 'xboxlle-ui.json'}
    manifest = dict(version=1, adapter_version=2, revision=REVISION, archive_sha256=hashlib.sha256(archive).hexdigest(),
                    adapter='Xbox native host: mod provider and audio-volume controls only', files=inventory)
    (output / 'xboxlle-ui.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify(output):
    output = Path(output).resolve()
    manifest = json.loads((output / 'xboxlle-ui.json').read_text(encoding='utf-8'))
    if manifest.get('revision') != REVISION or manifest.get('adapter_version') != 2:
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
