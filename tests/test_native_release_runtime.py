"""Fresh-profile ownership tests with authored inputs, without a game/window."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_release_runtime as runtime
import native_save_keys
from native_mod_launcher import Catalog, encoded
from create_eeprom import generate_eeprom
from test_native_save_keys import authored_xbe
from test_xbe_identity import fixture as identity_fixture


class FirstRunTests(unittest.TestCase):
    def test_prepare_only_accepts_an_iso_and_verifies_its_embedded_revision(self):
        from test_native_disc import make_disc, SilentProgress
        source=self.root/'selected.iso';make_disc(source,xbe=self.image.read_bytes())
        with patch.object(sys,'argv',['runtime','--root',str(self.root),
                '--prepare-only','--image',str(source)]), \
             patch.object(runtime,'descriptor',return_value=self.data), \
             patch('native_disc_progress.Progress',return_value=SilentProgress()):
            self.assertEqual(runtime.main(),0)
        self.assertTrue((self.root/'data/console/profile.json').is_file())

    def test_iso_selection_keeps_source_choice_and_launches_the_imported_xbe(self):
        from test_native_disc import make_disc, SilentProgress
        source=self.root/'selected.iso';make_disc(source,xbe=self.image.read_bytes())
        runtime.writable_environment(self.root)
        data=dict(self.data,title=dict(self.data['title'],name='Authored title'),
                  capabilities=dict(visual_mods=False),launcher_executable='launcher.exe')
        runtime.prepare_profile(self.root,data,self.image)
        with patch.object(Catalog,'run',return_value=(0,str(source))), \
             patch('native_disc_progress.Progress',return_value=SilentProgress()), \
             patch.object(runtime,'launch_game',return_value=0) as launch:
            self.assertEqual(runtime.interactive(self.root,data,self.root/'data',False),0)
        prepared=launch.call_args.args[2]
        self.assertEqual(prepared.name,'default.xbe')
        self.assertEqual(prepared.read_bytes(),self.image.read_bytes())
        self.assertEqual(json.loads((self.root/'data/setup.json').read_text())['image'],str(source))

    def test_canceled_import_returns_to_the_launcher_without_an_error_dialog(self):
        source=self.root/'selected.iso';source.write_bytes(b'fixture selection')
        runtime.writable_environment(self.root)
        data=dict(self.data,title=dict(self.data['title'],name='Authored title'),
                  capabilities=dict(visual_mods=False),launcher_executable='launcher.exe')
        with patch.object(Catalog,'run',side_effect=[(0,str(source)),(1,str(source))]) as ui, \
             patch.object(runtime,'prepare_game',side_effect=runtime.ImportCancelled()), \
             patch.object(runtime,'message',side_effect=AssertionError('Cancel is not an error')):
            self.assertEqual(runtime.interactive(self.root,data,self.root/'data',False),0)
        self.assertEqual(ui.call_count,2)

    def test_packaged_session_mod_uses_title_default_and_keeps_owner_choice(self):
        from native_session_mods import enabled
        data = dict(title=dict(game_id='5345000A', xbe_sha256=
            'fd19055756719893c466302809b433b785ecf5732df0441286f3f605f0f3ef9c'),
            capabilities=dict(visual_mods=False))
        paths=runtime.launcher_manifests(self.root,data)
        defaults={'xboxllr.diagnostics/session-replay':dict(enabled=True)}
        state=self.root/'selection.json'
        self.assertTrue(enabled(Catalog(paths,state,defaults=defaults)))
        state.write_text(json.dumps(dict(version=1,features={
            'xboxllr.diagnostics/session-replay':dict(enabled=False,options={})})))
        self.assertFalse(enabled(Catalog(paths,state,defaults=defaults)))

    def test_certificate_diagnostics_never_admit_an_unknown_hash_or_create_saves(self):
        data = dict(title=dict(name='Halo', game_id='4D530004', xbe_sha256='a' * 64))
        self.image.write_bytes(identity_fixture('Another title', 0x5345000a))
        with self.assertRaisesRegex(ValueError, r'Wrong game: selected Another title.*SE-010.*Expected Halo.*MS-004'):
            runtime.prepare_profile(self.root, data, self.image)
        self.image.write_bytes(identity_fixture('Halo'))
        with self.assertRaisesRegex(ValueError, r'not a supported original revision.*Halo.*MS-004'):
            runtime.prepare_profile(self.root, data, self.image)
        self.image.write_bytes(b'XBEH')
        with self.assertRaisesRegex(ValueError, 'Cannot read its title identity'):
            runtime.prepare_profile(self.root, data, self.image)
        self.assertFalse((self.root / 'data').exists())

    def test_title_switch_selection_reaches_native_process(self):
        runtime.writable_environment(self.root)
        profile = runtime.prepare_profile(self.root, self.data, self.image)
        (self.root/'game.exe').write_bytes(b'authored game fixture')
        data = dict(self.data, executable='game.exe', runtime={}, capabilities=dict(visual_mods=False),
            mods=dict(switches=[dict(package='fixture.enhancement.unlock',feature='early-unlock',
                manifest='mods/preloaded/packages/fixture.enhancement.unlock/1.0.0/manifest.toml',key='EARLY_UNLOCK')]))
        catalog=Catalog([],self.root/'selection.json')
        catalog.features=[dict(package='fixture.enhancement.unlock',id='early-unlock',enabled=False,options=[])]
        child=unittest.mock.Mock(pid=1,returncode=0)
        child.wait.return_value=0
        with patch('native_boot_launcher.run',return_value=None), patch.object(runtime.subprocess,'Popen',return_value=child) as spawn:
            self.assertEqual(runtime.launch_game(self.root,data,self.image,profile,catalog),0)
        self.assertEqual(spawn.call_args.kwargs['env']['XLLE_PILOT_TITLE_MOD_EARLY_UNLOCK'],'0')

    def test_intro_failure_returns_before_native_title_and_preserves_save_identity(self):
        runtime.writable_environment(self.root)
        profile = runtime.prepare_profile(self.root, self.data, self.image)
        identity = (profile / 'eeprom.bin').read_bytes(), (profile / 'save-keys.bin').read_bytes()
        save = self.root / 'data/state/Partition1/progress.sav'
        save.parent.mkdir(parents=True)
        save.write_bytes(b'owner progress')
        (self.root / 'game.exe').write_bytes(b'authored game fixture')
        data = dict(self.data, executable='game.exe', runtime={}, capabilities=dict(visual_mods=False))
        catalog = Catalog([], self.root / 'selection.json')
        events = []
        def intro(*args, **kwargs):
            events.append('intro-cleaned-up')
            return dict(status='failed')
        child = unittest.mock.Mock(pid=1, returncode=0)
        child.wait.return_value = 0
        def title(*args, **kwargs):
            events.append('title-spawned')
            return child
        with patch('native_boot_launcher.run', side_effect=intro), patch.object(runtime.subprocess, 'Popen', side_effect=title):
            self.assertEqual(runtime.launch_game(self.root, data, self.image, profile, catalog), 0)
        self.assertEqual(events, ['intro-cleaned-up', 'title-spawned'])
        self.assertEqual(identity, ((profile / 'eeprom.bin').read_bytes(), (profile / 'save-keys.bin').read_bytes()))
        self.assertEqual(save.read_bytes(), b'owner progress')

    def setUp(self):
        environment = patch.dict('os.environ')
        environment.start(); self.addCleanup(environment.stop)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.image = self.root / 'owned.xbe'
        self.image.write_bytes(authored_xbe())
        self.kernel = self.root / 'kernel.bin'
        self.kernel.write_bytes(bytes(native_save_keys.CERTIFICATE_KEY_OFFSET) + bytes(range(1, 17)))
        pin = patch.object(native_save_keys, 'KERNEL_SHA', runtime.sha(self.kernel))
        pin.start(); self.addCleanup(pin.stop)
        self.data = dict(title=dict(xbe_sha256=runtime.sha(self.image)))

    def test_fresh_setup_publishes_complete_identity_without_source_changes(self):
        before = runtime.sha(self.kernel), runtime.sha(self.image)
        profile = runtime.prepare_profile(self.root, self.data, self.image, self.kernel)
        self.assertEqual(runtime.verify_profile(self.root, self.data), profile)
        self.assertEqual(before, (runtime.sha(self.kernel), runtime.sha(self.image)))
        self.assertEqual(json.loads((profile / 'profile.json').read_text())['eeprom_source'], 'generated-r2')
        self.assertFalse((self.root / 'data/state').exists())

    def test_default_virtual_profile_needs_no_kernel_and_keeps_real_signatures(self):
        profile = runtime.prepare_profile(self.root, self.data, self.image)
        record = json.loads((profile / 'profile.json').read_text())
        self.assertEqual(record['signing'], 'virtual-local-v1')
        self.assertEqual(runtime.verify_profile(self.root, self.data), profile)
        keys = (profile / 'save-keys.bin').read_bytes()
        self.assertEqual(len(keys), 360)
        self.assertTrue(all(any(keys[start:start+16]) for start in range(72,360,16)))
        other = self.root / 'other-console'
        second = runtime.prepare_profile(other, self.data, self.image)
        self.assertNotEqual(keys[88:], (second / 'save-keys.bin').read_bytes()[88:])
        self.assertEqual((profile / 'save-keys.bin').read_bytes(), keys)

    def test_shared_console_keeps_identity_and_refuses_unowned_existing_storage(self):
        media = self.root / 'media'; media.mkdir()
        self.image = self.image.rename(media / self.image.name)
        storage = self.root / 'shared'; storage.mkdir()
        kernel, eeprom, seed = runtime.shared_identity(storage, self.image)
        first = runtime.prepare_profile(self.root / 'first', self.data, self.image, kernel, eeprom, virtual_root=seed)
        kernel2, eeprom2, seed2 = runtime.shared_identity(storage, self.image)
        second = runtime.prepare_profile(self.root / 'second', self.data, self.image, kernel2, eeprom2, virtual_root=seed2)
        self.assertEqual((first / 'save-keys.bin').read_bytes(), (second / 'save-keys.bin').read_bytes())
        occupied = self.root / 'old-storage'; occupied.mkdir()
        save = occupied / 'progress.sav'; save.write_bytes(b'owner progress')
        with self.assertRaisesRegex(ValueError, 'already contains files'):
            runtime.shared_identity(occupied, self.image)
        self.assertEqual(list(occupied.iterdir()), [save])
        self.assertEqual(save.read_bytes(), b'owner progress')

    def test_first_use_acknowledgement_is_distinct_from_game_selection(self):
        choice = self.root / 'console-choice.txt'
        self.assertFalse(runtime.read_console_choice(choice)['acknowledged'])
        choice.write_text('xboxllr-console-choice 1\nacknowledged 1\nkernel -\neeprom -\nstorage -\nend\n')
        self.assertEqual(runtime.read_console_choice(choice), dict(acknowledged=True,kernel='',eeprom='',storage=''))

    def test_image_and_folder_choices_are_distinct_and_utf8_round_trip(self):
        choice = self.root / 'console-choice.txt'
        folder, disk = 'G:/Saves/private', 'G:/Xbox disks/shared.qcow2'
        choice.write_text('xboxllr-console-choice 2\nacknowledged 1\nkernel -\neeprom -\nstorage '+
            encoded(folder)+'\nhdd '+encoded(disk)+'\nend\n')
        self.assertEqual(runtime.read_console_choice(choice),
            dict(acknowledged=True,kernel='',eeprom='',storage=folder,hdd=disk))

    def test_existing_disk_requires_verified_original_signing_inputs(self):
        disk = self.root / 'shared.qcow2';disk.write_bytes(b'authored placeholder')
        eeprom = self.root / 'owned-eeprom.bin';eeprom.write_bytes(generate_eeprom('r2'))
        with patch('native_storage.Storage') as backend:
            backend.return_value.__enter__.return_value.identity.return_value = 'ab'*32
            backend.return_value.__enter__.return_value.inspect.return_value = dict(disk_structure_verified=True)
            with self.assertRaisesRegex(ValueError,'kernel dump and matching EEPROM'):
                runtime.disk_identity(self.root,disk)
            self.assertFalse(disk.with_name(disk.name+'.xboxllr').exists())
            selected = runtime.disk_identity(self.root,disk,self.kernel,eeprom)
            profile = runtime.prepare_profile(self.root,self.data,self.image,selected['kernel'],selected['eeprom'])
            runtime.disk_identity(self.root,disk,prepared=profile)
            other = self.root / 'other-eeprom.bin';other.write_bytes(generate_eeprom('r2'))
            with self.assertRaisesRegex(ValueError,'does not match'):
                runtime.disk_identity(self.root,disk,self.kernel,other)
            self.assertEqual((selected['owner']/'eeprom.bin').read_bytes(),eeprom.read_bytes())
        self.assertEqual(disk.read_bytes(),b'authored placeholder')

    def test_failed_disk_admission_does_not_make_profile_or_fallback_storage(self):
        disk = self.root / 'invalid.img';disk.write_bytes(b'invalid disk')
        with patch('native_storage.Storage',side_effect=ValueError('Damaged HDD image')):
            with self.assertRaisesRegex(ValueError,'Damaged HDD'):
                runtime.disk_identity(self.root,disk,self.kernel)
        self.assertFalse((self.root/'data/console').exists())
        self.assertFalse((self.root/'data/state').exists())
        self.assertFalse(disk.with_name(disk.name+'.xboxllr').exists())

    def test_optional_eeprom_import_retains_exact_identity_and_source(self):
        original = self.root / 'original-eeprom.bin'
        eeprom = generate_eeprom(random_bytes=bytes(range(58)))
        original.write_bytes(eeprom)
        profile = runtime.prepare_profile(self.root, self.data, self.image, self.kernel, original)
        self.assertEqual((profile / 'eeprom.bin').read_bytes(), eeprom)
        self.assertEqual(original.read_bytes(), eeprom)
        runtime.verify_profile(self.root, self.data)

    def test_existing_identity_and_native_progress_are_never_replaced(self):
        destination = self.root / 'data/console'
        destination.mkdir(parents=True)
        save = destination / 'progress.sav'
        save.write_bytes(b'Owner progress')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            runtime.prepare_profile(self.root, self.data, self.image, self.kernel)
        self.assertEqual(save.read_bytes(), b'Owner progress')
        self.assertEqual(list(destination.iterdir()), [save])

    def test_bad_kernel_and_wrong_game_do_not_create_profile_output(self):
        self.kernel.write_bytes(b'not an admitted kernel')
        with self.assertRaisesRegex(ValueError, 'Unreviewed'):
            runtime.prepare_profile(self.root, self.data, self.image, self.kernel)
        self.assertFalse((self.root / 'data').exists())
        self.image.write_bytes(b'wrong title')
        with self.assertRaisesRegex(ValueError, 'supported original revision'):
            runtime.prepare_profile(self.root, self.data, self.image, self.kernel)
        self.assertFalse((self.root / 'data').exists())

    def test_invalid_optional_eeprom_is_not_replaced(self):
        original = self.root / 'bad-eeprom.bin'
        original.write_bytes(bytes(256))
        with self.assertRaisesRegex(ValueError, 'EEPROM is invalid'):
            runtime.prepare_profile(self.root, self.data, self.image, self.kernel, original)
        self.assertEqual(original.read_bytes(), bytes(256))
        self.assertFalse((self.root / 'data').exists())

    def test_changed_signing_or_settings_inputs_are_rejected(self):
        profile = runtime.prepare_profile(self.root, self.data, self.image, self.kernel)
        settings = profile / 'console-settings.bin'
        original = settings.read_bytes()
        settings.write_bytes(original[:-1])
        with self.assertRaisesRegex(ValueError, 'settings do not match'):
            runtime.verify_profile(self.root, self.data)
        settings.write_bytes(original)
        (profile / 'save-keys.bin').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            runtime.verify_profile(self.root, self.data)

    def test_package_paths_cannot_escape_the_application(self):
        for path in ['../old-saves', '/outside', 'C:/outside']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                runtime.inside(self.root, path)

    def test_second_setup_session_is_refused(self):
        data = runtime.writable_environment(self.root)
        with runtime.session_lock(data):
            with self.assertRaisesRegex(ValueError, 'already open'):
                with runtime.session_lock(data):
                    self.fail('Second writer admitted')

    def test_owner_launch_isolates_developer_overrides_and_has_no_deadline(self):
        runtime.writable_environment(self.root)
        data = dict(self.data, title=dict(self.data['title'],name='Blinx: The Time Sweeper'),
                    executable='game.exe', runtime=dict(XLLE_PILOT_GUEST_SCHEDULER='1'),
                    capabilities=dict(visual_mods=False))
        catalog = Catalog([], self.root / 'selection.json')
        catalog.player = dict(source='keyboard', guid='')
        profile = runtime.prepare_profile(self.root, data, self.image)
        (self.root / 'game.exe').write_bytes(b'authored launcher fixture')
        with patch.dict('os.environ', dict(XLLE_PILOT_INPUT_REPLAY='old-replay',
                XLLE_PILOT_MOD_PLAN='old-plan', XLLE_PILOT_SAVE_KEYS_FILE='old-keys',
                SDL_AUDIODRIVER='dummy')), patch.object(runtime.subprocess, 'Popen') as process:
            process.return_value.wait.return_value = 0
            process.return_value.pid = 123
            runtime.launch_game(self.root, data, self.image, profile, catalog)
            env = process.call_args.kwargs['env']
            self.assertNotIn('XLLE_PILOT_INPUT_REPLAY', env)
            self.assertNotIn('XLLE_PILOT_MOD_PLAN', env)
            self.assertNotIn('SDL_AUDIODRIVER', env)
            self.assertEqual(env['XLLE_PILOT_SAVE_KEYS_FILE'], str(profile / 'save-keys.bin'))
            self.assertEqual(env['XLLE_PILOT_GUEST_SCHEDULER'], '1')
            self.assertEqual(env['XLLE_PILOT_VISIBLE'], '1')
            self.assertEqual(env['XLLE_PILOT_AUDIO_OUTPUT_MUTE'], '0')
            self.assertEqual(env['XLLE_PILOT_WINDOW_TITLE'], 'Blinx: The Time Sweeper')
            process.return_value.wait.assert_called_once_with()
            process.return_value.terminate.assert_not_called()

    def test_setup_result_roundtrips_unicode_and_is_transactional(self):
        catalog = Catalog([], self.root / 'mods.json', setup=dict(required=True, kernel=''))
        result = self.root / 'result'
        text = 'xlle-launcher-result 1\nresult 1\nimage -\nvolume 100\nskip 0\nplayer keyboard -\nkernel '+encoded('G:/Owned files/原本.bin')+'\nend\n'
        result.write_text(text, encoding='ascii')
        catalog.ingest(result)
        self.assertEqual(catalog.setup['kernel'], 'G:/Owned files/原本.bin')
        result.write_text(text.replace('end\n', 'kernel -\nend\n'), encoding='ascii')
        with self.assertRaises(ValueError):
            catalog.ingest(result)
        self.assertEqual(catalog.setup['kernel'], 'G:/Owned files/原本.bin')


if __name__ == '__main__':
    unittest.main()
