#!/usr/bin/env python3
"""vlink-pool.py: vlink's thread pool on small and big links (Q-SCHED-03).

  bench/vlink-pool.py [-r REPS] [LABEL=DIR ...]      DIR holds vlink (default: build/src)

Three links from work/build-o0, each repeated inside one process (VLD_REPEAT),
with the default thread count and with --threads=1: vmlinux.o -r (thin archive
vmlinux.a, x20), the vmlinux link with its script (x10), and four small
objects -r (x500).  Prints wall and CPU (user + sys) of each, min and median
of REPS runs, and checks that the outputs of every DIR and thread count are
byte-identical.  Run it when `uptime` shows a low load.
"""
import os, sys, subprocess, tempfile, time, statistics, filecmp, shutil
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, f'{root}/bench'); import hostenv
B = hostenv.KTREE
reps = 3; dirs = []
av = sys.argv[1:]
while av:
    a = av.pop(0)
    if a == '-r': reps = int(av.pop(0))
    else: dirs.append(a.split('=', 1) if '=' in a else (a, a))
if not dirs: dirs = [('new', f'{root}/build/src')]
T = tempfile.mkdtemp(prefix='vlink-pool-')
base = ['-EL', '-maarch64elf', '-z', 'norelro', '-z', 'noexecstack']
small = ['kernel/async.o', 'kernel/capability.o', 'kernel/cred.o', 'kernel/cpu.o']
LINKS = [
    ('big -r x20', 20, base + ['-r', '--whole-archive', 'vmlinux.a', '--no-whole-archive']),
    ('script x10', 10, base + ['-X', '--pic-veneer', '-shared', '-Bsymbolic', '-z', 'notext', '--no-apply-dynamic-relocs',
                               '--build-id=sha1', '--orphan-handling=warn', '-T', 'arch/arm64/kernel/vmlinux.lds',
                               'vmlinux.o', 'vmlinux.unstripped.vroom-stubs.o']),
    ('4 small -r x500', 500, base + ['-r'] + small),
]
def run(exe, name, rep, args, threads, out):
    cmd = [exe] + (['--threads=1'] if threads == 1 else []) + args + ['-o', out]
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=B, env=dict(os.environ, VLD_REPEAT=str(rep)), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    _, st, ru = os.wait4(p.pid, 0)
    wall = time.time() - t0
    if st: sys.exit(f'{name}: {exe} failed: {p.stderr.read().decode()[-300:]}')
    return wall, ru.ru_utime + ru.ru_stime, ru.ru_nivcsw
print(f'load {os.getloadavg()[0]:.1f}, {reps} runs; wall / cpu in s, min (median), involuntary switches')
outs = {}
bad = 0
for name, rep, args in LINKS:
    for label, d in dirs:
        for threads in (0, 1):
            out = f'{T}/{label}-{threads}-{name.split()[0]}.out'
            rows = [run(f'{d}/vlink', name, rep, args, threads, out) for _ in range(reps)]
            w = [r[0] for r in rows]; c = [r[1] for r in rows]; s = [r[2] for r in rows]
            print(f'{name:16} {label:6} threads={"default" if not threads else 1:7} wall {min(w):.3f} ({statistics.median(w):.3f})  '
                  f'cpu {min(c):.3f} ({statistics.median(c):.3f})  nivcsw {min(s)} ({statistics.median(s):.0f})', flush=True)
            ref = outs.setdefault(name, out)
            if not filecmp.cmp(ref, out, shallow=False):
                print(f'  OUTPUT DIFFERS from {ref}'); bad += 1
print('outputs byte-identical' if not bad else f'{bad} outputs differ')
shutil.rmtree(T)
sys.exit(1 if bad else 0)
