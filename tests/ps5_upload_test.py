"""Install markers must never bless a partial or corrupted FTP copy."""
import contextlib
import ftplib
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('upload_game', ROOT / 'port/ps5/tools/upload-game.py')
upload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upload)


class FTP:
    def __init__(self, files):
        self.files = dict(files)
        self.events = []

    def size(self, path):
        if path not in self.files:
            raise ftplib.error_perm('550 Missing')
        return len(self.files[path])

    def retrbinary(self, command, callback, **kwargs):
        path = command.removeprefix('RETR ')
        self.events.append(('read', path))
        callback(self.files[path])

    def storbinary(self, command, source, **kwargs):
        path = command.removeprefix('STOR ')
        self.events.append(('write', path))
        self.files[path] = source.read()

    def rename(self, source, destination):
        self.events.append(('rename', destination))
        self.files[destination] = self.files.pop(source)


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.good = {'/game/default.xex': b'xex', '/game/NFS/data': b'data'}
        self.entries = [(p.removeprefix('/game/'), len(b), hashlib.sha256(b).hexdigest())
                        for p, b in self.good.items()]
        self.marker = '/game/.nfsmw-install'

    def finish(self, ftp, repair=None):
        with contextlib.redirect_stdout(io.StringIO()):
            upload.finish_install(ftp, '/game', 'test-version', self.entries, repair)

    def test_publish_only_after_all_files_verified(self):
        ftp = FTP(self.good)
        self.finish(ftp)
        self.assertIn(b'manifest_version = "test-version"', ftp.files[self.marker])
        first_write = next(i for i, e in enumerate(ftp.events) if e[0] == 'write')
        self.assertEqual(ftp.events[:first_write], [('read', p) for p in self.good])
        self.assertEqual(ftp.events[-1], ('rename', self.marker))

    def test_same_size_corruption_prevents_marker(self):
        ftp = FTP(self.good | {'/game/NFS/data': b'xxxx'})
        with self.assertRaisesRegex(ValueError, 'verification failed'):
            self.finish(ftp)
        self.assertNotIn(self.marker, ftp.files)
        self.assertFalse(any(e[0] == 'write' for e in ftp.events))

    def test_missing_and_truncated_files_prevent_marker(self):
        for bad in [None, b'dat']:
            ftp = FTP(self.good)
            if bad is None:
                del ftp.files['/game/NFS/data']
            else:
                ftp.files['/game/NFS/data'] = bad
            with self.assertRaises(ValueError):
                self.finish(ftp)
            self.assertNotIn(self.marker, ftp.files)

    def test_repair_is_rechecked_before_publication(self):
        ftp = FTP(self.good | {'/game/NFS/data': b'xxxx'})
        self.finish(ftp, lambda name, *_: ftp.files.update({'/game/' + name: self.good['/game/' + name]}))
        self.assertIn(self.marker, ftp.files)
        ftp = FTP(self.good | {'/game/NFS/data': b'xxxx'})
        with self.assertRaisesRegex(ValueError, 'repair verification failed'):
            self.finish(ftp, lambda *_: None)
        self.assertNotIn(self.marker, ftp.files)

    def test_source_hash_rejects_same_size_bad_data(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)
            (path / 'default.xex').write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError, 'corrupt source'):
                upload.local_files(path, self.entries[:1])

    def test_real_manifest_loads(self):
        version, entries = upload.load_manifest(ROOT / 'runtime/install/manifest.inc')
        self.assertEqual(version, '535fd83157066102')
        self.assertEqual(len(entries), 49)
        self.assertEqual(sum(e[1] for e in entries), 7123373273)


if __name__ == '__main__':
    unittest.main()
