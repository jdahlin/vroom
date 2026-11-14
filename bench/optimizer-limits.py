#!/usr/bin/env python3
"""Inventory exposed GCC parameters and LLVM numeric-option candidates.

This captures reported values, not effective per-function optimizer policy.
LLVM targets and pass constructors may override defaults; a pass may be off.
Boolean options, general enums and unexposed source constants are not enumerated
on the LLVM side. Numeric lists and ElementCount options are included explicitly.
No compilation, benchmark or network access is performed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from hostenv import llvm

LEVELS = ('O0', 'O1', 'O2', 'O3', 'Os')
NUMERIC_TYPES = 'uint|int|number|uint64|int64|double|float|ulong|long|N|x|seed|ElementCount'


def run(*args):
    p = subprocess.run(args, check=True, capture_output=True, text=True,
                       timeout=30, env=dict(os.environ, LC_ALL='C'))
    return p.stdout + p.stderr


def collect(gcc, opt):
    raw = {'gcc-descriptions': run(gcc, '--help=params'),
           'llvm-help': run(opt, '--help-hidden'),
           'llvm-values': run(opt, '--print-all-options', '--disable-output', os.devnull)}
    for level in LEVELS:
        raw['gcc-' + level] = run(gcc, '-' + level, '-Q', '--help=params')
    descriptions = {}
    name = None
    for line in raw['gcc-descriptions'].splitlines():
        m = re.match(r'\s*--param=([^=]+)=\S*\s+(.*)', line)
        if m:
            name = m[1]
            descriptions[name] = m[2].strip()
        elif name and line.strip():
            descriptions[name] += ' ' + line.strip()
    gcc_rows = {}
    for level in LEVELS:
        for line in raw['gcc-' + level].splitlines():
            m = re.match(r'\s*--param=([^=]+)=\S*\s+(.*)', line)
            if not m:
                continue
            row = gcc_rows.setdefault(m[1], dict(
                compiler='gcc', name=m[1], description=descriptions[m[1]], values={}))
            row['values'][level] = m[2].strip()
    values = {}
    for line in raw['llvm-values'].splitlines():
        m = re.match(r'\s*--([^\s=]+)\s*=\s*(.*?)\s+\(default: (.*?)\)\s*$', line)
        if m:
            values[m[1]] = dict(parsed=m[2], registered_default=m[3])
    llvm_rows = []
    pattern = re.compile(r'\s*--([^=\s]+)=<(' + NUMERIC_TYPES + r')>\s+- (.*)')
    for line in raw['llvm-help'].splitlines():
        m = pattern.match(line)
        if m:
            llvm_rows.append(dict(compiler='llvm', name=m[1], type=m[2],
                                  description=m[3], values=values.get(m[1], {})))
    rows = sorted([*gcc_rows.values(), *llvm_rows], key=lambda r: (r['compiler'], r['name']))
    if not gcc_rows or not llvm_rows:
        raise RuntimeError('empty inventory: compiler help format not recognized')
    if len({(r['compiler'], r['name']) for r in rows}) != len(rows):
        raise RuntimeError('duplicate option names')
    if any(set(r['values']) != set(LEVELS) for r in gcc_rows.values()):
        raise RuntimeError('GCC parameter set differs across optimization levels')
    metadata = dict(gcc_version=run(gcc, '--version').splitlines()[0],
                    gcc_target=run(gcc, '-dumpmachine').strip(),
                    llvm_version=run(opt, '--version').strip(),
                    counts=dict(gcc=len(gcc_rows), llvm=len(llvm_rows)),
                    missing_llvm_values=[r['name'] for r in llvm_rows if not r['values']],
                    scope=__doc__.strip())
    return rows, metadata, raw


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gcc', default='gcc')
    ap.add_argument('--opt', default=llvm('opt'))
    ap.add_argument('--out', type=Path, default=Path('work/optimizer-limits-capture'))
    a = ap.parse_args()
    rows, metadata, raw = collect(a.gcc, a.opt)
    a.out.mkdir(parents=True, exist_ok=True)
    for name, text in raw.items():
        (a.out / (name + '.txt')).write_text(text)
    for name, value in [('inventory', rows), ('metadata', metadata)]:
        (a.out / (name + '.json')).write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(metadata['counts']))
    if metadata['missing_llvm_values']:
        print('No printed value (not assumed zero): ' + ', '.join(metadata['missing_llvm_values']))


if __name__ == '__main__':
    main()
