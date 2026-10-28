#!/usr/bin/env python3
"""Same-public-PE CPU/native complete-workspace comparison; no timing threshold."""
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import struct
import subprocess
import sys


def main():
    runner, *fixtures, output = map(Path, sys.argv[1:])
    output.unlink(missing_ok=True)
    assert len(fixtures) == 4
    hashes = {}
    for p in fixtures:
        data = p.read_bytes()
        assert data[:2] == b'MZ', p
        at = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[at:at+4] == b'PE\0\0' and struct.unpack_from('<H', data, at+4)[0] == 0x14c, p
        assert struct.unpack_from('<H', data, at+24)[0] == 0x10b, p
        hashes[p.name] = hashlib.sha256(data).hexdigest()
    assert len(hashes) == 4
    run = subprocess.run([str(p.resolve()) for p in (runner, *fixtures)], text=True,
                         capture_output=True, timeout=180,
                         env=dict(os.environ, EGL_PLATFORM='surfaceless', LIBGL_ALWAYS_SOFTWARE='1'))
    print(run.stdout, end='')
    if run.returncode:
        print(run.stderr, file=sys.stderr)
        raise SystemExit(run.returncode)
    assert 'CPU/native workspace cycles PASS' in run.stdout
    rows, reads, exits = {}, {}, {}
    for line in run.stdout.splitlines():
        fields = line.split()
        if not fields or fields[0] not in ('CYCLE', 'READ', 'EXIT'):
            continue
        kind = fields[0]
        sample, gpu, workload = map(int, fields[1:4])
        assert sample in range(4) and gpu in (0, 1) and workload in range(4)
        session = (sample, gpu, workload)
        if kind == 'CYCLE':
            frame = int(fields[4]); assert frame in range(9)
            key = (*session, frame); assert key not in rows
            raster, refresh, output_us = map(float, fields[5:8])
            assert all(math.isfinite(t) and t >= 0 for t in (raster, refresh, output_us)) and refresh >= output_us
            candidates, accepted, refused, imports, commits, uploads, renderer_bytes, snapshots, reused = map(int, fields[8:])
            assert min(candidates, accepted, refused, imports, commits, uploads, renderer_bytes, snapshots, reused) >= 0
            admitted = gpu and workload != 3
            expected = (65 if workload == 2 else 64) * (frame+1)
            assert candidates == (expected if gpu else 0)
            assert accepted == (expected if admitted else 0) and refused == (expected if gpu and not admitted else 0)
            assert imports == ((frame+2 if workload == 2 else 1) if admitted else 0)
            assert commits == (frame+1 if admitted and workload == 2 else 0)
            assert uploads == (((16384+16*(frame+1))*4 if workload == 2 else 65536) if admitted else 0)
            assert snapshots == (1 if admitted and workload != 2 else frame+1)
            assert reused == (frame if admitted and workload != 2 else 0)
            assert renderer_bytes > 0
            rows[key] = dict(sample=sample, mode=('cpu', 'native')[gpu], workload=('tiles', 'partial', 'dib', 'refusal')[workload],
                             frame=frame, raster_us=raster, refresh_us=refresh, output_us=output_us,
                             prepare_us=refresh-output_us, cycle_us=raster+refresh,
                             candidates=candidates, accepted=accepted, refused=refused, imports=imports,
                             cpu_commits=commits, import_upload_bytes=uploads, renderer_upload_bytes=renderer_bytes,
                             snapshots=snapshots, reused=reused)
        elif kind == 'READ':
            assert session not in reads
            time = float(fields[4]); count = int(fields[5])
            assert math.isfinite(time) and time >= 0 and count == ((10 if workload == 2 else 1) if gpu and workload != 3 else 0)
            reads[session] = dict(read_us=time, final_target_reads=count)
        else:
            assert session not in exits and int(fields[4]) == 0
            tail = float(fields[5]); assert math.isfinite(tail) and tail >= 0
            exits[session] = tail
    assert len(rows) == 288 and len(reads) == len(exits) == 32 and reads.keys() == exits.keys()
    sessions = []
    for key, tail in exits.items():
        frames = [rows[(*key, frame)] for frame in range(9)]
        assert all(frames[n]['renderer_upload_bytes'] >= frames[n-1]['renderer_upload_bytes'] for n in range(1, 9))
        sessions.append(dict(sample=key[0], mode=('cpu', 'native')[key[1]], workload=('tiles', 'partial', 'dib', 'refusal')[key[2]],
                             exit_code=0, **reads[key], tail_us=tail,
                             sequence_us=sum(r['cycle_us'] for r in frames)+reads[key]['read_us']+tail))
    summary = {}
    for workload in ('tiles', 'partial', 'dib', 'refusal'):
        summary[workload] = {}
        for mode in ('cpu', 'native'):
            phases = {}
            for phase in ('cold', 'warm'):
                group = [r for r in rows.values() if r['sample'] > 0 and r['mode'] == mode and r['workload'] == workload and ((r['frame'] == 0) == (phase == 'cold'))]
                phases[phase] = {k: statistics.median(r[k] for r in group) for k in ('raster_us', 'prepare_us', 'output_us', 'refresh_us', 'cycle_us')}
                phases[phase].update(samples=len(group), cycle_min_us=min(r['cycle_us'] for r in group), cycle_max_us=max(r['cycle_us'] for r in group))
            group = [s for s in sessions if s['sample'] > 0 and s['mode'] == mode and s['workload'] == workload]
            phases['sequence'] = {k: statistics.median(s[k] for s in group) for k in ('sequence_us', 'read_us', 'tail_us')}
            summary[workload][mode] = phases
        summary[workload]['native_cpu_warm_ratio'] = summary[workload]['native']['warm']['cycle_us']/summary[workload]['cpu']['warm']['cycle_us']
    root = Path(__file__).resolve().parent.parent
    report = dict(schema=1, status='PASS_HOST_CPU_NATIVE_CYCLES_PENDING_HARDWARE',
                  head_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                  working_tree_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root)),
                  fixture_sha256=hashes, runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                  host=dict(platform=platform.platform(), machine=platform.machine(), mesa_shader_cache_disable=os.environ.get('MESA_SHADER_CACHE_DISABLE', 'unset')),
                  samples=list(rows.values()), sessions=sessions, summary=summary,
                  scope='32 real-x86 normal-exit0 sessions, 288 independent full1280x720 actual production EGL frames; four workloads and rotating same-build CPU/native pairs. CPU uses actual ShellSession.TryCompose/Scene -> Presentation.Submit/Render; native uses current coherent solid dispatch -> MixedWorkspace.Refresh -> actual renderer. Not CPU through empty mixed frames. Sample0 pairs excluded from phase/sequence medians. Raster sums timed RunOne calls only (same test-only clock instrumentation), includes real HLE/DIB/checkpoint work. Refresh includes prepare and driver output submit; cycle=raster+refresh. Explicit terminal GetPixel phase and guest exit tail are separate and included in sequence_us. Pixel oracle FBO reads, host inspection/bookkeeping, idle waits, module load/prepare, frontend setup and native teardown are excluded. FBO oracle synchronizes between frames; output is submission without physical swap/scanout/GPU timestamp. Attempted import and renderer payload bytes are distinct, not physical DMA. Size-refusal window is600x128; no target may allocate and all576 calls must refuse to real CPU. No timing threshold, default activation or physical Switch/FPS/game acceptance.')
    output.write_text(json.dumps(report, indent=2)+'\n')
    print('CPU/native measurement PASS (32 normal exits/288 EGL frames; actual CPU/native routes, reads/DIB/refusal, no timing threshold)')


if __name__ == '__main__':
    main()
