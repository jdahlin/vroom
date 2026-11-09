#!/usr/bin/env python3
"""vas-corpus.py: vasm on compiler-generated assembly (TODO T7).

  bench/vas-corpus.py [-j N] [-O LEVEL...] [--limit N] [PAT]

Every C file under ccs/vcc/tests (or those matching PAT) is compiled by
clang --target=aarch64-linux-gnu -S at each -O level (default -O0 -O2);
clang -c and vasm assemble the .s and the objects must be byte-identical.
Prints the failing files and a count of the mnemonics/directives vasm
rejects.  Files clang itself cannot compile are skipped.
"""
import argparse, collections, glob, os, re, subprocess, sys, tempfile
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VAS = os.environ.get('VAS', f'{BUILD}/vasm')
CL = llvm('clang')

def one(job):
    f, opt = job
    with tempfile.TemporaryDirectory() as t:
        s, c, v = f'{t}/a.s', f'{t}/c.o', f'{t}/v.o'
        if subprocess.run([CL, '--target=aarch64-linux-gnu', *opt.split(), '-w', '-S', f, '-o', s], capture_output=True).returncode:
            return ('skip', f, opt, '')
        if subprocess.run([CL, '--target=aarch64-linux-gnu', '-c', s, '-o', c], capture_output=True).returncode:
            return ('skip', f, opt, '')
        q = subprocess.run([VAS, s, '-o', v], capture_output=True, text=True)
        if q.returncode:
            m = re.search(r'error: (.*)', q.stderr)
            return ('fail', f, opt, m.group(1) if m else q.stderr[:200])
        return ('ok' if open(c, 'rb').read() == open(v, 'rb').read() else 'diff', f, opt, '')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-O', action='append', dest='opts')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('-v', action='store_true', help='list every failing file')
    ap.add_argument('pat', nargs='?')
    a = ap.parse_args()
    opts = [o if o.startswith('-O') else '-O' + o for o in (a.opts or ['0', '2'])]
    files = sorted(glob.glob(f'{R}/ccs/vcc/tests/**/*.c', recursive=True))
    if a.pat: files = [f for f in files if a.pat in f]
    if a.limit: files = files[:a.limit]
    jobs = [(f, o) for f in files for o in opts]
    with Pool(a.j) as p: res = p.map(one, jobs, chunksize=4)
    cnt = collections.Counter(r[0] for r in res)
    why = collections.Counter()
    for k, f, o, m in res:
        if k == 'fail':
            mm = re.match(r'([\w.]+): ', m)
            why[mm.group(1) if mm else m[:60]] += 1
        if k in ('fail', 'diff') and (a.v or k == 'diff'):
            print(k.upper(), os.path.relpath(f, R), o, m[:120])
    for w, n in why.most_common(25): print(f'{n:6d}  {w}')
    tot = cnt['ok'] + cnt['diff'] + cnt['fail']
    print(f"vas-corpus: {cnt['ok']}/{tot} identical, {cnt['diff']} differ, {cnt['fail']} rejected, {cnt['skip']} skipped")
    sys.exit(1 if cnt['diff'] or cnt['fail'] else 0)

if __name__ == '__main__': main()
