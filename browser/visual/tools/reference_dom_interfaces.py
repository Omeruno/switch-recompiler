"""Run the shared type/prototype regression fixture in Chromium."""
import hashlib
from pathlib import Path
from playwright.sync_api import sync_playwright
fixture=Path(__file__).resolve().parents[1]/'tests/fixtures/dom-interfaces.js'
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
    page=browser.new_page()
    page.set_content('<!doctype html><html><head></head><body></body></html>')
    page.add_script_tag(content=fixture.read_text())
    result=page.evaluate('globalThis.interfaceFixtureResult')
    if not result:raise RuntimeError('Interface fixture did not finish')
    record=f'Chromium {browser.version}\nSHA256 {hashlib.sha256(fixture.read_bytes()).hexdigest()}\n{result}\n'
    fixture.with_suffix('.reference.txt').write_text(record)
    print(record)
    browser.close()
