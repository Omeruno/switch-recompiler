"""Real loopback TLS tests: custom CA, hostname, expiry and redirects."""
import http.server
import json
import urllib.parse
import os
import ssl
import subprocess
import sys
import tempfile
import threading
import time
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
        form_counts={'forbidden':0,'echo':0,'preserved':0}
        api_counts={}
        def api(self):
            parsed=urllib.parse.urlsplit(self.path)
            if not parsed.path.startswith('/api-'):return False
            query=urllib.parse.parse_qs(parsed.query);origin=self.headers.get('Origin','')
            self.api_counts[parsed.path+':'+self.command]=self.api_counts.get(parsed.path+':'+self.command,0)+1
            length=int(self.headers.get('Content-Length','0'));data=self.rfile.read(min(length,65537)) if length else b''
            if parsed.path=='/api-slow':time.sleep(.25)
            status=200;extra=[]
            if self.command=='OPTIONS':
                body=b'';status=204
                extra=[('Access-Control-Allow-Methods','GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS'),('Access-Control-Allow-Headers',self.headers.get('Access-Control-Request-Headers',''))]
                if query.get('allow_headers')==['wildcard']:extra[-1]=('Access-Control-Allow-Headers','*')
            elif parsed.path=='/api-config':body=json.dumps({'cross':f'https://localhost:{cross}','expired':f'https://localhost:{expired}'}).encode()
            elif parsed.path=='/api-state':body=json.dumps(self.api_counts).encode()
            elif parsed.path=='/api-redirect':
                status=int(query.get('code',['307'])[0]);extra=[('Location',query.get('target',['/api-echo'])[0])];body=b''
            elif parsed.path=='/api-large':body=b'x'*600000
            else:
                status=404 if parsed.path=='/api-404' else 200
                body=json.dumps({'method':self.command,'text':data.decode('utf-8',errors='replace'),'bytes':list(data),'cookie':bool(self.headers.get('Cookie')),'new_cookie':'network_set=1' in self.headers.get('Cookie',''),'origin':origin,'custom':self.headers.get('X-Test'),'authorization':self.headers.get('Authorization'),'content_type':self.headers.get('Content-Type')},ensure_ascii=False).encode()
            self.send_response(status)
            if origin and parsed.path!='/api-deny' and query.get('cors')!=['deny']:self.send_header('Access-Control-Allow-Origin','*' if query.get('cors')==['wildcard'] else origin)
            for key,value in extra:self.send_header(key,value)
            if parsed.path=='/api-headers-large':self.send_header('X-Large','h'*17000)
            self.send_header('X-Visible','shown');self.send_header('X-Hidden','private')
            self.send_header('Access-Control-Expose-Headers','X-Visible')
            if parsed.path=='/api-set':self.send_header('Set-Cookie','network_set=1; Path=/; Secure; HttpOnly')
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers()
            if self.command!='HEAD':self.wfile.write(body)
            return True
        def do_OPTIONS(self):self.api()
        def do_PUT(self):self.api()
        def do_PATCH(self):self.api()
        def do_DELETE(self):self.api()
        def do_HEAD(self):self.api()

        def do_POST(self):
            if self.api():return
            length=int(self.headers.get('Content-Length','0'))
            data=self.rfile.read(min(length,65537))
            values=urllib.parse.parse_qs(data.decode('utf-8'),keep_blank_values=True)
            valid=self.headers.get('Content-Type','').startswith('application/x-www-form-urlencoded') and values.get('user')==['Иван + &'] and values.get('password')==['LOCAL_TEST_PASSWORD'] and values.get('csrf')==['TOKEN'] and values.get('multi')==['a','b']
            routes={'/forms-303':(303,'/forms-final'),'/forms-307':(307,'/forms-preserved'),'/forms-policy':(307,'/forbidden'),'/forms-downgrade':(307,f'http://localhost:{self.server.server_port}/forbidden'),'/forms-loop':(307,'/forms-loop')}
            if self.path in routes:
                status,location=routes[self.path];self.send_response(status);self.send_header('Location',location)
                if self.path=='/forms-303':self.send_header('Set-Cookie','form_login=FORM_COOKIE; Path=/; Secure; HttpOnly')
                self.send_header('Content-Length','0');self.end_headers();return
            if self.path=='/forms-400':
                response=b'<title>Invalid input</title><p>Invalid input</p>';self.send_response(400)
            else:
                if self.path=='/forms-echo':self.form_counts['echo']+=1
                elif self.path=='/forms-preserved':self.form_counts['preserved']+=1
                elif self.path=='/forbidden':self.form_counts['forbidden']+=1
                response=json.dumps({'accepted':valid,'method':'POST'}).encode();self.send_response(200)
            self.send_header('Content-Length',str(len(response)));self.end_headers();self.wfile.write(response)

        def websocket(self):
            parsed=urllib.parse.urlsplit(self.path)
            if not parsed.path.startswith('/ws-'):return False
            import base64,hashlib,struct,socket
            self.close_connection=True
            self.connection.settimeout(3)
            def frame(op,data=b'',fin=True):
                n=len(data);header=bytes([op|(128 if fin else 0)])
                header+=bytes([n]) if n<126 else b'\x7e'+struct.pack('!H',n) if n<65536 else b'\x7f'+struct.pack('!Q',n)
                return header+data
            incoming=bytearray();fragmented=0
            def receive():
                nonlocal incoming,fragmented
                def read(n):
                    result=b''
                    while len(result)<n:
                        part=self.rfile.read(n-len(result))
                        if not part:raise EOFError()
                        result+=part
                    return result
                while True:
                    a,b=read(2);assert b&128,'client frame must be masked'
                    n=b&127
                    if n==126:n=struct.unpack('!H',read(2))[0]
                    elif n==127:n=struct.unpack('!Q',read(8))[0]
                    assert n<=524288
                    mask=read(4);raw=read(n);data=bytes(v^mask[i%4] for i,v in enumerate(raw));op=a&15
                    if op>=8:return op,data
                    if op:assert not fragmented;fragmented=op
                    else:assert fragmented
                    incoming.extend(data);assert len(incoming)<=524288
                    if a&128:
                        result=fragmented,bytes(incoming);incoming.clear();fragmented=0;return result
            try:
                if parsed.path=='/ws-slow':time.sleep(.2)
                key=self.headers.get('Sec-WebSocket-Key','');assert len(base64.b64decode(key))==16
                accept=base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
                if parsed.path=='/ws-bad-accept':accept='wrong'
                protocol=self.headers.get('Sec-WebSocket-Protocol','').split(',')[0].strip()
                if parsed.path=='/ws-bad-protocol':protocol='unoffered'
                response='HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n'
                if protocol:response+='Sec-WebSocket-Protocol: '+protocol+'\r\n'
                response+='Set-Cookie: ws_secret=1; Path=/; Secure; HttpOnly\r\n\r\n'
                wire=response.encode()
                # Partial TLS records exercise incremental handshake and decoder.
                self.connection.sendall(wire[:17]);time.sleep(.005);self.connection.sendall(wire[17:])
                if parsed.path.startswith('/ws-bad-'):return True
                if parsed.path=='/ws-invalid-utf8':self.connection.sendall(frame(1,b'\xff'));return True
                if parsed.path=='/ws-masked':self.connection.sendall(b'\x81\x80'+b'1234');return True
                if parsed.path=='/ws-oversize':self.connection.sendall(b'\x82\x7f'+struct.pack('!Q',524289));return True
                if parsed.path=='/ws-fast-close':self.connection.sendall(frame(1,b'last message')+frame(8,struct.pack('!H',1000)));receive();return True
                if parsed.path=='/ws-peer-close':self.connection.sendall(frame(8,struct.pack('!H',4001)+'пока'.encode()));receive();return True
                if parsed.path=='/ws-eof':return True
                meta=json.dumps({'origin':self.headers.get('Origin'),'cookie':self.headers.get('Cookie',''),'protocol':protocol}).encode()
                if parsed.path=='/ws-meta':self.connection.sendall(frame(1,meta))
                if parsed.path=='/ws-fragments':
                    text='Живой сокет'.encode();self.connection.sendall(frame(1,text[:1],False)+frame(9,b'ping')+frame(0,text[1:]));op,data=receive();assert op==10 and data==b'ping'
                while True:
                    op,data=receive()
                    if op==8:self.connection.sendall(frame(8,data));break
                    if op in (1,2):self.connection.sendall(frame(op,data))
            except (EOFError,ConnectionError,ssl.SSLError,socket.timeout):pass
            return True

        def log_message(self,*args):
            pass
        def do_GET(self):
            if self.websocket():return
            if self.api():return
            route=urllib.parse.urlsplit(self.path).path
            if route=='/nonce-source.js':
                body=b'globalThis.nonceExternal=true;';self.send_response(200);self.send_header('Content-Length',str(len(body)));self.send_header('Content-Type','text/javascript');self.end_headers();self.wfile.write(body);return
            if route=='/partial-document':
                self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length','100');self.end_headers()
                self.wfile.write(b'<p>');self.wfile.flush();time.sleep(.3)
                try:self.wfile.write(b'x'*97)
                except (BrokenPipeError,ConnectionError,ssl.SSLError):pass
                return
            if route.startswith('/dyn-'):
                mime='text/javascript';status=200
                if route in ('/dyn-slow.js','/dyn-leak.js'):time.sleep(.25)
                if route=='/dyn-page':mime='text/html';body=b"""<!doctype html><html><head><script>globalThis.windowLoads=0;globalThis.domReady=0;addEventListener('load',()=>windowLoads++);document.addEventListener('DOMContentLoaded',()=>domReady++);</script></head><body><div id=net-result>Start</div></body></html>"""
                elif route=='/dyn-slow.js':body=b"if(document.currentScript.id!=='slow')throw Error('dynamic currentScript');dynTrace.push('slow');document.getElementById('net-result').textContent='Slow';"
                elif route=='/dyn-fast.js':body=b"dynTrace.push('fast');document.getElementById('net-result').textContent='Fast';"
                elif route=='/dyn-async.js':body=b"dynTrace.push('async');"
                elif route=='/dyn-large.js':body=b'/*'+b'x'*140000+b'*/globalThis.largeExecuted=true;'
                elif route=='/dyn-leak.js':body=b"localStorage.setItem('dynamic_leak','1');"
                else:status=404;body=b'Not found'
                self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)))
                if route=='/dyn-page':self.send_header('Content-Security-Policy',"script-src 'self' 'unsafe-inline'; script-src-attr 'none'")
                self.end_headers();self.wfile.write(body);return
            if route.startswith('/esm-'):
                if route=='/esm-config':body=json.dumps({'cross':f'https://localhost:{cross}'}).encode();mime='application/json'
                elif route=='/esm-fixtures/slow.js':time.sleep(.2);body=(Path(__file__).resolve().parents[1]/'visual/tests/fixtures/modules/slow.js').read_bytes();mime='text/javascript'
                elif route.startswith('/esm-fixtures/') and '/' not in route[len('/esm-fixtures/'):]:
                    path=Path(__file__).resolve().parents[1]/'visual/tests/fixtures/modules'/route[len('/esm-fixtures/'):]
                    if not path.is_file():self.send_error(404);return
                    body=path.read_bytes();mime='text/javascript'
                elif route=='/esm-delay.js':time.sleep(.25);body=b"localStorage.setItem('module_leak','1');export const value=1;";mime='text/javascript'
                elif route=='/esm-large.js':body=b'/*'+b'x'*140000+b'*/globalThis.largeModuleExecuted=true;';mime='text/javascript'
                elif route=='/esm-wrong.js':body=b'globalThis.badMimeExecuted=true;';mime='text/plain'
                elif route=='/esm-deny.js':body=b'globalThis.badCorsExecuted=true;';mime='text/javascript'
                elif route=='/esm-redirect.js':
                    self.send_response(307);self.send_header('Location',urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get('target',['/esm-fixtures/extra.js'])[0]);self.send_header('Content-Length','0');self.end_headers();return
                else:self.send_error(404);return
                self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)))
                if self.headers.get('Origin') and route!='/esm-deny.js':self.send_header('Access-Control-Allow-Origin',self.headers['Origin'])
                self.end_headers();self.wfile.write(body);return
            loop_scripts={
                '/loop-async.js':"globalThis.trace=globalThis.trace||[];trace.push('async');",
                '/loop-ordered.js':"if(document.currentScript.id!=='ordered')throw Error('currentScript');trace.push('ordered');",
                '/loop-defer1.js':"states.push(document.readyState);trace.push('defer1');",
                '/loop-defer2.js':"states.push(document.readyState);trace.push('defer2');",
            }
            if route.startswith('/loop-'):
                if route=='/loop-ordered.js':time.sleep(.25)
                if route=='/loop-defer1.js':time.sleep(.15)
                if route=='/loop-image.png':time.sleep(.65)
                if route=='/loop-image2.png':time.sleep(.15)
                mime='text/javascript'
                if route in loop_scripts:body=loop_scripts[route].encode()
                elif route=='/loop-page':
                    mime='text/html';body=b"""<!doctype html><head><link rel=stylesheet href=/loop-style.css><script>globalThis.trace=globalThis.trace||[];globalThis.states=[];trace.push('inline');setTimeout(()=>trace.push('during-wait'),0);document.addEventListener('DOMContentLoaded',()=>trace.push('dom'));addEventListener('load',()=>trace.push('load'));addEventListener('load',()=>trace.push('window-capture'),true);</script><script id=ordered src=/loop-ordered.js></script><script async src=/loop-async.js></script><script defer src=/loop-defer1.js></script><script defer src=/loop-defer2.js></script></head><body><input id=field><div id=marker>Marker</div><img src=/loop-image.png></body>"""
                elif route=='/loop-clean':mime='text/html';body=b'<!doctype html><p>Clean page</p>'
                elif route in ('/loop-image.png','/loop-image2.png'):
                    mime='image/png';body=(Path(__file__).resolve().parents[1]/'visual/romfs/landscape.png').read_bytes()
                elif route in ('/loop-style.css','/loop-late.css'):
                    mime='text/css';body=b'#marker{width:231px}' if route=='/loop-late.css' else b'#marker{width:123px}img{display:block}'
                else:mime='application/json';body=b'{"ok":true}'
                self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);return

            cookies=self.headers.get('Cookie','')
            if self.path in ('/forms-final','/forms-count','/forbidden'):
                if self.path=='/forbidden':self.form_counts['forbidden']+=1
                result=self.form_counts if self.path=='/forms-count' else {'authenticated':'form_login=FORM_COOKIE' in cookies,'method':'GET'}
                response=json.dumps(result).encode();self.send_response(200);self.send_header('Content-Length',str(len(response)));self.end_headers();self.wfile.write(response);return
            if self.path.startswith('/redirect-http'):
                self.send_response(302);self.send_header('Location',f'http://localhost:{self.server.server_port}/must-not-load');self.end_headers();return
            if self.path.startswith('/visual-404'):
                body=b'missing'
                self.send_response(404);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);return
            if self.path.startswith('/visual-json'):
                body='{"value":42,"message":"Загрузка выполнена"}'.encode('utf-8')
                self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);return
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
        for name in ['server','expired','server']:
            server=Server(('127.0.0.1',0),Handler)
            context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(root/f'{name}.pem',root/'server.key')
            server.socket=context.wrap_socket(server.socket,server_side=True)
            threading.Thread(target=server.serve_forever,daemon=True).start();servers.append(server)
        port,expired,cross=[s.server_port for s in servers]
        env=dict(os.environ,NO_PROXY='localhost,127.0.0.1',no_proxy='localhost,127.0.0.1')
        subprocess.run([sys.argv[1],*sys.argv[2:],str(root/'profile'),f'https://localhost:{port}',f'https://127.0.0.1:{port}/',f'https://localhost:{expired}/',str(root/'ca.pem'),str(root/'wrong.pem')],check=True,env=env)
    finally:
        for server in servers:
            server.shutdown();server.server_close()
