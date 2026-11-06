#!/usr/bin/env python3
"""Optional reference: invoke through browser/tests/https_fixture.py. Requires Playwright.

Chromium ignores the generated CA; expiry rejection is a native-only test.
"""
import json
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright

base = sys.argv[2]
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, args=['--no-sandbox'])
    context = browser.new_context(ignore_https_errors=True)
    context.add_cookies([{
        'name': 'hidden', 'value': '1', 'url': base, 'secure': True, 'httpOnly': True,
    }])
    page = context.new_page()
    page.goto(base + '/loop-clean')
    page.evaluate('''async () => {
        globalThis.browserReference = true;
        globalThis.config = await (await fetch('/api-config')).json();
    }''')
    fixture = Path(__file__).resolve().parent / 'fixtures/websocket.js'
    page.evaluate('() => {' + fixture.read_text() + '}')
    try:
        page.wait_for_function('globalThis.websocketDone || globalThis.websocketError', timeout=15000)
    except Exception:
        print('stage', page.evaluate('globalThis.websocketStage'), flush=True)
        raise
    result = page.evaluate('({done:globalThis.websocketDone,error:globalThis.websocketError})')
    print(browser.version, json.dumps(result))
    browser.close()
    assert result.get('done'), result
