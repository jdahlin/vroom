#!/usr/bin/env python3
"""Report frontend builtin recognition and __has_builtin coverage.

Names come from Clang's installed builtin tables, or its resource headers
when development tables are absent. Use --prefix __builtin_ia32_ for X9.
Recognition (including an argument diagnostic) does not prove code generation;
run the target's runtime/encoding harnesses to validate implemented semantics.
Missing names are reported; false-positive __has_builtin answers fail the run.
Compiler failures or an empty/incomplete inventory are errors, never zero gaps.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
NAME = r'(?:__builtin_\w+|__sync_\w+|__atomic_\w+|__c11_atomic_\w+)'


def invoke(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if p.returncode < 0 or p.returncode > 1:
        raise RuntimeError(f'compiler exited {p.returncode}: {shlex.join(cmd)}\n{p.stderr}')
    return p


def inventory(clang, include_dir, prefix):
    inc = Path(include_dir) if include_dir else None
    if not inc:
        p = invoke([clang, '-print-resource-dir'])
        if p.returncode:
            raise RuntimeError(p.stderr)
        resource = Path(p.stdout.strip())
        candidates = [resource.parent.parent.parent / 'include/clang/Basic',
                      Path(clang).resolve().parent.parent / 'include/clang/Basic']
        inc = next((x for x in candidates if (x / 'Builtins.inc').exists()), resource / 'include')
    tables = sorted(inc.glob('Builtins*.inc'))
    # Architecture wrapper subdirectories contain foreign-target examples.
    files = tables or sorted(inc.glob('*.h'))
    names = set()
    for path in files:
        text = path.read_text(errors='replace')
        pattern = rf'\bBI({NAME})\b' if tables else rf'\b({NAME})\b'
        names.update(re.findall(pattern, text))
    if prefix:
        names = {n for n in names if n.startswith(prefix) and len(n) > len(prefix)}
    if not names:
        raise RuntimeError(f'no builtin names found in {inc} (prefix {prefix!r})')
    return sorted(names), str(inc), 'builtin-tables' if tables else 'resource-headers'


def has_builtin(compiler, flags, names, wd):
    source = wd / 'has.c'
    source.write_text(''.join(f'{n} __has_builtin({n})\n' for n in names))
    p = invoke([compiler, *flags, '-E', '-P', '-x', 'c', str(source)])
    if p.returncode:
        raise RuntimeError(f'__has_builtin preprocessing failed: {p.stderr}')
    answers = {}
    wanted = set(names)
    for line in p.stdout.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] in wanted and fields[1] in ('0', '1'):
            answers[fields[0]] = fields[1] == '1'
    if len(answers) != len(names):
        raise RuntimeError(f'incomplete __has_builtin output: {len(answers)}/{len(names)}')
    return answers


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('--target', default='aarch64-linux-gnu')
    p.add_argument('--clang', default=os.environ.get('CLANG') or shutil.which('clang') or
                   '/opt/homebrew/opt/llvm/bin/clang')
    p.add_argument('--bin-dir', type=Path, default=Path(os.environ.get('VCC_BIN', ROOT / 'build/ccs')))
    p.add_argument('--include-dir', help='Clang builtin-table or resource-header directory')
    p.add_argument('--prefix', default='', help='limit names, e.g. __builtin_ia32_')
    p.add_argument('--cflag', action='append', default=[], help='extra flag for both compilers; --cflag=-mavx2')
    p.add_argument('--json', type=Path, help='write inventory, commands, and separate coverage lists')
    args = p.parse_args()
    names, inc, source = inventory(args.clang, args.include_dir, args.prefix)
    flags = [f'--target={args.target}', *args.cflag]
    vpp = str(args.bin_dir.resolve() / 'vpp')
    with tempfile.TemporaryDirectory(prefix='vcc-builtins-') as scratch:
        wd = Path(scratch)
        cl = has_builtin(args.clang, flags, names, wd)
        us = has_builtin(vpp, flags, names, wd)
        unknown = set()
        # Include names disabled by Clang's current ISA flags: recognition and
        # feature availability are separate properties of a target builtin.
        for i in range(0, len(names), 200):
            chunk = names[i:i + 200]
            src = wd / 'calls.c'
            src.write_text(''.join(f'void f{j}(void) {{ {n}(0); }}\n' for j, n in enumerate(chunk)))
            r = invoke([vpp, *flags, '-fsyntax-only', '-ferror-limit=0', '-w', str(src)])
            for n in chunk:
                if any(message in r.stderr for message in
                       (f"undeclared function '{n}'", f"undeclared identifier '{n}'", f"unknown builtin '{n}'")):
                    unknown.add(n)
            if re.search(r'\b(?:fatal error|internal compiler error):', r.stderr):
                raise RuntimeError(f'incomplete recognition probe: {r.stderr}')
    enabled = [n for n in names if cl[n]]
    missing = [n for n in enabled if not us[n]]
    extra = [n for n in names if us[n] and not cl[n]]
    report = dict(target=args.target, flags=flags, clang=args.clang, vpp=vpp,
                  inventory_source=source, inventory_directory=inc, inventory=names,
                  recognized=[n for n in names if n not in unknown], unknown=sorted(unknown),
                  clang_enabled=enabled, has_builtin_missing=missing, has_builtin_extra=extra,
                  scope='frontend recognition and feature queries; not codegen or runtime validation')
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + '\n')
    print(f'builtins: {len(names) - len(unknown)}/{len(names)} names recognized by vcc for {args.target}; '
          f'{len(enabled)} enabled by clang with selected flags; '
          f'{len(missing)} missing __has_builtin answers; {len(extra)} extra answers')
    if args.verbose:
        for label, rows in [('unknown', sorted(unknown)), ('has_builtin 0', missing), ('has_builtin extra', extra)]:
            print(label + ':', ' '.join(rows))
    return bool(extra)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'vcc-builtins: error: {error}', file=sys.stderr)
        sys.exit(2)
