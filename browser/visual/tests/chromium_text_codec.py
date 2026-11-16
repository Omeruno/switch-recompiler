"""Owned Encoding API fixture; no provider challenge is loaded or solved."""
from pathlib import Path
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    page=browser.new_page()
    page.on('pageerror',lambda error: print('OWNED FIXTURE:',error.stack))
    page.add_script_tag(content=(Path(__file__).parent/'fixtures/text-codec.js').read_text())
    assert page.evaluate('globalThis.textCodecDone===true')
    print(browser.version,'PASS shared Encoding API fixture')
    browser.close()
