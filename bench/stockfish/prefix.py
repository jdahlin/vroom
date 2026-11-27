#!/usr/bin/env python3
"""doc/PLAN-cxx-snapshot.md on Stockfish: a prefix of the system headers its
files name.  Per TU, `-fcxx-prefix=P` (the front end stops after P) must give
the object `-include P` gives, byte for byte; then both against the TU with its
own includes only, by defined symbols and sizes.

  bench/stockfish/prefix.py [-O 0|2] [--src DIR] [--bin-dir DIR] [--fork]
--fork (CS4): one vpp analyses P and forks a child per TU
(vpp --cxx-prefix-batch); the same objects again, and the CPU both ways.
"""
import argparse, filecmp, os, re, resource, subprocess, sys, time

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIBCXX = '/usr/lib/llvm-23/include/c++/v1'
# named in #if branches the configuration does not take
NOT_HERE = {'windows.h', 'intrin.h', 'direct.h', 'arm_neon.h', 'arm_acle.h',
            'processthreadsapi.h', 'libloaderapi.h'}


def prefix_list(src):
    names = set()
    for d, _, files in os.walk(src):
        for f in files:
            if f.endswith(('.cpp', '.h')):
                with open(os.path.join(d, f), errors='replace') as h:
                    names.update(re.findall(r'^\s*#\s*include\s*<([^>]+)>',
                                            h.read(), re.M))
    return sorted(n for n in names
                  if n not in NOT_HERE and not n.endswith('mmintrin.h'))


def symbols(obj):
    out = subprocess.run(['nm', '-S', '--defined-only', obj], check=True,
                         capture_output=True, text=True).stdout
    return sorted(' '.join(f[1:]) for f in
                  (line.split() for line in out.splitlines())
                  if not f[-1].startswith(('.L', 'GCC_except')))


def counted(command, cwd, stat):
    """command under perf stat: (status, instructions, CPU seconds)"""
    p = subprocess.run(['perf', 'stat', '-x,', '-o', stat, '-e',
                        'instructions:u,task-clock'] + command, cwd=cwd)
    got = {}
    with open(stat) as f:
        for line in f:
            w = line.split(',')
            if len(w) > 2:
                got[w[2].split(':')[0]] = float(w[0])
    return p.returncode, got['instructions'], got['task-clock'] / 1e3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-O', dest='level', default='2')
    ap.add_argument('--src', default=os.path.join(R, 'work/stockfish/src'))
    ap.add_argument('--bin-dir', default=os.path.join(R, 'build/linux/src'))
    ap.add_argument('--fork', action='store_true')
    ap.add_argument('-j', type=int, default=8)
    a = ap.parse_args()
    src = os.path.abspath(a.src)
    vpp = os.path.abspath(os.path.join(a.bin_dir, 'vpp'))
    out = os.path.join(R, 'build/stockfish/prefix')
    os.makedirs(out, exist_ok=True)
    prefix = os.path.join(out, 'prefix.h')
    with open(prefix, 'w') as f:
        f.write(''.join('#include <%s>\n' % n for n in prefix_list(src)))
    stat = os.path.join(out, 'stat')
    flags = ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++', '-isystem',
             LIBCXX, '-O' + a.level, '-DNDEBUG', '-DIS_64BIT',
             '-DUSE_PTHREADS', '-DNNUE_EMBEDDING_OFF', '-c']
    tus = sorted(os.path.relpath(os.path.join(d, f), src)
                 for d, _, fs in os.walk(src) for f in fs
                 if f.endswith('.cpp'))
    modes = {'own': [], 'include': ['-include', prefix],
             'stop': ['-fcxx-prefix=' + prefix]}

    def obj(mode, s):
        return os.path.join(out, '%s-%s.o' % (mode, s.replace('/', '_')))

    failed = 0
    total = {}
    for mode, extra in modes.items():
        ins = cpu = 0
        for s in tus:
            if os.path.exists(obj(mode, s)):
                os.unlink(obj(mode, s))
            status, i, c = counted([vpp] + flags + extra +
                                   [s, '-o', obj(mode, s)], src, stat)
            ins, cpu = ins + i, cpu + c
            if status:
                print('%s: %s failed' % (mode, s))
                failed += 1
        total[mode] = (ins, cpu)
        print('%-8s %6.2fG instructions, %5.2fs CPU' % (mode, ins / 1e9, cpu))
    if failed:
        return 1
    differ = [s for s in tus if not filecmp.cmp(
        obj('include', s), obj('stop', s), shallow=False)]
    print('stop against -include: %d of %d objects differ' %
          (len(differ), len(tus)), ' '.join(differ))
    changed = [s for s in tus
               if symbols(obj('own', s)) != symbols(obj('stop', s))]
    print('stop against own includes: defined symbols differ in %d' %
          len(changed), ' '.join(changed))
    failed += len(differ) + len(changed)
    if a.fork:
        jobs = os.path.join(out, 'jobs')
        with open(jobs, 'w') as f:
            for s in tus:
                f.write('%s\x1f%s\n' % (s, obj('fork', s)))
                if os.path.exists(obj('fork', s)):
                    os.unlink(obj('fork', s))
        for j in (1, a.j):
            before = resource.getrusage(resource.RUSAGE_CHILDREN)
            start = time.time()
            status, ins, cpu = counted(
                [vpp, '--cxx-prefix-batch', jobs, '-j', str(j)] + flags +
                modes['stop'] + [tus[0], '-o', obj('fork', tus[0])], src, stat)
            wall = time.time() - start
            after = resource.getrusage(resource.RUSAGE_CHILDREN)
            system = after.ru_stime - before.ru_stime
            differ = [s for s in tus if not os.path.exists(obj('fork', s)) or
                      not filecmp.cmp(obj('stop', s), obj('fork', s),
                                      shallow=False)]
            print('fork -j%-2d %6.2fG instructions, %5.2fs CPU (system %.2fs), '
                  'wall %.2fs; %d objects differ from stop %s' %
                  (j, ins / 1e9, cpu, system, wall, len(differ),
                   ' '.join(differ)))
            failed += bool(status) + len(differ)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
