"""Source releases include exact submodule source and reject private payloads."""
from pathlib import Path
import hashlib
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from source_release_package import git, package


class SourceReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.repo = self.base/'source'
        self.repo.mkdir()
        git(self.repo, 'init', '-q')
        (self.repo/'LICENSE').write_text('authored test fixture')
        self.commit(self.repo)

    def commit(self, repo):
        git(repo, 'add', '.')
        git(repo, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'fixture')

    def test_uses_tracked_commit_and_ignores_local_payload(self):
        (self.repo/'untracked.xbe').write_bytes(b'private')
        (self.repo/'LICENSE').write_text('dirty')
        output=self.base/'source.zip'
        result=package(self.repo, output)
        with zipfile.ZipFile(output) as z:
            self.assertEqual(z.read('LICENSE'), b'authored test fixture')
            self.assertNotIn('untracked.xbe', z.namelist())
        self.assertEqual(result['entries'], 2)

    def test_tracked_original_input_is_rejected(self):
        (self.repo/'default.xbe').write_bytes(b'owned')
        self.commit(self.repo)
        with self.assertRaisesRegex(ValueError, 'Private source path'):
            package(self.repo, self.base/'bad.zip')
        self.assertFalse((self.base/'bad.zip').exists())

    def test_reviewed_image_history_requires_exact_path_and_hash(self):
        name='docs/reviewed.png'
        path=self.repo/name
        path.parent.mkdir()
        versions=(b'\x89PNG\r\n\x1a\nreviewed current', b'\x89PNG\r\n\x1a\nreviewed previous')
        fingerprints=tuple(hashlib.sha256(data).hexdigest() for data in versions)
        with patch.dict('source_release_package.DOCUMENTATION_IMAGES', {name:fingerprints}):
            for index,data in enumerate(versions):
                path.write_bytes(data);self.commit(self.repo)
                output=self.base/f'reviewed-{index}.zip'
                package(self.repo,output)
                with zipfile.ZipFile(output) as archive:
                    self.assertEqual(archive.read(name),data)
            path.write_bytes(versions[0]+b'changed');self.commit(self.repo)
            with self.assertRaisesRegex(ValueError,'Private source path'):
                package(self.repo,self.base/'changed.zip')
            path.write_bytes(versions[0]);path.rename(path.with_name('renamed.png'))
            self.commit(self.repo)
            with self.assertRaisesRegex(ValueError,'Private source path'):
                package(self.repo,self.base/'renamed.zip')

    def test_translated_original_marker_is_rejected(self):
        (self.repo/'function.c').write_text('/* XLLE_PRIVATE_GUEST_ARTIFACT */\n')
        self.commit(self.repo)
        with self.assertRaisesRegex(ValueError, 'Private or unreviewed'):
            package(self.repo, self.base/'bad.zip')

    def test_recursive_source_uses_exact_gitlink_commit(self):
        child=self.repo/'framework'
        child.mkdir();git(child, 'init', '-q')
        (child/'api.h').write_text('original api')
        self.commit(child)
        pin=git(child,'rev-parse','HEAD').decode().strip()
        git(self.repo,'update-index','--add','--cacheinfo','160000',pin,'framework')
        git(self.repo,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid',
            'commit','-qm','pin')
        (child/'api.h').write_text('later api');self.commit(child)
        output=self.base/'recursive.zip'
        result=package(self.repo,output)
        with zipfile.ZipFile(output) as z:
            self.assertEqual(z.read('framework/api.h'),b'original api')
        self.assertEqual(result['revisions']['framework/'],pin)

    def test_existing_archive_is_never_replaced(self):
        output=self.base/'keep.zip';output.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            package(self.repo,output)
        self.assertEqual(output.read_bytes(),b'keep')
