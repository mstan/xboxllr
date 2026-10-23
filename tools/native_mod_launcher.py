"""Schema-driven native Xbox mod provider for the pinned recomp-ui launcher.

Bundled manifests supply labels and independent feature defaults. The title
supplies capabilities and its final resolver. Selection and visibility are
data only; this module never imports a package or writes generated guest code.
"""
import hashlib
import copy
import json
import os
import re
from pathlib import Path
import subprocess
import tomllib


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def encoded(value):
    return str(value).encode('utf-8').hex() or '-'


def decoded(value):
    return '' if value == '-' else bytes.fromhex(value).decode('utf-8')


class Catalog:
    def __init__(self, manifests, state, *, visibility=(), frame_modes=(), setup=None, defaults=None, boot=None):
        self.state_path = Path(state)
        defaults = {} if defaults is None else defaults
        if not isinstance(defaults, dict) or any(not isinstance(key, str) or not isinstance(value, dict) or
                set(value) - {'enabled', 'options'} or type(value.get('enabled', False)) is not bool or
                not isinstance(value.get('options', {}), dict) or
                any(not isinstance(k, str) or not isinstance(v, str) for k, v in value.get('options', {}).items())
                for key, value in defaults.items()):
            raise ValueError('Invalid title enhancement defaults')
        console_keys = {'acknowledged','eeprom','storage','locked','choice_path'}
        if setup is not None and (set(setup) not in ({'required', 'kernel'}, {'required', 'kernel'} | console_keys, {'required', 'kernel','hdd'} | console_keys) or
                type(setup['required']) is not bool or not isinstance(setup['kernel'], str) or
                ('acknowledged' in setup and (any(type(setup[k]) is not bool for k in ('acknowledged','locked')) or
                 any(not isinstance(setup[k], str) for k in ('eeprom','storage','choice_path')))) or
                ('hdd' in setup and not isinstance(setup['hdd'], str))):
            raise ValueError('Invalid native setup state')
        self.setup = dict(setup) if setup is not None else None
        if boot is not None and (self.setup is None or 'acknowledged' not in self.setup or
                set(boot) != {'enabled', 'kernel', 'status', 'choice_path'} or
                type(boot['enabled']) is not bool or any(not isinstance(boot[k], str) or '\0' in boot[k]
                    for k in ('kernel', 'status', 'choice_path')) or
                len(boot['kernel'].encode('utf-8')) > 4095 or len(boot['status'].encode('utf-8')) > 511):
            raise ValueError('Invalid native startup state')
        self.boot = dict(boot) if boot is not None else None
        self.packages, self.features, self.hashes = [], [], {}
        hidden, hidden_choices = set(), {}
        for path in visibility:
            if not Path(path).is_file():
                continue
            policy = json.loads(Path(path).read_text(encoding='utf-8'))
            if policy.get('version') != 1 or set(policy) != {'version', 'hidden_features', 'hidden_choices'}:
                raise ValueError('Invalid mod visibility policy')
            hidden.update(policy['hidden_features'])
            for key, values in policy['hidden_choices'].items():
                hidden_choices.setdefault(key, set()).update(values)
        persisted = json.loads(self.state_path.read_text(encoding='utf-8')) if self.state_path.is_file() else {}
        if persisted and persisted.get('version') != 1:
            raise ValueError('Unsupported mod selection version')
        self.volume = persisted.get('volume', 100)
        self.skip_launcher = persisted.get('skip_launcher') is True
        self.player = persisted.get('player1', dict(source='auto', guid=''))
        self.validate_player(self.player)
        if type(self.volume) is not int or not 0 <= self.volume <= 100:
            self.volume = 100
        for path in map(Path, manifests):
            manifest = tomllib.loads(path.read_text(encoding='utf-8'))
            pid = manifest['id']
            if manifest['format_version'] != 1 or manifest['version'] != '1.0.0':
                raise ValueError('Unsupported bundled mod catalog')
            self.hashes[pid] = hashlib.sha256(path.read_bytes()).hexdigest()
            for entry in manifest['feature']:
                fid = entry['id'];key = pid + '/' + fid
                if key in hidden or (fid == 'frame-rate' and not frame_modes):
                    continue
                if entry['default_enabled'] is not False:
                    raise ValueError('Elective native enhancements must default off')
                # Explicit title defaults are used only for new selections.
                # Saved owner choices, including disabled features, win.
                saved = persisted.get('features', {}).get(key, defaults.get(key, {}))
                feature = dict(package=pid, id=fid, name=entry['name'], description=entry.get('description', manifest['description']),
                               enabled=saved.get('enabled') is True, options=[])
                authored_options = [item for item in manifest.get('option', []) if item['feature'] == fid]
                for item in authored_options:
                    oid = item['id']
                    if item['type'] == 'aspect':
                        values = ['adaptive', '4:3'] + item['presets']
                        labels = ['Adaptive (Fit to screen)', 'Original 4:3'] + item['presets']
                    elif item['type'] == 'choice':
                        values, labels = item['values'], item['labels']
                    else:
                        raise ValueError('Unsupported bundled UI option type')
                    choices = list(zip(values, labels))
                    if fid == 'frame-rate' and oid == 'mode':
                        choices = [(v, label) for v, label in choices if v == 'original' or v in frame_modes]
                    if fid == 'frame-rate' and oid == 'rate' and 'actual' in frame_modes:
                        choices = [(v, label) for v, label in choices if v == '60']
                    excluded = hidden_choices.get(key + '/' + oid, set())
                    choices = [(v, label) for v, label in choices if v not in excluded]
                    if not choices:
                        feature['enabled'] = False
                        feature['options'] = []
                        break
                    default = item['default'] if any(v == item['default'] for v, _ in choices) else choices[0][0]
                    selected = saved.get('options', {}).get(oid, default)
                    if not any(v == selected for v, _ in choices):
                        selected = default
                        feature['enabled'] = False
                    feature['options'].append(dict(id=oid, label=item.get('label', {'height': 'Render height', 'method': 'Method', 'axis': 'Samples',
                        'anisotropy': 'Filter strength', 'aspect': 'Aspect ratio', 'mode': 'Mode', 'rate': 'Target rate'}.get(oid, oid)),
                        value=selected, default=default, choices=choices))
                # A checkbox-only feature has no authored options. A feature
                # whose choices were all hidden must still remain unavailable.
                if feature['options'] or not authored_options:
                    self.features.append(feature)
            if any(f['package'] == pid for f in self.features):
                self.packages.append(dict(id=pid, name=manifest['name'], version=manifest['version'], description=manifest['description']))

    def write(self, path, title, region, sha):
        lines = ['xlle-launcher 1', f'title {encoded(title)} {encoded(region)}', f'identity {sha}', f'volume {self.volume}', f'skip {int(self.skip_launcher)}']
        lines.append(f'player {self.player["source"]} {encoded(self.player["guid"])}')
        if self.setup is not None:
            lines.append(f'setup {int(self.setup["required"])} {encoded(self.setup["kernel"])}')
            if 'acknowledged' in self.setup:
                s = self.setup
                lines.append(f'console {int(s["acknowledged"])} {int(s["locked"])} {encoded(s["eeprom"])} {encoded(s["storage"])} {encoded(s["choice_path"])}' +
                             (f' {encoded(s["hdd"])}' if 'hdd' in s else ''))
        if self.boot is not None:
            b = self.boot
            lines.append(f'boot {int(b["enabled"])} {encoded(b["kernel"])} {encoded(b["status"])} {encoded(b["choice_path"])}')
        for p in self.packages:
            lines.append('package ' + ' '.join(encoded(p[k]) for k in ('id', 'name', 'version', 'description')))
        for f in self.features:
            ids = encoded(f['package']) + ' ' + encoded(f['id'])
            lines.append(f'feature {ids} {encoded(f["name"])} {encoded(f["description"])} {int(f["enabled"])}')
            for o in f['options']:
                prefix = ids + ' ' + encoded(o['id'])
                lines.append('option ' + prefix + ' ' + ' '.join(encoded(o[k]) for k in ('label', 'value', 'default')))
                for value, label in o['choices']:
                    lines.append(f'choice {prefix} {encoded(value)} {encoded(label)}')
        lines.append('end')
        Path(path).write_text('\n'.join(lines) + '\n', encoding='ascii', newline='\n')

    def ingest(self, path):
        if Path(path).stat().st_size > 262144:
            raise ValueError('Oversized launcher result')
        lines = Path(path).read_text(encoding='ascii').splitlines()
        if not lines or lines[0] != 'xlle-launcher-result 1' or lines[-1] != 'end' or len(lines) > 600:
            raise ValueError('Incomplete launcher result')
        result = None;image = '';seen = set()
        staged = copy.deepcopy(self.features)
        staged_volume = self.volume
        staged_skip = self.skip_launcher
        staged_player = dict(self.player)
        staged_kernel = self.setup['kernel'] if self.setup is not None else None
        staged_console = dict(self.setup) if self.setup is not None and 'acknowledged' in self.setup else None
        staged_boot = dict(self.boot) if self.boot is not None else None
        by_key = {(f['package'], f['id']): f for f in staged}
        for line in lines[1:-1]:
            t = line.split()
            if not t:
                raise ValueError('Invalid launcher result')
            if t[0] == 'result' and len(t) == 2 and result is None:
                result = int(t[1])
            elif t[0] == 'image' and len(t) == 2 and 'image' not in seen:
                image = decoded(t[1]);seen.add('image')
            elif t[0] == 'volume' and len(t) == 2 and 'volume' not in seen:
                volume = int(t[1])
                if not 0 <= volume <= 100:
                    raise ValueError('Invalid volume')
                staged_volume = volume;seen.add('volume')
            elif t[0] == 'skip' and len(t) == 2 and 'skip' not in seen and t[1] in ('0', '1'):
                staged_skip = t[1] == '1';seen.add('skip')
            elif t[0] == 'player' and len(t) == 3 and 'player' not in seen:
                staged_player = dict(source=t[1], guid=decoded(t[2]))
                self.validate_player(staged_player)
                seen.add('player')
            elif t[0] == 'feature' and len(t) == 4:
                key = tuple(map(decoded, t[1:3]));f = by_key[key]
                if key in seen or t[3] not in ('0', '1'):
                    raise ValueError('Duplicate or invalid feature state')
                seen.add(key);f['enabled'] = t[3] == '1'
            elif t[0] == 'kernel' and len(t) == 2 and self.setup is not None and 'kernel' not in seen:
                staged_kernel = decoded(t[1]);seen.add('kernel')
            elif t[0] == 'console' and len(t) in (4,5) and staged_console is not None and 'console' not in seen and t[1] in ('0','1'):
                staged_console.update(acknowledged=t[1]=='1', eeprom=decoded(t[2]), storage=decoded(t[3]))
                if len(t)==5: staged_console['hdd']=decoded(t[4])
                seen.add('console')
            elif t[0] == 'boot' and len(t) == 3 and staged_boot is not None and 'boot' not in seen and t[1] in ('0', '1'):
                kernel = decoded(t[2])
                if '\0' in kernel or len(kernel.encode('utf-8')) > 4095:
                    raise ValueError('Invalid startup kernel path')
                staged_boot.update(enabled=t[1] == '1', kernel=kernel)
                seen.add('boot')
            elif t[0] == 'option' and len(t) == 5:
                key = tuple(map(decoded, t[1:4]));f = by_key[key[:2]];value = decoded(t[4])
                o = next(o for o in f['options'] if o['id'] == key[2])
                if key in seen or not any(v == value for v, _ in o['choices']):
                    raise ValueError('Unsupported launcher option')
                seen.add(key);o['value'] = value
            else:
                raise ValueError('Invalid launcher result record')
        expected = {'image', 'volume', 'skip', 'player'} | set(by_key) | {(f['package'], f['id'], o['id']) for f in self.features for o in f['options']}
        if self.setup is not None:
            expected.add('kernel')
        if staged_console is not None:
            expected.add('console')
        if staged_boot is not None:
            expected.add('boot')
        if seen != expected or result not in (0, 1, 2, 3):
            raise ValueError('Incomplete launcher result inventory')
        self.features, self.volume = staged, staged_volume
        self.skip_launcher = staged_skip
        self.player = staged_player
        self.boot = staged_boot
        if self.setup is not None:
            if staged_console is not None:
                self.setup = staged_console
            self.setup['kernel'] = staged_kernel
        return result, image

    @staticmethod
    def validate_player(player):
        if not isinstance(player, dict) or set(player) != {'source', 'guid'} or player['source'] not in ('auto', 'none', 'keyboard', 'gamepad'):
            raise ValueError('Invalid Player 1 source')
        guid = player['guid']
        if not isinstance(guid, str) or (player['source'] == 'gamepad' and not re.fullmatch('[0-9a-f]{32}', guid)) or (player['source'] != 'gamepad' and guid):
            raise ValueError('Invalid Player 1 controller identity')

    def selection(self):
        return dict(version=1, volume=self.volume, skip_launcher=self.skip_launcher, player1=dict(self.player), features={f['package'] + '/' + f['id']:
                    dict(enabled=f['enabled'], options={o['id']: o['value'] for o in f['options']}) for f in self.features})

    def values(self):
        return {(f['package'], f['id'], o['id']): o['value'] for f in self.features if f['enabled'] for o in f['options']}

    def run(self, binary, output, *, title, region, sha, image, validate=False, env=None):
        output = Path(output);output.mkdir(parents=True, exist_ok=True)
        source, target = output / 'launcher.catalog', output / 'launcher.result'
        self.write(source, title, region, sha)
        command = [str(binary), str(source.resolve()), str(target.resolve()), str(image)]
        if validate:
            command.append('--validate')
        subprocess.run(command, cwd=Path(binary).parent, env=env, check=True)
        result, selected_image = self.ingest(target)
        if result in (0, 1):
            atomic_json(self.state_path, self.selection())
        return result, selected_image
