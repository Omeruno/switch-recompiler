"""Compare the shared buffered-network interface fixture with Chromium."""
import hashlib
from pathlib import Path
from playwright.sync_api import sync_playwright
fixture = Path(__file__).resolve().parents[1] / 'tests/fixtures/network-types.js'
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, args=['--no-sandbox'])
    page = browser.new_page()
    page.set_content('<!doctype html><html><body></body></html>')
    page.add_script_tag(content=fixture.read_text())
    page.wait_for_function('globalThis.networkTypesResult || globalThis.networkTypesError')
    error = page.evaluate('globalThis.networkTypesError')
    if error:
        raise RuntimeError(error)
    result = page.evaluate('globalThis.networkTypesResult')
    record = f'Chromium {browser.version}\nSHA256 {hashlib.sha256(fixture.read_bytes()).hexdigest()}\n{result}\n'
    fixture.with_suffix('.reference.txt').write_text(record)
    print(record)
    browser.close()
