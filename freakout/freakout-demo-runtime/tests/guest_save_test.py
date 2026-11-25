from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from guest_save import PendingSave


class GuestSaveTest(unittest.TestCase):
    def test_abort_preserves_previous_profile(self):
        with TemporaryDirectory() as directory:
            profile = Path(directory) / 'profile.pro'
            profile.write_bytes(b'old')
            pending = PendingSave(profile)
            pending.stream.write(b'partial')
            self.assertEqual(profile.read_bytes(), b'old')
            pending.abort()
            self.assertEqual(profile.read_bytes(), b'old')
            self.assertFalse(pending.temporary.exists())

    def test_append_preserves_previous_bytes_even_after_seek(self):
        with TemporaryDirectory() as directory:
            profile = Path(directory) / 'profile.pro'
            profile.write_bytes(b'old')
            pending = PendingSave(profile, append=True)
            pending.stream.seek(0)
            pending.stream.write(b'new')
            pending.commit()
            self.assertEqual(profile.read_bytes(), b'oldnew')

    def test_io_failure_does_not_replace_committed_profile(self):
        with TemporaryDirectory() as directory:
            profile = Path(directory) / 'profile.pro'
            profile.write_bytes(b'old')
            pending = PendingSave(profile)
            pending.stream.write(b'new')
            with patch('guest_save.os.fsync', side_effect=OSError('disk full')):
                with self.assertRaisesRegex(OSError, 'disk full'):
                    pending.commit()
            self.assertEqual(profile.read_bytes(), b'old')
            self.assertFalse(pending.temporary.exists())
