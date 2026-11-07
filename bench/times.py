#!/usr/bin/env python3
"""Analys av VROOM_TIMES-loggen från vroomd."""
import collections, sys
L = [l.split() for l in open(sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-times.log')]
t0 = min(float(x[0]) for x in L)
K = [x for x in L if x[3] == 'K']
print(f'förfrågningar {len(L)}, kärn-C {len(K)}, CPU-sum kärn {sum(float(x[2]) for x in K)/1e3:.1f}s, '
      f'övrigt {sum(float(x[2]) for x in L if x[3] != "K")/1e3:.1f}s')
print(f'första kärn-C {min(float(x[0]) for x in K)-t0:.2f}s, sista klar {max(float(x[1]) for x in K)-t0:.2f}s')
b = collections.Counter()
for x in L:
    s, e = float(x[0]) - t0, float(x[1]) - t0
    for sec in range(int(s), int(e) + 1):
        b[sec] += max(0, min(e, sec + 1) - max(s, sec))
print('samtidighet/s:', ' '.join(f'{v:.0f}' for k, v in sorted(b.items())))
print('störst:')
for x in sorted(L, key=lambda x: -float(x[2]))[:12]:
    print(f'  {float(x[2]):7.0f} ms  start {float(x[0])-t0:5.1f}s  {x[4]}')
