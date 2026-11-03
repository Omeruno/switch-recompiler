#!/usr/bin/env python3
"""Paired budgeted native-cache evidence. Never a Switch FPS/speed threshold."""
import argparse
import hashlib
import json
import platform
import os
import shlex
from pathlib import Path
import statistics
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--runner',default='')
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--guest',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--workload',choices=['dispatch','address'],default='dispatch')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
command=([a.runner] if a.runner else [])+[str(a.binary.resolve()),str(a.guest.resolve()),'--measure']
run=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.with_suffix('.log').write_text(run.stdout)
if run.returncode:raise SystemExit(run.returncode)
samples=[json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
assert len(samples)==8 and [s['pair'] for s in samples]==list(range(8))
assert samples[0]['warmup'] and all(not s['warmup'] for s in samples[1:])
assert all(s['native_architecture'] and s['native_steps']>0 and s['hits']>0 and s['compiles']>0 and s['batch_calls']>0 and 1<s['max_batch']<=32 for s in samples)
assert len({(s['instructions'],s['native_steps'],s['quanta']) for s in samples})==1
if a.workload=='address':
    assert all(s['lea_instructions']>=32768 and s['native_steps']*10>s['instructions']*9 and s['max_batch']>=5 for s in samples)
summary={}
for stage in ['total','first_loop','hot_loop']:
    ratios=[s[f'fast_{stage}_ns']/s[f'native_{stage}_ns'] for s in samples[1:]]
    assert all(s[f'fast_{stage}_ns']>0 and s[f'native_{stage}_ns']>0 for s in samples)
    summary[stage]={'median_g5_over_native':statistics.median(ratios),'min_ratio':min(ratios),'max_ratio':max(ratios)}
paths=['source/cpu/native_cache.h','source/cpu/native_cache.cpp','source/cpu/arm64_emitter.h','source/cpu/arm64_emitter.cpp','source/cpu/block_ir.h','source/cpu/block_ir.cpp','source/cpu/code_storage.h','source/cpu/code_storage.cpp','source/cpu/code_storage_platform.cpp','source/cpu/interpreter.h','source/cpu/interpreter.cpp','source/cpu/cpu_state.h','source/cpu/paged_memory.h','source/runtime/guest_session.h','source/runtime/guest_session.cpp','source/runtime/guest_execution.h','source/runtime/guest_execution.cpp','source/runtime/cpu_validation.h','source/runtime/cpu_validation.cpp','source/runtime/workload_profile.h','source/runtime/integer_execution_report.h','source/main.cpp','tests/test_native_dispatch_pe.cpp','tests/guest/native_dispatch.c','tests/guest/native_dispatch_dll.c','tests/guest/native_dispatch_kernel.h','tools/run_arm64_native_dispatch.py','tools/measure_native_dispatch.py']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
if a.workload=='address':paths+=['tests/test_native_address.cpp','tests/test_native_address_pe.cpp','tests/guest/native_address.c']
report={'schema':2,'native_mode':'budgeted_batch','status':'PASS_PAIRED_QEMU_ARM64_ONLY' if a.runner else 'PASS_PAIRED_NATIVE_HOST_ONLY','head_sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'working_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True)),'host':platform.platform(),'machine':platform.machine(),'runner':a.runner,'runner_version':subprocess.check_output([a.runner,'--version'],text=True).splitlines()[0] if a.runner else 'direct native host','binary_sha256':digest(a.binary),'fixture_sha256':digest(a.guest),'workload':a.workload,'dll_sha256':digest(a.guest.parent/'native_dispatch.dll') if a.workload=='dispatch' else None,'source_sha256':{path:digest(root/path) for path in paths},'compiler_version':subprocess.check_output([os.environ.get('ARM64_CXX','aarch64-linux-gnu-g++'),'--version'],text=True).splitlines()[0],'compiler_flags':['-std=c++17','-O1','-Wall','-static']+shlex.split(os.environ.get('ARM64_EXTRA_FLAGS','')),'fixture_compiler_version':subprocess.check_output([os.environ.get('MINGW_CC','i686-w64-mingw32-gcc'),'--version'],text=True).splitlines()[0],'pairs':7,'samples':samples,'summary':summary,'scope':'Budgeted experimental native batches versus ordinary G5. Total includes load/init/callbacks/threads/file/complete session and cache teardown;loop intervals use acknowledged guest phase markers and include scheduler/dispatch. EXE kernel is already visited by callbacks/worker before timing. No exclusive CPU time,physical Switch FPS or general speed claim. No pass/fail timing threshold.'}
if a.workload=='address':report['scope']='Authored LEA-heavy real PE versus G5;rotating pairs,acknowledged loops,full opcode/HLE/instruction/quanta and independent checksums;total includes complete session/code retirement. No DLL/window/file workload in this fixture. ARM64/QEMU only,no physical Switch FPS/general speed claim or timing threshold.'
a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print('Native dispatch paired measurement PASS (real ARM64 PE/native execution,exact CPU/HLE/quanta and independent checksums;host/QEMU only): '+json.dumps(summary),flush=True)
