#!/usr/bin/env python3
"""Stockfish's TUs through the vcc shim and one private vccd, concurrently;
objects against bench/stockfish/threads.py's (run that first)."""
import filecmp, os, signal, subprocess, tempfile, time
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.environ.get('STOCKFISH_SRC') or os.path.join(R, 'work/stockfish/src')
BIN = os.path.join(R, 'build/linux/src')
REF = os.path.join(R, 'build/stockfish/threads/one')
FLAGS = ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++', '-isystem',
         '/usr/lib/llvm-23/include/c++/v1', '-O2', '-DNDEBUG', '-DIS_64BIT',
         '-DUSE_PTHREADS', '-DNNUE_EMBEDDING_OFF', '-c']

T = tempfile.mkdtemp(prefix='sf-vccd-')
sock = os.path.join(T, 's')
d = subprocess.Popen([BIN + '/vccd', '--sock', sock, '-j', '8'])
while not os.path.exists(sock):
    time.sleep(0.02)
tus = [os.path.relpath(os.path.join(p, f), SRC)
       for p, _, fs in os.walk(SRC) for f in fs if f.endswith('.cpp')]


def one(s):
    name = s.replace('/', '_') + '.o'
    out = os.path.join(T, name)
    p = subprocess.run([BIN + '/vcc'] + FLAGS + ['-o', out, s], cwd=SRC,
                       env=dict(os.environ, VCC_SOCK=sock))
    return p.returncode == 0 and filecmp.cmp(out, os.path.join(REF, name),
                                             shallow=False)


for n in range(3):
    t = time.time()
    with ThreadPoolExecutor(8) as pool:
        good = sum(pool.map(one, tus))
    print('build %d: %d/%d identical, %.2fs' % (n, good, len(tus),
                                                time.time() - t))
d.send_signal(signal.SIGTERM)
d.wait()
