#!/usr/bin/env python3
"""Fail-closed ledger after the complete MinGW/Mesa runner; bounded native scope."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--guests', required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve(); build = root/'build-host'
    destination = build/'stage-gpu-acceptance.json'
    destination.unlink(missing_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root))
    cases = {
        'test_gpu_gdi_unavailable': ['GPU GDI unavailable: PASS'],
        'gpu_gdi_prototype': ['GPU GDI prototype: PASS'],
        'gpu_gdi_targets': ['GPU GDI targets: PASS'],
        'gpu_gdi_shared_context': ['GPU GDI shared context: PASS', 'Resident production layers: PASS', 'Owned mixed frame: PASS', 'Owned mixed context loss: PASS'],
        'gpu_window_bindings': ['GPU window bindings: PASS'],
        'gpu_window_coherence': ['Mixed native workspace: PASS', 'GPU coherence mode=GL-error: PASS', 'GPU coherence mode=actual-loss: PASS']+
            [f'Mixed output device mode={n}: PASS' for n in range(7)]+
            [f'Host pixel audit mode={mode}: PASS' for mode in ('read-write', 'fault', 'thread-exit', 'thread-exit-fault')],
        'test_cpu_access': ['CPU host access: PASS'],
        'test_window_damage_mask': ['Exact window damage mask PASS'],
        'gpu_damage_mask': ['Exact damage x86 PASS'],
        'gpu_capacity_replay': ['Capacity checkpoint replay PASS'],
        'gpu_differential_import': ['Differential CPU import PASS'],
        'gpu_gdi_retirement': ['Covered journal retirement: PASS'],
        'gpu_gdi_dispatch': ['Ordinary x86 GDI dispatch: PASS', 'GDI dispatch measurement: PASS'],
        'gpu_gdi_warm': ['Warm ordinary GDI: PASS', 'Warm GDI measurement: PASS'],
        'gpu_workspace_reuse': ['Retained transitions PASS', 'Workspace reuse measurement: PASS'],
        'gpu_workspace_cycles': ['CPU/native workspace cycles PASS', 'CPU/native measurement PASS', 'CPU route transitions mode0 PASS', 'CPU route transitions mode1 PASS', 'CPU route transitions mode2 PASS'],
        'gpu_gdi_batches': ['GPU GDI batching: PASS'],
    }
    evidence = []
    for name, markers in cases.items():
        path = build/(name+'.log'); data = path.read_bytes(); text = data.decode('utf-8', errors='replace')
        for marker in markers:
            if marker not in text:
                raise RuntimeError(f'Missing {marker!r} in {name}')
        evidence.append(dict(case=name, markers=markers, sha256=digest(data)))
    fixture_names = ['gpu_gdi_dispatch.exe', 'gpu_gdi_warm.exe', 'gpu_workspace_PARTIAL.exe',
                     'gpu_workspace_COPY.exe', 'gpu_workspace_REFUSAL.exe', 'gpu_window_bindings.exe',
                     'gpu_window_coherence.exe', 'gpu_damage_mask.exe']
    fixtures = {}
    for name in fixture_names:
        data = (args.guests/name).read_bytes()
        assert data[:2] == b'MZ', name
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', data, pe+4)[0] == 0x14c, name
        assert struct.unpack_from('<H', data, pe+24)[0] == 0x10b, name
        fixtures[name] = dict(bytes=len(data), sha256=digest(data))
    reports = []
    requirements = {
        'gpu_gdi_dispatch': (1, 'PASS_HOST_X86_GL_DISPATCH_PENDING_HARDWARE', 14),
        'gpu_gdi_warm': (1, 'PASS_HOST_WARM_GDI_PENDING_HARDWARE', 108),
        'gpu_workspace_reuse': (4, 'PASS_HOST_RETAINED_WORKSPACE_PENDING_HARDWARE', 540),
        'gpu_workspace_cycles': (2, 'PASS_HOST_CPU_NATIVE_CYCLES_PENDING_HARDWARE', 432),
        'gpu_gdi_batches': (1, 'PASS_HOST_GL_BATCH_COMPARISON_PENDING_HARDWARE', 42),
    }
    for name, (schema, status, count) in requirements.items():
        path = build/(name+'.json'); data = path.read_bytes(); r = json.loads(data)
        assert r['schema'] == schema and r['status'] == status and len(r['samples']) == count, name
        assert r['head_sha'] == revision and r['working_tree_dirty'] == dirty, f'Stale source identity: {name}'
        hashes = r.get('fixture_sha256', {})
        if isinstance(hashes, str):
            fixture = 'gpu_gdi_dispatch.exe' if name == 'gpu_gdi_dispatch' else 'gpu_gdi_warm.exe'
            hashes = {fixture: hashes}
        for fixture, expected in hashes.items():
            assert fixtures[fixture]['sha256'] == expected, f'Stale PE identity: {name}/{fixture}'
        if 'runner_sha256' in r:
            runner = {'gpu_gdi_dispatch': 'test_gpu_gdi_dispatch', 'gpu_gdi_warm': 'test_gpu_gdi_warm',
                      'gpu_workspace_cycles': 'test_gpu_workspace_cycles', 'gpu_gdi_batches': 'benchmark_gpu_gdi'}[name]
            assert r['runner_sha256'] == digest((build/runner).read_bytes()), f'Stale runner: {name}'
        reports.append(dict(name=name, schema=schema, samples=count, sha256=digest(data)))
    cycles = json.loads((build/'gpu_workspace_cycles.json').read_bytes())
    assert len(cycles['sessions']) == 48 and all(s['exit_code'] == 0 for s in cycles['sessions'])
    # Source snapshot supplements SHA/dirty state for locally uncommitted evidence.
    manifest = {}
    for directory in ('source', 'tests', 'tools'):
        for path in sorted((root/directory).rglob('*')):
            if path.is_file() and path.suffix in ('.cpp', '.h', '.c', '.py', '.sh'):
                manifest[str(path.relative_to(root))] = digest(path.read_bytes())
    report = dict(format='switch-recompiler-gpu-acceptance-v1', status='PASS_BOUNDED_NATIVE_EXPERIMENT_PENDING_HARDWARE',
                  scope='G4b.2a-o bounded shared-context/window/CPU-GPU/shell/device/ordinary-solid native integration',
                  head_sha=revision, working_tree_dirty=dirty, full_mingw_mesa_runner_succeeded=True,
                  cases=evidence, measurements=reports, public_fixtures=fixtures,
                  source_manifest_sha256=digest(json.dumps(manifest, sort_keys=True).encode()), source_manifest=manifest,
                  workload_comparison=cycles['summary'],
                  default_gdi='CPU', native_activation='flag1 reference native; flag2 no-resident CPU routing; bounded top-level solid FillRect/PATCOPY',
                  decision='No broad/default activation from host timing. Routing stays explicit flag2 with reference comparisons and real DIB/device/read/owner/fault gates; D1 bounded registry implemented; next real key names/VK-scancode translation.',
                  pending_hardware=['Paint/GPU/fallback performance and correctness', 'Lite/controller/touch input', 'normal/error/abort repeated console launches'],
                  outside_scope=['full Windows/XP shell', 'arbitrary GDI/ROP/memory GPU dispatch', 'large or child-window native admission', 'x64', 'full ARM64 dynarec', 'DirectDraw/D3D9/shaders/audio translation', 'PC-game compatibility/FPS', 'installers/ZIP/RAR'],
                  contracts=['docs/G4B2N_SOFTWARE_ACCEPTANCE.md', 'docs/G4B2M_DIFFERENTIAL_IMPORT.md', 'docs/G4B2G_HOST_PIXEL_AUDIT.md'])
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(f'GPU integration ledger PASS: {len(evidence)} software groups, five source/fixture-bound reports; default CPU and hardware gates remain open')


if __name__ == '__main__':
    main()
