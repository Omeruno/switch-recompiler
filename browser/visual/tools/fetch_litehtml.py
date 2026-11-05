"""Fetch exactly the reviewed renderer commit into an ignored build directory."""
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lock = json.loads((root / 'litehtml.lock.json').read_text())
target = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/litehtml'
target.mkdir(parents=True, exist_ok=True)
if not (target / '.git').exists():
    subprocess.run(['git', 'init', str(target)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['git', '-C', str(target), 'remote', 'add', 'origin', lock['repository']], check=True)
current = subprocess.run(['git', '-C', str(target), 'rev-parse', 'HEAD'], capture_output=True, text=True)
if current.returncode or current.stdout.strip() != lock['commit']:
    subprocess.run(['git', '-C', str(target), 'fetch', '--depth=1', 'origin', lock['commit']], check=True)
    subprocess.run(['git', '-C', str(target), 'checkout', '--detach', lock['commit']], check=True)
assert subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip() == lock['commit']
if subprocess.check_output(['git', '-C', str(target), 'status', '--porcelain'], text=True).strip():
    raise RuntimeError('litehtml dependency has local changes; refusing an unreviewed renderer')
print(target.resolve())
