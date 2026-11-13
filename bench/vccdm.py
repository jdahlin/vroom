#!/usr/bin/env python3
"""vccdm.py: kernel TUs compiled through one private vccd by several process groups (Q-SCHED-01).

  bench/vccdm.py [--bin-dir DIR] [-w WORKERS] [-c CLIENTS] [-g GROUPS] [-n TUS] [-r REPS]

The same 168-TU style load as bench/vccd-cc.py, but the clients belong to
GROUPS process groups, alternating job by job, as GROUPS makes at once would
send them (the daemon sees one `pgrp` per make).  Prints per repetition the
wall time, vccd's CPU time and peak memory and its cache flushes, then min and
median.  With GROUPS=1 nothing may flush; with GROUPS=2 the flush count must be
0 or 1 (it was one per switch before Q-SCHED-01 was fixed).
"""
import os, sys, subprocess, tempfile, time, signal, re, resource, statistics, argparse, importlib.util
from multiprocessing.pool import ThreadPool
ap = argparse.ArgumentParser()
ap.add_argument('--bin-dir'); ap.add_argument('-w', type=int, default=6); ap.add_argument('-c', type=int, default=6)
ap.add_argument('-g', type=int, default=2); ap.add_argument('-n', type=int, default=168); ap.add_argument('-r', type=int, default=3)
args, rest = ap.parse_known_args()
if args.bin_dir: os.environ['VCC_BIN'] = args.bin_dir
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
sys.argv = sys.argv[:1]
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)

ts = [t for t in vd.tus(None) if not t[0].startswith('scripts/') and '-E' not in t[1] and '-mcmodel=tiny' not in t[1] and not t[0].endswith('.lds')]
ts = ts[::max(1, len(ts) // args.n)][:args.n]

def once():
    T = tempfile.mkdtemp(prefix='vccdm-')
    sock = f'{T}/vccd.sock'; logf = f'{T}/vccd.log'
    d = subprocess.Popen([f'{vd.BIN}/vccd', '--sock', sock, '-j', str(args.w)], stderr=open(logf, 'w'), stdout=subprocess.DEVNULL,
                         env=dict(os.environ, VCC_LOG='1'))
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    leaders = [subprocess.Popen(['sleep', '600'], process_group=0) for _ in range(args.g)]
    def run(it):
        i, (obj, a) = it
        o = f'{T}/{obj.replace("/", "__")}'
        p = subprocess.run([f'{vd.BIN}/vcc'] + a[:-1] + ['-c', '-o', o, a[-1]], cwd=vd.B, capture_output=True, text=True,
                           env=dict(os.environ, VCC_SOCK=sock), process_group=leaders[i % args.g].pid)
        ok = p.returncode == 0 and os.path.exists(o) and os.path.getsize(o) > 0
        if os.path.exists(o): os.unlink(o)
        return ok
    t0 = time.time()
    res = ThreadPool(args.c).map(run, list(enumerate(ts)))
    wall = time.time() - t0
    d.send_signal(signal.SIGTERM)
    _, _, ru = os.wait4(d.pid, 0)
    for l in leaders: l.kill(); l.wait()
    log = open(logf).read()
    m = re.search(r'(\d+) builds \(cache flushes\)', log)
    return wall, ru.ru_utime + ru.ru_stime, ru.ru_maxrss / 2**20, int(m.group(1)) if m else -1, sum(res), len(res)

rows = []
print(f'load {os.getloadavg()[0]:.1f}; workers {args.w} clients {args.c} groups {args.g} TUs {len(ts)}')
for r in range(args.r):
    row = once(); rows.append(row)
    print(f'  rep {r + 1}: wall {row[0]:.2f} s  vccd cpu {row[1]:.2f} s  max rss {row[2]:.0f} MB  flushes {row[3]}  ok {row[4]}/{row[5]}', flush=True)
for k, name in enumerate(['wall s', 'cpu s', 'rss MB', 'flushes']):
    v = [r[k] for r in rows]
    print(f'{name:8} min {min(v):.2f}  median {statistics.median(v):.2f}')
sys.exit(0 if all(r[4] == r[5] for r in rows) else 1)
