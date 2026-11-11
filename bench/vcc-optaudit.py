#!/usr/bin/env python3
"""Developer audit of x86 O0..O3, not a performance or CI regression gate.

Requires native x86-64 Linux, vpp, Clang, GCC and GNU binutils. Captures
assembly, disassembly, symbol sizes, GCC options/passes, LLVM pass execution,
and optimization remarks. Executes tiny correctness probes, never a timed
benchmark. Counts describe these examples, not compiler-wide capability.

  PATH=/usr/lib/llvm-23/bin:$PATH python3 bench/vcc-optaudit.py
  ... --native   # add host-native results; keep baseline results as well
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

from hostenv import BUILD, R, llvm


def run(cmd):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=60)
    if p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {cmd}\n{p.stderr}")
    return p


def metrics(obj):
    sizes = {}
    for line in run(['nm', '-S', '--defined-only', obj]).stdout.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3].startswith('oa_'):
            sizes[fields[3]] = int(fields[1], 16)
    dis = run(['objdump', '-d', '--no-show-raw-insn', obj]).stdout
    functions = {}
    for match in re.finditer(r'^([0-9a-f]+) <(oa_\w+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)', dis, re.M | re.S):
        start, name, body = int(match[1], 16), match[2], match[3]
        # Symbol size excludes following alignment padding and table data.
        ins = [m[2].strip() for m in re.finditer(r'^\s*([0-9a-f]+):\s+(.+)$', body, re.M)
               if int(m[1], 16) < start + sizes[name]]
        functions[name] = {
            'bytes': sizes[name], 'instructions': len(ins),
            'stack_refs': sum(bool(re.search(r'\(%r(?:bp|sp)', s)) for s in ins),
            'calls': sum(s.startswith('call') for s in ins),
            'branches': sum(s.startswith('j') for s in ins),
            'divides': sum(bool(re.match(r'i?div\b', s)) for s in ins),
            'instructions_text': ins,
        }
    return functions, dis


def check_controls(functions, name):
    # Existing behaviours to preserve; missing optimizations are audit findings,
    # not silently accepted XFAILs. A volatile memory operand may be folded into
    # an add, so count accesses, not just instructions named "mov".
    ins = functions['oa_volatile']['instructions_text']
    reads = [s for s in ins if '(' in s and not s.startswith('lea')
             and not re.search(r'%r(?:sp|bp)', s)]
    if len(reads) != 2:
        raise RuntimeError(f'{name}: expected two volatile input reads, got {reads}')
    if functions['oa_always']['calls']:
        raise RuntimeError(f'{name}: always_inline probe retained a call')
    if any(re.match(r'(?:i?mul|add|xor|sh[lr])\b', s)
           for s in functions['oa_fold']['instructions_text']):
        raise RuntimeError(f'{name}: literal constant expression was not folded')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--vpp', default=os.path.join(BUILD, 'vpp'))
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--gcc', default='gcc')
    ap.add_argument('--out', type=Path, default=Path(BUILD).parent / 'optaudit-levels')
    args = ap.parse_args()
    if platform.machine() != 'x86_64' or platform.system() != 'Linux':
        ap.error('requires native x86-64 Linux')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = Path(R) / 'bench/optaudit/probe.c'
    driver = Path(R) / 'bench/optaudit/check.c'
    run([args.clang, '-O2', '-c', driver, '-o', out / 'check.o'])
    cpu = Path('/proc/cpuinfo').read_text()
    report = {
        'revision': run(['git', '-C', R, 'rev-parse', 'HEAD']).stdout.strip(),
        'cpu': re.search(r'model name\s*:\s*(.+)', cpu)[1],
        'versions': {k: run([v, '--version']).stdout.splitlines()[0]
                     for k, v in [('clang', args.clang), ('gcc', args.gcc)]},
        'vpp': str(Path(args.vpp).resolve()), 'results': {},
        'vpp_sha256': hashlib.sha256(Path(args.vpp).read_bytes()).hexdigest(),
        'probe_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'caller_sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
        'note': 'Observed examples, not complete pass coverage or performance measurements.',
    }
    expected = None
    for arch in ['x86-64'] + (['native'] if args.native else []):
        for level in range(4):
            for compiler, executable in [('vcc', args.vpp), ('clang', args.clang), ('gcc', args.gcc)]:
                name = f'{compiler}-O{level}-{arch}'
                base = out / name
                # Distribution GCC enables CET and stack protection by default.
                # Normalize these so the audit measures optimization differences.
                flags = [f'-O{level}', f'-march={arch}', '-fno-pie',
                         '-fno-stack-protector', '-fcf-protection=none']
                cmd = [executable, *flags, '-S', source, '-o', base.with_suffix('.s')]
                if compiler == 'vcc':
                    cmd += ['--target=x86_64-linux-gnu']
                elif compiler == 'clang':
                    cmd += ['-Xclang', '-fdebug-pass-manager', '-Rpass=.*', '-Rpass-missed=.*', '-Rpass-analysis=.*']
                else:
                    base.with_suffix('.remarks').write_text('')
                    cmd += ['-fdump-passes', f'-fopt-info-all={base}.remarks']
                    opts = run([executable, *flags, '-Q', '--help=optimizers']).stdout
                    base.with_suffix('.options').write_text(opts)
                p = run(cmd)
                base.with_suffix('.passes').write_text(p.stderr)
                run([args.clang, '-c', base.with_suffix('.s'), '-o', base.with_suffix('.o')])
                functions, dis = metrics(base.with_suffix('.o'))
                if len(functions) != 29:
                    raise RuntimeError(f'{name}: expected 29 functions, got {len(functions)}')
                check_controls(functions, name)
                base.with_suffix('.dis').write_text(dis)
                run([args.clang, '-no-pie', out / 'check.o', base.with_suffix('.o'), '-o', base])
                checksum = run([base]).stdout.strip()
                if expected is None:
                    expected = checksum
                if checksum != expected:
                    raise RuntimeError(f'{name}: checksum {checksum} != {expected}')
                inventory = {}
                if compiler == 'clang':
                    inventory['observed_passes'] = sorted(set(re.findall(r'^Running pass: (\S+) on ', p.stderr, re.M)))
                elif compiler == 'gcc':
                    inventory['enabled_options'] = re.findall(r'^\s*(-\S+)\s+\[enabled\]', opts, re.M)
                    inventory['configured_passes_on'] = re.findall(r'^\s*(\S+)\s+:\s+ON\s*$', p.stderr, re.M)
                report['results'][name] = {'command': [str(x) for x in cmd], 'checksum': checksum,
                                           'inventory': inventory, 'functions': functions}
                print(f'{name:22} {sum(v["bytes"] for v in functions.values()):5} bytes  checksum {checksum}')
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Artifacts: {out}')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
