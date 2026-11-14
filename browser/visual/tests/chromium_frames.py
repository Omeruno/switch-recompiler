"""Owned two-origin iframe reference; no public verification is solved."""
import sys
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    context=browser.new_context(ignore_https_errors=True)
    page=context.new_page();page.goto(sys.argv[2]+'/loop-clean')
    page.evaluate('''async()=>{
      document.body.style.margin='0';globalThis.config=await(await fetch('/api-config')).json();
      const frame=document.createElement('iframe');frame.id='frame';frame.style.cssText='display:block;border:0;width:300px;height:150px';document.body.appendChild(frame);
      globalThis.events=[];onmessage=e=>{if(e.source!==frame.contentWindow||e.origin!==config.cross)throw Error('origin/source');events.push(e.data);};frame.src=config.cross+'/frame-child';
    }''')
    page.wait_for_function("events.some(e=>e.kind==='ready'&&e.width===300&&e.height===150)")
    page.evaluate("globalThis.originalWindow=frame.contentWindow;frame.contentWindow.postMessage('ping',config.cross)")
    page.wait_for_function("events.some(e=>e.kind==='pong'&&e.counter===1)")
    page.locator('#frame').content_frame.locator('#inner').click(position={'x':30,'y':30})
    page.wait_for_function("events.some(e=>e.kind==='clicked'&&e.trusted&&e.x===30&&e.y===30)")
    page.locator('#frame').content_frame.locator('#check').check()
    page.wait_for_function("events.some(e=>e.kind==='checked'&&e.checked)")
    page.evaluate("frame.style.width='450px';frame.style.height='220px'")
    page.wait_for_function("events.some(e=>e.kind==='resized'&&e.width===450&&e.height===220)")
    assert page.evaluate('frame.contentWindow===originalWindow&&frame.contentDocument===null')
    for path in ['/frame-deny','/frame-same','/frame-none']:
        page.evaluate('(path)=>{events=[];frame.src=config.cross+path}',path)
        page.wait_for_timeout(300)
        assert page.evaluate('events.length===0'),path
    page.evaluate("events=[];frame.src=config.cross+'/frame-allow'")
    page.wait_for_function("events.some(e=>e.kind==='ready')")
    assert page.evaluate('frame.contentWindow===originalWindow')
    page.evaluate("const policy=document.createElement('meta');policy.httpEquiv='Content-Security-Policy';policy.content=\"frame-src 'self'\";document.head.appendChild(policy);frame.src=location.origin+'/frame-redirect'")
    page.wait_for_timeout(400)
    counts=page.evaluate("async()=>await(await fetch('/api-state')).json()")
    assert not counts.get('/frame-forbidden'),counts
    print(browser.version,'PASS iframe source/origin, native clicks, checkbox, viewport/state, XFO/ancestors and CSP redirect non-contact')
    browser.close()
