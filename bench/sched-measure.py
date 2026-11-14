#!/usr/bin/env python3
"""sched-measure.py: the scheduling measurements of doc/review/quality-scheduling.md 3.2,
rerunnable (doc/quality/WORK-scheduler.md).

  bench/sched-measure.py batch [--bin-dir D ...] [-j 1,2,4,6] [--emit T] [--warm 0] [--every 4] [-r REPS]
      A, B: `vpp --batch` on every 4th kernel TU (-c), cold process: wall, CPU
      (user + sys), busy = CPU / (wall x j), max RSS, involuntary switches.
      --emit T sets VCC_THREADS=T (nested emit inside the batch); --warm N
      adds N untimed passes first (B: max RSS must not grow with N).
  bench/sched-measure.py emit [--bin-dir D ...] [-t 1,2,4,8] [-O LEVEL] [FILE]
      C: one large TU (default work/sqlite/st/sqlite3.c) -c at LEVEL (default -O2: VIR) at
      VCC_THREADS=T: wall, max RSS; the objects must be byte-identical.
  bench/sched-measure.py link [LABEL=DIR ...] [-r REPS]
      E: bench/vlink-pool.py (small, big and script links, default threads
      and --threads=1, outputs compared).
  bench/sched-measure.py vccd [--bin-dir D ...] [-w WORKERS] [-c CLIENTS] [-n TUS] [-r REPS] [--vccd-args "..."]
      D: TUS kernel TUs through one private vccd from CLIENTS shim processes:
      wall, vccd CPU, max RSS, objects ok.  --vccd-args "--shared-pool" for
      the shared pool (stage 3).
  bench/sched-measure.py kbuild-log LOG...
      F: from vccd's VCC_LOG lines (bench/vccd-kbuild.sh writes OUT.vccd.log):
      compiles, their summed time in flight, the most and mean in flight
      over the compile phase, and the share of the build with nothing in flight.

Several --bin-dir (LABEL=DIR or DIR) compare builds side by side.  Run one
measurement at a time, when `uptime` shows a load under 6; the load is printed.
"""
import os, sys, subprocess, tempfile, time, statistics, filecmp, shutil, re, argparse, signal, importlib.util
from multiprocessing.pool import ThreadPool

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def vd_module():
    spec = importlib.util.spec_from_file_location('vd', os.path.join(ROOT, 'bench', 'vcc-diff.py'))
    saved = sys.argv
    sys.argv = sys.argv[:1]
    vd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vd)
    sys.argv = saved
    return vd


def bins(a, vd):
    out = []
    for d in a or [vd.BIN]:
        lab, _, p = d.rpartition('=')
        out.append((lab or os.path.basename(os.path.dirname(os.path.abspath(p))) + '/' + os.path.basename(p), os.path.abspath(p)))
    return out


def kernel_c_tus(vd):
    return [t for t in vd.tus(None) if not t[0].startswith('scripts/') and '-E' not in t[1]
            and '-mcmodel=tiny' not in t[1] and not t[0].endswith('.lds') and t[1][-1].endswith('.c')]


def rusage_run(cmd, env=None, cwd=None):
    """wall, cpu, max rss MB, involuntary switches, stdout"""
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=cwd, env=dict(os.environ, **(env or {})), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out = p.stdout.read()
    err = p.stderr.read()
    _, st, ru = os.wait4(p.pid, 0)
    wall = time.time() - t0
    rss = ru.ru_maxrss / (2**20 if sys.platform == 'darwin' else 2**10)
    return st, wall, ru.ru_utime + ru.ru_stime, rss, ru.ru_nivcsw, out.decode(errors='replace') + err.decode(errors='replace')


def load():
    return f'load {os.getloadavg()[0]:.1f}'


def cmd_batch(a):
    vd = vd_module()
    tus = kernel_c_tus(vd)[::a.every]
    T = tempfile.mkdtemp(prefix='sched-batch-')
    jobs = f'{T}/jobs'
    with open(jobs, 'w') as f:
        for o, args in tus:
            f.write('\x1f'.join(['-'] + args + ['-c']) + '\n')
    env = {'VCC_THREADS': str(a.emit)} if a.emit else {}
    print(f'{load()}; {len(tus)} TUs (every {a.every}th kernel C TU), -c, VCC_THREADS={a.emit or "unset"}, '
          f'--warm {a.warm}, {a.r} runs: min (median)')
    for lab, b in bins(a.bin_dir, vd):
        for j in [int(x) for x in a.j.split(',')]:
            rows = []
            for _ in range(a.r):
                st, wall, cpu, rss, sw, out = rusage_run([f'{b}/vpp', '--batch', jobs, '-j', str(j), '-C', vd.B,
                                                         '--warm', str(a.warm)], env)
                m = re.search(r'(\d+)/(\d+) ok', out)
                if st or not m or m.group(1) != m.group(2):
                    sys.exit(f'{lab}: vpp --batch failed:\n{out[-1500:]}')
                w = float(re.search(r'wall (\d+) ms', out).group(1)) / 1e3	# the timed pass
                rows.append((w, cpu, rss, sw))
            col = lambda k: (min(r[k] for r in rows), statistics.median(r[k] for r in rows))
            w, c, r, s = col(0), col(1), col(2), col(3)
            print(f'{lab:24} -j {j:2}  wall {w[0]:.2f} ({w[1]:.2f}) s  cpu {c[0]:.2f} ({c[1]:.2f}) s  '
                  f'busy {c[1] / (w[1] * j) * 100 if a.warm == 0 else float("nan"):.0f} %  rss {r[1]:.0f} MB  nivcsw {s[1]:.0f}', flush=True)
    shutil.rmtree(T)


def cmd_emit(a):
    vd = vd_module()
    src = a.file or os.path.join(os.path.dirname(vd.B), 'sqlite', 'st', 'sqlite3.c')
    if not os.path.exists(src):
        sys.exit(f'{src}: not found (bench/sqlite/run.py fetches it)')
    T = tempfile.mkdtemp(prefix='sched-emit-')
    print(f'{load()}; {src} {a.O} -c, {a.r} runs: min (median)')
    bad = 0
    for lab, b in bins(a.bin_dir, vd):
        ref = None
        for t in [int(x) for x in a.t.split(',')]:
            rows = []
            o = f'{T}/{lab.replace("/", "_")}-{t}.o'
            for _ in range(a.r):
                st, wall, cpu, rss, sw, out = rusage_run([f'{b}/vpp', a.O, '-c', src, '-o', o], dict(VCC_THREADS=str(t)))
                if st:
                    sys.exit(f'{lab}: vpp failed:\n{out[-1500:]}')
                rows.append((wall, cpu, rss))
            ref = ref or o
            same = filecmp.cmp(ref, o, shallow=False)
            bad += not same
            print(f'{lab:24} VCC_THREADS={t}  wall {min(r[0] for r in rows):.3f} ({statistics.median(r[0] for r in rows):.3f}) s  '
                  f'cpu {statistics.median(r[1] for r in rows):.3f} s  rss {statistics.median(r[2] for r in rows):.0f} MB'
                  f'{"" if same else "  OBJECT DIFFERS"}', flush=True)
    shutil.rmtree(T)
    sys.exit(1 if bad else 0)


def cmd_link(a):
    sys.exit(subprocess.call([sys.executable, os.path.join(ROOT, 'bench', 'vlink-pool.py'), '-r', str(a.r)] + a.dirs))


def cmd_vccd(a):
    vd = vd_module()
    ts = kernel_c_tus(vd)
    ts = ts[::max(1, len(ts) // a.n)][:a.n]
    print(f'{load()}; {len(ts)} TUs, {a.c} clients, vccd -j {a.w or "default"} {a.vccd_args}, {a.r} runs')
    ok_all = True
    for lab, b in bins(a.bin_dir, vd):
        rows = []
        for _ in range(a.r):
            T = tempfile.mkdtemp(prefix='sched-vccd-')
            sock = f'{T}/vccd.sock'
            cmd = [f'{b}/vccd', '--sock', sock] + (['-j', str(a.w)] if a.w else []) + a.vccd_args.split()
            d = subprocess.Popen(cmd, stderr=open(f'{T}/vccd.log', 'w'), stdout=subprocess.DEVNULL)
            for _ in range(100):
                if os.path.exists(sock):
                    break
                time.sleep(0.05)

            def run(it):
                obj, args = it
                o = f'{T}/{obj.replace("/", "__")}'
                p = subprocess.run([f'{b}/vcc'] + args[:-1] + ['-c', '-o', o, args[-1]], cwd=vd.B, capture_output=True,
                                   env=dict(os.environ, VCC_SOCK=sock))
                good = p.returncode == 0 and os.path.exists(o) and os.path.getsize(o) > 0
                return good
            t0 = time.time()
            res = ThreadPool(a.c).map(run, ts)
            wall = time.time() - t0
            d.send_signal(signal.SIGTERM)
            _, _, ru = os.wait4(d.pid, 0)
            rss = ru.ru_maxrss / (2**20 if sys.platform == 'darwin' else 2**10)
            rows.append((wall, ru.ru_utime + ru.ru_stime, rss, sum(res)))
            shutil.rmtree(T)
        ok_all &= all(r[3] == len(ts) for r in rows)
        med = lambda k: statistics.median(r[k] for r in rows)
        print(f'{lab:24} wall {min(r[0] for r in rows):.2f} ({med(0):.2f}) s  vccd cpu {med(1):.2f} s  '
              f'max rss {med(2):.0f} MB  ok {min(r[3] for r in rows)}/{len(ts)}', flush=True)
    sys.exit(0 if ok_all else 1)


LOGLINE = re.compile(r'^vccd (\S+)\s+([\d.]+) ms rc=(-?\d+) @([\d.]+)')


def cmd_kbuild_log(a):
    for path in a.logs:
        ev = []
        for l in open(path, errors='replace'):
            m = LOGLINE.match(l)
            if m:
                kind, ms, t0 = m.group(1), float(m.group(2)), float(m.group(4))
                ev.append((kind, t0, t0 + ms))
        if not ev:
            print(f'{path}: no VCC_LOG lines')
            continue
        cc = [e for e in ev if e[0] == 'cc-own']
        start, end = min(e[1] for e in ev), max(e[2] for e in ev)
        # in-flight count over time, from start/end points
        pts = sorted([(e[1], 1) for e in ev] + [(e[2], -1) for e in ev])
        n = mx = 0
        idle = 0.0
        last = start
        for t, d in pts:
            if n == 0:
                idle += t - last
            last = t
            n += d
            mx = max(mx, n)
        line = f'{path}: {len(ev)} requests over {(end - start) / 1e3:.2f} s, most in flight {mx}, nothing in flight {idle / (end - start) * 100:.0f} %'
        if cc:
            cs, ce = min(e[1] for e in cc), max(e[2] for e in cc)
            busy = sum(e[2] - e[1] for e in cc)
            line += (f'; {len(cc)} compiles, time in flight {busy / 1e3:.1f} s (mean {busy / len(cc):.1f} ms), '
                     f'compile phase {(ce - cs) / 1e3:.2f} s with {busy / (ce - cs):.1f} in flight on average')
        print(line)
        for l in open(path, errors='replace'):
            if l.startswith('vccd:'):
                print('  ' + l.rstrip())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('batch')
    p.add_argument('--bin-dir', action='append'); p.add_argument('-j', default='1,2,4,6'); p.add_argument('--emit', type=int)
    p.add_argument('--warm', type=int, default=0); p.add_argument('--every', type=int, default=4); p.add_argument('-r', type=int, default=1)
    p = sub.add_parser('emit')
    p.add_argument('--bin-dir', action='append'); p.add_argument('-t', default='1,2,4,8'); p.add_argument('-O', default='-O2', type=lambda v: v if v.startswith('-O') else '-O' + v)
    p.add_argument('-r', type=int, default=3); p.add_argument('file', nargs='?')
    p = sub.add_parser('link')
    p.add_argument('-r', type=int, default=3); p.add_argument('dirs', nargs='*')
    p = sub.add_parser('vccd')
    p.add_argument('--bin-dir', action='append'); p.add_argument('-w', type=int, default=6, help='vccd -j (0: its default)'); p.add_argument('-c', type=int, default=6)
    p.add_argument('-n', type=int, default=168); p.add_argument('-r', type=int, default=3); p.add_argument('--vccd-args', default='')
    p = sub.add_parser('kbuild-log')
    p.add_argument('logs', nargs='+')
    a = ap.parse_args()
    {'batch': cmd_batch, 'emit': cmd_emit, 'link': cmd_link, 'vccd': cmd_vccd, 'kbuild-log': cmd_kbuild_log}[a.cmd](a)


if __name__ == '__main__':
    main()
