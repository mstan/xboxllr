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
        self.manifest.write_text(json.dumps(dict(sha256=n.digest(self.xbe),
            files={artifact.name: n.digest(artifact)}, cpu_binding={'authored': 'admitted CPU proof'})))
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

    def select_families(self, families):
        old = self.data['generation']['bindings']
        self.source.write_text(self.source.read_text().replace(
            'bindings = ' + json.dumps(old), 'bindings = ' + json.dumps(families)))
        _, self.data = n.definition(self.source)

    def assert_not_configurable_or_launchable(self):
        pathsfile = self.root / 'paths.json'
        pathsfile.write_text(json.dumps({key: str(value) for key, value in self.paths.items()}))
        arguments = ['native_title.py', '--title', str(self.source), '--paths', str(pathsfile),
                     '--generated', str(self.output), '--build-dir', str(self.build)]
        with patch.object(n.subprocess, 'run') as runner, patch.object(n.subprocess, 'Popen') as spawn:
            for action in ('configure', 'build'):
                with self.subTest(action=action), patch.object(sys, 'argv', [arguments[0], action, *arguments[1:]]):
                    with self.assertRaises(ValueError):
                        n.main()
            with self.assertRaises(ValueError):
                n.launch(self.data, self.paths, self.output, self.build, detach=True, source=self.source)
            runner.assert_not_called()
            spawn.assert_not_called()

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
        for key, option in (('save_keys', 'PILOT_SAVE_KEYS_FILE'),
                            ('console_settings', 'PILOT_CONSOLE_SETTINGS_FILE')):
            with self.subTest(dependency=key):
                private = self.root / (key + '.bin')
                private.write_bytes(b'authored fixture')
                command = n.configure_command(self.data, {key: private}, self.output, self.build, 'cmake', 'Ninja', 1)
                self.assertIn('-D' + option + '=' + str(private), command)
                private.unlink()
                with self.assertRaises(ValueError):
                    n.configure_command(self.data, {key: private}, self.output, self.build, 'cmake', 'Ninja', 1)

    def test_content_drive_namespace_owner_default_and_title(self):
        n.write_runtime_profile(self.output, self.data)
        header = self.output / 'gen/pilot_title_runtime.h'
        self.assertIn('#define PILOT_TITLE_DRIVE_OWNER "HOST"', header.read_text())
        self.source.write_text(self.source.read_text() + '\n[content]\ndrive_owner = "TITLE"\n')
        _, self.data = n.definition(self.source)
        n.write_runtime_profile(self.output, self.data)
        self.assertIn('#define PILOT_TITLE_DRIVE_OWNER "TITLE"', header.read_text())
        manifest = n.check_generated(self.output, self.data['title'])
        self.assertEqual(manifest['title_runtime']['drive_owner'], 'TITLE')
        self.assertEqual(manifest['files'][header.name], n.digest(header))

    def test_unknown_content_drive_namespace_owner_rejected(self):
        original = self.source.read_text()
        for owner in ('"UNKNOWN"', '""', 'false', '1'):
            with self.subTest(owner=owner):
                self.source.write_text(original + '\n[content]\ndrive_owner = ' + owner + '\n')
                with self.assertRaisesRegex(ValueError, 'drive_owner'):
                    n.definition(self.source)

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

    def test_run_command_propagates_an_early_child_failure(self):
        pathsfile = self.root / 'paths.json'
        pathsfile.write_text(json.dumps({key: str(value) for key, value in self.paths.items()}))
        child = Mock(pid=123, returncode=2)
        arguments = ['native_title.py', 'run', '--title', str(self.source),
                     '--paths', str(pathsfile), '--generated', str(self.output),
                     '--build-dir', str(self.build), '--automated', '--seconds', '15']
        with patch.object(sys, 'argv', arguments), patch.object(n.subprocess, 'Popen', return_value=child):
            self.assertEqual(n.main(), 2)

    def test_incremental_bind_revalidates_old_admission_and_refreshes_proof(self):
        manifest = json.loads(self.manifest.read_text())
        manifest['cpu_binding'] = {'authored': 'previous proof'}
        self.manifest.write_text(json.dumps(manifest))
        self.receipt()
        old = self.manifest.read_bytes()
        self.profile.write_text(self.profile.read_text() + ' ')
        self.paths['xdk_symbols'] = self.root / 'symbols.json'
        def checked_binding(xbe, output, symbols, *, profile):
            self.assertEqual((xbe, output, symbols, profile),
                (self.xbe, self.output, self.paths['xdk_symbols'], self.profile))
            value = n.check_generated(output, self.data['title'])
            header = output / 'gen/cpu-proof.h'
            header.write_bytes(b'/* authored newly checked proof */')
            value['files'][header.name] = n.digest(header)
            value['cpu_binding'] = {'authored': 'newly checked proof'}
            self.manifest.write_text(json.dumps(value))
        module = Mock(bind=Mock(side_effect=checked_binding))
        with patch.object(n.importlib, 'import_module', return_value=module) as importer:
            result = n.bind_prepared(self.source, self.data, self.paths, self.output)
            importer.assert_called_once_with('native_cpu_binding')
            module.bind.assert_called_once()
        self.assertEqual((Path(result['history']) / self.manifest.name).read_bytes(), old)
        n.identity(self.source, self.data, self.output)
        with self.assertRaises(ValueError):
            n.launch(self.data, self.paths, self.output, self.build, source=self.source)

    def test_incremental_bind_cannot_leave_an_old_family_unvalidated(self):
        manifest = json.loads(self.manifest.read_text())
        manifest['audio_binding'] = {'authored': 'existing audio ownership'}
        self.manifest.write_text(json.dumps(manifest))
        self.select_families(['cpu', 'audio'])
        self.receipt()
        self.select_families(['cpu'])
        with patch.object(n.importlib, 'import_module') as importer:
            with self.assertRaises(ValueError):
                n.bind_prepared(self.source, self.data, self.paths, self.output)
            importer.assert_not_called()

    def test_unadmitted_selected_family_cannot_be_blessed_by_configure(self):
        self.select_families(['cpu', 'startup'])
        with self.assertRaisesRegex(ValueError, 'actual generation admissions'):
            n.identity(self.source, self.data, self.output)
        self.assert_not_configurable_or_launchable()

    def test_failed_bind_before_mutation_blocks_configuration_and_retry_can_finish(self):
        self.paths['xdk_symbols'] = self.root / 'symbols.json'
        prior_files = json.loads(self.manifest.read_text())['files']
        module = Mock(bind=Mock(side_effect=ValueError('original proof rejected')))
        with patch.object(n.importlib, 'import_module', return_value=module), self.assertRaisesRegex(ValueError, 'original proof rejected'):
            n.bind_prepared(self.source, self.data, self.paths, self.output)
        self.assertEqual(json.loads(self.manifest.read_text())['files'], prior_files)
        self.assertTrue((self.output / n.INCOMPLETE_BIND).exists())
        with self.assertRaisesRegex(ValueError, 'refresh is incomplete'):
            n.identity(self.source, self.data, self.output)
        self.assert_not_configurable_or_launchable()
        with patch.object(n.importlib, 'import_module', return_value=Mock(bind=Mock(return_value={}))):
            n.bind_prepared(self.source, self.data, self.paths, self.output)
        self.assertFalse((self.output / n.INCOMPLETE_BIND).exists())
        n.identity(self.source, self.data, self.output)

    def test_failed_new_family_after_first_binder_mutation_cannot_bless_binary(self):
        self.select_families(['cpu', 'startup'])
        self.paths['xdk_symbols'] = self.root / 'symbols.json'
        original_binary = self.binary.read_bytes()
        def first_binding(*args, **kwargs):
            manifest = n.check_generated(self.output, self.data['title'])
            header = self.output / 'gen/cpu-proof.h'
            header.write_text('/* newly verified CPU header */')
            manifest['files'][header.name] = n.digest(header)
            manifest['cpu_binding'] = {'authored': 'refreshed CPU proof'}
            self.manifest.write_text(json.dumps(manifest))
        modules = {'native_cpu_binding': Mock(bind=Mock(side_effect=first_binding)),
                   'native_startup_binding': Mock(bind=Mock(side_effect=ValueError('startup proof rejected')))}
        with patch.object(n.importlib, 'import_module', side_effect=modules.__getitem__), patch.object(n.subprocess, 'run') as runner:
            with self.assertRaisesRegex(ValueError, 'startup proof rejected'):
                n.bind_prepared(self.source, self.data, self.paths, self.output)
            runner.assert_not_called()  # Incremental binding never retranslates the CPU.
        self.assertTrue((self.output / 'gen/cpu-proof.h').exists())
        self.assertNotIn('startup_binding', json.loads(self.manifest.read_text()))
        self.assert_not_configurable_or_launchable()
        self.assertEqual(self.binary.read_bytes(), original_binary)

    def test_successful_incremental_bind_must_actually_admit_the_new_family(self):
        self.select_families(['cpu', 'startup'])
        self.paths['xdk_symbols'] = self.root / 'symbols.json'
        with patch.object(n.importlib, 'import_module', return_value=Mock(bind=Mock(return_value={}))):
            with self.assertRaisesRegex(ValueError, 'actual generation admissions'):
                n.bind_prepared(self.source, self.data, self.paths, self.output)
        self.assert_not_configurable_or_launchable()

    def test_bind_safely_upgrades_legacy_preparation(self):
        path = self.output / 'native-title-preparation.json'
        prior = json.loads(path.read_text())
        prior.pop('bindings')
        prior.pop('generation_proof_sha256')
        path.write_text(json.dumps(prior))
        self.paths['xdk_symbols'] = self.root / 'symbols.json'
        with self.assertRaises(ValueError):
            n.identity(self.source, self.data, self.output)
        with patch.object(n.importlib, 'import_module', return_value=Mock(bind=Mock(return_value={}))):
            n.bind_prepared(self.source, self.data, self.paths, self.output)
        current = json.loads(path.read_text())
        self.assertEqual(current['bindings'], ['cpu'])
        self.assertIn('generation_proof_sha256', current)
        n.identity(self.source, self.data, self.output)

    def test_admission_metadata_tamper_cannot_erase_retained_family(self):
        original = json.loads(self.manifest.read_text())
        for change in ('erase', 'replace'):
            manifest = dict(original)
            if change == 'erase':
                del manifest['cpu_binding']
            else:
                manifest['cpu_binding'] = {'authored': 'changed admission'}
            self.manifest.write_text(json.dumps(manifest))
            with self.subTest(change=change), patch.object(n.importlib, 'import_module') as importer:
                with self.assertRaisesRegex(ValueError, 'changed generation proof'):
                    n.bind_prepared(self.source, self.data, self.paths, self.output)
                importer.assert_not_called()

    def test_legacy_jump_abi_admission_is_retained(self):
        manifest = json.loads(self.manifest.read_text())
        manifest['jump_abi'] = {'authored': 'legacy jump admission'}
        self.manifest.write_text(json.dumps(manifest))
        self.select_families(['cpu', 'jump'])
        self.receipt()
        self.select_families(['cpu'])
        with patch.object(n.importlib, 'import_module') as importer, self.assertRaisesRegex(ValueError, 'all previously admitted'):
            n.bind_prepared(self.source, self.data, self.paths, self.output)
        importer.assert_not_called()

    def test_empty_or_null_admission_is_not_a_checked_binding(self):
        original = json.loads(self.manifest.read_text())
        for key, value in [('cpu_binding', None), ('cpu_binding', {}), ('jump_abi', None), ('jump_abi', {})]:
            manifest = dict(original)
            manifest[key] = value
            self.manifest.write_text(json.dumps(manifest))
            with self.subTest(key=key, value=value), self.assertRaisesRegex(ValueError, 'invalid native binding admission'):
                n.preparation_identity(self.source, self.data, self.output)

    def explicit_prepare(self):
        self.paths.update(cpu_donor=self.root, xdk_symbols=self.root / 'symbols.json')
        pathsfile = self.root / 'paths.json'
        pathsfile.write_text(json.dumps({key: str(value) for key, value in self.paths.items()}))
        arguments = ['native_title.py', 'prepare', '--title', str(self.source), '--paths', str(pathsfile),
                     '--generated', str(self.output), '--build-dir', str(self.build)]
        with patch.object(sys, 'argv', arguments), patch.object(n.subprocess, 'run') as runner:
            n.main()
            runner.assert_called_once()

    def test_explicit_prepare_recovers_only_after_complete_family_admission(self):
        marker = self.output / n.INCOMPLETE_BIND
        marker.write_text('authored failed binding attempt')
        self.explicit_prepare()  # Mocked generator returns a fully admitted authored tree.
        self.assertFalse(marker.exists())
        n.identity(self.source, self.data, self.output)

    def test_explicit_prepare_cannot_clear_failure_for_missing_admission(self):
        marker = self.output / n.INCOMPLETE_BIND
        marker.write_text('authored failed binding attempt')
        self.select_families(['cpu', 'startup'])
        preparation = self.output / 'native-title-preparation.json'
        old_receipt = preparation.read_bytes()
        with self.assertRaisesRegex(ValueError, 'actual generation admissions'):
            self.explicit_prepare()
        self.assertTrue(marker.exists())
        self.assertEqual(preparation.read_bytes(), old_receipt)
        self.assert_not_configurable_or_launchable()

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

    def test_input_replay_is_frozen_and_recorded_without_rebuilding(self):
        source = self.root / 'controller.txt'
        payload = b'0 000000000000000000000000000000000000\n500 disconnected\n'
        source.write_bytes(payload)
        build_receipt = (self.build / 'native-title-build.json').read_bytes()
        child = Mock(pid=123, returncode=0)
        with patch.object(n.subprocess, 'Popen', return_value=child) as spawn:
            result = n.launch(self.data, self.paths, self.output, self.build, automated=True,
                              seconds=2, source=self.source, input_replay=source)
        env = spawn.call_args.kwargs['env']
        self.assertEqual(env['XLLE_PILOT_INPUT'], 'replay')
        staged = Path(env['XLLE_PILOT_INPUT_REPLAY'])
        self.assertEqual(staged.parent, Path(spawn.call_args.args[0][0]).parent)
        source.write_bytes(b'future edit')
        self.assertEqual(staged.read_bytes(), payload)
        self.assertEqual(result['input_replay_sha256'], n.digest(staged))
        marker = json.loads((staged.parent / 'native-title-stage.json').read_text())
        self.assertEqual(marker['input_replay_sha256'], n.digest(staged))
        self.assertEqual((self.build / 'native-title-build.json').read_bytes(), build_receipt)
        self.assertEqual(env['XLLE_PILOT_AUDIO_OUTPUT_MUTE'], '1')

    def test_invalid_input_replay_rejected_before_staging_or_spawn(self):
        empty = self.root / 'empty.txt'
        empty.write_bytes(b'')
        with patch.object(n.subprocess, 'Popen') as spawn:
            for replay in (self.root / 'missing.txt', self.root, empty):
                with self.subTest(replay=replay), self.assertRaisesRegex(ValueError, 'input replay'):
                    n.launch(self.data, self.paths, self.output, self.build, automated=True,
                             seconds=2, source=self.source, input_replay=replay)
            spawn.assert_not_called()
        self.assertFalse((self.build / 'runs').exists())

    def test_capture_uses_a_run_directory_outside_immutable_app(self):
        child = Mock(pid=123, returncode=0)
        with patch.object(n.subprocess, 'Popen', return_value=child) as spawn:
            result = n.launch(self.data, self.paths, self.output, self.build, automated=True,
                              seconds=2, source=self.source, capture=True)
        env = spawn.call_args.kwargs['env']
        capture = Path(env['XLLE_PILOT_PRESENT_CAPTURE'])
        stage = Path(spawn.call_args.args[0][0]).parent
        self.assertTrue(capture.is_dir())
        self.assertEqual(capture.parent, stage.parent)
        self.assertFalse(capture.is_relative_to(stage))
        self.assertEqual(result['captures'], str(capture))
        self.assertEqual(env['XLLE_PILOT_PRESENT_CAPTURE_INTERVAL_MS'], '1000')

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
