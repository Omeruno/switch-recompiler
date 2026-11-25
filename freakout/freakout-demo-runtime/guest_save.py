"""Publish an original game save only after the guest closes it successfully."""
import os
from pathlib import Path
import shutil
import tempfile


class PendingSave:
    def __init__(self, path, append=False):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False,
                                         prefix=f'.{path.name}.', suffix='.tmp') as temporary:
            self.temporary = Path(temporary.name)
        self.stream = None
        try:
            self.stream = self.temporary.open('a+b' if append else 'w+b')
            if append and path.is_file():
                with path.open('rb') as previous:
                    shutil.copyfileobj(previous, self.stream)
        except BaseException:
            self.abort()
            raise

    def commit(self):
        try:
            self.stream.flush()
            os.fsync(self.stream.fileno())
            self.stream.close()
            os.replace(self.temporary, self.path)
        except BaseException:
            self.abort()
            raise

    def abort(self):
        try:
            if self.stream is not None:
                self.stream.close()
        finally:
            self.temporary.unlink(missing_ok=True)
