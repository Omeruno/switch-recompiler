"""Independent reference check of the owned Wasm framebuffer and Canvas subset."""
import http.server
from playwright.sync_api import sync_playwright
import tempfile
import threading
from pathlib import Path
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    (folder/'game.wasm').write_bytes((root/'romfs/game-core.wasm').read_bytes())
    (folder/'index.html').write_text('''<!doctype html><meta charset="utf-8"><pre id="result">waiting</pre><script>
(async()=>{const check=(v,s)=>{if(!v)throw Error(s);};
const {instance}=await WebAssembly.instantiateStreaming(fetch('game.wasm'));
const e=instance.exports;e.step(0,0,0,95,20,1);check(e.score.value===1,'coin collision');
const canvas=document.createElement('canvas');canvas.width=128;canvas.height=80;document.body.appendChild(canvas);
const ctx=canvas.getContext('2d'),data=new ImageData(new Uint8ClampedArray(e.memory.buffer,0,128*80*4),128,80);ctx.putImageData(data,0,0);
check([...ctx.getImageData(95,20,1,1).data].join()==='58,208,255,255','Wasm player RGBA');
ctx.fillStyle='red';ctx.fillRect(0,0,4,4);ctx.globalAlpha=.5;ctx.fillStyle='blue';ctx.fillRect(0,0,1,1);
const mix=ctx.getImageData(0,0,1,1).data;check(Math.abs(mix[0]-127)<=1&&Math.abs(mix[2]-128)<=1&&mix[3]===255,'alpha');
canvas.width=canvas.width;check(ctx.globalAlpha===1&&ctx.getImageData(0,0,1,1).data[3]===0,'resize reset');
e.reset();check(e.score.value===0,'reset');document.getElementById('result').textContent='PASS owned Wasm and Canvas reference';
})().catch(e=>document.getElementById('result').textContent='FAIL '+e.message);
</script>''')
    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map={**http.server.SimpleHTTPRequestHandler.extensions_map,'.wasm':'application/wasm'}
        def __init__(self,*args,**kwargs):super().__init__(*args,directory=temp,**kwargs)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
            page=browser.new_page()
            page.goto(f'http://127.0.0.1:{server.server_port}/index.html')
            page.wait_for_function("document.getElementById('result').textContent!=='waiting'")
            result=page.locator('#result').inner_text()
            browser.close()
            assert result=='PASS owned Wasm and Canvas reference',result
    finally:
        server.shutdown()
    print('PASS Chromium owned Wasm/Canvas: collision, RGBA, alpha, resize and reset')
