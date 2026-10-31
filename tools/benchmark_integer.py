#!/usr/bin/env python3
"""Paired common CPU fast/reference execution with independent PE/pixel oracles."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('runner',type=Path);p.add_argument('fixture',type=Path);p.add_argument('output',type=Path)
p.add_argument('--pairs',type=int,default=7,choices=range(3,16));a=p.parse_args();a.output.unlink(missing_ok=True)
runner,fixture=a.runner.resolve(),a.fixture.resolve()
expected={'cpu':(0x7a218cb8,'769ca21d883d8625'),'tiles':(0x204080,'8e38b3b5c7422625'),'strokes':(0x208040,'91dd4ca5ebbb7625')}
samples=[];summary={}
def run(workload,path,mode='baseline'):
 r=json.loads(subprocess.check_output([str(runner),str(fixture),workload,mode,'fast',path],text=True,timeout=45))
 assert r['status']=='PASS' and r['cpu_path']==path and (r['checksum'],r['pixel_hash'])==expected[workload]
 assert r['integer_reference']>0 and (r['integer_fast']>0 if path=='fast' else r['integer_fast']==0)
 return r
for workload in expected:
 rows=[]
 for pair in range(a.pairs+1):
  batch={}
  for path in (['fast','reference'] if pair%2 else ['reference','fast']):
   r=run(workload,path);r.update(pair=pair,warmup=pair==0);samples.append(r);batch[path]=r
  f,b=batch['fast'],batch['reference']
  assert f['quanta']==b['quanta'] and f['integer_fast']+f['integer_reference']==b['integer_reference']
  if pair:rows.append(b['run_ns']/f['run_ns'])
 f=run(workload,'fast','census');b=run(workload,'reference','census')
 for key in ['instructions','hosts','opcodes','host_entries','quanta','checksum','pixel_hash']:assert f[key]==b[key],key
 assert f['instructions']==f['integer_fast']+f['integer_reference'] and not b['integer_fast']
 summary[workload]={'median_reference_over_fast':statistics.median(rows),'min_ratio':min(rows),'max_ratio':max(rows),
                    'fast_instructions':f['integer_fast'],'reference_instructions':f['integer_reference'],'total_instructions':f['instructions']}
root=Path(__file__).resolve().parent.parent
paths=['source/cpu/interpreter.cpp','source/cpu/interpreter.h','source/cpu/paged_memory.h','source/cpu/cpu_state.h',
       'source/runtime/integer_execution_report.h','source/cpu/exec_context.h','source/cpu/guest_memory.h',
       'source/cpu/fpu.cpp','source/cpu/sse.cpp','source/runtime/workload_profile.h','source/runtime/workload_report.h',
       'tests/test_integer_fast.cpp','tests/benchmark_guest.cpp','tests/guest/graphics_workload.c','tools/benchmark_integer.py']
report={'schema':1,'status':'PASS_HOST_CPU_MEASUREMENT_ONLY',
 'head_sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
 'working_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip()),
 'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'machine':platform.machine(),
 'runner_sha256':hashlib.sha256(runner.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),
 'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in paths},
 'pairs':a.pairs,'summary':summary,'samples':samples,
 'scope':'Real authored PE32 via scheduler/interpreter/HLE/GDI. CPU register fast path versus unchanged reference semantics; identical checksums,pixels,quanta and opcode/HLE census. Tracking enabled in both arms. Warmup excluded;no timing threshold,Switch FPS claim or dynarec acceptance.'}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print('Integer measurement PASS (3 public PE workloads,rotating pairs,exact CPU/pixel/quanta/opcode/HLE parity;host only): '+json.dumps(summary))
