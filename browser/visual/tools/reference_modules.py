"""Run the real ES module graph in Chromium; retain trace and source provenance."""
import hashlib
import json
from pathlib import Path
from urllib.parse import urlsplit
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parents[1]/'tests/fixtures/modules'
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    page=browser.new_page()
    html="""<!doctype html><html><head><script>globalThis.moduleTrace=[];globalThis.lifecycle=[];addEventListener('DOMContentLoaded',()=>lifecycle.push('dom'));addEventListener('load',()=>lifecycle.push('load'));addEventListener('error',e=>globalThis.fixtureError=e.message);</script><script nomodule>globalThis.badNoModule=true;</script><script type=module src='/esm-fixtures/main.js'></script></head><body></body></html>"""
    def route(r):
        path=urlsplit(r.request.url).path
        if path=='/':r.fulfill(content_type='text/html',body=html)
        else:r.fulfill(content_type='text/javascript',body=(root/path.rsplit('/',1)[-1]).read_bytes())
    page.route('https://modules.test/**',route)
    page.goto('https://modules.test/')
    page.wait_for_function('globalThis.moduleFixtureResult || globalThis.fixtureError')
    error=page.evaluate('globalThis.fixtureError')
    if error:raise RuntimeError(error)
    result=page.evaluate('({result:moduleFixtureResult,trace:moduleTrace,lifecycle,nomodule:!!globalThis.badNoModule})')
    page.evaluate("globalThis.moduleErrors=[];globalThis.negativeDone=false;(async()=>{for(const name of ['parse-error','link-error','runtime-error','parent-error']){let a,b;try{await import('/esm-fixtures/'+name+'.js');}catch(e){a=e;}try{await import('/esm-fixtures/'+name+'.js');}catch(e){b=e;}if(!a||!b)throw Error('missing error');moduleErrors.push({name,type:a.name,repeatType:b.name,same:a===b});}negativeDone=true;})().catch(e=>globalThis.fixtureError=String(e));")
    page.wait_for_function('negativeDone || globalThis.fixtureError')
    error=page.evaluate('globalThis.fixtureError')
    if error:raise RuntimeError(error)
    result['errors']=page.evaluate('moduleErrors')
    events={}
    for filename in ['parse-error.js','link-error.js','runtime-error.js','tla-error.js']:
        item=browser.new_page()
        if filename=='tla-error.js':
            body="<script>globalThis.moduleLifecycle=[];addEventListener('DOMContentLoaded',()=>moduleLifecycle.push('dom'));addEventListener('load',()=>moduleLifecycle.push('window-load'));addEventListener('error',()=>moduleLifecycle.push('error'));</script><script type=module src='/esm-fixtures/tla-error.js' onload=\"moduleLifecycle.push('module-load')\"></script>"
        else:
            body="<script>globalThis.errorsTrace=[];addEventListener('error',()=>errorsTrace.push('window-error'));addEventListener('load',()=>errorsTrace.push('window-load'));</script><script type=module src='/esm-fixtures/"+filename+"' onload=\"errorsTrace.push('script-load')\" onerror=\"errorsTrace.push('script-error')\"></script>"
        def error_route(r):
            path=urlsplit(r.request.url).path
            if path=='/':r.fulfill(content_type='text/html',body=body)
            else:r.fulfill(content_type='text/javascript',body=(root/path.rsplit('/',1)[-1]).read_bytes())
        item.route('https://module-errors.test/**',error_route)
        item.goto('https://module-errors.test/')
        if filename=='tla-error.js':
            item.wait_for_function("moduleLifecycle.includes('error')")
            events[filename]=item.evaluate('moduleLifecycle')
        else:events[filename]=item.evaluate('errorsTrace')
        item.close()
    result['errorEvents']=events
    record={'browser':f'Chromium {browser.version}','sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(root.glob('*.js'))},'result':result}
    target=root.parent/'modules.reference.json'
    target.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(result))
    browser.close()
