#!/usr/bin/env python3
"""vcc-dwarf.py: -g line tables (TODO.md DWARF).

  bench/vcc-dwarf.py [-j N] [-O LEVEL] [PAT]

For every C file in ccs/vcc/tests/vcc and tests/gcc-torture/execute
(matching PAT), compiled for aarch64 Linux:
  - vpp -g -c passes llvm-dwarfdump --verify;
  - -g does not change the code: .text is byte-identical with and without -g;
  - vpp -g -c equals clang -c of vpp -g -S (the line table from the text);
  - rows at lines clang -g has no row for are counted (-v lists them): clang
    -O0 drops an if whose condition folds, vcc -O0 keeps its code.
"""
import argparse, glob, os, re, subprocess, sys, tempfile
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
CL, DD, OC = llvm('clang'), llvm('llvm-dwarfdump'), llvm('llvm-objcopy')
RT = f'{R}/ccs/vcc/tests/rt/include'
FL = ['--target=aarch64-linux-gnu', '-w', '-ffreestanding', '-I', RT]

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)

def text(o, t):
    run([OC, '-O', 'binary', '--only-section=.text', o, t])
    return open(t, 'rb').read()

ROW = re.compile(r'^0x[0-9a-f]+\s+(\d+)\s+(\d+)\s+(\d+)\s')
def lines(o, entry=True):
    """(file name, line) of each row, names from the file table; entry=False
    drops column-0 rows (a function's first: clang has none without a frame)"""
    out = run([DD, '--debug-line', o], text=True).stdout
    names, cur, rows = {}, None, set()
    for l in out.splitlines():
        m = re.match(r'file_names\[\s*(\d+)\]:', l)
        if m: cur = int(m.group(1)); continue
        m = re.match(r'\s+name: "(.*)"', l)
        if m and cur is not None: names[cur] = os.path.basename(m.group(1)); continue
        m = ROW.match(l)
        if m and int(m.group(1)) and (entry or int(m.group(2))): rows.add((names.get(int(m.group(3))), int(m.group(1))))
    return rows

def one(job):
    f, opt = job
    with tempfile.TemporaryDirectory() as t:
        d = os.path.dirname(f); b = os.path.basename(f)
        a = FL + [opt, b]
        if run([CL] + a + ['-g', '-c', '-o', f'{t}/c.o'], cwd=d).returncode:
            return ('skip', f, '')
        if run([VPP] + a + ['-c', '-o', f'{t}/n.o'], cwd=d).returncode:
            return ('skip', f, '')
        p = run([VPP] + a + ['-g', '-c', '-o', f'{t}/g.o'], cwd=d, text=True)
        if p.returncode:
            return ('fail', f, 'vpp -g -c: ' + p.stderr[:200])
        v = run([DD, '--verify', f'{t}/g.o'], text=True)
        if v.returncode:
            return ('fail', f, 'dwarfdump --verify: ' + v.stdout[-300:])
        if text(f'{t}/n.o', f'{t}/n.t') != text(f'{t}/g.o', f'{t}/g.t'):
            return ('fail', f, '-g changes .text')
        if run([VPP] + a + ['-g', '-S', '-o', f'{t}/g.s'], cwd=d).returncode or \
           run([CL, '--target=aarch64-linux-gnu', '-c', f'{t}/g.s', '-o', f'{t}/s.o']).returncode:
            return ('fail', f, 'vpp -g -S does not assemble with clang')
        if open(f'{t}/s.o', 'rb').read() != open(f'{t}/g.o', 'rb').read():
            return ('fail', f, 'vpp -g -c differs from clang -c of vpp -g -S')
        extra = lines(f'{t}/g.o', False) - lines(f'{t}/c.o')
        if extra:	# clang -O0 drops an if whose condition folds, vcc keeps its code
            return ('ok', f, f'lines clang has no row for: {sorted(extra)[:5]}')
        return ('ok', f, '')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-O', dest='opt', default='0')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pat', nargs='?')
    a = ap.parse_args()
    files = sorted(glob.glob(f'{R}/ccs/vcc/tests/vcc/*.c') + glob.glob(f'{R}/ccs/vcc/tests/gcc-torture/execute/*.c'))
    if a.pat: files = [f for f in files if a.pat in f]
    with Pool(a.j) as p: res = p.map(one, [(f, '-O' + a.opt) for f in files], chunksize=4)
    n = {'ok': 0, 'fail': 0, 'skip': 0}
    note = 0
    for k, f, m in res:
        n[k] += 1
        if k == 'ok' and m:
            note += 1
            if a.v: print('NOTE', os.path.relpath(f, R), m)
        if k == 'fail': print('FAIL', os.path.relpath(f, R), m)
    print(f"vcc-dwarf: {n['ok']}/{n['ok'] + n['fail']} ok, {n['skip']} skipped, {note} with rows clang lacks (-v)")
    sys.exit(1 if n['fail'] else 0)

if __name__ == '__main__': main()
