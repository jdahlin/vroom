#!/usr/bin/env python3
"""
VIR's own tests (SPEC-vir.md 17; PLAN-vir.md step V1).

Every ccs/vcc/tests/vir/**/*.vir is a unit with directives in comments at the
top (`; PASSES: canon`), a line `; ---`, and the expected output of
`virtest FILE`: the unit printed, or the errors.  virtest runs from the test
directory (so the file names in syntax errors are stable).  A test passes
when
  - the standard output is exactly the expected text,
  - the exit status is 0, or for a file under bad/ exactly 1, and
  - for bad/RULE-name.vir (RULE: S1 .. N1) every error line carries that rule
    id and there is at least one.
(virtest itself parses its printed text again and prints it a second time:
print-parse-print must be identical, SPEC 11.1.)

  bench/vir-test.py [-j N] [-v] [--bin-dir DIR] [PATTERN]   -v: the diff of each failure
  bench/vir-test.py --bless PATTERN         rewrite the expected part of the matching tests
  bench/vir-test.py --mutate N [--seed S]   N random mutants of the test units: virtest must exit 0 or 1
                                            (never crash, hang or report a sanitizer error), alone and
                                            with --no-verify; run it with --bin-dir build/ccs-asan
"""
import difflib, os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = f'{R}/ccs/vcc/tests/vir'


def bin_dir():
    d = os.environ.get('VCC_BIN', f'{R}/build/ccs')
    if '--bin-dir' in sys.argv:
        k = sys.argv.index('--bin-dir'); d = sys.argv[k + 1]; del sys.argv[k:k + 2]
    return os.path.abspath(d)


VIRTEST = os.path.join(bin_dir(), 'virtest')


def tests(pat):
    out = []
    for root, _, files in os.walk(D):
        for f in files:
            if f.endswith('.vir'):
                rel = os.path.relpath(os.path.join(root, f), D)
                if not pat or pat in rel:
                    out.append(rel)
    return sorted(out)


def expected(rel):
    text = open(f'{D}/{rel}', encoding='utf-8', errors='surrogateescape', newline='').read()
    m = re.search(r'^; ---[ \r]*\n', text, re.M)
    return None if not m else text[m.end():]


def run(rel, *extra):
    env = dict(os.environ)
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    return subprocess.run([VIRTEST, *extra, rel], cwd=D, capture_output=True, text=True, errors='surrogateescape', env=env, timeout=120)


def selftest():
    env = dict(os.environ)
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    p = subprocess.run([VIRTEST, '--selftest'], capture_output=True, text=True, env=env, timeout=300)
    return None if p.returncode == 0 and p.stdout == 'selftest: ok\n' else f'status {p.returncode}\n{p.stdout}{p.stderr[-1500:]}'


def check(rel):
    """None if the test passes, else why."""
    if rel.startswith('selftest'):
        return selftest()
    exp = expected(rel)
    if exp is None:
        return 'no expected output: run bench/vir-test.py --bless ' + rel
    p = run(rel)
    bad = rel.startswith('bad/')
    if p.returncode < 0 or p.returncode > 2 or 'Sanitizer' in p.stderr or 'runtime error' in p.stderr:
        return f'virtest crashed (status {p.returncode}):\n{p.stderr[-2000:]}'
    if p.returncode != (1 if bad else 0):
        return f'exit status {p.returncode}, expected {1 if bad else 0}\n{p.stdout[-800:]}{p.stderr[-800:]}'
    m = re.match(r'bad/([A-Z]\d)-', rel)
    if m:
        lines = [l for l in p.stdout.splitlines() if l]
        ids = set(re.findall(r'^@\S+(?: b\d+(?:\.\d+)?)?: ([A-Z]\d): ', '\n'.join(lines), re.M))
        if ids != {m.group(1)}:
            return f'expected exactly the rule {m.group(1)}, got {sorted(ids)}\n{p.stdout[-800:]}'
    if p.stdout != exp:
        return 'output differs:\n' + ''.join(difflib.unified_diff(exp.splitlines(True), p.stdout.splitlines(True), 'expected', 'actual'))
    return None


WORDS = ['add', 'sub', 'br', 'brif', 'switch', 'ibr', 'asmgoto', 'ret', 'trap', 'call', 'proj', 'param', 'load.8u', 'store.4',
         'memcpy', 'va_arg', 'asm', 'b0', 'b1', 'b999', 'v0', 'v1', 'v99', '$0', '$7', '@f', '@x', '%s0', '!0', '&b1', ':', ',', '(', ')',
         '[', ']', '{', '}', '..', '...', '->', '=', '-1', '0', '4294967296', '18446744073709551615', '99999999999999999999',
         '0x1p+0', '0x1.8p-1074', 'bits:0x7ff0000000000000', ':i32', ':i64', ':f64', ':ptr', ':multi', 'nsw', 'exact', 'volatile',
         'align 4', 'align 3', 'tag 1', 'likely', 'loop', 'addr', 'slot', 'func', 'sig', 'asm', 'data', 'extern', 'mem 8 align 8',
         'sret', 'none', 'int:int:int:int', 'labels 99', 'outs(', 'ins(', 'clobber(', '"', '\\', '\n', '\x00', 'exposure', 'nofp']


def mutate(rng, text):
    b = bytearray(text.encode('utf-8', 'surrogateescape'))
    for _ in range(rng.choice([1, 1, 2, 3, 5])):
        if not b: break
        k = rng.randrange(7); i = rng.randrange(len(b))
        if k == 0: del b[i:i + rng.randrange(1, 9)]
        elif k == 1: b[i] = rng.randrange(256)
        elif k == 2: b[i:i] = rng.choice(WORDS).encode().replace(b'\\n', b'\n').replace(b'\\x00', b'\0') + b' '
        elif k == 3:
            ls = b.split(b'\n'); j = rng.randrange(len(ls)); ls.insert(rng.randrange(len(ls)), ls[j]); b = bytearray(b'\n'.join(ls))
        elif k == 4:
            ls = b.split(b'\n'); x, y = rng.randrange(len(ls)), rng.randrange(len(ls)); ls[x], ls[y] = ls[y], ls[x]; b = bytearray(b'\n'.join(ls))
        elif k == 5: del b[i:]
        else:
            ls = b.split(b'\n'); del ls[rng.randrange(len(ls))]; b = bytearray(b'\n'.join(ls))
    return bytes(b)


def mutate_all(n, seed, jobs):
    rng = random.Random(seed)
    srcs = []
    for rel in tests(''):
        t = open(f'{D}/{rel}', encoding='utf-8', errors='surrogateescape', newline='').read()
        m = re.search(r'^; ---[ \r]*\n', t, re.M)
        srcs.append(t[:m.start()] if m else t)
    work = tempfile.mkdtemp(prefix='vir-mutate-')
    cases = []
    for i in range(n):
        data = mutate(rng, rng.choice(srcs))
        path = f'{work}/m{i}.vir'
        open(path, 'wb').write(data)
        cases.append((path, rng.random() < 0.5))
    env = dict(os.environ)
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')

    def one(c):
        path, nov = c
        try:
            p = subprocess.run([VIRTEST] + (['--no-verify'] if nov else []) + [path], capture_output=True, errors='surrogateescape', env=env, timeout=60)
        except subprocess.TimeoutExpired:
            return path, 'timeout'
        err = p.stderr.decode('utf-8', 'replace') if isinstance(p.stderr, bytes) else p.stderr
        if p.returncode not in (0, 1) or 'Sanitizer' in err or 'runtime error' in err:
            return path, f'status {p.returncode}: {err[-600:]}'
        return path, None
    with ThreadPoolExecutor(jobs) as ex:
        bad = [r for r in ex.map(one, cases) if r[1]]
    for path, why in bad[:10]:
        print(f'FAIL {path}: {why}')
    print(f'vir-test --mutate: {n - len(bad)}/{n} survived (seed {seed}, inputs in {work})')
    if not bad:
        import shutil; shutil.rmtree(work)
    sys.exit(1 if bad else 0)


def main():
    a = sys.argv[1:]
    jobs = 6; verbose = False; bless = False; pat = ''; mut = 0; seed = 1
    while a:
        x = a.pop(0)
        if x == '-j': jobs = int(a.pop(0))
        elif x == '-v': verbose = True
        elif x == '--bless': bless = True
        elif x == '--mutate': mut = int(a.pop(0))
        elif x == '--seed': seed = int(a.pop(0))
        else: pat = x
    if mut:
        mutate_all(mut, seed, jobs)
    ts = tests(pat)
    if not os.path.exists(VIRTEST):
        sys.exit(f'{VIRTEST} not built: make -C ccs vcc')
    if bless:
        if not pat:
            sys.exit('--bless needs a PATTERN (a part of the test path)')
        for rel in ts:
            run(rel, '--bless')
            print('blessed', rel)
        return
    if not pat:
        ts = ts + ['selftest (the builder API)']
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(check, ts))
    fails = [(t, r) for t, r in zip(ts, res) if r]
    for t, r in fails:
        print(f'FAIL {t}')
        if verbose:
            print('    ' + r.replace('\n', '\n    '))
    print(f'vir-test: {len(ts) - len(fails)}/{len(ts)} passed' + (f', {len(fails)} failed' if fails else ''))
    sys.exit(1 if fails else 0)


main()
