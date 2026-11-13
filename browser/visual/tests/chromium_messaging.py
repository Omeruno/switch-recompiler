"""Owned messaging fixture shared with the native browser; run using https_fixture.py."""
from pathlib import Path
import sys
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    context=browser.new_context(ignore_https_errors=True)
    page=context.new_page()
    page.goto(sys.argv[2]+'/loop-clean')
    page.add_script_tag(content=(Path(__file__).parent/'fixtures/messaging.js').read_text())
    page.wait_for_function('globalThis.messagingDone||globalThis.messagingError',timeout=15000)
    result=page.evaluate('({done:globalThis.messagingDone,error:globalThis.messagingError})')
    print(browser.version,result)
    browser.close()
    assert result.get('done'),result
