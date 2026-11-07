"""Package the exact renderer fixtures, font, roots and license notices."""
from pathlib import Path
import shutil
root = Path(__file__).resolve().parents[1]
out = root / 'build/romfs'
(out / 'visual').mkdir(parents=True, exist_ok=True)
for name in ('index.html', 'style.css', 'landscape.png', 'landscape.jpg', 'landscape.webp', 'interactions.js', 'state.json', 'dynamic-demo.js', 'modules-demo.js', 'module-state.js', 'module-step.js', 'game-demo.js', 'game-core.wasm', 'game-sidecar.wasm'):
    shutil.copyfile(root / 'romfs' / name, out / 'visual' / name)
for name in ('DejaVuSans.ttf', 'DejaVu-LICENSE.txt', 'cacert.pem', 'CA_README.txt', 'CA_BUNDLE.json'):
    shutil.copyfile(root.parent / 'romfs' / name, out / name)
shutil.copytree(root / 'licenses', out / 'licenses', dirs_exist_ok=True)
