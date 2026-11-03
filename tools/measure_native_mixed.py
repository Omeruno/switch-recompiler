#!/usr/bin/env python3
"""Source-bound mixed PE engine coverage and rotating timing controls;not FPS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--runner',required=True)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--guest',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
run=subprocess.run([a.runner,str(a.binary.resolve()),str(a.guest.resolve()),'--measure'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.with_suffix('.log').write_text(run.stdout)
if run.returncode:raise SystemExit(run.returncode)
rows=[json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
names=['register','memory','stack','calls','mixed']
engines=['fast-register','fast-mapped-lea-stack','reference','portable-ir','native-arm64','unclassified']
assert len(rows)==80 and {r['workload'] for r in rows}==set(names)
summary={}
for name in names:
    selected=[r for r in rows if r['workload']==name]
    assert len({(r['instructions'],r['quanta'],r['native_steps']) for r in selected})==1
    result={}
    for enabled in [False,True]:
        samples=[r for r in selected if r['engine_census']==enabled]
        assert len(samples)==8 and [r['pair'] for r in samples]==list(range(8))
        assert samples[0]['warmup'] and all(not r['warmup'] for r in samples[1:])
        for r in samples:
            assert r['native_architecture'] and r['native_steps']>0 and r['batch_calls']>0 and 1<r['max_batch']<=32
            combined=[]
            for key in ['fast_engines','native_engines']:
                matrix=r[key];assert set(matrix)==set(engines)
                opcode_totals={}
                for engine in engines:
                    group=matrix[engine];assert sum(group['opcodes'].values())==group['total']
                    for opcode,count in group['opcodes'].items():
                        assert 0<=int(opcode,16)<513 and count>0
                        opcode_totals[opcode]=opcode_totals.get(opcode,0)+count
                assert sum(opcode_totals.values())==(r['instructions'] if enabled else 0)
                assert not matrix['unclassified']['total'] and not matrix['portable-ir']['total']
                combined.append(opcode_totals)
            assert combined[0]==combined[1]
            assert r['native_engines']['native-arm64']['total']==(r['native_steps'] if enabled else 0)
            assert not r['fast_engines']['native-arm64']['total']
        stages={}
        for stage in ['total','first_loop','hot_loop']:
            assert all(r['fast_'+stage+'_ns']>0 and r['native_'+stage+'_ns']>0 for r in samples)
            ratios=[r['fast_'+stage+'_ns']/r['native_'+stage+'_ns'] for r in samples[1:]]
            stages[stage]={'median_g5_over_native':statistics.median(ratios),'min_ratio':min(ratios),'max_ratio':max(ratios)}
        result['engine_census_on' if enabled else 'engine_census_off']=stages
    off={r['pair']:r for r in selected if not r['engine_census'] and not r['warmup']}
    on={r['pair']:r for r in selected if r['engine_census'] and not r['warmup']}
    result['median_census_on_over_off_hot_wall']={mode:statistics.median(on[i][mode+'_hot_loop_ns']/off[i][mode+'_hot_loop_ns'] for i in off) for mode in ['fast','native']}
    sample=on[1]
    fallback={}
    for engine,group in sample['native_engines'].items():
        if engine!='native-arm64':
            for opcode,count in group['opcodes'].items():fallback[opcode]=fallback.get(opcode,0)+count
    result['native_instruction_fraction']=sample['native_steps']/sample['instructions']
    result['ranked_fallback_opcodes']=sorted(fallback.items(),key=lambda item:(-item[1],item[0]))
    summary[name]=result

paths=sorted(str(path.relative_to(root)) for path in (root/'source').rglob('*') if path.is_file())
paths+=['tests/guest/native_mixed.c','tests/guest/build.sh','tests/test_native_mixed_pe.cpp','tests/test_native_memory_boundary.cpp','tests/test_workload_profile.cpp','tools/measure_native_mixed.py','tools/run_arm64_native_dispatch.py']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
report={'schema':1,'status':'PASS_MIXED_PAIRED_QEMU_ARM64_ONLY','head_sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        'working_tree_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True)),
        'host':platform.platform(),'machine':platform.machine(),'runner':a.runner,'runner_version':subprocess.check_output([a.runner,'--version'],text=True).splitlines()[0],
        'compiler_version':subprocess.check_output([os.environ.get('ARM64_CXX','aarch64-linux-gnu-g++'),'--version'],text=True).splitlines()[0],
        'runtime_compiler_flags':['-std=c++17','-O1','-Wall']+shlex.split(os.environ.get('ARM64_EXTRA_FLAGS','')),
        'inspector_compiler_flags':['-std=c++17','-O1','-Wall','-Wextra','-Werror','-static']+shlex.split(os.environ.get('ARM64_EXTRA_FLAGS','')),
        'fixture_compiler_version':subprocess.check_output([os.environ.get('MINGW_CC','i686-w64-mingw32-gcc'),'--version'],text=True).splitlines()[0],
        'binary_sha256':digest(a.binary),'fixture_sha256':digest(a.guest),'source_sha256':{path:digest(root/path) for path in paths},
        'workloads':names,'pairs_per_condition':7,'samples':rows,'summary':summary,
        'scope':'Five authored PE workloads;global opcode/HLE profiling on in both controls,optional engine census off/on. Independent recurrences/1024 actual cells/balanced stack and exact CPU/HLE/quanta checked. Wall total includes load,metadata/verification/session/code retirement;acknowledged loop intervals include scheduler/dispatch/HLE acknowledgements. Census overhead uses hot intervals,not total verifier costs. Counts are executed instruction attempts,not time or byte/memory traces. No physical Switch/game FPS,general speed promise or timing threshold.'}
a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print('Native mixed paired measurement PASS (80 rows/5 workloads/7 measured pairs per census control;actual ARM64,independent values/full census/quanta;not physical FPS): '+json.dumps({name:{'native_fraction':r['native_instruction_fraction'],'g5_over_native_hot_profile_only':r['engine_census_off']['hot_loop']['median_g5_over_native']} for name,r in summary.items()}),flush=True)
