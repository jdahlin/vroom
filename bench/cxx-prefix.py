#!/usr/bin/env python3
"""cxx-prefix.py: vpp's -fcxx-prefix=P (the front end stops after P, before the
main file) against -include P over src/test/cxx/run: the same exit status,
diagnostics and object, byte for byte.  P includes C headers of the test runtime.

  bench/cxx-prefix.py [-j N] [--bin-dir DIR] [-v] [PAT]
"""
import argparse, importlib.util, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(R, 'src/test/cxx/run')
spec = importlib.util.spec_from_file_location(
    'cxx_run', os.path.join(R, 'bench/cxx-run.py'))
cxx_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cxx_run)
PREFIX = ''.join('#include <%s>\n' % h for h in
                 'stddef.h stdint.h stdio.h string.h stdlib.h'.split())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build/linux/src'))
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    a = ap.parse_args()
    vpp = os.path.abspath(os.path.join(a.bin_dir, 'vpp'))
    T = tempfile.mkdtemp(prefix='cxx-prefix-')
    prefix = os.path.join(T, 'prefix.h')
    with open(prefix, 'w') as f:
        f.write(PREFIX)

    def one(name):
        path = os.path.join(TESTS, name)
        with open(path) as f:
            extra = re.findall(r'cxx-run-flags:(.*?)\*/', f.read())
        flags = cxx_run.CXX + ['-O1', '-c'] + ' '.join(extra).split()
        got = []
        for k, how in enumerate((['-include', prefix],
                                 ['-fcxx-prefix=' + prefix])):
            obj = os.path.join(T, '%s.%d.o' % (name, k))
            p = subprocess.run([vpp] + flags + how + [path, '-o', obj],
                               capture_output=True, timeout=60)
            data = b''
            if os.path.exists(obj):
                with open(obj, 'rb') as f:
                    data = f.read()
                os.unlink(obj)
            got.append((p.returncode, p.stderr, data))
        differs = [w for w, x, y in zip(('status', 'diagnostics', 'object'),
                                        *got) if x != y]
        return name, differs, got

    names = sorted(f for f in os.listdir(TESTS)
                   if f.endswith('.cpp') and re.search(a.pattern, f))
    with ThreadPoolExecutor(a.j) as pool:
        results = list(pool.map(one, names))
    bad = [r for r in results if r[1]]
    for name, differs, got in bad[:20]:
        print('%s: %s differ' % (name, ', '.join(differs)))
        if a.v:
            print(got[0][1].decode()[:400], '---', got[1][1].decode()[:400])
    compiled = sum(1 for r in results if not r[2][0][0])
    # src/test/cxx/prefix: NAME.cpp on NAME.h as its prefix, whose headers the
    # stop must not change; each compiles as with -include
    cases = os.path.join(R, 'src/test/cxx/prefix')
    for name in sorted(os.listdir(cases)):
        if not name.endswith('.cpp'):
            continue
        for how in (['-include'], ['-fcxx-prefix=']):
            h = os.path.join(cases, name[:-4] + '.h')
            if how == ['-include']:
                how = ['-include', h]
            else:
                how = ['-fcxx-prefix=' + h]
            p = subprocess.run([vpp] + cxx_run.CXX + ['-c'] + how +
                               [os.path.join(cases, name), '-o',
                                os.path.join(T, 'case.o')],
                               capture_output=True, text=True, timeout=60)
            if p.returncode:
                print('%s %s: %s' % (name, how[0][:12], p.stderr[:300]))
                bad.append((name, ['failed'], None))
    print('cxx-prefix: %d programs (%d compile), %d differ' %
          (len(results), compiled, len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
