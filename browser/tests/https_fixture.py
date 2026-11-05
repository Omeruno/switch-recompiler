"""Real loopback TLS tests: custom CA, hostname, expiry and redirects."""
import http.server
import os
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

def openssl(*args):
    subprocess.run(['openssl', *map(str,args)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    for name in ['ca', 'wrong']:
        openssl('req','-x509','-newkey','rsa:2048','-nodes','-keyout',root/f'{name}.key','-out',root/f'{name}.pem','-days','1','-subj',f'/CN=Browser Test {name}')
    openssl('req','-newkey','rsa:2048','-nodes','-keyout',root/'server.key','-out',root/'server.csr','-subj','/CN=localhost')
    (root/'extensions').write_text('subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n')
    openssl('x509','-req','-in',root/'server.csr','-CA',root/'ca.pem','-CAkey',root/'ca.key','-CAcreateserial','-out',root/'server.pem','-days','1','-extfile',root/'extensions')
    (root/'index.txt').touch()
    (root/'serial').write_text('01\n')
    (root/'ca.conf').write_text(f"""[ca]
default_ca=test
[test]
database={root}/index.txt
serial={root}/serial
new_certs_dir={root}
certificate={root}/ca.pem
private_key={root}/ca.key
default_md=sha256
policy=policy
x509_extensions=server
[policy]
commonName=supplied
[server]
subjectAltName=DNS:localhost
extendedKeyUsage=serverAuth
""")
    openssl('ca','-batch','-notext','-config',root/'ca.conf','-in',root/'server.csr','-out',root/'expired.pem','-startdate','20000101000000Z','-enddate','20010101000000Z')
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):
            pass
        def do_GET(self):
            cookies=self.headers.get('Cookie','')
            if self.path.startswith('/redirect-http'):
                self.send_response(302);self.send_header('Location',f'http://localhost:{self.server.server_port}/must-not-load');self.end_headers();return
            self.send_response(200)
            if self.path.startswith('/set'):
                self.send_header('Set-Cookie','auth=HTTP_ONLY_SECRET; Path=/; Secure; HttpOnly; Max-Age=3600')
                self.send_header('Set-Cookie','ui=ru; Path=/; Secure; Max-Age=3600')
                self.send_header('Content-Security-Policy',"script-src 'self'")
                body=b"<title>Before</title><p>loaded</p><script src='/resource.js'></script>"
            elif self.path.startswith('/resource.js'):
                body=b"document.title='Cookie reached script'" if 'auth=HTTP_ONLY_SECRET' in cookies else b"throw 'missing resource cookie'"
            else:
                body=b'<p>cookie ok</p>' if 'auth=HTTP_ONLY_SECRET' in cookies else b'<p>cookie missing</p>'
            self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    class Server(http.server.ThreadingHTTPServer):
        def handle_error(self, request, address):
            # Negative TLS tests deliberately disconnect during the handshake.
            if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError, ssl.SSLError)):
                return
            super().handle_error(request, address)
    servers=[]
    try:
        for name in ['server','expired']:
            server=Server(('127.0.0.1',0),Handler)
            context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(root/f'{name}.pem',root/'server.key')
            server.socket=context.wrap_socket(server.socket,server_side=True)
            threading.Thread(target=server.serve_forever,daemon=True).start();servers.append(server)
        port,expired=[s.server_port for s in servers]
        env=dict(os.environ,NO_PROXY='localhost,127.0.0.1',no_proxy='localhost,127.0.0.1')
        subprocess.run([sys.argv[1],str(root/'profile'),f'https://localhost:{port}',f'https://127.0.0.1:{port}/',f'https://localhost:{expired}/',str(root/'ca.pem'),str(root/'wrong.pem')],check=True,env=env)
    finally:
        for server in servers:
            server.shutdown();server.server_close()
