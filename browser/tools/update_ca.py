"""Update the pinned Mozilla/curl CA bundle, or validate it without networking."""
import hashlib
import json
import re
import ssl
import sys
import urllib.request
from pathlib import Path

root = Path(__file__).resolve().parents[1]
bundle = root / 'romfs/cacert.pem'
manifest = root / 'romfs/CA_BUNDLE.json'
source = 'https://curl.se/ca/cacert.pem'
if '--check' not in sys.argv:
    with urllib.request.urlopen(source, timeout=30) as response:
        content = response.read(512 * 1024 + 1)
    if len(content) > 512 * 1024:
        raise ValueError('CA bundle exceeds limit')
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(cadata=b'\n'.join(re.findall(rb'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----', content, re.S)).decode('ascii'))
    count = content.count(b'-----BEGIN CERTIFICATE-----')
    if count < 100:
        raise ValueError('Unexpectedly small trust store')
    date = re.search(rb'Certificate data from Mozilla as of: ([^\r\n]+)', content)
    bundle.write_bytes(content)
    manifest.write_text(json.dumps({'source': source, 'extracted': date[1].decode() if date else '',
                                   'sha256': hashlib.sha256(content).hexdigest(), 'certificates': count}, indent=2) + '\n')
content = bundle.read_bytes()
metadata = json.loads(manifest.read_text())
assert hashlib.sha256(content).hexdigest() == metadata['sha256'], 'CA checksum mismatch'
assert content.count(b'-----BEGIN CERTIFICATE-----') == metadata['certificates'] >= 100
context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
context.load_verify_locations(cadata=b'\n'.join(re.findall(rb'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----', content, re.S)).decode('ascii'))
print(f"CA bundle: PASS ({metadata['certificates']} certificates, {metadata['extracted']})")
