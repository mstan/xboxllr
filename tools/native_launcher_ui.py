#!/usr/bin/env python3
"""Prepare the pinned Xbox-themed shared launcher, with explicit host limits.

This host consumes mod settings and the shared controller source.
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
                if item.filename in ('src/consoles/xbox/xbox_profile.h', 'src/common/backends/imgui/launcher_imgui.cpp',
                                     'src/common/launcher_platform_sdl2.c', 'src/common/launcher_model.c'):
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
        'static const char* const kPanelsXboxNativeSettings[]={"system",NULL};\n'
        'static const char* const kPanelsXboxNativeController[]={"controller_config",NULL};\n'
        'static const SystemProfile kSystemProfileXbox = {').replace(old, new)
    if code.count('    NULL, 0, NULL, NULL,') != 1:
        raise ValueError('Unreviewed Xbox sample-rate control boundary')
    code = code.replace('    NULL, 0, NULL, NULL,', '    NULL, 1, "", NULL,')
    if code.count('"pad_xbox.tga"') != 1:
        raise ValueError('Unreviewed Xbox controller artwork path')
    code = code.replace('"pad_xbox.tga"', '"pad_xbox.jpg"')
    code = code.replace('{ "*.xbe" }', '{ "*.xbe", "*.iso", "*.xiso" }')
    code = code.replace('{ kXboxExecutablePatterns, 1, "Xbox executable (.xbe)" }',
                        '{ kXboxExecutablePatterns, 3, "Xbox game (.xbe, .iso, .xiso)" }')
    data = code.encode('utf-8')
    if not profile.exists() or profile.read_bytes() != data:
        profile.write_bytes(data)
    view = output / 'src/common/backends/imgui/launcher_imgui.cpp'
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        code = files.read('src/common/backends/imgui/launcher_imgui.cpp').decode('utf-8')
    adapter = Path(__file__).resolve().parents[1] / 'pilots/native-launcher'
    for source, name in [('console_setup.h','xboxllr_console_setup.h'), ('console_setup_ui.inc','xboxllr_console_setup_ui.inc'),
                         ('xbe_identity.h', 'xboxllr_xbe_identity.h')]:
        data = (adapter / source).read_bytes()
        target = output / 'src/common' / name
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    boundary = 'int avail_system(const LauncherModel* m) { return launcher_model_bios_applies(m); }'
    if code.count(boundary) != 1:
        raise ValueError('Unreviewed native console-settings boundary')
    code = code.replace('#include "launcher_debug.h"', '#include "launcher_debug.h"\n#include "xboxllr_console_setup.h"\n#include "xboxllr_xbe_identity.h"')
    replacements = {
        '        float reserve = px(198.0f);':
            '        float reserve = px(198.0f);\n        if (xboxllr_xbe_identity.valid) reserve += px(56.0f);',
        '        else                   snprintf(line, sizeof(line), "%s not recognized", noun);':
            '        else                   snprintf(line, sizeof(line), "%s", xboxllr_xbe_rejection());',
        '        kv_row("File",   m->rom_file, th, false, false);':
            '''        if (xboxllr_xbe_identity.valid) {
            if (xboxllr_xbe_identity.title_name[0])
                kv_row("Title", xboxllr_xbe_identity.title_name, th, false, false);
            kv_row("Title ID", xboxllr_xbe_identity.title_label, th, false, false);
        }
        kv_row("File",   m->rom_file, th, false, false);''',
    }
    for old, new in replacements.items():
        if code.count(old) != 1:
            raise ValueError('Unreviewed XBE identity UI boundary: ' + old)
        code = code.replace(old, new)
    code = code.replace(boundary, '#include "xboxllr_console_setup_ui.inc"\n'
        'int avail_system(const LauncherModel* m) { return xboxllr_console_setup || launcher_model_bios_applies(m); }')
    boundary = 'void draw_system_controls(LauncherModel* m, const LauncherTheme& th) {'
    if code.count(boundary) != 1 or code.count('    draw_setup_wizard_modal(m, th);') != 1:
        raise ValueError('Unreviewed native first-launch boundary')
    code = code.replace(boundary, boundary + '\n    if(xboxllr_console_setup){draw_xboxllr_console_controls(m,th);return;}')
    code = code.replace('    draw_setup_wizard_modal(m, th);', '    draw_xboxllr_console_welcome(m, th);\n    draw_setup_wizard_modal(m, th);')
    boundary = 'void draw_settings(LauncherModel* m, const LauncherTheme& th) {'
    if code.count(boundary) != 1:
        raise ValueError('Unreviewed native settings layout boundary')
    code = code.replace(boundary, boundary + '''
    if(xboxllr_console_setup) {
        g_settings_two_col=false;
        const auto *profile=(const SystemProfile*)m->profile;
        for(const char *id : {"system"}) {
            const LauncherPanel *panel=find_composed(profile->panels_settings,id,m);
            if(!panel)continue;
            begin_container(id,ImVec2(ImGui::GetContentRegionAvail().x,0),ImGuiChildFlags_AutoResizeY);
            panel->draw(m,&th);end_container();ImGui::Dummy(ImVec2(0,px(th.spacing_md)));
        }
        return;
    }''')
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
    # The shared BIOS slot carries a one-time signing input for this host;
    # never describe it as firmware that the native runtime executes.
    setup_labels = {
        '"%s needs a BIOS image and a playable %s before you can launch. "':
            '"%s needs a kernel dump for save setup and an extracted %s. "',
        '"Pick both below (you must legally own these dumps).",\n                game, noun);\n        } else {':
            '"Choose your own files below; the kernel is read once, not booted.",\n                game, noun);\n        } else {',
        ': "1. BIOS";': ': "1. Kernel dump (preview save setup)";',
        ': "Browse for a BIOS image required by this console.";':
            ': "Used once to prepare signed saves; no original kernel runs during play.";',
        '                                      : "Select BIOS file";':
            '                                      : "Select your Xbox kernel dump";',
        'ImGui::Button("Browse BIOS##setup", ImVec2(px(120), px(32)))':
            'ImGui::Button("Browse kernel##setup", ImVec2(px(140), px(32)))',
        ': "Select your game ROM file.";':
            ': "Select an Xbox ISO/XISO or the .xbe in its extracted game folder.";',
        'ImGui::Button(browse_lbl, ImVec2(px(128), px(32)))':
            'ImGui::Button(browse_lbl, ImVec2(px(200), px(32)))',
    }
    for old, new in setup_labels.items():
        if code.count(old) != 1:
            raise ValueError('Unreviewed native first-run label boundary: ' + old)
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
    old = '        launcher_platform_present(p);'
    if code.count(old) != 1:
        raise ValueError('Unreviewed low-power launcher boundary')
    code = code.replace(old, old + '''
        if(SDL_getenv("XLLE_LAUNCHER_LOW_POWER") &&
           !(SDL_GetWindowFlags(p->window)&SDL_WINDOW_INPUT_FOCUS)) SDL_Delay(200);''')
    data = code.encode('utf-8')
    if not view.exists() or view.read_bytes() != data:
        view.write_bytes(data)
    model = output / 'src/common/launcher_model.c'
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        code = files.read('src/common/launcher_model.c').decode('utf-8')
    old = '    m->rom_sha1_hex[0] = \'\\0\';'
    if code.count(old) != 1:
        raise ValueError('Unreviewed XBE selection boundary')
    code = code.replace('#include "launcher_model.h"', '#include "launcher_model.h"\n#include "xboxllr_xbe_identity.h"')
    code = code.replace(old, '    xboxllr_xbe_select(m->rom_full);\n' + old)
    old = 'size_t hdr = ((size_t)n % 1024 == 512) ? 512 : 0;'
    if code.count(old) != 1:
        raise ValueError('Unreviewed XBE whole-file fingerprint boundary')
    code = code.replace(old, 'size_t hdr = 0; /* XBE hashes include every header byte. */')
    # The Xbox host fingerprints only the embedded executable. The generic
    # cartridge loader uses long/whole-file allocation, unsuitable for DVDs.
    start = code.index("    m->rom_size[0] = '\\0';", code.index('void launcher_model_set_rom('))
    end = code.index('    /* Keep the roster selection', start)
    code = code[:start] + '''    m->rom_size[0] = '\\0';
    m->crc_match = false;m->sha_match = false;m->sha1_match = false;
    if (m->rom_present) {
        unsigned char *body = NULL;size_t bytes = 0;uint64_t source_size = 0;
        if (xboxllr_read_game_image(m->rom_full, &body, &bytes, &source_size)) {
            snprintf(m->rom_size, sizeof(m->rom_size), "%.2f MB", (double)source_size / (1024.0 * 1024.0));
            uint8_t digest[32];recompui_sha256_compute(body, bytes, digest);
            uint32_t crc = recompui_crc32_compute(body, bytes);
            m->crc_match = m->has_expected_crc && crc == m->expected_crc;
            for (size_t k=0;k<m->num_known_sha256;++k)
                if (!memcmp(digest,m->known_sha256[k],32)) {m->sha_match=true;break;}
            free(body);
        }
    }
    if (!m->rom_size[0]) safe_copy(m->rom_size, sizeof(m->rom_size), "--");

''' + code[end:]
    data = code.encode('utf-8')
    if not model.exists() or model.read_bytes() != data:
        model.write_bytes(data)
    platform = output / 'src/common/launcher_platform_sdl2.c'
    with zipfile.ZipFile(io.BytesIO(archive)) as files:
        code = files.read('src/common/launcher_platform_sdl2.c').decode('utf-8')
    old = '    if (test_hidden) window_flags |= SDL_WINDOW_HIDDEN;'
    if code.count(old) != 1 or code.count('    if (!test_hidden)\n        SDL_RaiseWindow') != 1:
        raise ValueError('Unreviewed minimized launcher boundary')
    code = code.replace(old, old + '\n    const bool start_minimized = getenv("XLLE_LAUNCHER_START_MINIMIZED") != NULL;\n'
                        '    if(start_minimized) window_flags |= SDL_WINDOW_MINIMIZED;')
    code = code.replace('    if (!test_hidden)\n        SDL_RaiseWindow',
                        '    if (!test_hidden && !start_minimized)\n        SDL_RaiseWindow')
    data = code.encode('utf-8')
    if not platform.exists() or platform.read_bytes() != data:
        platform.write_bytes(data)
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
    manifest = dict(version=1, adapter_version=11, revision=REVISION, archive_sha256=hashlib.sha256(archive).hexdigest(),
                    adapter='Xbox native host: mod provider, console storage and shared Player 1 source',
                    controller_photo=dict(url=CONTROLLER_PHOTO_URL, sha256=CONTROLLER_PHOTO_SHA256), files=inventory)
    (output / 'xboxlle-ui.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify(output):
    output = Path(output).resolve()
    manifest = json.loads((output / 'xboxlle-ui.json').read_text(encoding='utf-8'))
    if manifest.get('revision') != REVISION or manifest.get('adapter_version') != 11:
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
