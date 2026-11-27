#!/usr/bin/env python3
"""Proof of concept: Stockfish's TUs compiled by threads of one process, as
vccd would (vpp --batch), against one vpp process per TU.

  bench/stockfish/threads.py [-j N] [--bin-dir DIR] [--warm N] [--src DIR]

Objects must be byte-identical; --bin-dir build/linux/src-tsan checks races;
--warm N repeats the batch N times first (peak RSS shows what a server keeps).
"""
import argparse, filecmp, os, subprocess, sys, time

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIBCXX = '/usr/lib/llvm-23/include/c++/v1'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--bin-dir', default=os.path.join(R, 'build/linux/src'))
    ap.add_argument('--warm', type=int, default=0)
    ap.add_argument('--src', default=os.path.join(R, 'work/stockfish/src'))
    ap.add_argument('-O', dest='level', default='2')
    a = ap.parse_args()
    src = os.path.abspath(a.src)
    vpp = os.path.abspath(os.path.join(a.bin_dir, 'vpp'))
    out = os.path.join(R, 'build/stockfish/threads')
    tus = sorted(os.path.relpath(os.path.join(d, f), src)
                 for d, _, fs in os.walk(src) for f in fs
                 if f.endswith('.cpp'))
    flags = ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++', '-isystem',
             LIBCXX, '-O' + a.level, '-DNDEBUG', '-DIS_64BIT',
             '-DUSE_PTHREADS', '-DNNUE_EMBEDDING_OFF', '-c']
    for d in ('one', 'batch'):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    def obj(d, s):
        return os.path.join(out, d, s.replace('/', '_') + '.o')

    t = time.time()
    for s in tus:
        subprocess.run([vpp] + flags + [s, '-o', obj('one', s)], cwd=src,
                       check=True)
    one = time.time() - t
    jobs = os.path.join(out, 'jobs')
    with open(jobs, 'w') as f:
        for s in tus:
            if os.path.exists(obj('batch', s)):
                os.unlink(obj('batch', s))
            f.write('\x1f'.join(['-'] + flags + [s, '-o', obj('batch', s)])
                    + '\n')
    p = subprocess.run(['/usr/bin/time', '-f', 'peak RSS %M KB, %es', vpp,
                        '--batch', jobs, '-j', str(a.j), '-C', src] +
                       (['--warm', str(a.warm)] if a.warm else []),
                       env=dict(os.environ, VPP_BATCH_ERR='1'))
    bad = [s for s in tus if not os.path.exists(obj('batch', s)) or
           not filecmp.cmp(obj('one', s), obj('batch', s), shallow=False)]
    print('%d TUs, %d differ; one process each, serially: %.2fs; batch exit %d'
          % (len(tus), len(bad), one, p.returncode))
    for s in bad:
        print('  differs:', s)
    return 1 if bad or p.returncode else 0


if __name__ == '__main__':
    sys.exit(main())
