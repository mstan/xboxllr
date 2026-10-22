"""Check or reproduce a private tester session in a NEW disposable directory.

Requires the identical packaged binary and your own matching extracted game.
Never opens the owner's original save storage for writing. Input replay is not
yet deterministic machine replay; scheduling/clock divergence is reported.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from native_diagnostics import digest, durable_json, inspect_session, safe_relative, SCHEMA


def reproduce(session, root, image, work, *, seconds=300, visible=False):
    from native_release_runtime import checked_image, descriptor, inside
    if not isinstance(seconds, (float, int)) or not 0 < seconds <= 3600:
        raise ValueError('Reproduction timeout must be between 0 and 3600 seconds')
    session, root, image = map(lambda p: Path(p).resolve(strict=True), (session, root, image))
    work = Path(work).resolve()
    report, replay, inputs = inspect_session(session)
    data = descriptor(root)
    checked_image(data, image)
    executable = inside(root, data['executable'])
    if (digest(executable) != replay['executable_sha256'] or
            digest(root / 'release.json') != replay['package_manifest_sha256'] or
            data['title']['xbe_sha256'] != replay['xbe_sha256']):
        raise ValueError('Reproduction requires the identical package/build and original game revision')
    if work.exists() or any(work.is_relative_to(source) for source in (session, root, image.parent)):
        raise ValueError('Choose a new reproduction directory outside the package, session and game assets')
    private = safe_relative(session, 'PRIVATE-DO-NOT-SHARE')
    work.mkdir(parents=True, exist_ok=False)
    # Copy only verified manifest entries. Unlisted files are never reproduced.
    for key, directory in [('state_files', 'initial-state'), ('console_files', 'initial-console')]:
        destination = work / ('state' if key == 'state_files' else 'console')
        destination.mkdir()
        for name in replay[key]:
            target = safe_relative(destination, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(safe_relative(private / directory, name), target)
    shutil.copyfile(safe_relative(private, 'inputs.bin'), work / 'inputs.bin')
    # A bundle cannot supply arbitrary environment variables, inject DLLs,
    # redirect saves or enable memory writers/debug listeners.
    forbidden = ('CAPTURE', 'CONTROL', 'TRACE', 'TRIGGER', 'DEBUG', 'PROBE', 'PATCH', 'STOP_', 'HDD_', 'CHECKPOINT')
    allowed_exact = {'XLLE_PILOT_INPUT_GUID', 'XLLE_PILOT_WINDOW_TITLE', 'RECOMP_VOLUME', 'SDL_AUDIODRIVER', 'SDL_AUDIO_DRIVER'}
    configured = replay['environment']
    if not isinstance(configured, dict) or len(configured) > 256:
        raise ValueError('Invalid recorded environment')
    # Only title-authored runtime settings and presentation values are allowed.
    allowed = set(data.get('runtime', {})) | allowed_exact
    env = {key: value for key, value in os.environ.items() if not key.startswith('XLLE_PILOT_')}
    for key, value in configured.items():
        if key in allowed and not any(word in key for word in forbidden):
            if not isinstance(value, str) or len(value) > 4096 or '\0' in value:
                raise ValueError('Invalid recorded runtime option')
            env[key] = value
    for name in ('TEMP', 'TMP', 'APPDATA', 'LOCALAPPDATA'):
        path = work / name.lower()
        path.mkdir(exist_ok=True)
        env[name] = str(path)
    env.update(PYTHONDONTWRITEBYTECODE='1', XLLE_PILOT_INPUT='diagnostic-replay',
        XLLE_PILOT_DIAGNOSTIC_REPLAY=str(work / 'inputs.bin'),
        XLLE_PILOT_SAVE_KEYS_FILE=str(work / 'console/save-keys.bin'),
        XLLE_PILOT_CONSOLE_SETTINGS_FILE=str(work / 'console/console-settings.bin'),
        XLLE_PILOT_VISIBLE='1' if visible else '0', XLLE_PILOT_AUDIO_OUTPUT_MUTE='1', RECOMP_VOLUME='0', SDL_AUDIODRIVER='dummy')
    if replay.get('mod_plan_sha256'):
        shutil.copyfile(private / 'mods.plan', work / 'mods.plan')
        env['XLLE_PILOT_MOD_PLAN'] = str(work / 'mods.plan')
    command = [str(executable), str(image), str(work / 'console/eeprom.bin'), str(image.parent), str(work / 'state')]
    outcome = dict(schema=SCHEMA, source_session=session.name, input_records=inputs['records'],
        deterministic_machine_replay=False, original_exit_code=report.get('exit_code'))
    durable_json(work / 'reproduction.json', dict(outcome, state='starting'))
    with (work / 'game.log').open('w', encoding='utf-8') as log:
        child = subprocess.Popen(command, cwd=work / 'state', env=env, stdout=log, stderr=log)
        try:
            code = child.wait(timeout=seconds)
            reason = 'process-exit'
        except subprocess.TimeoutExpired:
            child.terminate()
            code = child.wait()
            reason = 'timeout'
    outcome.update(state='finished', exit_code=code, reason=reason)
    durable_json(work / 'reproduction.json', outcome)
    return outcome


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('--check', action='store_true', help='validate without executing anything')
    parser.add_argument('--root', type=Path, help='identical extracted release package')
    parser.add_argument('--image', type=Path, help='your own matching original .xbe')
    parser.add_argument('--work-dir', type=Path, help='new disposable reproduction directory')
    parser.add_argument('--seconds', type=float, default=300)
    parser.add_argument('--visible', action='store_true', help='show the reproduction with muted audio')
    args = parser.parse_args()
    report, replay, inputs = inspect_session(args.session)
    print(json.dumps(dict(title=report['title'], inputs=inputs, limitation=report['limitation']), indent=2))
    if args.check:
        return 0
    if not all((args.root, args.image, args.work_dir)):
        parser.error('reproduction requires --root, --image and --work-dir')
    result = reproduce(args.session, args.root, args.image, args.work_dir, seconds=args.seconds, visible=args.visible)
    print(json.dumps(result, indent=2))
    # An exhausted input stream is a useful attempt, not a successful complete
    # simulation replay. The native boundary uses exit 4 for exhaustion.
    return result['exit_code'] or (5 if result['reason'] == 'timeout' else 0)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        raise SystemExit(str(error))
