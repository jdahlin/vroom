#!/usr/bin/env python3
"""
bench/stockfish/run.py: Stockfish (C++17, 23 TUs, no dependencies) built by
vcc's C++ front end, checked by its own `bench` command.

Source: work/stockfish (git clone --depth 1 --branch sf_17.1
https://github.com/official-stockfish/Stockfish.git work/stockfish, then
`make -C work/stockfish/src net` for the two NNUE files).  The networks are
loaded from the source directory (NNUE_EMBEDDING_OFF: no .incbin), the build
is Stockfish's ARCH=general-64 (--arch sse2: x86-64), single-threaded bench,
against libc++.

Each TU is compiled by `vpp -x c++ -c` (one at a time, nice, with a timeout);
the summary groups the TUs by first error as bench/cxx-json.py does.  When
every TU compiles, vcc's objects are linked by clang++ and `stockfish bench
16 1 DEPTH default depth` must print the node count of clang++'s build: the
search is deterministic with one thread, so one wrong evaluation or move
changes it.  Both builds' nodes per second are printed (a shallow search, so
that a slow build is measured in seconds).  `--mix` also links each vcc object alone with clang's others,
to find the TU that changes the count.

  bench/stockfish/run.py [-O 0123] [--depth N] [--mix] [--force] [-v] [PAT]

Results are cached in build/stockfish/cache.json (key: flags, source and
vpp size and mtime).
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys

B = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
R = os.path.dirname(B)
SRC = os.path.join(R, 'work', 'stockfish', 'src')
OUT = os.path.join(R, 'build', 'stockfish')
CACHE = os.path.join(OUT, 'cache.json')
LIBCXX = '/usr/lib/llvm-23/include/c++/v1'
DEFINES = ['-DNDEBUG', '-DIS_64BIT', '-DUSE_PTHREADS', '-DNNUE_EMBEDDING_OFF']
# Stockfish's ARCH=x86-64 (--arch sse2); the default is ARCH=general-64,
# without intrinsics
SSE2 = ['-DUSE_SSE2', '-msse2']

spec = importlib.util.spec_from_file_location(
    'cxx_json', os.path.join(B, 'cxx-json.py'))
cj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cj)


def sources():
    out = []
    for d, _, files in os.walk(SRC):
        for f in files:
            if f.endswith('.cpp'):
                out.append(os.path.relpath(os.path.join(d, f), SRC))
    return sorted(out)


def obj_name(src):
    return src.replace('/', '_')[:-4] + '.o'


def compile_vcc(vpp, src, flags, out, timeout):
    cmd = ['nice', '-n', '15', vpp] + flags + ['-c', src, '-o', out]
    try:
        p = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'state': 'timeout'}
    if p.returncode == 0:
        return {'state': 'ok'}
    message, subclause, site, where = cj.first_error(p.stderr)
    if not message:
        return {'state': 'crash', 'message': 'status %d' % p.returncode}
    state = 'analysis'
    if message.startswith('C++ code generation'):
        state = 'lowering'
    return {'state': state, 'message': message, 'subclause': subclause,
            'site': site, 'where': where}


def build_clang(level, d):
    """clang++'s objects and executable in d (once per level)"""
    exe = os.path.join(d, 'stockfish')
    if os.path.exists(exe):
        return exe
    os.makedirs(d, exist_ok=True)
    procs = [subprocess.Popen(['clang++', '-std=c++17', '-stdlib=libc++',
                               level] + DEFINES +
                              ['-c', s, '-o', os.path.join(d, obj_name(s))],
                              cwd=SRC) for s in sources()]
    if any(p.wait() for p in procs):
        sys.exit('stockfish: clang++ failed')
    link(d, [os.path.join(d, obj_name(s)) for s in sources()], exe)
    return exe


def link(d, objs, exe):
    # -no-pie: vcc's address of a libc function (tolower) is PC-relative even
    # with -fPIE, which ld rejects in a PIE
    p = subprocess.run(['clang++', '-stdlib=libc++', '-pthread', '-no-pie'] +
                       objs +
                       ['-o', exe], capture_output=True, text=True)
    if p.returncode:
        return p.stderr.strip().splitlines()[-1]
    return None


NODES = re.compile(r'^Nodes searched\s*:\s*(\d+)', re.M)
NPS = re.compile(r'^Nodes/second\s*:\s*(\d+)', re.M)


def nodes(exe, depth, speed=None):
    """(node count or None, error text) of exe's bench; speed gets its
    nodes per second"""
    try:
        p = subprocess.run([exe, 'bench', '16', '1', str(depth), 'default',
                            'depth'], cwd=SRC, capture_output=True, text=True,
                           timeout=600)
    except subprocess.TimeoutExpired:
        return None, 'timeout'
    m = NODES.search(p.stderr + p.stdout)
    if p.returncode or not m:
        return None, 'exit %d' % p.returncode
    n = NPS.search(p.stderr + p.stdout)
    if n and speed is not None:
        speed.append(int(n.group(1)))
    return int(m.group(1)), ''


def main():
    global OUT, CACHE
    ap = argparse.ArgumentParser()
    ap.add_argument('--arch', choices=('general', 'sse2'), default='general')
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('-O', dest='levels', default='2')
    ap.add_argument('--depth', type=int, default=6)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--mix', action='store_true')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?')
    a = ap.parse_args()
    if a.arch == 'sse2':
        DEFINES.extend(SSE2)
    else:
        OUT = os.path.join(OUT, 'general')
        CACHE = os.path.join(OUT, 'cache.json')
    vpp = os.path.join(a.bin_dir, 'vpp')
    if not os.path.exists(os.path.join(SRC, 'nn-1c0000000000.nnue')):
        print('stockfish: no %s with its networks (see this file\'s header)'
              % SRC)
        return 0
    cache = {}
    if os.path.exists(CACHE) and not a.force:
        with open(CACHE) as f:
            cache = json.load(f)
    failed = 0
    for level in ('-O' + c for c in a.levels):
        vdir = os.path.join(OUT, 'vcc' + level)
        os.makedirs(vdir, exist_ok=True)
        flags = ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++', '-isystem', LIBCXX,
                 level] + DEFINES
        results = {}
        for s in sources():
            if a.pattern and a.pattern not in s:
                continue
            out = os.path.join(vdir, obj_name(s))
            key = cj.key_of(vpp, os.path.join(SRC, s), flags)
            ck = level + ' ' + s
            r = cache.get(ck)
            if not r or r.get('key') != key or (
                    r['state'] == 'ok' and not os.path.exists(out)):
                r = compile_vcc(vpp, s, flags, out, a.timeout)
                r['key'] = key
                cache[ck] = r
                with open(CACHE, 'w') as f:
                    json.dump(cache, f, indent=1, sort_keys=True)
            results[s] = r
            if a.v:
                print('%-32s %s' % (s, cj.group_of(r)))
        ok = [s for s, r in results.items() if r['state'] == 'ok']
        analysed = sum(1 for r in results.values()
                       if r['state'] in ('ok', 'lowering'))
        print('stockfish %s: %d TUs: %d through the analysis, %d through -c'
              % (level, len(results), analysed, len(ok)))
        groups = {}
        for s, r in results.items():
            if r['state'] != 'ok':
                groups.setdefault(cj.group_of(r), []).append(s)
        for g, names in sorted(groups.items(),
                               key=lambda x: (-len(x[1]), x[0])):
            print('%4d  %s' % (len(names), g))
            if a.v:
                print('      ' + ' '.join(names))
        if not ok or a.pattern:
            continue
        cdir = os.path.join(OUT, 'clang' + level)
        fast, slow = [], []
        want, err = nodes(build_clang(level, cdir), a.depth, fast)
        if want is None:
            sys.exit('stockfish: clang++\'s build: ' + err)
        if a.mix:
            for s in ok:
                objs = [os.path.join(cdir, obj_name(t)) for t in sources()
                        if t != s] + [os.path.join(vdir, obj_name(s))]
                exe = os.path.join(vdir, 'mix-' + obj_name(s)[:-2])
                err = link(vdir, objs, exe)
                got = None
                if not err:
                    got, err = nodes(exe, a.depth)
                state = 'ok'
                if got != want:
                    state = 'FAIL'
                    failed += 1
                print('  %s mix %-28s %s' % (state, s, err or got))
        if len(ok) < len(results):
            continue
        exe = os.path.join(vdir, 'stockfish')
        err = link(vdir, [os.path.join(vdir, obj_name(s)) for s in sources()],
                   exe)
        got = None
        if not err:
            got, err = nodes(exe, a.depth, slow)
        if fast and slow:
            print('stockfish %s: %d nodes/s, clang++ %d'
                  % (level, slow[0], fast[0]))
        if got == want:
            print('stockfish %s: bench depth %d: %d nodes, as clang++\'s'
                  % (level, a.depth, got))
        else:
            failed += 1
            print('stockfish %s: bench depth %d: %s, clang++ %d nodes'
                  % (level, a.depth, err or '%d nodes' % got, want))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
