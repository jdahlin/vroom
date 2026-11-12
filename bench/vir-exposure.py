#!/usr/bin/env python3
"""Exercise VIR slot exposure through long, reverse-ordered CFG joins."""
import argparse
import random
from pathlib import Path
import subprocess
import tempfile


def graph(n, *, escapes, declared):
    parts = ['vir 1\ntarget x86_64-linux\nsig %s = (int) -> int\n'
             'func @f %s exposure {\n  slot $0 size 8 align 8' +
             (' exposed' if declared else '') +
             '\nb0:\n  v0:i64 = param 0.0\n  v1:ptr = i2p v0\n']
    cases = ', '.join(f'{i-2}: b{i}' for i in range(2, n + 1))
    parts.append(f'  switch v0, b1 [{cases}]\n')
    for i in range(1, n + 1):
        parts.append(f'b{i}:\n  br b{n+i}({"$0" if i == n else "v1"})\n')
    for i in range(1, n + 1):
        parts.append(f'b{n+i}(v{i+1}:ptr):\n')
        if i == 1:
            parts.append(('  store.8 v1, v2\n' if escapes else f'  v{n+2}:i64 = load.8 v2\n') + '  ret v0\n')
        else:
            parts.append(f'  br b{n+i-1}(v{i+1})\n')
    return ''.join(parts) + '}\n'


def cyclic(*, escapes, declared):
    return ("vir 1\ntarget x86_64-linux\nsig %s = (int) -> int\n"
            "func @f %s exposure {\n  slot $0 size 8 align 8" +
            (" exposed" if declared else "") +
            "\nb0:\n  v0:i64 = param 0.0\n  v1:ptr = i2p v0\n  br b1($0)\n"
            "b1(v2:ptr) loop:\n  v3:ptr = ptradd v2, 4\n"
            "  v4:ptr = select v0, v3, v1\n  brif v0, b2, b3\n"
            "b2:\n  br b1(v4)\nb3:\n" +
            ("  store.8 v1, v2\n" if escapes else "  v5:i64 = load.8 v2\n") +
            "  ret v0\n}\n")



def random_graph(edges, start, declared):
    n = len(edges)
    parts = ['vir 1\ntarget x86_64-linux\nsig %s = (int) -> int\n'
             'func @f %s exposure {\n  slot $0 size 8 align 8' +
             (' exposed' if declared else '') +
             '\nb0:\n  v0:i64 = param 0.0\n  v1:ptr = i2p v0\n']
    cases = ', '.join(f'{i-2}: b{i}' for i in range(2, n + 1))
    parts.append(f'  switch v0, b1 [{cases}]\n')
    for i in range(n):
        parts.append(f'b{i+1}:\n  br b{n+i+1}({"$0" if i == start else "v1"})\n')
    for i, successor in enumerate(edges):
        parts.append(f'b{n+i+1}(v{i+2}:ptr):\n')
        parts.append('  store.8 v1, v%d\n  ret v0\n' % (i+2) if successor < 0 else
                     f'  br b{n+successor+1}(v{i+2})\n')
    return ''.join(parts) + '}\n'


def escapes_from(edges, start):
    seen = set()
    while start >= 0 and start not in seen:
        seen.add(start)
        start = edges[start]
    return start < 0


def multiple_slots(claims):
    slots = ''.join(f'  slot ${i} size 16 align 8' + (' exposed' if claim else '') + '\n'
                    for i, claim in enumerate(claims))
    return ('vir 1\ntarget x86_64-linux\nsig %s = (int) -> int\n'
            'func @f %s exposure {\n' + slots +
            'b0:\n  v0:i64 = param 0.0\n  v1:ptr = i2p v0\n'
            '  v2:i64 = load.8 $0\n  v3:i64 = load.8 $2\n'
            '  v4:ptr = ptradd $0+4, 1\n  v5:ptr = select v0, v4, $1\n'
            '  store.8 v1, v5\n  ret v0\n}\n')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bin-dir', type=Path, default=Path(__file__).resolve().parent.parent / 'build/ccs')
    args = ap.parse_args()
    exe = (args.bin_dir / 'virtest').resolve()
    cases = [(5, True, False), (1003, True, False), (1003, True, True),
             (1003, False, False), (1003, False, True)]
    generated = [(f'links={n}', graph(n, escapes=escapes, declared=declared), escapes != declared)
                 for n, escapes, declared in cases]
    generated += [(f'cycle escapes={e} declared={d}', cyclic(escapes=e, declared=d), e != d)
                  for e in (False, True) for d in (False, True)]
    for declared in (False, True):
        source = ('vir 1\ntarget x86_64-linux\nsig %s = (int) -> int\n'
                  'func @f %s exposure {\n  slot $0 size 8 align 8' +
                  (' exposed' if declared else '') +
                  '\nb0:\n  v0:i64 = param 0.0\n  ret v0\n}\n')
        generated.append((f'no slot address declared={declared}', source, declared))
    for claims in [(True, True, False), (False, True, False), (True, False, False),
                   (True, True, True), (False, False, False)]:
        generated.append((f'multiple slots {claims}', multiple_slots(claims), claims != (True, True, False)))
    rng = random.Random(42)
    for case in range(20):
        edges = [rng.randrange(-1, 28) for _ in range(28)]
        start = rng.randrange(28)
        if case % 2 == 0:
            edges[start] = -1
        escapes = escapes_from(edges, start)
        for declared in (False, True):
            generated.append((f'random={case} declared={declared}',
                              random_graph(edges, start, declared), escapes != declared))
    with tempfile.TemporaryDirectory(prefix='vir-exposure-') as tmp:
        for name, source, invalid in generated:
            path = Path(tmp) / 'case.vir'
            path.write_text(source)
            p = subprocess.run([str(exe), str(path)], capture_output=True, text=True, timeout=30)
            if (p.returncode != (1 if invalid else 0) or
                    (invalid and 'M1:' not in p.stdout + p.stderr)):
                raise SystemExit(f'{name}: '
                                 f'exit {p.returncode}\n{(p.stdout + p.stderr)[:1000]}')
    print(f'vir-exposure: {len(generated)}/{len(generated)} passed')


if __name__ == '__main__':
    main()
