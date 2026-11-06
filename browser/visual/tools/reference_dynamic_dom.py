"""Run the same behavioral fixture in Chromium and save its provenance/result."""
from pathlib import Path
import hashlib
from playwright.sync_api import sync_playwright
fixture=Path(__file__).resolve().parents[1]/'tests/fixtures/dynamic-dom.js'
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    page=browser.new_page()
    page.set_content('<!doctype html><html><head></head><body></body></html>')
    page.add_script_tag(content=fixture.read_text())
    page.wait_for_function('globalThis.dynamicFixtureDone===true')
    error=page.evaluate('globalThis.dynamicFixtureError||null')
    if error:raise RuntimeError(error)
    result=page.evaluate('globalThis.dynamicFixtureResult')
    record=f'Chromium {browser.version}\nSHA256 {hashlib.sha256(fixture.read_bytes()).hexdigest()}\n{result}\n'
    fixture.with_suffix('.reference.txt').write_text(record)
    print(record)
    browser.close()
