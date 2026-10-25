"""Retained native-title logs and opt-in, private input reproduction sessions.

These are input-boundary recordings, not deterministic machine checkpoints.
Host clocks, thread scheduling and external files can still cause divergence.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import uuid

SCHEMA = 'xboxllr.diagnostic-session.v1'
INPUT_HEADER = b'XLLRINP1' + struct.pack('<II', 1, 64)
INPUT_RECORD = struct.Struct('<QQIIII22s10s')
SNAPSHOT_LIMIT = 128 * 1024 * 1024
INPUT_LIMIT = 128 * 1024 * 1024
MAX_FILES = 20000
LIMITATION = ('Input-boundary replay only: host clocks and thread scheduling are not restored. '
              'Matching inputs do not establish deterministic full-machine replay. '
              'An abrupt exit can lose the writer queue tail (normally up to 100 ms).')


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def durable_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8') as output:
        json.dump(value, output, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(path)


def utc():
    return datetime.now(timezone.utc).isoformat()


def safe_relative(root, relative):
    relative = Path(relative)
    if relative.is_absolute() or '..' in relative.parts or ':' in str(relative):
        raise ValueError('Invalid diagnostic bundle path')
    candidate = root / relative
    if not candidate.resolve().is_relative_to(root.resolve()):
        raise ValueError('Diagnostic path leaves its session')
    for part in (candidate, *candidate.parents):
        if part.is_relative_to(root) and (part.is_symlink() or getattr(part, 'is_junction', lambda: False)()):
            raise ValueError('Diagnostic symlinks and junctions are not accepted')
    return candidate


def snapshot_tree(source, destination, *, limit=SNAPSHOT_LIMIT, include_top=None):
    """Bounded copy, never writes to the source or traverses links/reparse dirs."""
    source, destination = Path(source), Path(destination)
    files, total = [], 0
    if source.is_symlink() or getattr(source, 'is_junction', lambda: False)():
        raise ValueError('Linked save/profile directories cannot be snapshotted')
    for parent, directories, names in os.walk(source, followlinks=False):
        parent = Path(parent)
        if include_top is not None and parent == source:
            directories[:] = [name for name in directories if name.casefold() in include_top]
            names = [name for name in names if name.casefold() in include_top]
        for name in directories + names:
            path = parent / name
            if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                raise ValueError('Linked save/profile contents cannot be snapshotted')
        for name in names:
            path = parent / name
            if not path.is_file():
                raise ValueError('Unsupported save/profile entry')
            total += path.stat().st_size
            files.append(path)
            if len(files) > MAX_FILES or total > limit:
                raise ValueError('Initial save/profile snapshot exceeds its 128 MiB budget')
    destination.mkdir(parents=True, exist_ok=False)
    manifest = {}
    for path in sorted(files):
        relative = path.relative_to(source).as_posix()
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        manifest[relative] = dict(size=target.stat().st_size, sha256=digest(target))
    return manifest


def inspect_inputs(path):
    """Validate bounded fixed records; never execute, allocate from record sizes,
    or accept a partially written record as a completed input delivery."""
    path = Path(path)
    size = path.stat().st_size
    if size < len(INPUT_HEADER) or size > INPUT_LIMIT:
        raise ValueError('Missing, oversized or incomplete input recording')
    counts, previous_time, sequence = {1: 0, 2: 0, 3: 0}, 0, 0
    with path.open('rb') as source:
        if source.read(16) != INPUT_HEADER:
            raise ValueError('Unsupported input recording header')
        while raw := source.read(INPUT_RECORD.size):
            if len(raw) != INPUT_RECORD.size:
                raise ValueError('Input recording ends in a partial record')
            seq, when, kind, port, status, argument, payload, reserved = INPUT_RECORD.unpack(raw)
            if (seq != sequence or when < previous_time or kind not in counts or port > 3 or
                    any(reserved) or (kind != 3 and argument) or
                    (kind == 1 and any(payload[18:])) or (kind == 3 and any(payload))):
                raise ValueError('Invalid input boundary record at sequence ' + str(sequence))
            counts[kind] += 1
            previous_time, sequence = when, sequence + 1
    return dict(records=sequence, state_calls=counts[1], capabilities_calls=counts[2],
                rumble_calls=counts[3], last_elapsed_ms=previous_time, sha256=digest(path))


def stop_summary(log):
    """Only a whitelisted terminal marker is public; raw logs remain private."""
    with Path(log).open('rb') as source:
        source.seek(0, 2)
        source.seek(max(0, source.tell() - 65536))
        tail = source.read().decode('utf-8', errors='replace')
    matches = re.findall(r'\[(?:PILOT STOP|D3D BACKEND STOP|PILOT AUDIO STOP|PILOT DSP STOP|PILOT INPUT STOP|SESSION REPLAY DIVERGENCE)\] ([A-Za-z0-9 _(),=:+.-]{1,180})', tail)
    return matches[-1] if matches else None


def input_status(log):
    # This marker can occur well before a later terminal stop. Scan without
    # keeping the whole game log in memory, preserving the capture failure.
    status = dict(warning=False, final=None)
    with Path(log).open('r', encoding='utf-8', errors='replace') as source:
        for line in source:
            if '[DIAGNOSTIC INPUTS INCOMPLETE]' in line:
                status['warning'] = True
            final = re.fullmatch(r'\[DIAGNOSTIC INPUTS FINAL\] records=(\d+) flushed=([01])\s*', line)
            if final:
                status['final'] = dict(records=int(final[1]), flushed=final[2] == '1')
    return status


def private_error(private, label, error):
    try:
        with (private / 'capture-errors.txt').open('a', encoding='utf-8') as output:
            output.write(label + ': ' + repr(error) + '\n')
    except OSError:
        pass  # A failing diagnostics disk must not replace the game failure.


def bounded_json(path):
    if Path(path).stat().st_size > 8 * 1024 * 1024:
        raise ValueError('Oversized diagnostic metadata')
    value = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(value, dict):
        raise ValueError('Invalid diagnostic metadata')
    return value


class Session:
    def __init__(self, root, data, executable, image, profile, state, selection, env, *, enabled=False, hdd=None):
        self.path = Path(root) / 'data/diagnostics' / (datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:12])
        self.path.mkdir(parents=True, exist_ok=False)
        self.private = self.path / 'PRIVATE-DO-NOT-SHARE'
        self.private.mkdir()
        self.log_path = self.private / 'game.log'
        self.report = dict(schema=SCHEMA, started_utc=utc(), state='preparing',
            title=data['title'].get('name', ''), xbe_sha256=data['title']['xbe_sha256'],
            executable_sha256=digest(executable), framework_revision=data.get('framework_revision'),
            framework_dirty=data.get('framework_dirty', False),
            package_manifest_sha256=digest(Path(root) / 'release.json') if (Path(root) / 'release.json').is_file() else None,
            packaged_files=data.get('files', {}), system=dict(os=platform.system(), release=platform.release(),
                machine=platform.machine(), processor=os.environ.get('PROCESSOR_IDENTIFIER', platform.machine()), cpu_count=os.cpu_count()),
            selection=selection, diagnostic_requested=enabled, replay_available=False, limitation=LIMITATION,
            privacy='Share report.json by default. PRIVATE-DO-NOT-SHARE contains logs, saves and console signing secrets.')
        self.replay = None
        if enabled:
            try:
                if hdd:
                    raise ValueError('HDD image sessions retain inputs/logs only; no large disk image is automatically cloned. Reproduction needs an explicitly prepared independent copy of its initial disk.')
                # Xbox partition1 is persistent E: (UDATA/TDATA). C: may hold
                # original Dashboard content; X:/Y:/Z: and their manifest are
                # rebuildable caches and can be hundreds of megabytes. Do not
                # copy original content or bulk caches into every bug report.
                state_files = snapshot_tree(state, self.private / 'initial-state', include_top={'partition1'})
                profile_files = snapshot_tree(profile, self.private / 'initial-console')
                plan = env.get('XLLE_PILOT_MOD_PLAN')
                if plan:
                    shutil.copyfile(plan, self.private / 'mods.plan')
                self.replay = dict(schema=SCHEMA, executable_sha256=self.report['executable_sha256'],
                    package_manifest_sha256=self.report['package_manifest_sha256'], xbe_sha256=data['title']['xbe_sha256'],
                    state_files=state_files, console_files=profile_files,
                    state_policy='persistent-partition1-only-cold-cache',
                    mod_plan_sha256=digest(self.private / 'mods.plan') if plan else None,
                    environment={k: v for k, v in env.items() if k.startswith('XLLE_PILOT_') or k in ('RECOMP_VOLUME', 'SDL_AUDIODRIVER', 'SDL_AUDIO_DRIVER')})
                durable_json(self.private / 'replay.json', self.replay)
                self.report['initial_snapshot'] = 'complete'
                self.report['initial_snapshot_policy'] = 'Persistent partition1 and console profile; original Dashboard content and utility caches are omitted. Replay starts with cold caches.'
            except (OSError, ValueError) as error:
                self.report['initial_snapshot'] = 'unavailable'
                self.report['initial_snapshot_reason'] = 'hdd-image-not-snapshotted' if hdd else 'snapshot-failed-see-private-log'
                private_error(self.private, 'initial snapshot', error)
            env['XLLE_PILOT_DIAGNOSTIC_RECORD'] = str(self.private / 'inputs.bin')
        durable_json(self.path / 'report.json', self.report)
        durable_json(self.path.parent / 'last-run.json', dict(session=self.path.name, **self.report))
        (self.path / 'README.txt').write_text(
            'Retained xboxllr diagnostic session\n\nShare report.json first.\n'
            'Do not upload PRIVATE-DO-NOT-SHARE: it can contain console signing keys,\n'
            'EEPROM, private saves, original file paths and raw game logs.\n\n' + LIMITATION + '\n', encoding='utf-8')

    def started(self, child):
        self.report.update(state='running', child_pid=child.pid)
        durable_json(self.path / 'report.json', self.report)
        durable_json(self.path.parent / 'last-run.json', dict(session=self.path.name, **self.report))

    def finish(self, code, *, reason='process-exit'):
        self.report.update(state='finished', finished_utc=utc(), exit_code=code, exit_reason=reason,
                           stop=stop_summary(self.log_path) if self.log_path.exists() else None)
        inputs = self.private / 'inputs.bin'
        status = input_status(self.log_path) if self.log_path.exists() else dict(warning=False, final=None)
        self.report['input_delivery_complete'] = False
        if inputs.exists():
            try:
                self.report['inputs'] = inspect_inputs(inputs)
                if status['warning']:
                    self.report['input_error'] = 'Native input recording stopped before the session ended; see private log.'
                final = status['final']
                self.report['input_delivery_complete'] = bool(final and final['flushed'] and
                    final['records'] == self.report['inputs']['records'] and not status['warning'])
                self.report['replay_available'] = bool(self.replay and self.report['inputs']['records'] and not status['warning'])
            except (OSError, ValueError) as error:
                self.report['input_error'] = 'Input recording is invalid or unavailable; see private log.'
                private_error(self.private, 'input recording', error)
        elif self.report['diagnostic_requested']:
            self.report['input_error'] = 'No native input recording was produced; the host may not support capture or may have stopped before initialization.'
        durable_json(self.path / 'report.json', self.report)
        durable_json(self.path.parent / 'last-run.json', dict(session=self.path.name, **self.report))


def inspect_session(session):
    session = Path(session).resolve(strict=True)
    report = bounded_json(safe_relative(session, 'report.json'))
    if report.get('schema') != SCHEMA:
        raise ValueError('Unsupported diagnostic session')
    if report.get('input_error'):
        raise ValueError('Capture is incomplete: ' + str(report['input_error']))
    private = safe_relative(session, 'PRIVATE-DO-NOT-SHARE')
    replay = bounded_json(safe_relative(private, 'replay.json'))
    if replay.get('schema') != SCHEMA:
        raise ValueError('Unsupported reproduction metadata')
    if replay.get('state_policy') != 'persistent-partition1-only-cold-cache':
        raise ValueError('Unsupported initial-state policy')
    for key, directory in [('state_files', 'initial-state'), ('console_files', 'initial-console')]:
        entries = replay[key]
        if not isinstance(entries, dict) or len(entries) > MAX_FILES:
            raise ValueError('Invalid snapshot manifest')
        total = 0
        for name, expected in entries.items():
            path = safe_relative(private / directory, name)
            total += path.stat().st_size
            if total > SNAPSHOT_LIMIT or path.stat().st_size != expected['size'] or digest(path) != expected['sha256']:
                raise ValueError('Initial snapshot changed or exceeds its budget')
    if replay.get('mod_plan_sha256') and digest(safe_relative(private, 'mods.plan')) != replay['mod_plan_sha256']:
        raise ValueError('Recorded mod plan changed')
    inputs = inspect_inputs(safe_relative(private, 'inputs.bin'))
    if report.get('inputs') and inputs != report['inputs']:
        raise ValueError('Input recording changed after the session ended')
    return report, replay, inputs
