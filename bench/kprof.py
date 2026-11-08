#!/usr/bin/env python3
"""Where a cold kernel build's time goes: make recipes (bench/tsh.c) + vccd jobs.

    cc -O2 -o build/ccs/trace/sh bench/tsh.c      # named sh: make keeps its fast path
    TSH_LOG=OUT.tsh bench/vccd-kbuild.sh OUT -j 14 SHELL=$PWD/build/ccs/trace/sh
    bench/kprof.py OUT

Prints, per 0.5 s: sub-make recipes, leaf recipes and vccd compiles in flight;
recipes by kind (wall sum, user, sys of the shell and its children); and for
CC/AS recipes the time before the job reaches vccd, in vccd, and after it.
Also: what is in flight per second (CC split into shell+shim and in-vccd, AR,
other), and the recipes before the first kernel CC.
The trace costs ~1-2 s on a 15 s build; commands that make runs without a shell
(no metacharacters) are not seen.  For where vccd's own CPU goes, see
bench/vccd-sample.py.
"""
import collections
import os
import re
import statistics
import sys

KSRC = os.environ.get('VROOM_KSRC', '/Users/johandahlin/dev/vroom/work/linux-pf').rstrip('/') + '/'


def load(out):
    V = []
    for l in open(out + '.vccd.log'):
        m = re.match(r'vccd (\S+)\s+([\d.]+) ms rc=-?\d+ @([\d.]+) (\S*) ?(.*)', l)
        if m:
            V.append(dict(k=m[1], d=float(m[2]), s=float(m[3]), last=(m[5].split() or [''])[-1]))
    T = []
    for l in open(out + '.tsh'):
        head, _, cmd = l.partition(' | ')
        f = head.split()
        cmd = cmd.strip()
        m = re.search(r"echo '  (\S+)\s+(\S+?)'?;", cmd)
        sub = bool(re.match(r'-c (\S*/)?make ', cmd))
        T.append(dict(s=float(f[0]), e=float(f[1]), u=float(f[5]), y=float(f[6]), cmd=cmd,
                      kind='submake' if sub else m[1] if m else 'other', tgt=m[2] if m else None))
    return V, T


def series(items, t0, n, step):
    b = [0.0] * n
    for s, e in items:
        s, e = s - t0, e - t0
        for i in range(int(s / step), min(n, int(e / step) + 1)):
            b[i] += max(0, min(e, (i + 1) * step) - max(s, i * step)) / step
    return b


def q(a):
    a = sorted(a)
    return f'sum {sum(a) / 1e3:6.1f} s  median {statistics.median(a):6.1f}  p90 {a[int(len(a) * .9)]:6.1f} ms'


def main():
    V, T = load(sys.argv[1])
    t0 = min(x['s'] for x in T)
    end = max(x['e'] for x in T)
    step = 500
    n = int((end - t0) / step) + 1
    S = series([(x['s'], x['e']) for x in T if x['kind'] == 'submake'], t0, n, step)
    L = series([(x['s'], x['e']) for x in T if x['kind'] != 'submake'], t0, n, step)
    C = series([(x['s'], x['s'] + x['d']) for x in V if x['k'] == 'cc-own'], t0, n, step)
    print(f'trace {(end - t0) / 1e3:.2f} s from the first recipe\n    t  submakes  leaves  vccd-cc')
    for i in range(n):
        print(f'{i * step / 1e3:5.1f}  {S[i]:7.1f}  {L[i]:6.1f}  {C[i]:7.1f}')
    k = collections.defaultdict(lambda: [0, 0.0, 0.0, 0.0])
    for x in T:
        r = k[x['kind']]
        r[0] += 1; r[1] += x['e'] - x['s']; r[2] += x['u']; r[3] += x['y']
    print('\nrecipe kind       n   wall-sum   user    sys  (s; submakes include their children)')
    for a, (c, w, u, y) in sorted(k.items(), key=lambda i: -i[1][1])[:15]:
        print(f'  {a:12s} {c:5d} {w / 1e3:8.1f} {u / 1e3:6.1f} {y / 1e3:6.1f}')
    own = {x['last']: x for x in V if x['k'] == 'cc-own'}
    pre, comp, post = [], [], []
    for x in T:
        if x['kind'] not in ('CC', 'AS') or not x['tgt']:
            continue
        j = own.get(KSRC + x['tgt'][:-2] + '.c') or own.get(KSRC + x['tgt'][:-2] + '.S')
        if j:
            pre.append(j['s'] - x['s']); comp.append(j['d']); post.append(x['e'] - j['s'] - j['d'])
    if pre:
        print(f'\nCC/AS recipes matched to vccd jobs: {len(pre)}')
        print(f'  before vccd  {q(pre)}\n  in vccd      {q(comp)}\n  after vccd   {q(post)}')
    # per second: what holds the job slots
    st = 1000; m = int((end - t0) / st) + 1
    pre_i, in_i = [], []
    for x in T:
        if x['kind'] in ('CC', 'AS') and x['tgt']:
            j = own.get(KSRC + x['tgt'][:-2] + '.c') or own.get(KSRC + x['tgt'][:-2] + '.S')
            if j:
                pre_i.append((x['s'], j['s'])); in_i.append((j['s'], j['s'] + j['d']))
    P, I = series(pre_i, t0, m, st), series(in_i, t0, m, st)
    K = {a: series([(x['s'], x['e']) for x in T if x['kind'] == a], t0, m, st) for a in ('AR', 'other', 'HOSTCC')}
    print('\nin flight per second:  cc:shell+shim  cc:in-vccd    AR  other  HOSTCC')
    for i in range(m):
        print(f'  {i:4d}               {P[i]:8.1f}  {I[i]:10.1f} {K["AR"][i]:5.1f} {K["other"][i]:6.1f} {K["HOSTCC"][i]:7.1f}')
    kcc = [x['s'] for x in T if x['kind'] == 'CC' and x['tgt'] and not x['tgt'].startswith('scripts')]
    if kcc:
        f = min(kcc)
        early = [x for x in T if x['s'] < f and x['kind'] != 'submake']
        print(f'\nfirst kernel CC at {(f - t0) / 1e3:.2f} s; {len(early)} leaf recipes before it, longest:')
        for x in sorted(early, key=lambda x: x['s'] - x['e'])[:10]:
            lab = f"{x['kind']} {x['tgt']}" if x['tgt'] else x['cmd'][3:80]
            print(f"  {(x['s'] - t0) / 1e3:5.2f} s {x['e'] - x['s']:6.0f} ms  {lab}")
    kk = collections.defaultdict(lambda: [0, 0.0])
    for x in V:
        kk[x['k']][0] += 1; kk[x['k']][1] += x['d']
    print('\nvccd jobs: ' + ', '.join(f'{a} {c} / {s / 1e3:.1f} s'
                                      for a, (c, s) in sorted(kk.items(), key=lambda i: -i[1][1])[:5]))


if __name__ == '__main__':
    main()
