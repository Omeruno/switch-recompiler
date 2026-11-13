"""Run through browser/tests/https_fixture.py; no public CAPTCHA is solved."""
from pathlib import Path
import sys
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parent
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    context=browser.new_context(ignore_https_errors=True)
    page=context.new_page();page.goto(sys.argv[2]+'/loop-clean')
    page.evaluate("async()=>{globalThis.browserReference=true;globalThis.config=await(await fetch('/api-config')).json();}")
    page.add_script_tag(content=(root/'fixtures/browser-compat.js').read_text())
    assert page.evaluate('globalThis.browserCompatDone===true'),'Crypto/base64/Event reference'
    page.add_script_tag(content=(root/'fixtures/classic-compat.js').read_text())
    page.wait_for_function('globalThis.classicCompatibilityDone||globalThis.classicCompatibilityError',timeout=20000)
    result=page.evaluate('({done:globalThis.classicCompatibilityDone,error:globalThis.classicCompatibilityError})')
    print(browser.version,result)
    browser.close()
    assert result.get('done'),result
