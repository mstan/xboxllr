"""Private, verified native session replay from launch.

The native journal records external observations and CPU/device admission;
normal translated execution rebuilds native call stacks and device ownership.
This is not an arbitrary-PC save-state format. Old input-only sessions retain
their separate inspector and are never promoted to this replay contract.
"""
from __future__ import annotations
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import zlib

from native_diagnostics import (Session, bounded_json, digest, durable_json,
                                private_error, safe_relative, snapshot_tree, stop_summary, utc)

SCHEMA = 'xboxllr.session-replay.v2'
MAGIC = b'XLRRPLY2'
HEADER = struct.Struct('<8sII32s')
RECORD = struct.Struct('<QIIQII')
TURN = struct.Struct('<IIQQ')
SCHEDULE_ACTOR = 0xfffffffe
TURN_TAG = 0x5455524e
VALUE_LIMIT = 1024 * 1024
JOURNAL_LIMIT = 2 * 1024**3
STATE_LIMIT = 2 * 1024**3
PROFILE_LIMIT = 128 * 1024**2
HDD_LIMIT = 32 * 1024**3
HISTORY_LIMIT = 32 * 1024**3
FREE_FLOOR = 4 * 1024**3
MAX_ACTORS = 512
LIMITATION = ('Replay starts at the recorded launch with the identical executable, dependencies, '
              'game content and private initial storage. Each CPU/device turn and frame state check '
              'must match. An interrupted journal has only a verified prefix; it does not establish '
              'that the original terminal failure was reproduced. Recordings are private and local.')


def journal_records(path, identity, actor):
    """Streaming, allocation-bounded parser shared by inspection and tests."""
    path = Path(path)
    if path.is_symlink() or getattr(path, 'is_junction', lambda: False)() or not path.is_file():
        raise ValueError('Journal entries must be ordinary files')
    if not HEADER.size <= path.stat().st_size <= JOURNAL_LIMIT:
        raise ValueError('Journal file is missing or oversized')
    with path.open('rb') as source:
        raw = source.read(HEADER.size)
        if len(raw) != HEADER.size or HEADER.unpack(raw) != (MAGIC, 2, actor, bytes.fromhex(identity)):
            raise ValueError('Journal executable identity or header differs')
        sequence = 0
        while raw := source.read(RECORD.size):
            if len(raw) != RECORD.size:
                raise ValueError('Journal ends in an incomplete record header')
            seq, tag, size, argument, checksum, reserved = RECORD.unpack(raw)
            if seq != sequence or not tag or size > VALUE_LIMIT or reserved:
                raise ValueError('Invalid journal sequence, tag, size or reserved bytes')
            payload = source.read(size)
            if len(payload) != size or zlib.crc32(payload, zlib.crc32(raw[:24])) != checksum:
                raise ValueError('Journal record checksum or extent differs')
            yield seq, tag, argument, payload
            sequence += 1


def inspect_journals(directory, identity):
    directory = Path(directory)
    paths = list(directory.iterdir())
    if not paths or len(paths) > MAX_ACTORS:
        raise ValueError('Missing or excessive actor journals')
    files, records, total, frames, fault = {}, 0, 0, 0, None
    turns, terminal, previous = 0, False, {}
    for path in sorted(paths):
        match = re.fullmatch(r'actor-([0-9a-f]{8})\.xlr', path.name)
        if not match:
            raise ValueError('Unexpected journal entry')
        actor = int(match[1], 16)
        total += path.stat().st_size
        if total > JOURNAL_LIMIT + MAX_ACTORS * HEADER.size:
            raise ValueError('Session journal budget exceeded')
        count = 0
        for _, tag, argument, payload in journal_records(path, identity, actor):
            if actor == SCHEDULE_ACTOR:
                if tag != TURN_TAG or argument or len(payload) != TURN.size or terminal:
                    raise ValueError('Invalid record in CPU/device schedule')
                owner, boundary, checkpoint, evidence = TURN.unpack(payload)
                if not 1 <= boundary <= 6 or checkpoint < previous.get(owner, 0):
                    raise ValueError('Invalid CPU/device checkpoint order')
                if not turns and (owner != 0 or boundary != 1):
                    raise ValueError('Schedule does not start at the launch baseline')
                previous[owner] = checkpoint
                terminal = boundary == 6
                turns += 1
            elif tag == 12:
                if len(payload) != 32:
                    raise ValueError('Invalid frame evidence record')
                frame, _, _, _ = struct.unpack('<QQQQ', payload)
                if frame != argument:
                    raise ValueError('Frame evidence identity differs')
                frames += 1
            elif tag == 15:
                if len(payload) > 180:
                    raise ValueError('Oversized terminal observation')
                fault = dict(actor=actor, code=argument, reason=payload.decode('ascii', 'strict'))
            count += 1
        files[path.name] = dict(size=path.stat().st_size, sha256=digest(path), records=count)
        records += count
    if not turns:
        raise ValueError('Session has no complete CPU/device turns')
    return dict(files=files, records=records, turns=turns, frames=frames, bytes=total,
                terminal_complete=terminal and fault is not None, terminal=fault)


def sparse_copy(source, destination, *, limit=HDD_LIMIT):
    """Independent file copy; never links back to mutable owner storage."""
    source, destination = Path(source), Path(destination)
    if source.is_symlink() or getattr(source, 'is_junction', lambda: False)() or not source.is_file():
        raise ValueError('Snapshot source must be an ordinary file')
    before = source.stat()
    if before.st_size > limit:
        raise ValueError('HDD snapshot exceeds the recording budget')
    hasher = hashlib.sha256()
    chunk_size = 1024 * 1024
    zero = bytes(chunk_size)
    with source.open('rb') as original, destination.open('xb') as copied:
        sparse = os.name != 'nt'
        if os.name == 'nt':
            import ctypes
            import msvcrt
            from ctypes import wintypes
            api = ctypes.WinDLL('kernel32', use_last_error=True).DeviceIoControl
            api.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                            wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
            api.restype = wintypes.BOOL
            returned = wintypes.DWORD()
            sparse = bool(api(msvcrt.get_osfhandle(copied.fileno()), 0x900c4, None, 0, None, 0,
                              ctypes.byref(returned), None))  # FSCTL_SET_SPARSE on our new file only.
        copied_bytes = 0
        while chunk := original.read(chunk_size):
            copied_bytes += len(chunk)
            if copied_bytes > limit:
                raise ValueError('Snapshot source grew beyond its budget')
            hasher.update(chunk)
            if sparse and chunk == zero[:len(chunk)]:
                copied.seek(len(chunk), 1)
            else:
                copied.write(chunk)
        copied.truncate(copied_bytes)
        copied.flush()
        os.fsync(copied.fileno())
    after = source.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or copied_bytes != before.st_size:
        raise ValueError('Snapshot source changed during capture')
    return dict(size=copied_bytes, sha256=hasher.hexdigest())


def retained_history(parent):
    """Read immediate session receipts; never crawl other folders or game media."""
    total = 0
    for path in parent.iterdir():
        if not path.is_dir() or path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            continue
        report = path / 'report.json'
        if report.is_file():
            data = bounded_json(report)
            if data.get('schema') == SCHEMA:
                value = data.get('reserved_bytes', data.get('retained_bytes', 0))
                if type(value) is not int or not 0 <= value <= HDD_LIMIT + 2 * STATE_LIMIT + JOURNAL_LIMIT:
                    raise ValueError('Invalid retained-session storage receipt')
                total += value
    return total


def snapshot_storage(source, destination, *, limit):
    files = snapshot_tree(source, destination, limit=limit)
    directories = []
    for parent, names, _ in os.walk(source, followlinks=False):
        for name in names:
            path = Path(parent) / name
            if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                raise ValueError('Linked baseline directories are not supported')
            relative = path.relative_to(source).as_posix()
            directories.append(relative)
            if len(directories) + len(files) > 20000:
                raise ValueError('Baseline directory/file count exceeds its budget')
            (Path(destination) / relative).mkdir(parents=True, exist_ok=True)
    return files, sorted(directories)


class FullSession(Session):
    def __init__(self, root, data, executable, image, profile, state, selection, env, *, hdd=None):
        super().__init__(root, data, executable, image, profile, state, selection, env, enabled=False, hdd=hdd)
        self.report.update(schema=SCHEMA, replay_kind='verified-from-launch', diagnostic_requested=True,
                           limitation=LIMITATION, initial_snapshot='unavailable', replay_available=False)
        self.replay = None
        try:
            hdd_bytes = Path(hdd['path']).stat().st_size if hdd else 0
            reserve = STATE_LIMIT + PROFILE_LIMIT + JOURNAL_LIMIT + hdd_bytes
            if hdd_bytes > HDD_LIMIT or retained_history(self.path.parent) + reserve > HISTORY_LIMIT:
                raise ValueError('Local recording history budget reached; existing sessions were preserved')
            if shutil.disk_usage(self.path).free < reserve + FREE_FLOOR:
                raise ValueError('Insufficient free storage for a complete replay baseline and journal')
            self.report['reserved_bytes'] = reserve
            self.persist()
            state_files, state_directories = snapshot_storage(state, self.private / 'initial-state', limit=STATE_LIMIT)
            console_files, console_directories = snapshot_storage(profile, self.private / 'initial-console', limit=PROFILE_LIMIT)
            disk = None
            if hdd:
                disk = dict(file='initial-hdd.img', identity=hdd['identity'],
                            **sparse_copy(hdd['path'], self.private / 'initial-hdd.img'))
            plan = env.get('XLLE_PILOT_MOD_PLAN')
            if plan:
                shutil.copyfile(plan, self.private / 'mods.plan')
            self.replay = dict(schema=SCHEMA, executable_sha256=self.report['executable_sha256'],
                package_manifest_sha256=self.report['package_manifest_sha256'],
                xbe_sha256=data['title']['xbe_sha256'], state_files=state_files, console_files=console_files,
                state_directories=state_directories, console_directories=console_directories,
                hdd=disk, state_policy='complete-writable-storage',
                mod_plan_sha256=digest(self.private / 'mods.plan') if plan else None,
                environment={k: v for k, v in env.items() if k.startswith('XLLE_PILOT_') or k == 'RECOMP_VOLUME'})
            durable_json(self.private / 'replay.json', self.replay)
            journal = self.private / 'journal'
            journal.mkdir()
            env['XLLE_PILOT_SESSION_RECORD'] = str(journal)
            self.report['initial_snapshot'] = 'complete'
            self.report['initial_snapshot_policy'] = 'Complete writable storage, utility caches, console profile and any selected HDD image.'
        except (OSError, ValueError, KeyError) as error:
            self.report['initial_snapshot_reason'] = 'capture-unavailable-see-private-log'
            private_error(self.private, 'full replay baseline', error)
            # A partial baseline must never enable a recorder that appears usable.
            env.pop('XLLE_PILOT_SESSION_RECORD', None)
        self.persist()
        (self.path / 'README.txt').write_text(
            'Private native session replay\n\nShare report.json first.\n'
            'PRIVATE-DO-NOT-SHARE can contain signing secrets, personal saves,\n'
            'cached original content and an HDD snapshot. Never upload it wholesale.\n\n' + LIMITATION + '\n', encoding='utf-8')

    def persist(self):
        durable_json(self.path / 'report.json', self.report)
        durable_json(self.path.parent / 'last-run.json', dict(session=self.path.name, **self.report))

    def finish(self, code, *, reason='process-exit'):
        self.report.update(state='finished', finished_utc=utc(), exit_code=code, exit_reason=reason,
                           stop=stop_summary(self.log_path) if self.log_path.exists() else None,
                           journal_delivery_complete=False, replay_available=False)
        try:
            if not self.replay:
                raise ValueError('Complete initial storage was not captured')
            info = inspect_journals(self.private / 'journal', self.report['executable_sha256'])
            final = None
            incomplete = False
            with self.log_path.open(encoding='utf-8', errors='replace') as source:
                for line in source:
                    if '[SESSION REPLAY INCOMPLETE]' in line:
                        incomplete = True
                    match = re.fullmatch(r'\[SESSION REPLAY FINAL\] records=(\d+) written=(\d+) complete=([01])\s*', line)
                    if match:
                        final = tuple(map(int, match.groups()))
            complete = bool(final and final[2] and final[0] == final[1] == info['records'] and not incomplete)
            self.report.update(journals=info, journal_delivery_complete=complete,
                               replay_available=complete and info['terminal_complete'])
            self.replay['journals'] = info['files']
            durable_json(self.private / 'replay.json', self.replay)
            self.report['retained_bytes'] = (sum(f['size'] for f in self.replay['state_files'].values()) +
                sum(f['size'] for f in self.replay['console_files'].values()) +
                (self.replay['hdd']['size'] if self.replay['hdd'] else 0) + info['bytes'] + self.log_path.stat().st_size)
            self.report.pop('reserved_bytes', None)
        except (OSError, ValueError, KeyError, UnicodeError) as error:
            self.report['replay_error'] = 'Replay is incomplete or invalid; see the private capture log.'
            private_error(self.private, 'full replay journal', error)
        self.persist()


def inspect_session(session):
    session = Path(session).resolve(strict=True)
    report = bounded_json(safe_relative(session, 'report.json'))
    if report.get('schema') != SCHEMA or report.get('replay_kind') != 'verified-from-launch':
        raise ValueError('Expected a full native session replay recording')
    if not report.get('replay_available') or not report.get('journal_delivery_complete'):
        raise ValueError('Recording has no complete, sealed terminal replay')
    private = safe_relative(session, 'PRIVATE-DO-NOT-SHARE')
    replay = bounded_json(safe_relative(private, 'replay.json'))
    if replay.get('schema') != SCHEMA or replay.get('state_policy') != 'complete-writable-storage':
        raise ValueError('Unsupported replay baseline policy')
    for key, folder, limit in [('state_files', 'initial-state', STATE_LIMIT),
                               ('console_files', 'initial-console', PROFILE_LIMIT)]:
        entries = replay[key]
        if not isinstance(entries, dict) or len(entries) > 20000:
            raise ValueError('Invalid baseline manifest')
        directories = replay[key.replace('_files', '_directories')]
        if (not isinstance(directories, list) or any(not isinstance(name, str) for name in directories) or
            len(entries) + len(directories) > 20000 or len(set(directories)) != len(directories)):
            raise ValueError('Invalid baseline directory manifest')
        for name in directories:
            if not safe_relative(private / folder, name).is_dir():
                raise ValueError('Baseline directory changed')
        total = 0
        for name, metadata in entries.items():
            path = safe_relative(private / folder, name)
            total += path.stat().st_size
            if total > limit or path.stat().st_size != metadata['size'] or digest(path) != metadata['sha256']:
                raise ValueError('Replay baseline changed or exceeds its budget')
    disk = replay.get('hdd')
    if disk:
        if disk.get('file') != 'initial-hdd.img' or not re.fullmatch('[0-9a-f]{64}', disk.get('identity', '')):
            raise ValueError('Invalid HDD replay identity')
        path = safe_relative(private, disk['file'])
        if not 0 < path.stat().st_size <= HDD_LIMIT or path.stat().st_size != disk['size'] or digest(path) != disk['sha256']:
            raise ValueError('HDD replay baseline changed')
    if replay.get('mod_plan_sha256') and digest(safe_relative(private, 'mods.plan')) != replay['mod_plan_sha256']:
        raise ValueError('Replay mod plan changed')
    info = inspect_journals(safe_relative(private, 'journal'), replay['executable_sha256'])
    if not info['terminal_complete'] or info['files'] != replay.get('journals') or info != report.get('journals'):
        raise ValueError('Replay journal changed or terminal seal differs')
    return report, replay, info
