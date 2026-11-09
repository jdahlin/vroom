#!/usr/bin/env python3
"""speed.py: compile speed on PostgreSQL's C files, clang against vcc.

  bench/pg/speed.py [-j N] [-r R] [MODE...]

Takes the C compile commands of work/build-pg-clang (compile_commands.json,
from bench/pg/build.sh clang), drops the depfile options, sends objects to a
scratch directory ($TMPDIR/pg-speed) and runs every file with -c in each mode:

  clang      the build's own flags (-O2 -g)
  clang-O0   -O0 -g0: no optimizer and no debug info, like vcc
  vpp        vcc, one process per file (as bench/pg/cc runs it, minus Python)
  vpp-batch  vcc, all files in one process on N threads (vpp --batch, as vccd)

Modes alternate across R rounds (default 2); each line is the best wall
time with its CPU (user+sys of the children).  Nothing in the build tree is
written.  Default -j 6 (the machine is shared).
"""
import json, os, resource, shlex, shutil, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W = os.path.realpath(f'{ROOT}/work')
B = f'{W}/build-pg-clang'
CLANG = os.environ.get('VCC_PG_CLANG', '/opt/homebrew/opt/llvm/bin/clang')
VPP = os.environ.get('VPP') or f'{ROOT}/build/ccs/vpp'
OUT = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'pg-speed')
MODES = ['clang', 'clang-O0', 'vpp', 'vpp-batch']


def jobs():
    """[(args without the compiler, object name)] for each .c compile"""
    r = []
    for d in json.load(open(f'{B}/compile_commands.json')):
        if not d['file'].endswith('.c'):
            continue
        a, out, i = shlex.split(d['command'])[1:], [], 0
        while i < len(a):
            if a[i] in ('-MQ', '-MF', '-MT', '-o'):
                i += 2
                continue
            if a[i] not in ('-MD', '-MMD'):
                out.append(a[i])
            i += 1
        r.append((out, d['output'].replace('/', '__')))
    return r


def cpu():
    u = resource.getrusage(resource.RUSAGE_CHILDREN)
    return u.ru_utime + u.ru_stime


def run(mode, js, j):
    d = f'{OUT}/{mode}'
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    c0, t0 = cpu(), time.time()
    if mode == 'vpp-batch':
        jf = f'{OUT}/jobs'
        with open(jf, 'w') as f:
            for a, o in js:
                f.write('\x1f'.join(['-'] + a + ['-o', f'{d}/{o}']) + '\n')
        p = subprocess.run([VPP, '--batch', jf, '-j', str(j), '-C', B], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        bad = None	# counted from the objects below
    else:
        def one(t):
            a, o = t
            if mode == 'clang':
                cmd = [CLANG] + a
            elif mode == 'clang-O0':
                cmd = [CLANG] + [x for x in a if x != '-g' and not x.startswith('-O')] + ['-O0', '-g0']
            else:
                cmd = [VPP] + a
            return subprocess.run(cmd + ['-o', f'{d}/{o}'], cwd=B, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode
        with ThreadPoolExecutor(j) as ex:
            bad = sum(1 for rc in ex.map(one, js) if rc)
    wall, c = time.time() - t0, cpu() - c0
    nobj = sum(1 for f in os.listdir(d) if f.endswith('.o'))
    size = sum(os.path.getsize(f'{d}/{f}') for f in os.listdir(d) if f.endswith('.o'))
    if bad is None:
        bad = len(js) - nobj
    return wall, c, bad, nobj, size


def main():
    av = sys.argv[1:]
    j, rounds = 6, 2
    for opt in ('-j', '-r'):
        if opt in av:
            k = av.index(opt)
            v = int(av[k + 1])
            del av[k:k + 2]
            j, rounds = (v, rounds) if opt == '-j' else (j, v)
    modes = av or MODES
    js = jobs()
    print(f'{len(js)} C files, -j {j}, {rounds} rounds (best wall shown)')
    best = {}
    for r in range(rounds):
        for m in (modes if r % 2 == 0 else modes[::-1]):
            res = run(m, js, j)
            print(f'  round {r + 1} {m:10s} {res[0]:6.1f} s wall {res[1]:7.1f} s CPU', flush=True)
            if m not in best or res[0] < best[m][0]:
                best[m] = res
    ref = best.get('clang')
    print(f'\n{"mode":10s} {"wall":>7s} {"CPU":>8s} {"files/s":>8s} {"vs clang":>9s} {"objects":>8s} {"MB":>6s}  failed')
    for m in modes:
        w, c, bad, n, size = best[m]
        rel = f'{ref[0] / w:.2f}x' if ref else ''
        print(f'{m:10s} {w:6.1f}s {c:7.1f}s {len(js) / w:8.0f} {rel:>9s} {n:8d} {size / 1e6:6.1f}  {bad}')


if __name__ == '__main__':
    main()
