"""Check the committed micro-modules and generate the C fixture table."""
import hashlib
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
manifest = json.loads((root/'fixtures/manifest.json').read_text())
parts = ['// Generated from fixtures/manifest.json; see the adjacent WAT sources.\n#include "fixtures.h"\n']
for item in manifest:
    data = (root/'fixtures'/item['wasm']).read_bytes()
    if hashlib.sha256(data).hexdigest() != item['sha256']:
        raise ValueError('Fixture checksum mismatch: '+item['name'])
    parts.append('static const unsigned char '+item['name']+'[] = {'+','.join('0x%02x'%b for b in data)+'};\n')
parts.append('const EngineFixture engine_fixtures[] = {\n')
for item in manifest:
    values = item['expected']+[0]*(2-len(item['expected']))
    parts.append('{"%s",%s,sizeof(%s),{%s},%d,%d},\n'%(item['name'],item['name'],item['name'],','.join(map(str,values)),len(item['expected']),item['trap']))
parts.append('};\nconst unsigned engine_fixture_count = sizeof(engine_fixtures)/sizeof(engine_fixtures[0]);\n')
(root/'src/fixtures.c').write_text(''.join(parts))
