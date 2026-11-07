"""Fetch exactly the reviewed runtime commit into an ignored build directory."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lock = json.loads((root / 'wamr.lock.json').read_text())
target = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/wamr'
target.mkdir(parents=True, exist_ok=True)
if not (target / '.git').exists():
    subprocess.run(['git', 'init', str(target)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['git', '-C', str(target), 'remote', 'add', 'origin', lock['repository']], check=True)
current = subprocess.run(['git', '-C', str(target), 'rev-parse', 'HEAD'], capture_output=True, text=True)
if current.returncode or current.stdout.strip() != lock['commit']:
    if subprocess.check_output(['git','-C',str(target),'status','--porcelain'],text=True).strip():
        raise RuntimeError('Refusing to replace a modified WAMR checkout')
    subprocess.run(['git', '-C', str(target), 'fetch', '--depth=1', 'origin', lock['commit']], check=True)
    subprocess.run(['git', '-C', str(target), 'checkout', '--detach', lock['commit']], check=True)
assert subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip() == lock['commit']
patch = root / lock['patch']['file']
if hashlib.sha256(patch.read_bytes()).hexdigest() != lock['patch']['sha256']:
    raise RuntimeError('WAMR patch checksum differs from lock')
status=subprocess.check_output(['git','-C',str(target),'status','--porcelain','--untracked-files=all'],text=True)
if not status:
    subprocess.run(['git','-C',str(target),'apply','--check',str(patch.resolve())],check=True)
    subprocess.run(['git','-C',str(target),'apply',str(patch.resolve())],check=True)
status=subprocess.check_output(['git','-C',str(target),'status','--porcelain','--untracked-files=all'],text=True)
if sorted(line[3:] for line in status.splitlines()) != sorted(lock['patch']['files']) or any(line[:3]!=' M ' for line in status.splitlines()):
    raise RuntimeError('Unreviewed WAMR changes')
for name,digest in lock['patch']['files'].items():
    if hashlib.sha256((target/name).read_bytes()).hexdigest()!=digest:
        raise RuntimeError('Unreviewed WAMR file: '+name)
print(target.resolve())
