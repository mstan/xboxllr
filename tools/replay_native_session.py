"""Verify and replay a complete native game session in independent storage.

An identical package and owned game revision are required. The recording can
never name an executable, DLL, output save directory or debugger to load. All
writes go to a newly created directory; original saves/HDDs remain untouched.
"""
from __future__ import annotations
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess

from native_diagnostics import digest, durable_json, safe_relative
from native_session_diagnostics import inspect_session, sparse_copy, SCHEMA


def replay_environment(data, recorded, work):
    if not isinstance(recorded, dict) or len(recorded) > 256:
        raise ValueError('Invalid recorded runtime settings')
    # Only the package's trusted title policy and enumerated presentation/input
    # controls cross this boundary. No DLL search path, executable, debugger,
    # original storage path or arbitrary capture/control override is accepted.
    forbidden = ('CAPTURE', 'CONTROL', 'TRACE', 'TRIGGER', 'DEBUG', 'PROBE', 'PATCH',
                 'STOP_', 'HDD_', 'CHECKPOINT', 'SESSION_', 'DIAGNOSTIC', 'SAVE_KEYS', 'CONSOLE_SETTINGS')
    exact = {'XLLE_PILOT_WINDOW_TITLE', 'XLLE_PILOT_INPUT', 'XLLE_PILOT_INPUT_GUID',
             'XLLE_PILOT_CAMERA_INVERT_X', 'XLLE_PILOT_CAMERA_INVERT_Y'}
    allowed = set(data.get('runtime', {})) | exact
    from native_title_switches import declarations, PREFIX
    allowed.update(PREFIX + switch['key'] for switch in declarations(data))
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('XLLE_PILOT_', 'BLINX_', 'LNG_', 'RECOMP_'))}
    for key, value in recorded.items():
        if key in allowed and not any(word in key for word in forbidden):
            if not isinstance(value, str) or len(value) > 4096 or '\0' in value:
                raise ValueError('Invalid recorded runtime option')
            env[key] = value
    for name in ('TEMP', 'TMP', 'APPDATA', 'LOCALAPPDATA'):
        path = work / name.lower()
        path.mkdir(exist_ok=True)
        env[name] = str(path)
    env.update(PYTHONDONTWRITEBYTECODE='1', XLLE_PILOT_INPUT='none',
               XLLE_PILOT_SESSION_REPLAY=str(work / 'journal'),
               XLLE_PILOT_SAVE_KEYS_FILE=str(work / 'console/save-keys.bin'),
               XLLE_PILOT_CONSOLE_SETTINGS_FILE=str(work / 'console/console-settings.bin'),
               XLLE_PILOT_AUDIO_OUTPUT_MUTE='1', RECOMP_VOLUME='0',
               SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
    return env


def reproduce(session, root, image, work, *, seconds=3600, visible=False):
    from native_release_runtime import checked_image, descriptor, inside
    from native_revisions import select
    if type(seconds) not in (int, float) or not math.isfinite(seconds) or not 0 < seconds <= 86400:
        raise ValueError('Replay timeout must be between 0 and 86400 seconds')
    session, root, image = (Path(p).resolve(strict=True) for p in (session, root, image))
    work = Path(work).resolve()
    report, replay, info = inspect_session(session)
    data = descriptor(root)
    checked_image(data, image)
    data = select(data, digest(image))
    executable = inside(root, data['executable'])
    if (digest(executable) != replay['executable_sha256'] or
        digest(root / 'release.json') != replay['package_manifest_sha256'] or
        data['title']['xbe_sha256'] != replay['xbe_sha256']):
        raise ValueError('Replay requires the identical package, executable and game revision')
    if work.exists() or any(work.is_relative_to(source) for source in (session, root, image.parent)):
        raise ValueError('Choose a new replay directory outside the package, recording and game files')
    private = safe_relative(session, 'PRIVATE-DO-NOT-SHARE')
    # Validate settings before creating any output. A hostile bundle cannot
    # select an inherited input source or expose a remote-control listener.
    if not isinstance(replay.get('environment'), dict):
        raise ValueError('Missing recorded settings')
    work.mkdir(parents=True, exist_ok=False)
    for key, folder, target in [('state_files', 'initial-state', 'state'),
                                 ('console_files', 'initial-console', 'console'),
                                 ('journals', 'journal', 'journal')]:
        destination = work / target
        destination.mkdir()
        if key != 'journals':
            for name in replay[key.replace('_files', '_directories')]:
                safe_relative(destination, name).mkdir(parents=True, exist_ok=True)
        for name, metadata in replay[key].items():
            output = safe_relative(destination, name)
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(safe_relative(private / folder, name), output)
            if output.stat().st_size != metadata['size'] or digest(output) != metadata['sha256']:
                raise ValueError('Recording changed while copying to isolated replay storage')
    env = replay_environment(data, replay['environment'], work)
    env['XLLE_PILOT_VISIBLE'] = '1' if visible else '0'
    if replay.get('mod_plan_sha256'):
        shutil.copyfile(private / 'mods.plan', work / 'mods.plan')
        if digest(work / 'mods.plan') != replay['mod_plan_sha256']:
            raise ValueError('Mod plan changed during replay preparation')
        env['XLLE_PILOT_MOD_PLAN'] = str(work / 'mods.plan')
    if replay.get('hdd'):
        metadata = sparse_copy(private / 'initial-hdd.img', work / 'replay-hdd.img')
        if metadata['sha256'] != replay['hdd']['sha256']:
            raise ValueError('HDD baseline changed during replay preparation')
        env.update(XLLE_PILOT_HDD_IMAGE=str(work / 'replay-hdd.img'),
                   XLLE_PILOT_HDD_IDENTITY=replay['hdd']['identity'])
    command = [str(executable), str(image), str(work / 'console/eeprom.bin'), str(image.parent), str(work / 'state')]
    outcome = dict(schema=SCHEMA, source_session=session.name, state='starting',
                   recorded_turns=info['turns'], recorded_frames=info['frames'],
                   original_exit_code=report.get('exit_code'), verified=False)
    durable_json(work / 'reproduction.json', outcome)
    with (work / 'game.log').open('w', encoding='utf-8') as log:
        child = subprocess.Popen(command, cwd=work / 'state', env=env, stdout=log, stderr=log,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            code = child.wait(timeout=seconds)
            reason = 'process-exit'
        except subprocess.TimeoutExpired:
            child.terminate()
            code = child.wait()
            reason = 'timeout'
    verified = False
    diverged = False
    with (work / 'game.log').open(encoding='utf-8', errors='replace') as log:
        for line in log:
            verified |= '[SESSION REPLAY VERIFIED]' in line
            diverged |= '[SESSION REPLAY DIVERGENCE]' in line
    outcome.update(state='finished', exit_code=code, reason=reason,
                   verified=verified and not diverged and code == report.get('exit_code') and reason == 'process-exit')
    durable_json(work / 'reproduction.json', outcome)
    return outcome


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--root', type=Path)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--work-dir', type=Path)
    parser.add_argument('--seconds', type=float, default=3600)
    parser.add_argument('--visible', action='store_true', help='show the replay; audio remains muted')
    args = parser.parse_args()
    report, _, info = inspect_session(args.session)
    print(json.dumps(dict(title=report['title'], turns=info['turns'], frames=info['frames'],
                          terminal=info['terminal']), indent=2))
    if args.check:
        return 0
    if not all((args.root, args.image, args.work_dir)):
        parser.error('Replay requires --root, --image and --work-dir')
    result = reproduce(args.session, args.root, args.image, args.work_dir,
                       seconds=args.seconds, visible=args.visible)
    print(json.dumps(result, indent=2))
    # Reproducing a recorded native stop (exit 2) is a successful replay. Only
    # the verified terminal marker plus matching exit establishes that result.
    return 0 if result['verified'] else 3


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        raise SystemExit(str(error)) from error
