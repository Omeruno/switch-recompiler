#!/usr/bin/env python3
"""Record the successful full runner's bounded F evidence; fail closed on gaps.
This is called only after run_host_tests.sh succeeds, with fresh case logs and
its unique public guest build directory. It is not a physical acceptance report.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--guests', required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    build = root / 'build-host'
    destination = build / 'stage-f-acceptance.json'
    destination.unlink(missing_ok=True)
    names = [
        'test_vfs_mounts', 'test_file_operations', 'test_file_save_api',
        'test_file_copy', 'test_desktop_shell', 'test_shell_browser',
        'test_shell_file_actions', 'test_shell_context', 'test_shell_preferences',
        'test_text_document', 'test_bitmap_document', 'test_shell_links_tree',
        'workspace_output', 'shell_busy', 'shell_workspace', 'shell_workspace_gl',
        'shell_documents', 'shell_bitmaps', 'filesystem_sessions',
        'guest_sessions', 'gui_sessions', 'legacy_sessions',
    ]
    cases = []
    for name in names:
        path = build / (name + '.log')
        data = path.read_bytes()
        lines = data.decode('utf-8', errors='replace').splitlines()
        last = next((line for line in reversed(lines) if line.strip()), '')
        if 'PASS' not in last and not last.endswith('OK'):
            raise RuntimeError(f'Missing successful final result: {path.name}: {last!r}')
        cases.append({'case': name, 'result': last, 'sha256': hashlib.sha256(data).hexdigest()})
    fixtures = []
    for name in ['shell_bitmap.exe', 'shell_document.exe', 'shell_workspace.exe', 'shell_busy.exe']:
        data = (args.guests / name).read_bytes()
        if not data.startswith(b'MZ'):
            raise RuntimeError(f'Invalid public PE fixture: {name}')
        fixtures.append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root))
    report = {
        'format': 'switch-recompiler-stage-f-v1',
        'status': 'PASS_SOFTWARE_PENDING_HARDWARE',
        'scope': 'bounded single-Process native-shell F0-F5 baseline',
        'head_sha': revision, 'working_tree_dirty': dirty,
        'full_minGW_mesa_runner_succeeded': True,
        'cases': cases, 'public_fixtures': fixtures,
        'pending_hardware': ['Paint save/find/reopen', 'SD storage behavior',
                             'GPU/software fallback', 'Lite/touch/controller input',
                             'normal/error/abort repeated launches'],
        'outside_baseline': ['multiple guest Processes', 'universal EXE/MSI installers',
                             'COM/IShellLink', 'ZIP/RAR/archive import',
                             'PNG/JPEG and unsupported BMP encodings',
                             'pixel-identical XP theme', 'dynarec', 'DirectDraw/Direct3D'],
        'contracts': ['docs/F_ACCEPTANCE.md', 'docs/F3C_SHORTCUTS_TREE.md',
                      'docs/F4C_WORKSPACE_LAYERS.md', 'docs/F5_FILE_SAVE.md'],
    }
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f'Stage F: {len(cases)} software evidence groups PASS; hardware gates remain pending')


if __name__ == '__main__':
    main()
