#!/usr/bin/env python3
"""Check pinned decoder and repository-authored MP3 payloads, no network."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'tests/data/audio/manifest.json').read_text())
for path,expected in manifest.items():
    actual=hashlib.sha256((root/path).read_bytes()).hexdigest()
    if actual!=expected:raise SystemExit('Audio asset mismatch: '+path)
print('Audio assets PASS: pinned dr_mp3 + three authored MP3 fixtures')
