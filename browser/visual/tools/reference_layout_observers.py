"""Run the shared layout-observer fixture in Chromium and record provenance."""
import hashlib
from pathlib import Path
from playwright.sync_api import sync_playwright

fixture = Path(__file__).resolve().parents[1] / 'tests/fixtures/layout-observers.js'
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, args=['--no-sandbox'])
    page = browser.new_page(viewport={'width': 1280, 'height': 636})
    page.set_content('<!doctype html><html><head></head><body></body></html>')
    page.add_script_tag(content=fixture.read_text())
    page.wait_for_function('globalThis.layoutFixtureResult || globalThis.layoutFixtureError')
    error = page.evaluate('globalThis.layoutFixtureError')
    if error:
        raise RuntimeError(error)
    result = page.evaluate('globalThis.layoutFixtureResult')
    page.set_viewport_size({'width': 640, 'height': 636})
    page.wait_for_function('mediaChanges.length === 1')
    page.evaluate("if(fixtureMedia.matches || mediaChanges.join(',')!=='false' || resizeEvents!==1)throw Error('narrow media/resize')")
    page.set_viewport_size({'width': 1280, 'height': 636})
    page.wait_for_function('mediaChanges.length === 2')
    page.evaluate("if(mediaChanges.join(',')!=='false,true' || resizeEvents!==2)throw Error('wide media/resize')")
    record = f'Chromium {browser.version}\nSHA256 {hashlib.sha256(fixture.read_bytes()).hexdigest()}\n{result}|media-resize-changes\n'
    fixture.with_suffix('.reference.txt').write_text(record)
    print(record)
    browser.close()
