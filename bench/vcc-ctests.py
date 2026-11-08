#!/usr/bin/env python3
"""vcc-ctests.py: run vendored C compiler test suites through vcc.

  bench/vcc-ctests.py [-j N] [--suite gcc|tcc] [--fe] [-v] [--show CAT] [PAT]

Suites (ccs/vcc/tests/, see the README there):
  gcc  gcc.c-torture/execute: exits 0 on success, abort()s on failure
  tcc  tinycc tests2: prints output, compared with clang's (and tcc's .expect)

Every test is built twice: by clang (the oracle) and by vcc (vpp -c), each
linked with the bare-metal runtime in ccs/vcc/tests/rt (built by clang) and
run under qemu-system-aarch64 -M virt with semihosting.  vcc passes when its
binary prints what clang's prints and exits with the same status (0 for gcc).

Skips: tcc tests tied to tcc itself (flags, bounds checker, other arches),
tests clang rejects (clang-rejects), and tests whose clang build does not
pass on this runtime (ref-fail: no libc function, no long double runtime,
file I/O ...).  vcc failures are sorted by stage: fe (vpp -fsyntax-only),
fp (floating point, not in the emitter yet), emit, link, run.

--fe stops after the front end (no code, no qemu): fe pass rate only.
Clang reference results are cached in build/ccs/ctests/ref (keyed by the
source, flags and runtime), so a rerun only builds and runs vcc's side.
"""
import hashlib, json, os, re, shutil, subprocess, sys, time
from collections import Counter, defaultdict
from multiprocessing import Pool

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/ccs/vcc/tests'
RT = f'{T}/rt'
CL = '/opt/homebrew/opt/llvm/bin/clang'
VPP = f'{R}/build/ccs/vpp'
OBJCOPY = '/opt/homebrew/opt/llvm/bin/llvm-objcopy'
LLD = shutil.which('ld.lld') or '/opt/homebrew/bin/ld.lld'
QEMU = shutil.which('qemu-system-aarch64') or '/opt/homebrew/bin/qemu-system-aarch64'
OUT = f'{R}/build/ccs/ctests'
RES = subprocess.run([CL, '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'

BASE = ['--target=aarch64-linux-gnu', '-nostdinc', '-I', f'{RT}/include', '-isystem', RES,
        '-fno-PIE', '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-O2']
# clang -O2, with gcc's leniency for old code (the tests are compiled by gcc -w)
CLFL = ['-w', '-Wno-error=implicit-function-declaration', '-Wno-error=implicit-int',
        '-Wno-error=int-conversion', '-Wno-error=incompatible-pointer-types', '-Wno-error=return-type',
        '-Wno-error=incompatible-function-pointer-types', '-fno-strict-aliasing', '-ffp-contract=off']
RTFL = ['--target=aarch64-linux-gnu', '-ffreestanding', '-fno-builtin', '-nostdinc', '-isystem', RES,
        '-fno-PIE', '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-O1']

# tcc tests2 that test tcc itself (see tcc-tests2/Makefile): run modes, -dt
# snippets, bounds checker, other targets, threads/TLS, several files, argv
TCC_SKIP = {
    '31_args': 'argv', '137_funcall_struct_args': 'void main: exit status undefined', '46_grep': 'argv, files', '34_array_assignment': 'not C (tcc skips it)',
    '60_errors_and_warnings': 'tcc -dt', '96_nodata_wanted': 'tcc -dt', '125_atomic_misc': 'tcc -dt',
    '128_run_atexit': 'tcc -dt', '139_arm64_errors': 'tcc -dt', '112_backtrace': 'tcc -b',
    '113_btdll': 'dll', '114_bound_signal': 'tcc -b', '115_bound_setjmp': 'tcc -b',
    '116_bound_setjmp2': 'tcc -b', '117_builtins': 'tcc -b', '126_bound_global': 'tcc -b',
    '132_bound_test': 'tcc -b', '98_al_ax_extend': 'i386', '99_fastcall': 'i386',
    '85_asm-outside-function': 'x86 asm', '127_asm_goto': 'x86 asm', '141_riscv_asm': 'riscv',
    '145_winarm64_interlocked': 'win32', '148_linker_symbols': 'shared libs',
    '106_versym': 'pthread', '124_atomic_counter': 'pthread', '144_tls': 'TLS', '146_tls_extern': 'TLS',
    '104_inline': 'two files', '120_alias': 'two files', '95_bitfields_ms': 'msvc layout',
}

def tests(suite, pat):
    ts = []
    if suite in (None, 'gcc'):
        d = f'{T}/gcc-torture/execute'
        ts += [('gcc', f[:-2], f'{d}/{f}') for f in sorted(os.listdir(d)) if f.endswith('.c')]
    if suite in (None, 'tcc'):
        d = f'{T}/tcc-tests2'
        ts += [('tcc', f[:-2], f'{d}/{f}') for f in sorted(os.listdir(d))
               if re.match(r'\d+_.*\.c$', f) and '+' not in f]
    if pat:
        ts = [t for t in ts if re.search(pat, f'{t[0]}/{t[1]}')]
    return ts

DG = re.compile(r'\{\s*(dg-options|dg-additional-options)\s+"([^"]*)"\s*(\{[^}]*\})?\s*\}')
def dg_flags(src):
    """options from dg-options/dg-additional-options that apply to aarch64-linux"""
    fl = []
    for m in DG.finditer(src):
        sel = m.group(3)
        if sel and not re.search(r'aarch64|\*-\*-\*|lp64|int128|!', sel):
            continue
        fl += [x for x in m.group(2).split() if not x.startswith(('-m', '-fno-tree', '-ftree', '-fdump'))]
    return fl

def sh(cmd, timeout=60, **kw):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, **kw)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        return 'timeout', e.stdout or b'', e.stderr or b''

def qemu(elf):
    rc, out, err = sh([QEMU, '-M', 'virt', '-cpu', 'cortex-a57', '-m', '256M', '-nographic',
                       '-monitor', 'none', '-semihosting', '-kernel', elf], timeout=20,
                      stdin=subprocess.DEVNULL)
    return rc, out.decode('latin-1').replace('\r\n', '\n')

def link(objs, elf):
    rc, _, err = sh([LLD, '-T', f'{RT}/link.ld', f'{OUT}/rt/start.o', f'{OUT}/rt/rt.o'] + objs + ['-o', elf])
    return rc, err.decode(errors='replace')

def first_err(s):
    for l in s.splitlines():
        if 'error' in l:
            return re.sub(r'^.*?error: ', '', l).strip()
    return s.strip().splitlines()[0] if s.strip() else '(no message)'

def build_rt():
    os.makedirs(f'{OUT}/rt', exist_ok=True)
    for src, o in (('rt.c', 'rt.o'), ('start.S', 'start.o')):
        if not os.path.exists(f'{OUT}/rt/{o}') or os.path.getmtime(f'{OUT}/rt/{o}') < os.path.getmtime(f'{RT}/{src}'):
            subprocess.run([CL] + RTFL + ['-c', f'{RT}/{src}', '-o', f'{OUT}/rt/{o}'], check=True)
            # weak, so that a test can define its own abs, div, ...
            subprocess.run([OBJCOPY, '--weaken', f'{OUT}/rt/{o}'], check=True)
    h = hashlib.sha1()
    for f in ('rt.o', 'start.o'):
        h.update(open(f'{OUT}/rt/{f}', 'rb').read())
    h.update(open(f'{RT}/link.ld', 'rb').read())
    for d, _, fs in sorted(os.walk(f'{RT}/include')):
        for f in sorted(fs):
            h.update(open(f'{d}/{f}', 'rb').read())
    return h.hexdigest()[:12]

def norm(s):
    # diff -b: trailing blanks and runs of blanks don't count
    return '\n'.join(re.sub(r'[ \t]+', ' ', l).rstrip() for l in s.rstrip('\n').split('\n'))

def reference(suite, name, path, flags, rthash, wd):
    key = hashlib.sha1((open(path, 'rb').read().decode('latin-1') + ' '.join(flags) + rthash + ' '.join(CLFL)).encode()).hexdigest()[:16]
    cf = f'{OUT}/ref/{suite}/{name}.json'
    try:
        r = json.load(open(cf))
        if r['key'] == key:
            return r
    except (OSError, ValueError, KeyError):
        pass
    r = {'key': key}
    rc, _, err = sh([CL] + BASE + CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=os.path.dirname(path))
    if rc:
        r['skip'] = 'clang-rejects'; r['why'] = first_err(err.decode(errors='replace'))
    else:
        rc, err = link([f'{wd}/ref.o'], f'{wd}/ref.elf')
        if rc:
            m = re.search(r'undefined symbol: (\S+)', err)
            r['skip'] = 'ref-fail'; r['why'] = 'link: ' + (m.group(1) if m else first_err(err))
        else:
            rc, out = qemu(f'{wd}/ref.elf')
            r['rc'] = rc; r['out'] = out
            if suite == 'gcc' and rc != 0:
                r['skip'] = 'ref-fail'; r['why'] = f'clang build exits {rc}'
            elif suite == 'tcc':
                exp = f'{os.path.dirname(path)}/{name}.expect'
                r['expect_ok'] = os.path.exists(exp) and norm(open(exp, errors='replace').read()) == norm(out)
    os.makedirs(os.path.dirname(cf), exist_ok=True)
    json.dump(r, open(cf, 'w'))
    return r

def one(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    if suite == 'tcc' and name in TCC_SKIP:
        return t, 'skip', 'tcc-specific', TCC_SKIP[name]
    flags = dg_flags(src) if suite == 'gcc' else []
    if name.startswith('76_dollars'):
        flags = ['-fdollars-in-identifiers']
    wd = f'{OUT}/w/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    cwd = os.path.dirname(path)
    vfl = BASE + CLFL + flags   # the same flags as clang (vpp ignores what it does not know)
    if FE_ONLY:
        rc, _, err = sh([CL, '-fsyntax-only'] + BASE + CLFL + flags + [path], cwd=cwd)
        if rc:
            return t, 'skip', 'clang-rejects', first_err(err.decode(errors='replace'))
        rc, _, err = sh([VPP, '-fsyntax-only'] + vfl + [path], cwd=cwd)
        return (t, 'pass', '', '') if rc == 0 else (t, 'fail', 'fe', first_err(err.decode(errors='replace')))
    ref = reference(suite, name, path, flags, RTHASH, wd)
    if 'skip' in ref:
        return t, 'skip', ref['skip'], ref['why']
    rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
    if rc:
        rc2, _, err2 = sh([VPP, '-fsyntax-only'] + vfl + [path], cwd=cwd)
        if rc2:
            return t, 'fail', 'fe', first_err(err2.decode(errors='replace'))
        e = err.decode(errors='replace')
        cat = 'fp' if re.search(r'floating point|long double', e) else 'emit'
        return t, 'fail', cat, first_err(e) if rc != 'timeout' and rc > 0 else f'vpp: signal/timeout {rc}: ' + first_err(e)
    rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc.elf')
    if rc:
        m = re.search(r'undefined symbol: (\S+)', err)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else first_err(err)
    rc, out = qemu(f'{wd}/vcc.elf')
    if rc == ref['rc'] and out == ref['out']:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    why = f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    open(f'{wd}/vcc.out', 'w').write(out); open(f'{wd}/ref.out', 'w').write(ref['out'])
    return t, 'fail', 'run', why

def init(fe, rth):
    global FE_ONLY, RTHASH
    FE_ONLY, RTHASH = fe, rth

if __name__ == '__main__':
    av = sys.argv[1:]
    j, suite, show, verbose = 6, None, None, False
    fe = '--fe' in av
    av = [a for a in av if a != '--fe']
    if '-v' in av: verbose = True; av.remove('-v')
    for opt in ('-j', '--suite', '--show'):
        if opt in av:
            k = av.index(opt); v = av[k + 1]; del av[k:k + 2]
            if opt == '-j': j = int(v)
            elif opt == '--suite': suite = v
            else: show = v
    pat = av[0] if av else None
    rth = build_rt()
    ts = tests(suite, pat)
    t0 = time.time()
    with Pool(j, initializer=init, initargs=(fe, rth)) as pool:
        res = pool.map(one, ts, chunksize=1)
    by = defaultdict(Counter)
    whys = defaultdict(Counter)
    for (s, n, _), st, cat, why in res:
        by[s][st] += 1
        if st != 'pass':
            by[s][f'{st}:{cat}'] += 1
            whys[(st, cat)][why] += 1
        if verbose and st == 'fail':
            print(f'FAIL {cat:5s} {s}/{n}: {why}')
        if show and st != 'pass' and cat == show:
            print(f'{st} {cat} {s}/{n}: {why}')
    for (st, cat), c in sorted(whys.items()):
        if st == 'fail' or verbose:
            print(f'--- {st} {cat}: {sum(c.values())}')
            for w, k in c.most_common(int(os.environ.get('SHOW', 8))):
                print(f'  {k:4d}  {w[:150]}')
    tot = Counter()
    for s in sorted(by):
        c = by[s]; tot.update(c)
    for s, c in sorted(by.items()) + [('all', tot)]:
        run = c['pass'] + c['fail']
        det = ' '.join(f'{k}={v}' for k, v in sorted(c.items()) if ':' in k)
        print(f'{s:4s} {c["pass"]}/{run} pass ({100 * c["pass"] / max(run, 1):.1f}%), {c["skip"]} skipped   {det}')
    if suite in (None, 'tcc') and not fe:
        bad = [n for (s, n, _), st, cat, _ in res if s == 'tcc' and st != 'skip'
               and not json.load(open(f'{OUT}/ref/tcc/{n}.json')).get('expect_ok')]
        if bad:
            print(f'note: clang+rt output differs from tcc .expect for {len(bad)}: {" ".join(bad[:12])}')
    print(f'{time.time() - t0:.1f} s')
