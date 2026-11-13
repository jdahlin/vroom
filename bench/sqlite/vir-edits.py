#!/usr/bin/env python3
"""Count x86 VIR allocator sites with alignment-controlled assembly NOP probes.

Input: sqlite3 assembly built with VCC_VL_MAP=1 VCC_VL_FILTER=sqlite3VdbeExec,
its speedtest1 object, and the compiler's stderr (--decisions). No timing claims
are made for these binaries. Runtime comparisons belong in vir-x86.py using
unmodified executables. Counters exclude out-of-line callees.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

KINDS = ['reload', 'remat', 'move', 'store', 'edge-remat', 'edge-store',
         'edge-reload', 'edge-move', 'definition-store', 'lowering', 'edge-exchange']


def sites(text):
    stack, rows, owners = [], [], {}
    for n, line in enumerate(text.splitlines(keepends=True)):
        if '# vl-site ' in line:
            row = dict(re.findall(r'(\w+)=([^\s]+)', line))
            row = {k: (v if k in ('op', 'ty') else int(v)) for k, v in row.items()}
            row.update(start=n, instructions=[])
            rows.append(row)
            stack.append(row)
        elif '# vl-end' in line:
            if not stack:
                raise ValueError('unmatched vl-end')
            stack.pop()['end'] = n
        elif stack and re.match(r'^\s+[a-z][a-z0-9]*\s', line):
            stack[-1]['instructions'].append(n)
            owners[n] = stack[-1]
    if stack or not rows:
        raise ValueError('missing or unbalanced allocator mapping')
    if len({r['id'] for r in rows}) != len(rows):
        raise ValueError('map exactly one function with VCC_VL_FILTER')
    return rows, owners


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--asm', type=Path, required=True)
    p.add_argument('--speedtest-object', type=Path, required=True)
    p.add_argument('--decisions', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--clang', default='/usr/lib/llvm-23/bin/clang')
    p.add_argument('--cpu', default='7')
    p.add_argument('--size', default='100')
    p.add_argument('--stack-offset', type=int, default=776,
                   help='candidate aMem RSP offset from the measured assembly')
    p.add_argument('--target', type=int, default=17, help='dispatch block')
    p.add_argument('--runs', type=int, default=2)
    p.add_argument('--site', type=int, action='append', default=[],
                   help='also count one emitted edit site (repeatable)')
    p.add_argument('--value', type=int, action='append', default=[],
                   help='also count edits of an internal VIR value (repeatable)')
    p.add_argument('--only', help='measure only counter group names matching this regular expression')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    text = a.asm.read_text()
    rows, owners = sites(text)
    lines = text.splitlines(keepends=True)
    groups = {k: [] for k in KINDS if k != 'lowering'}
    for r in rows:
        if r['bit'] != 9:
            groups[KINDS[r['bit']]].append(r['end'])
            if r['to'] != 4294967295:
                direction = 'earlier' if r['b'] < r['to'] else 'later'
                groups.setdefault(direction + '-' + KINDS[r['bit']], []).append(r['end'])
            if r['id'] in a.site:
                groups.setdefault('site-' + str(r['id']), []).append(r['end'])
            if r['v'] in a.value:
                groups.setdefault('value-' + str(r['v']), []).append(r['end'])
                groups.setdefault('value-' + str(r['v']) + '-' + KINDS[r['bit']], []).append(r['end'])
    if any('site-' + str(s) not in groups for s in a.site):
        raise ValueError('--site must name an emitted allocator edit, not ordinary lowering')
    for n, r in owners.items():
        line = lines[n].strip()
        kind = KINDS[r['bit']]
        if re.match(r'mov[lq]\s+\$0,', line):
            groups.setdefault('zero-' + kind, []).append(n)
            if r['bit'] == 4:
                key = 'zero-edge-to-dispatch' if r['to'] == a.target else 'zero-edge-other'
                groups.setdefault(key, []).append(n)
                if r['to'] == a.target:
                    decision = ('param-' + str(r['param']) if r['param'] else 'live-v' + str(r['v']))
                    groups.setdefault('zero-dispatch-' + decision, []).append(n)
        if re.match(r'mov\w*\s+' + str(a.stack_offset) + r'\(%rsp\),', line):
            groups.setdefault('amem-' + kind, []).append(n)
    groups = {k: sorted(set(v)) for k, v in groups.items() if v}
    if a.only:
        groups = {k: v for k, v in groups.items() if re.fullmatch(a.only, k)}
        if not groups:
            raise ValueError('--only matched no counter groups')
    # Site IDs distinguish emitted layout duplicates; pos and b link back to
    # the allocation. Decisions are saved verbatim with the final slot map.
    (a.out / 'sites.json').write_text(json.dumps(rows, indent=2) + '\n')
    at = dict(owners)
    at.update((r['end'], r) for r in rows)
    membership = {k: sorted({at[n]['id'] for n in points}) for k, points in groups.items()}
    (a.out / 'groups.json').write_text(json.dumps(membership, indent=2) + '\n')
    (a.out / 'decisions.txt').write_text(a.decisions.read_text())
    result = dict(size=int(a.size), cpu=a.cpu, scope='mapped function, exclusive of callees',
                  static={k: len(v) for k, v in groups.items()}, samples={}, dynamic={})
    expected = None

    def run(cmd, **kw):
        return subprocess.run(list(map(str, cmd)), check=True, timeout=30, **kw)

    def binary(key):
        chosen = set(groups.get(key, []))
        # Both controls and probes remove all internal alignment directives in
        # the mapped function. Alignments outside it are identical in both.
        output = []
        # Function has mapping only inside it; use .size to bound trailing
        # edge stubs/epilogue, and retain the function entry alignment.
        start = min(r['start'] for r in rows)
        stop = next(n for n in range(max(r['end'] for r in rows), len(lines))
                    if re.match(r'\s*\.size\s', lines[n]))
        for n, line in enumerate(lines):
            if start <= n <= stop and re.match(r'\s*\.(?:p2align|balign|align)\b', line):
                continue
            if n in chosen:
                output.append('\tnop\n')
            output.append(line)
        stem = a.out / key
        stem.with_suffix('.s').write_text(''.join(output))
        run([a.clang, '-c', stem.with_suffix('.s'), '-o', stem.with_suffix('.o')], capture_output=True)
        run([a.clang, stem.with_suffix('.o'), a.speedtest_object, '-o', stem], capture_output=True)
        return stem

    binaries = {k: binary(k) for k in ['control', *groups, 'control-repeat']}
    for key, exe in binaries.items():
        check = run([exe, '--size', '10', '--memdb', '--testset', 'main', '--verify'],
                    capture_output=True, text=True)
        h = re.search(r'Verification Hash: (.*)', check.stdout)[1]
        if expected is None:
            expected = h
        if h != expected:
            raise RuntimeError(f'{key}: verification mismatch')
        result['samples'][key] = []
    # Repeat each full series, including bracketing controls. Keep raw counts;
    # the control drift states the measurement's practical precision.
    for iteration in range(a.runs):
        for key, exe in binaries.items():
            raw = a.out / f'{key}-{iteration}.csv'
            run(['taskset', '-c', a.cpu, 'perf', 'stat', '-x,', '-e', 'instructions:u',
                 '-o', raw, '--', exe, '--size', a.size, '--memdb', '--testset', 'main'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            count = int(next(l.split(',')[0] for l in raw.read_text().splitlines()
                             if ',instructions:u,' in l))
            result['samples'][key].append(count)
            if key in groups:
                result['dynamic'][key] = [v - c for v, c in zip(
                    result['samples'][key], result['samples']['control'])]
            result['verification'] = expected
            (a.out / 'counts.json').write_text(json.dumps(result, indent=2) + '\n')
            print(iteration, key, count if key not in groups else result['dynamic'][key][-1], flush=True)


if __name__ == '__main__':
    main()
