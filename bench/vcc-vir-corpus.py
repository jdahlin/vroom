#!/usr/bin/env python3
"""VPP wrapper for native x86-64 corpus builds; retain per-TU diagnostics and VIR coverage.

VPP=$PWD/bench/vcc-vir-corpus.py VCC_VIR_CORPUS_LOG=DIR bench/pg/build.sh ...
VCC_VIR_CORPUS_VPP selects vpp; VCC_VIR_CORPUS_MODE is bvls (default) or 0.
Compile jobs explicitly use x86-64, -O2 and -g0 so debug fallback cannot hide coverage.
The surrounding build may use bench/pg/cc to link with clang. No compiler fallback here.
Summarize a run with: bench/vcc-vir-corpus.py --summary DIR
"""
import collections
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from hostenv import llvm

ROOT = Path(__file__).resolve().parent.parent

def summary(directory):
    rows = [json.loads(s) for s in (Path(directory) / 'jobs.jsonl').read_text().splitlines()]
    reasons = collections.Counter()
    built = lowered = 0
    failed = []
    failed_probes = []
    invalid = []
    modes = {}
    for r in rows:
        built += len(r['built'])
        lowered += len(r['lowered'])
        reasons.update(r['fallback'].values())
        m = modes.setdefault(r['mode'], dict(jobs=0, built=0, lowered=0, vectorized_candidates=0))
        m['jobs'] += 1
        m['built'] += len(r['built'])
        m['lowered'] += len(r['lowered'])
        m['vectorized_candidates'] += len(r['vectorized'])
        if r.get('invalid_vir'):
            invalid.append(r['diagnostics'])
        if r['status']:
            (failed_probes if r.get('probe') else failed).append(
                {k: r[k] for k in ('cwd', 'argv', 'status', 'diagnostics')})
    result = dict(jobs=len(rows), failed_jobs=len(failed), built_functions=built,
                  lowered_functions=lowered, modes=modes, fallback_reasons=dict(reasons.most_common()),
                  invalid_vir_jobs=invalid, failed_probes=failed_probes, failures=failed)
    (Path(directory) / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--summary':
        summary(sys.argv[2]); return 0
    directory = Path(os.environ['VCC_VIR_CORPUS_LOG']).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    vpp = os.environ.get('VCC_VIR_CORPUS_VPP', str(ROOT / 'build/linux/ccs/vpp'))
    mode = os.environ.get('VCC_VIR_CORPUS_MODE', os.environ.get('VCC_VIR', 'bvls'))
    if mode != '0':
        mode += 'vs'
    argv = sys.argv[1:]
    includes = []
    system = os.environ.get('VCC_VIR_CORPUS_SYSINCLUDES')
    if system is None:
        resource = subprocess.check_output([llvm('clang'), '-print-resource-dir'], text=True).strip()
        system = os.pathsep.join(p for p in (resource + '/include', '/usr/local/include',
                                             '/usr/include/x86_64-linux-gnu', '/usr/include') if Path(p).is_dir())
    for path in system.split(os.pathsep):
        if path:
            includes += ['-isystem', path]
    cmd = [vpp, *argv, *includes, '--target=x86_64-linux-gnu', '-O2', '-g0', '-fdiagnostics-color=never']
    env = dict(os.environ, VCC_VIR=mode, VCC_VIR_CHECK='1', VCC_VIR_VEC='r')
    start = time.monotonic()
    p = subprocess.run(cmd, stderr=subprocess.PIPE, env=env)
    diagnostics = p.stderr.decode(errors='replace')
    path = directory / f'{time.time_ns()}-{os.getpid()}.stderr'
    path.write_text(diagnostics)
    # Keep the complete diagnostic stream on disk; build logs contain actual diagnostics only.
    sys.stderr.write(diagnostics if os.environ.get('VCC_VIR_CORPUS_REPORT') else
                     '\n'.join(l for l in diagnostics.splitlines() if not l.startswith(('vir: ', 'vec: '))) + '\n')
    binary = Path(vpp).stat()
    row = dict(cwd=os.getcwd(), argv=argv, command=cmd, mode=mode, status=p.returncode,
               compiler=dict(path=str(Path(vpp).resolve()), size=binary.st_size, mtime_ns=binary.st_mtime_ns),
               probe=any('meson-private' in x or 'conftest' in x for x in argv),
               invalid_vir=bool(re.search(r'verifier failures|allocation check:', diagnostics)),
               seconds=time.monotonic()-start, diagnostics=str(path),
               built=re.findall(r'^vir: (.*?): built,', diagnostics, re.M),
               lowered=re.findall(r'^vir: (.*?): lowered,', diagnostics, re.M),
               fallback=dict(re.findall(r'^vir: (.*?): (?:not lowered: |no: )(.*)$', diagnostics, re.M)),
               vectorized=re.findall(r'^vec: (.*?): loop b\d+: vectorized$', diagnostics, re.M))
    with (directory / 'jobs.jsonl').open('a') as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        f.write(json.dumps(row) + '\n')
    return p.returncode if p.returncode >= 0 else 128-p.returncode

if __name__ == '__main__':
    raise SystemExit(main())
