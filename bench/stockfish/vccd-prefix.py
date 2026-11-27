#!/usr/bin/env python3
"""vccd's C++ prefix workers (doc/PLAN-cxx-snapshot.md CS5, CS6) on a copy of
Stockfish: through the vcc shim and a private vccd, objects, depfiles and
diagnostics must be those of the same compile in one vpp process, which of
them a worker did, and after every change of an input.

  bench/stockfish/vccd-prefix.py [--bin-dir DIR] [--src DIR]
"""
import argparse, importlib.util, os, re, shutil, signal, subprocess, tempfile
import time
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location(
    'sf_prefix', os.path.join(R, 'bench/stockfish/prefix.py'))
sf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sf)


def read(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin-dir', default=os.path.join(R, 'build/linux/src'))
    ap.add_argument('--src', default=os.path.join(R, 'work/stockfish/src'))
    a = ap.parse_args()
    B = os.path.abspath(a.bin_dir)
    T = tempfile.mkdtemp(prefix='sf-vccd-prefix-')
    tree = os.path.join(T, 'src')
    shutil.copytree(a.src, tree,
                    ignore=shutil.ignore_patterns('*.nnue', '*.o'))
    os.makedirs(os.path.join(T, 'first'))
    prefix = os.path.join(T, 'prefix.h')
    with open(prefix, 'w') as f:
        f.write(''.join('#include <%s>\n' % n for n in sf.prefix_list(tree)))
        f.write('#include "types.h"\n#include <probe_me.h>\n')
    with open(os.path.join(tree, 'probe_me.h'), 'w') as f:
        f.write('#pragma once\ninline int probe_me() { return 1; }\n')
    flags = ['-x', 'c++', '-std=c++17', '-fPIE', '-nostdinc++', '-isystem',
             sf.LIBCXX, '-O2', '-DNDEBUG', '-DIS_64BIT', '-DUSE_PTHREADS',
             '-DNNUE_EMBEDDING_OFF', '-iquote', tree, '-I', 'first', '-I', '.',
             '-fcxx-prefix=' + prefix, '-c']
    os.makedirs(os.path.join(tree, 'first'))
    tus = sorted(os.path.relpath(os.path.join(d, f), tree)
                 for d, _, fs in os.walk(tree) for f in fs
                 if f.endswith('.cpp'))
    sock = os.path.join(T, 's')
    log = open(os.path.join(T, 'log'), 'w+')
    server = subprocess.Popen([B + '/vccd', '--sock', sock, '-j', '8'],
                              stderr=log, env=dict(os.environ, VCC_LOG='1'))
    while not os.path.exists(sock):
        time.sleep(0.02)
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)
            print('FAIL', what)

    def compile_one(s, extra, through):
        name = os.path.join(T, ('v-' if through else 'r-') +
                            s.replace('/', '_'))
        for end in ('.o', '.d'):
            if os.path.exists(name + end):
                os.unlink(name + end)
        # the same -MT on both sides: the depfiles can be compared
        args = flags + extra + ['-MD', '-MF', name + '.d', '-MT', 'T', s,
                                '-o', name + '.o']
        p = subprocess.run([B + ('/vcc' if through else '/vpp')] + args,
                           cwd=tree, capture_output=True,
                           env=dict(os.environ, VCC_SOCK=sock))
        return (p.returncode, p.stderr, read(name + '.o'), read(name + '.d'))

    def build(what, units=None, extra=(), workers=True):
        units = units or tus
        before = log_text()
        start = time.time()
        with ThreadPoolExecutor(8) as pool:
            got = list(pool.map(lambda s: compile_one(s, list(extra), True),
                                units))
        wall = time.time() - start
        with ThreadPoolExecutor(8) as pool:
            want = list(pool.map(lambda s: compile_one(s, list(extra), False),
                                 units))
        for s, g, w in zip(units, got, want):
            for k, part in enumerate(('status', 'diagnostics', 'object',
                                      'depfile')):
                check(g[k] == w[k], '%s: %s: %s differs' % (what, s, part))
        new = log_text()[len(before):]
        by = new.count('compiled by a worker')
        started = new.count('started')
        print('%-34s %2d units, %2d by a worker, %d started, %.2fs' %
              (what, len(units), by, started, wall))
        if workers:
            check(by == len(units), what + ': not all by a worker')
        return by, started

    def log_text():
        log.flush()
        with open(log.name) as f:
            return f.read()

    def edit(path, text):
        with open(os.path.join(tree, path), 'a') as f:
            f.write(text)

    build('cold')
    check(build('warm')[1] == 0, 'warm: started a worker')
    edit('types.h', '// edited\n')
    check(build('a prefix header edited')[1] == 1, 'edit: no new worker')
    with open(os.path.join(tree, 'first/probe_me.h'), 'w') as f:
        f.write('#pragma once\ninline int probe_me() { return 2; }\n')
    check(build('a header now shadows another')[1] == 1,
          'shadow: no new worker')
    check(build('another -D', extra=['-DOTHER'])[1] == 1,
          'key: no new worker')
    with open(os.path.join(tree, 'bad.cpp'), 'w') as f:
        f.write('int f() { return undeclared; }\n')
    build('an error in the unit', ['bad.cpp'], workers=False)
    edit('types.h', 'int broken = nothing;\n')
    build('an error in the prefix', tus[:3], workers=False)
    with open(os.path.join(tree, 'types.h')) as f:
        text = f.read().replace('int broken = nothing;\n', '')
    with open(os.path.join(tree, 'types.h'), 'w') as f:
        f.write(text)
    time.sleep(2.5)  # a broken prefix is tried again after 2 s
    build('the prefix mended', tus[:3])
    build('another key again', extra=['-DTHIRD'])
    for pid in re.findall(r'worker (\d+) started', log_text()):
        try:
            os.kill(int(pid), signal.SIGKILL)
        except OSError:
            pass
    check(build('workers killed', extra=['-DTHIRD'])[1] >= 1,
          'killed: no new worker')
    server.send_signal(signal.SIGTERM)
    server.wait()
    # (a sanitizer build: the server's reports are in its log; a worker's
    # stderr is /dev/null, so give the sanitizer a log_path to see its own)
    check(not re.search('Sanitizer|runtime error', log_text()),
          'a sanitizer report in vccd\'s log')
    shutil.rmtree(T)
    print('vccd-prefix: %s' % ('%d failures' % len(failed) if failed else 'ok'))
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
