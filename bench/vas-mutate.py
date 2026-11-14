#!/usr/bin/env python3
"""vas-mutate.py: vasm on mutated assembly never crashes (run it with an ASan build).

  bench/vas-mutate.py [-n N] [--seed S] [--target T] [--bin-dir DIR] [FILE.s...]

Default inputs: src/test/vas-obj/win64-*.s and --target x86_64-w64-mingw32.
Each mutant changes one to three lines of an input (a line deleted,
duplicated or moved; a token replaced by another of the file, a number by an
extreme one, bytes flipped, a line cut short) and goes through
`vasm --target=T`.  A mutant passes when vasm exits 0 or 1 with no sanitizer
report; anything else (a signal, an ASan/UBSan report) is printed with the
mutant saved under build/.../vas-mutate/; a mutant still running after 10 s
(a .rept or .space of 2^32: long in clang too) is counted as slow.  With
--bin-dir build/linux/src-asan (VCC_BIN), ASAN_OPTIONS=detect_leaks=0 and
UBSAN_OPTIONS=halt_on_error=1 are set.
"""
import argparse, glob, os, random, re, subprocess, sys, tempfile
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

NUMS = ['0', '-1', '4294967296', '0x7fffffffffffffff', '-9223372036854775808', '65536', '255', '256', '17', '31', '32', '8193']
JUNK = ['@', '%', '"', ',', '(', ')', '$', ';', '\\', '\x00', '\xff', '.L', '@feat.00', '.seh_endproc', '.seh_proc f', '.section .x,"dr",associative,zz']


def mutate(rng, lines):
    ls = list(lines)
    for _ in range(rng.randint(1, 3)):
        if not ls:
            break
        i = rng.randrange(len(ls))
        k = rng.randrange(8)
        toks = re.findall(r'[\w.$@%]+|\S', ls[i])
        if k == 0:
            del ls[i]
        elif k == 1:
            ls.insert(rng.randrange(len(ls) + 1), ls[i])
        elif k == 2:
            ls.insert(rng.randrange(len(ls) + 1), ls.pop(i))
        elif k == 3 and toks:
            other = re.findall(r'[\w.$@%]+', ls[rng.randrange(len(ls))]) or ['x']
            t = rng.choice(toks)
            ls[i] = ls[i].replace(t, rng.choice(other), 1)
        elif k == 4:
            ls[i] = re.sub(r'-?\b\d+\b', lambda m: rng.choice(NUMS), ls[i], count=1)
        elif k == 5:
            s = list(ls[i])
            for _ in range(rng.randint(1, 3)):
                if s:
                    s[rng.randrange(len(s))] = chr(rng.randrange(1, 256))
            ls[i] = ''.join(s)
        elif k == 6:
            ls[i] = ls[i][:rng.randrange(len(ls[i]) + 1)]
        else:
            ls.insert(i, '\t' + rng.choice(JUNK) + ' ' + rng.choice(NUMS))
    return ls


def one(job):
    path, seed, vas, target, keep = job
    rng = random.Random(seed)
    text = open(path, encoding='latin-1').read().split('\n')
    m = '\n'.join(mutate(rng, text)) + '\n'
    with tempfile.TemporaryDirectory() as t:
        s = f'{t}/m.s'
        open(s, 'w', encoding='latin-1').write(m)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        try:
            p = subprocess.run([vas, f'--target={target}', s, '-o', f'{t}/m.o'], capture_output=True, timeout=10, env=env)
            rc, err = p.returncode, p.stderr.decode('latin-1')
        except subprocess.TimeoutExpired:
            rc, err = 'timeout', ''
    if rc in (0, 1) and 'Sanitizer' not in err and 'runtime error' not in err:
        return None
    if rc == 'timeout':
        return 'slow'
    os.makedirs(keep, exist_ok=True)
    out = f'{keep}/{os.path.basename(path)}.{seed}.s'
    open(out, 'w', encoding='latin-1').write(m)
    return f'{out}: exit {rc}: ' + (err.strip().splitlines() or [''])[-1][:200]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', type=int, default=2000)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--target', default='x86_64-w64-mingw32')
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', BUILD))
    ap.add_argument('files', nargs='*')
    a = ap.parse_args()
    files = a.files or sorted(glob.glob(f'{R}/src/test/vas-obj/win64-*.s'))
    vas = os.path.join(os.path.abspath(a.bin_dir), 'vasm')
    keep = f'{BUILD}/vas-mutate'
    jobs = [(files[i % len(files)], a.seed * 1000003 + i, vas, a.target, keep) for i in range(a.n)]
    with Pool(a.j) as p:
        res = [r for r in p.map(one, jobs, chunksize=16) if r]
    bad = [r for r in res if r != 'slow']
    for b in bad:
        print('CRASH', b)
    print(f'vas-mutate: {a.n - len(res)}/{a.n} mutants handled cleanly, {len(res) - len(bad)} slow (over 10 s), '
          f'{len(bad)} crashed ({len(files)} inputs, {a.target})')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
