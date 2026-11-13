#!/usr/bin/env python3
"""Measure small SQLite routines using Clang bodies in a VIR assembly file.

Build a padded VIR control, one substitution per function, and all substitutions.
The fixed text slots keep other text symbols at identical addresses. Only routines
without private string/data references are accepted; local code/jump-table labels
are renamed and jump tables stay in text. Use vir-x86.py run to time the binaries.
These are diagnostic hybrids, never compiler benchmark results.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'bench'))
from hostenv import llvm

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--vir', type=Path, required=True)
ap.add_argument('--clang', type=Path, required=True)
ap.add_argument('--driver-object', type=Path, required=True)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--slot', type=int, default=2048)
ap.add_argument('functions', nargs='+')
a = ap.parse_args()
if a.slot <= 0 or a.slot % 64 or len(set(a.functions)) != len(a.functions):
    ap.error('slots must be positive multiples of 64; functions must be distinct')
if any(n in ('control', 'all') for n in a.functions):
    ap.error('control and all are reserved variant names')
if not all(re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', n) for n in a.functions):
    ap.error('expected plain C function names')
a.out.mkdir(parents=True, exist_ok=True)
vir, clang = a.vir.read_text(), a.clang.read_text()
original, replacement = {}, {}
for name in a.functions:
    pattern = re.compile(r'^' + name + r':\n.*?^\s*\.size\t' + name + r',[^\n]+', re.M | re.S)
    m = pattern.search(vir)
    if not m:
        raise SystemExit(f'{name}: VIR definition missing')
    original[name] = (pattern, m[0])
    m = re.search(r'^' + name + r':.*?^\s*# -- End function', clang, re.M | re.S)
    if not m or '.cfi_endproc' not in m[0]:
        raise SystemExit(f'{name}: complete Clang definition missing')
    body = m[0]
    defined = set(re.findall(r'^(\.L[\w.]+):', body, re.M))
    referenced = set(re.findall(r'\.L[\w.]+', body))
    if referenced - defined:
        raise SystemExit(f'{name}: outside local references: {sorted(referenced - defined)}')
    body = re.sub(r'\.L([\w.]+)', lambda m: '.Lswap_' + name + '_' + m[1], body)
    body = re.sub(r'^\s*\.section\s+\.rodata[^\n]*', '\n\t.text', body, flags=re.M)
    replacement[name] = body


def checked(cmd):
    p = subprocess.run(list(map(str, cmd)), capture_output=True, text=True, timeout=15)
    if p.returncode:
        raise SystemExit(f'failed ({p.returncode}): {" ".join(map(str, cmd))}\n{p.stdout}{p.stderr}')
    return p


def text_symbols(binary):
    result = {}
    for line in checked([llvm('llvm-nm'), '--defined-only', '--format=posix', binary]).stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[1] in ('t', 'T'):
            result[fields[0]] = fields[2]
    return result


control = None
for tag, names in [('control', [])] + [(n, [n]) for n in a.functions] + [('all', a.functions)]:
    text = vir
    for name in a.functions:
        pattern, old = original[name]
        body = replacement[name] if name in names else old
        body += f'\n\t.text\n\t.org {name}+{a.slot}, 0x90\n'
        text = pattern.sub(lambda _: body, text, count=1)
    stem = a.out / tag
    stem.with_suffix('.s').write_text(text)
    checked([llvm('clang'), '-c', stem.with_suffix('.s'), '-o', stem.with_suffix('.o')])
    checked([llvm('clang'), stem.with_suffix('.o'), a.driver_object, '-o', stem])
    symbols = text_symbols(stem)
    if control is None:
        control = symbols
    elif symbols != control:
        changed = sorted(k for k in symbols.keys() | control.keys() if symbols.get(k) != control.get(k))
        raise SystemExit(f'{tag}: text symbol addresses differ: {changed[:10]}')
    print(f'{tag}: {len(symbols)} text symbol addresses checked', flush=True)
