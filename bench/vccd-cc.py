#!/usr/bin/env python3
"""vccd-cc.py: kernel TUs compiled (-c) through the vcc shim by a private vccd.

  bench/vccd-cc.py [--bin-dir DIR] [PAT]    N=150 TUs (.c and .S), 6 clients

Exercises vccd's own request path (socket, worker threads, cc-own jobs) without
a kernel build; meant for the sanitizer variants (AGENTS.md, "Sanitizers").
vccd comes from --bin-dir (or VCC_BIN), the shim from build/ccs.  There is no
backend: jobs vccd forwards fail (the vDSO's linker script, for one).  vccd's
stderr, with any sanitizer report, goes to $LOG (default $TMPDIR/vccd-cc.log).
"""
import os, sys, subprocess, tempfile, time, signal, importlib.util
from multiprocessing.pool import ThreadPool
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
T = tempfile.mkdtemp(prefix='vccd-cc-')
sock = f'{T}/vccd.sock'
logf = os.environ.get('LOG', os.path.join(tempfile.gettempdir(), 'vccd-cc.log'))
log = open(logf, 'w')
d = subprocess.Popen([f'{vd.BIN}/vccd', '--sock', sock, '--backend-sock', f'{T}/none.sock', '-j', '6'],
                     stderr=log, stdout=log, env=dict(os.environ, VCC_LOG='1'))
for _ in range(100):
    if os.path.exists(sock): break
    time.sleep(0.05)
pat = sys.argv[1] if len(sys.argv) > 1 else None
ts = [t for t in vd.tus(pat) if not t[0].startswith('scripts/') and '-E' not in t[1] and '-mcmodel=tiny' not in t[1]]
n = int(os.environ.get('N', 150)); ts = ts[::max(1, len(ts) // n)]

def run(t):
    obj, a = t
    o = f'{T}/{obj.replace("/", "__")}'
    # vccd wants the source last
    p = subprocess.run([f'{vd.R}/build/ccs/vcc'] + a[:-1] + ['-c', '-o', o, a[-1]], cwd=vd.B,
                       capture_output=True, text=True, env=dict(os.environ, VCC_SOCK=sock))
    ok = p.returncode == 0 and os.path.exists(o) and os.path.getsize(o) > 0
    if os.path.exists(o): os.unlink(o)
    return obj, ok, p.stderr[-500:]

res = ThreadPool(6).map(run, ts)
d.send_signal(signal.SIGTERM); d.wait()
bad = [r for r in res if not r[1]]
for o, _, e in bad[:20]: print('---', o, e)
print(f'{len(res)-len(bad)}/{len(res)} ok ({sum(1 for t in ts if t[1][-1].endswith(".S"))} .S); vccd log {logf}')
