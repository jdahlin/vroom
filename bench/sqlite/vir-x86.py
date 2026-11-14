#!/usr/bin/env python3
"""Short native x86 SQLite comparisons, with checked exits and verification hashes.

build --source DIR [--out DIR]: compile the amalgamation and speedtest1 at -O2,
using vpp -S then clang's assembler for fast backend iteration. Both binaries
use the same SQLite options. run NAME=EXE... [--runs 3] [--size 30]: alternate
short perf-stat runs of the main workload; retain raw output and summary.json.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'bench'))
from hostenv import BUILD, llvm


def checked(cmd, **kw):
    return subprocess.run(list(map(str, cmd)), check=True, timeout=15, **kw)


def build(a):
    a.out.mkdir(parents=True, exist_ok=True)
    clang = llvm('clang')
    resource = subprocess.check_output([clang, '-print-resource-dir'], text=True).strip()
    flags = ['--target=x86_64-linux-gnu', '-O2', '-DSQLITE_OMIT_LOAD_EXTENSION',
             '-DSQLITE_THREADSAFE=0', '-I' + str(a.source), '-isystem', resource + '/include',
             '-isystem', '/usr/include/x86_64-linux-gnu', '-isystem', '/usr/include']
    for variant in a.variants:
        objects = []
        for source in ('sqlite3', 'speedtest1'):
            stem = a.out / (variant + '-' + source)
            obj = stem.with_suffix('.o')
            start = time.monotonic()
            with stem.with_suffix('.log').open('w') as log:
                if variant == 'clang':
                    checked([clang, *flags, '-c', a.source / (source + '.c'), '-o', obj], stderr=log)
                else:
                    asm = stem.with_suffix('.s')
                    checked([a.bin_dir / 'vpp', *flags, '-fvir-verify', '-fvir-report', '-fvir-check',
                             '-S', a.source / (source + '.c'), '-o', asm], stderr=log)
                    checked([clang, '-c', asm, '-o', obj], stderr=log)
            objects.append(obj)
            print(f'{variant} {source}: {time.monotonic() - start:.3f} s', flush=True)
        checked([clang, *objects, '-o', a.out / ('sqlite-' + variant)])


def run(a):
    a.out.mkdir(parents=True, exist_ok=True)
    binaries = dict(item.split('=', 1) for item in a.binaries)
    if len(binaries) != len(a.binaries) or len(binaries) < 2:
        raise SystemExit('supply at least two distinct NAME=EXE pairs')
    rows = {}
    for name, binary in binaries.items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
            raise SystemExit('names must contain only letters, digits, underscores or hyphens')
        binary = str(Path(binary).resolve())
        binaries[name] = binary
        p = checked([binary, '--size', '10', '--memdb', '--testset', 'main', '--verify'],
                    capture_output=True, text=True)
        (a.out / (name + '-verify.txt')).write_text(p.stdout + p.stderr)
        hashes = re.findall(r'Verification Hash: (.*)', p.stdout)
        if len(hashes) != 1:
            raise SystemExit(f'{name}: missing verification hash')
        rows[name] = dict(binary=binary, sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
                          verification=hashes[0], samples=[])
    if len({r['verification'] for r in rows.values()}) != 1:
        raise SystemExit('verification hashes differ')
    names = list(binaries)
    for iteration in range(a.runs):
        # Rotate the first executable to reduce consistent order bias.
        for name in names[iteration % len(names):] + names[:iteration % len(names)]:
            stem = a.out / f'{name}-{iteration}'
            with stem.with_suffix('.stdout').open('w') as stdout, stem.with_suffix('.stderr').open('w') as stderr:
                checked(['perf', 'stat', '-x,', '-e', 'cycles:u,instructions:u,task-clock:u',
                         '-o', stem.with_suffix('.csv'), '--', binaries[name], '--size', str(a.size),
                         '--memdb', '--testset', 'main'], stdout=stdout, stderr=stderr)
            sample = {}
            for line in stem.with_suffix('.csv').read_text().splitlines():
                fields = line.split(',')
                if len(fields) >= 3 and fields[2] in ('cycles:u', 'instructions:u', 'task-clock:u'):
                    sample[fields[2]] = float(fields[0])
            if len(sample) != 3:
                raise SystemExit(f'{name}: incomplete perf counters')
            rows[name]['samples'].append(sample)
    reference = names[0]
    for row in rows.values():
        row['median'] = {k: statistics.median(s[k] for s in row['samples'])
                         for k in ('cycles:u', 'instructions:u', 'task-clock:u')}
    for name, row in rows.items():
        row['ratio'] = {k: row['median'][k] / rows[reference]['median'][k] for k in row['median']}
        print(f"{name}: {row['median']['task-clock:u']:.2f} ms CPU; "
              f"cycles {row['ratio']['cycles:u']:.3f}x, instructions {row['ratio']['instructions:u']:.3f}x, "
              f"CPU time {row['ratio']['task-clock:u']:.3f}x {reference}")
    (a.out / 'summary.json').write_text(json.dumps(dict(size=a.size, runs=a.runs, results=rows), indent=2) + '\n')


ap = argparse.ArgumentParser(description=__doc__)
sub = ap.add_subparsers(dest='command', required=True)
b = sub.add_parser('build')
b.add_argument('--variants', nargs='+', choices=('clang', 'vir'), default=['clang', 'vir'])
b.add_argument('--source', type=lambda p: Path(p).resolve(), required=True)
b.add_argument('--out', type=lambda p: Path(p).resolve(), default=ROOT / 'work/sqlite-x86-perf')
b.add_argument('--bin-dir', type=lambda p: Path(p).resolve(), default=Path(BUILD).resolve())
r = sub.add_parser('run')
r.add_argument('binaries', nargs='+')
r.add_argument('--out', type=lambda p: Path(p).resolve(), default=ROOT / 'work/sqlite-x86-perf/results')
r.add_argument('--size', type=int, choices=range(1, 101), default=30)
r.add_argument('--runs', type=int, choices=range(1, 8), default=3)
a = ap.parse_args()
(build if a.command == 'build' else run)(a)
