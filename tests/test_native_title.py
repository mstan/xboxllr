"""Native title driver contracts using authored inputs and mocked subprocesses."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_title as n


class NativeTitleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'external/title.toml'
        self.source.parent.mkdir()
        self.xbe = self.root / 'authored.xbe'
        self.xbe.write_bytes(b'authored executable')
        self.profile = self.root / 'profile.json'
        self.profile.write_text(json.dumps(dict(schema='native-title-profile.v1', xbe_sha256=n.digest(self.xbe))))
        self.source.write_text('''schema = "xboxllr.native-title.v1"
[title]
id = "authored-title"
name = "Authored Title"
executable = "Authored"
xbe_sha256 = "''' + n.digest(self.xbe) + '''"
[generation]
profile = "../profile.json"
bindings = ["cpu"]
[build]
PILOT_RENDERER = "D3D11"
[runtime]
XLLE_PILOT_TRACE = "0"
''')
        _, self.data = n.definition(self.source)
        self.output = self.root / 'output'
        (self.output / 'gen').mkdir(parents=True)
        artifact = self.output / 'gen/recomp.c'
        artifact.write_text('/* authored fixture */')
        self.manifest = self.output / 'generation.aot-private.json'
        self.manifest.write_text(json.dumps(dict(sha256=n.digest(self.xbe), files={artifact.name: n.digest(artifact)})))
        self.build = self.root / 'build'
        binary = n.executable(self.build, 'Authored')
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b'authored host binary')
        self.binary = binary
        self.paths = dict(xbe=self.xbe, eeprom=self.root / 'eeprom', media_root=self.root / 'media', state_dir=self.root / 'state')
        self.paths['eeprom'].write_bytes(b'authored')
        self.paths['media_root'].mkdir()
        self.paths['state_dir'].mkdir()
        self.receipt()

    def receipt(self):
        (self.output / 'native-title-preparation.json').write_text(json.dumps(n.preparation_identity(self.source, self.data, self.output)))
        value = n.identity(self.source, self.data, self.output)
        value['executable_sha256'] = n.digest(self.binary)
        (self.build / 'native-title-build.json').write_text(json.dumps(value))

    def test_owned_identity_and_profile_rejected(self):
        self.xbe.write_bytes(b'tampered')
        with self.assertRaises(ValueError):
            n.check_image(self.data['title'], self.paths)
        self.profile.write_text(json.dumps(dict(schema='native-title-profile.v1', xbe_sha256='0' * 64)))
        with self.assertRaises(ValueError):
            n.definition(self.source)

    def test_generated_tamper_and_unlisted_translation_rejected(self):
        notices = self.output / 'gen/third_party'
        notices.mkdir()
        (notices / 'LICENSE').write_text('authored notice')
        n.check_generated(self.output, self.data['title'])
        (self.output / 'gen/unlisted.c').write_text('extra')
        with self.assertRaises(ValueError):
            n.check_generated(self.output, self.data['title'])
        (self.output / 'gen/unlisted.c').unlink()
        (self.output / 'gen/recomp.c').write_text('changed')
        with self.assertRaises(ValueError):
            n.check_generated(self.output, self.data['title'])

    def test_external_profile_and_private_path_resolution(self):
        pathsfile = self.root / 'external/paths.json'
        pathsfile.write_text(json.dumps(dict(xbe='../authored.xbe')))
        self.assertEqual(n.private_paths(pathsfile)['xbe'], self.xbe)
        command = n.prepare_command(self.source, self.data, dict(cpu_donor=self.root, xbe=self.xbe, xdk_symbols=self.root / 'symbols'), self.output)
        self.assertEqual(command[command.index('--profile') + 1], str(self.profile))
        self.assertIn('--cpu-symbols', command)

    def test_native_configuration_for_external_title(self):
        command = n.configure_command(self.data, {}, self.output, self.build, 'cmake', 'Ninja', 1)
        for argument in ('-DXBOXLLR_BUILD_MODE=NATIVE_TITLE', '-DPILOT_EXECUTABLE_NAME=Authored', '-DPILOT_COMPILE_JOBS=1', '-DCMAKE_BUILD_TYPE=Release'):
            self.assertIn(argument, command)
        command = n.configure_command(self.data, {}, self.output, self.build, 'cmake', 'Visual Studio 17 2022', 1)
        self.assertEqual(command[command.index('-A') + 1], 'x64')
        self.assertFalse(any(x.startswith('-DCMAKE_BUILD_TYPE=') for x in command))
        keys = self.root / 'private-save-keys.bin'
        keys.write_bytes(b'authored fixture')
        command = n.configure_command(self.data, {'save_keys': keys}, self.output, self.build, 'cmake', 'Ninja', 1)
        self.assertIn('-DPILOT_SAVE_KEYS_FILE=' + str(keys), command)
        keys.unlink()
        with self.assertRaises(ValueError):
            n.configure_command(self.data, {'save_keys': keys}, self.output, self.build, 'cmake', 'Ninja', 1)

    def test_partition_keeps_proofs_and_invalidates_the_previous_binary(self):
        from native_unit_budget import parse_unit
        prefix = (b'/**\n * Authored generated unit\n */\n\n#define RECOMP_GENERATED_CODE\n'
                  b'#include "recomp_funcs.h"\n#include <math.h>\n\n')
        sections = [(
            f'/**\n * sub_{address:08X}\n * Original: 0x{address:08X} - 0x{address+4:08X} (4 bytes, 1 insns)\n'
            f' */\nvoid sub_{address:08X}(void)\n{{\n    return;\n}}\n\n'
        ).encode() for address in (0x1000, 0x2000)]
        numeric = self.output / 'gen/recomp_0000.c'
        numeric.write_bytes(prefix + b''.join(sections))
        manifest = json.loads(self.manifest.read_text())
        manifest['files'][numeric.name] = n.digest(numeric)
        self.manifest.write_text(json.dumps(manifest))
        self.receipt()
        prior = self.manifest.read_bytes()
        self.data['generation'].update(unit_max_functions=1, unit_max_bytes=524288)
        transformed = n.partition_prepared(self.source, self.data, self.output)
        self.assertTrue(transformed['changed'])
        self.assertEqual(self.manifest.with_name(self.manifest.name + '.before-unit-budget').read_bytes(), prior)
        recovered = [f.section for p in sorted((self.output/'gen').glob('recomp_0*.c'))
                     for f in parse_unit(p.read_bytes())[1]]
        self.assertEqual(recovered, sections)
        n.identity(self.source, self.data, self.output)
        with self.assertRaises(ValueError):
            n.launch(self.data, self.paths, self.output, self.build, source=self.source)
        published = self.manifest.read_bytes()
        self.assertFalse(n.partition_prepared(self.source, self.data, self.output)['changed'])
        self.assertEqual(self.manifest.read_bytes(), published)

    def test_partition_rejects_changed_proof_before_transform(self):
        self.data['generation'].update(unit_max_functions=1)
        self.profile.write_text(self.profile.read_text() + ' ')
        with patch('native_unit_budget.apply') as transform:
            with self.assertRaises(ValueError):
                n.partition_prepared(self.source, self.data, self.output)
            transform.assert_not_called()

    def test_deadline_policy_rejects_invalid_and_unowned_runs(self):
        cases = [dict(seconds=1), dict(automated=True), dict(automated=True, seconds=1, detach=True)]
        cases += [dict(automated=True, seconds=x) for x in (0, -1, float('nan'), float('inf'))]
        with patch.object(n.subprocess, 'Popen') as spawn:
            for kwargs in cases:
                with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                    n.launch(self.data, self.paths, self.output, self.build, source=self.source, **kwargs)
            spawn.assert_not_called()

    def test_owner_audio_visible_immutable_staging(self):
        child = Mock(pid=123, returncode=0)
        with patch.dict(os.environ, {'RECOMP_VOLUME': '0', 'SDL_AUDIODRIVER': 'dummy', 'SDL_AUDIO_DRIVER': 'dummy', 'XLLE_PILOT_TRACE': '1'}), patch.object(n.subprocess, 'Popen', return_value=child) as spawn:
            n.launch(self.data, self.paths, self.output, self.build, detach=True, source=self.source)
            command = spawn.call_args.args[0]
            env = spawn.call_args.kwargs['env']
            self.assertEqual(env['RECOMP_VOLUME'], '100')
            self.assertEqual(env['XLLE_PILOT_VISIBLE'], '1')
            self.assertEqual(env['XLLE_PILOT_AUDIO_OUTPUT_MUTE'], '0')
            self.assertEqual(env['XLLE_PILOT_TRACE'], '0')
            self.assertNotIn('SDL_AUDIODRIVER', env)
            self.assertNotIn('SDL_AUDIO_DRIVER', env)
            child.wait.assert_not_called()
            staged = Path(command[0])
            self.binary.write_bytes(b'future rebuild')
            self.assertEqual(staged.read_bytes(), b'authored host binary')
            with self.assertRaises(ValueError):
                n.reject_staging_mutation(staged.parent / 'nested')

    def test_automated_muted_deadline_owned(self):
        child = Mock(pid=123, returncode=0)
        child.wait.side_effect = [subprocess.TimeoutExpired('fixture', 2), 0]
        with patch.object(n.subprocess, 'Popen', return_value=child) as spawn:
            n.launch(self.data, self.paths, self.output, self.build, automated=True, seconds=2, source=self.source)
            env = spawn.call_args.kwargs['env']
            self.assertEqual(env['RECOMP_VOLUME'], '0')
            self.assertEqual(env['SDL_AUDIODRIVER'], 'dummy')
            self.assertEqual(env['XLLE_PILOT_VISIBLE'], '0')
            child.kill.assert_called_once()
            self.assertEqual(child.wait.call_args_list[0].kwargs, {'timeout': 2})

    def test_stale_binary_or_evidence_rejected_before_spawn(self):
        with patch.object(n.subprocess, 'Popen') as spawn:
            self.binary.write_bytes(b'other binary')
            with self.assertRaises(ValueError):
                n.launch(self.data, self.paths, self.output, self.build, detach=True, source=self.source)
            self.receipt()
            self.profile.write_text(self.profile.read_text() + '\n')
            with self.assertRaises(ValueError):
                n.launch(self.data, self.paths, self.output, self.build, detach=True, source=self.source)
            spawn.assert_not_called()

    def test_changed_profile_cannot_be_reblessed_by_configuration(self):
        self.profile.write_text(self.profile.read_text() + '\n')
        with self.assertRaises(ValueError):
            n.identity(self.source, self.data, self.output)

    def test_included_proof_change_invalidates_preparation(self):
        fragment = self.root / 'fragment.json'
        fragment.write_text('{"adapter": "authored"}')
        profile = json.loads(self.profile.read_text())
        profile['bindings'] = {'cpu': {'include': 'fragment.json'}}
        self.profile.write_text(json.dumps(profile))
        self.receipt()
        fragment.write_text('{"adapter": "changed"}')
        with self.assertRaises(ValueError):
            n.identity(self.source, self.data, self.output)


if __name__ == '__main__':
    unittest.main()
