#!/usr/bin/env python3
"""Build a full ARM64 runtime and execute the authored PE through real native dispatch.

No prebuilt host objects/guest binaries. CI runs this alongside the subset oracles.
Rootless local cross tools can pass ARM64_CXX/ARM64_EXTRA_FLAGS/MINGW_CC/QEMU_ARM64.
"""
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
out=root/'build-host/arm64-dispatch'
objects=out/'objects'
guest=out/'guest'
objects.mkdir(parents=True,exist_ok=True)
guest.mkdir(exist_ok=True)
cxx=os.environ.get('ARM64_CXX','aarch64-linux-gnu-g++')
flags=['-std=c++17','-O1','-Wall','-I'+str(root)]+shlex.split(os.environ.get('ARM64_EXTRA_FLAGS',''))
sources=sorted(p for folder in ['cpu','ui','runtime','loader','decoder','win32'] for p in (root/'source'/folder).glob('*.cpp'))
assert len({p.stem for p in sources})==len(sources)
def compile_source(path):
    target=objects/(path.stem+'.o')
    subprocess.run([cxx,*flags,'-c',str(path),'-o',str(target)],cwd=root,check=True)
    return target
with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get('NATIVE_BUILD_JOBS','2'))) as pool:
    built=list(pool.map(compile_source,sources))
binary=out/'test_native_dispatch_pe'
subprocess.run([cxx,*flags,'-static',str(root/'tests/test_native_dispatch_pe.cpp'),*map(str,built),'-o',str(binary)],cwd=root,check=True)
cc=os.environ.get('MINGW_CC','i686-w64-mingw32-gcc')
base=[cc,'-O2','-Wall','-Wextra','-Werror','-nostdlib']
subprocess.run([*base,'-shared','-e','_DllMain@12','-Wl,--image-base,0x64000000',str(root/'tests/guest/native_dispatch_dll.c'),'-o',str(guest/'native_dispatch.dll'),'-lkernel32'],check=True)
subprocess.run([*base,'-e','_start@0',str(root/'tests/guest/native_dispatch.c'),'-o',str(guest/'native_dispatch.exe'),'-lkernel32','-luser32'],check=True)
runner=os.environ.get('QEMU_ARM64','qemu-aarch64')
log=out/'native-dispatch-pe-arm64.log'
with log.open('w') as stream:
    subprocess.run([runner,str(binary),str(guest/'native_dispatch.exe')],cwd=root,stdout=stream,stderr=subprocess.STDOUT,check=True)
print(log.read_text().splitlines()[-1],flush=True)
for name in ['test_native_batches','test_cpu_validation']:
    target=out/name
    subprocess.run([cxx,*flags,'-static',str(root/'tests'/(name+'.cpp')),*map(str,built),'-o',str(target)],cwd=root,check=True)
    args=[runner,str(target)]
    if name=='test_cpu_validation':args +=[str(guest/'native_dispatch.exe'),str(out/'cpu-validation-arm64.txt')]
    with (out/(name+'.log')).open('w') as stream:
        subprocess.run(args,cwd=root,stdout=stream,stderr=subprocess.STDOUT,check=True)
    print((out/(name+'.log')).read_text().splitlines()[-1],flush=True)
subprocess.run([sys.executable,str(root/'tools/measure_native_dispatch.py'),'--runner',runner,'--binary',str(binary),'--guest',str(guest/'native_dispatch.exe'),'--output',str(out/'native-dispatch-measurement.json')],cwd=root,check=True)
