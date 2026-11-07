"""Build owned Wasm with wasmtime, or verify committed assets without a compiler."""
from pathlib import Path
import hashlib
import json
import sys

root = Path(__file__).resolve().parents[1]
folder = root / 'tests/wasm'
check = '--check' in sys.argv
records = {}
if not check:
    import wasmtime
    for source in sorted(folder.glob('*.wat')):
        data = wasmtime.wat2wasm(source.read_text())
        if source.stem == 'bridge':
            data += bytes([0, 9, 5]) + b'owned' + bytes([1, 2, 3])
        source.with_suffix('.wasm').write_bytes(data)
for source in sorted(folder.glob('*.wat')):
    binary = source.with_suffix('.wasm')
    records[source.name] = {
        'wat_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'wasm': binary.name,
        'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    }
fixture = 'globalThis.wasmBinaries=' + json.dumps({
    p.stem: list(p.read_bytes()) for p in sorted(folder.glob('*.wasm'))
}, separators=(',', ':')) + ';\n'
manifest = folder / 'manifest.json'
js = root / 'tests/fixtures/wasm-binaries.js'
assets = ['game-core.wasm', 'game-sidecar.wasm']
if check:
    assert json.loads(manifest.read_text()) == records, 'Owned Wasm manifest mismatch'
    assert js.read_text() == fixture, 'Owned JS fixtures mismatch'
    for name in assets:
        assert (root/'romfs'/name).read_bytes() == (folder/name).read_bytes(), 'Game asset mismatch: '+name
else:
    manifest.write_text(json.dumps(records, indent=2) + '\n')
    js.write_text(fixture)
    for name in assets:
        (root/'romfs'/name).write_bytes((folder/name).read_bytes())
print(f'Verified {len(records)} owned Wasm modules' if check else f'Built {len(records)} owned Wasm modules')
