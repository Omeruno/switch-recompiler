#!/usr/bin/env python3
"""Regenerate own fixture coordinates with Playwright Chromium, not our renderer."""
import hashlib
from pathlib import Path
from playwright.sync_api import sync_playwright
fixture = Path(__file__).resolve().parents[1] / "tests/fixtures/flex-gaps.html"
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, args=["--no-sandbox"])
    lines = [f"# Chromium {browser.version}; HTML SHA256 {hashlib.sha256(fixture.read_bytes()).hexdigest()}"]
    for width in (1280, 640):
        page = browser.new_page(viewport={"width": width, "height": 1500}, device_scale_factor=1)
        page.goto(fixture.as_uri())
        rows = page.eval_on_selector_all('[data-test]', '''nodes => nodes.map(n => {
          const box=e=>{const r=e.getBoundingClientRect(),s=getComputedStyle(e);
            const l=parseFloat(s.borderLeftWidth)+parseFloat(s.paddingLeft),t=parseFloat(s.borderTopWidth)+parseFloat(s.paddingTop);
            const right=parseFloat(s.borderRightWidth)+parseFloat(s.paddingRight),bottom=parseFloat(s.borderBottomWidth)+parseFloat(s.paddingBottom);
            return {x:r.x+l,y:r.y+t,w:r.width-l-right,h:r.height-t-bottom};};
          const a=box(n),b=box(n.parentElement);return [n.id,a.x-b.x,a.y-b.y,a.w,a.h];})''')
        for row in rows:
            lines.append(str(width) + " " + " ".join(str(x) for x in row))
        page.close()
    browser.close()
fixture.with_suffix('.tsv').write_text('\n'.join(lines)+'\n')
print(lines[0])
