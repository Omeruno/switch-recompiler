"""Fetch the pinned renderer plus its exact reviewed native-DOM patch."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
lock = json.loads((root / 'litehtml.lock.json').read_text())
check_only = '--check' in sys.argv
args = [x for x in sys.argv[1:] if x != '--check']
target = Path(args[0]) if args else root / 'build/litehtml'
patch = root / lock['patch']['file']
if hashlib.sha256(patch.read_bytes()).hexdigest() != lock['patch']['sha256']:
    raise RuntimeError('Renderer patch checksum differs from lock file')
def git(*args, **kwargs):
    return subprocess.run(['git', '-C', str(target), *args], check=True, **kwargs)
def status():
    return subprocess.check_output(['git','-C',str(target),'status','--porcelain','--untracked-files=all'],text=True)
def verify():
    actual = subprocess.check_output(['git','-C',str(target),'rev-parse','HEAD'],text=True).strip()
    if actual != lock['commit']:
        raise RuntimeError('Renderer commit differs from lock file')
    entries = status().splitlines()
    if sorted(line[3:] for line in entries) != sorted(lock['patch']['files']) or any(line[:3] != ' M ' for line in entries):
        raise RuntimeError('Renderer has changes beyond the reviewed patch')
    for name,digest in lock['patch']['files'].items():
        if hashlib.sha256((target/name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Unreviewed renderer changes: '+name)
if check_only:
    verify()
else:
    target.mkdir(parents=True, exist_ok=True)
    if not (target/'.git').exists():
        git('init', stdout=subprocess.DEVNULL)
        git('remote','add','origin',lock['repository'])
    current=subprocess.run(['git','-C',str(target),'rev-parse','HEAD'],capture_output=True,text=True)
    if current.returncode or current.stdout.strip()!=lock['commit']:
        if status():
            raise RuntimeError('Refusing to replace a modified renderer checkout')
        git('fetch','--depth=1','origin',lock['commit'])
        git('checkout','--detach',lock['commit'])
    if not status():
        git('apply','--check',str(patch.resolve()))
        git('apply',str(patch.resolve()))
    verify()
print(target.resolve())
