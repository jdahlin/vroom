#!/usr/bin/env python3
"""vcc-ctests.py: run vendored C compiler test suites through vcc.

  bench/vcc-ctests.py [-j N] [--suite gcc|tcc|vcc] [--fe] [--cpu QEMU_CPU] [-v] [--show CAT] [PAT]

Suites (ccs/vcc/tests/, see the README there):
  gcc  gcc.c-torture/execute: exits 0 on success, abort()s on failure
  tcc  tinycc tests2: prints output, compared with clang's (and tcc's .expect)
  vcc  our own regression tests (ccs/vcc/tests/vcc): exit 0 (or abort()) and
       print what clang's build prints.  Comments in a test steer the runner:
         /* vcc-flags: -std=c23 ... */   extra flags for both compilers
         /* vcc-noref: reason */         clang rejects it (a GNU extension): no
                                         reference, vcc must exit 0 silently
         /* vcc-threads-same */          -S output must be the same with
                                         VCC_THREADS=1 and 8 (four builds)
         /* vcc-timeout: S */            vcc's compile must finish in S seconds
         /* vcc-ref-timeout: S */        clang reference limit (1..600; default 60)
                                         (default 60): a bound on compile time
         /* vcc-xfail: TODO item */      known vcc bug: a failure is counted as
                                         xfail, a pass as XPASS (an error: drop
                                         the marker in the commit that fixes it)
       A clang build that does not exit 0 is a broken test (fail:test).

Exit status 1 when anything fails or XPASSes.

Every test is built twice: by clang (the oracle) and by vcc (vpp -c), each
linked with the bare-metal runtime in ccs/vcc/tests/rt (built by clang) and
run under qemu-system-aarch64 -M virt with semihosting.  vcc passes when its
binary prints what clang's prints and exits with the same status (0 for gcc).

Skips: tcc tests tied to tcc itself (flags, bounds checker, other arches),
tests clang rejects (clang-rejects), and tests whose clang build does not
pass on this runtime (ref-fail: no libc function, no long double runtime,
file I/O ...).  vcc failures are sorted by stage: fe (vpp -fsyntax-only),
fp (floating point the emitter rejects), emit, link, opt (an
undefined link_error/link_failure: the test checks that the optimizer removed
a call, and vcc has no such pass), run.

--fe stops after the front end (no code, no qemu): fe pass rate only.
Clang reference results are cached in build/ccs/ctests/ref (keyed by the
source, flags and runtime), so a rerun only builds and runs vcc's side.  A vcc object byte-identical
to one that passed before (build/ccs/ctests/pass, keyed by the object, the
runtime and the reference) is not linked or run again; --no-cache runs all.
"""
import hashlib, json, os, re, shutil, subprocess, sys, time
from collections import Counter, defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = f'{R}/ccs/vcc/tests'
RT = f'{T}/rt'
CL = llvm('clang')
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
OBJCOPY = llvm('llvm-objcopy')
LLD = os.environ.get('VCC_LD') or shutil.which('ld.lld') or llvm('ld.lld')	# VCC_LD=build/ccs/vlink: link with vlink
QEMU = shutil.which('qemu-system-aarch64') or '/opt/homebrew/bin/qemu-system-aarch64'
OUT = f'{BUILD}/ctests'
RES = subprocess.run([CL, '-print-resource-dir'], capture_output=True, text=True).stdout.strip() + '/include'

BASE = ['--target=aarch64-linux-gnu', '-nostdinc', '-I', f'{RT}/include', '-isystem', RES,
        '-fno-PIE', '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-O2',
        '-mno-outline-atomics']	# a Linux clang that finds libgcc calls __aarch64_ldadd4_acq_rel ..., which rt lacks
BASE += os.environ.get('VCC_CFLAGS', '').split()	# e.g. -fasynchronous-unwind-tables (both compilers)
# clang -O2, with gcc's leniency for old code (the tests are compiled by gcc -w)
CLFL = ['-w', '-Wno-error=implicit-function-declaration', '-Wno-error=implicit-int',
        '-Wno-error=int-conversion', '-Wno-error=incompatible-pointer-types', '-Wno-error=return-type',
        '-Wno-error=incompatible-function-pointer-types', '-fno-strict-aliasing', '-ffp-contract=off']
RTFL = ['--target=aarch64-linux-gnu', '-ffreestanding', '-fno-builtin', '-nostdinc', '-isystem', RES,
        '-fno-PIE', '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-O1']

# tcc tests2 that test tcc itself (see tcc-tests2/Makefile): run modes, -dt
# snippets, bounds checker, other targets, threads/TLS, several files, argv
# gcc torture tests whose output depends on the compiler, not on correctness
GCC_SKIP = {'return-addr': 'prints code addresses'}

TCC_SKIP = {
    '31_args': 'argv', '137_funcall_struct_args': 'void main: exit status undefined', '79_vla_continue': 'compares addresses of dead VLAs (clang and tcc differ)', '46_grep': 'argv, files', '34_array_assignment': 'not C (tcc skips it)',
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
    if suite in (None, 'vcc'):
        d = f'{T}/vcc'
        ts += [('vcc', f[:-2], f'{d}/{f}') for f in sorted(os.listdir(d)) if f.endswith('.c')]
    if pat:
        ts = [t for t in ts if re.search(pat, f'{t[0]}/{t[1]}')]
    return ts

DG = re.compile(r'\{\s*(dg-options|dg-additional-options)\s+(?P<brace>\{\s*)?"(?P<flags>[^"]*)"\s*(?(brace)\}\s*)(?P<selector>\{[^}]*\})?\s*\}')
def dg_flags(src):
    """options from dg-options/dg-additional-options that apply to aarch64-linux"""
    fl = []
    for m in DG.finditer(src):
        sel = m.group("selector")
        if sel and not re.search(r'aarch64|\*-\*-\*|lp64|int128|!', sel):
            continue
        fl += [x for x in m.group("flags").split() if not x.startswith(('-m', '-fno-tree', '-ftree', '-fdump'))]
    return fl

def sh(cmd, timeout=60, **kw):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, **kw)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        return 'timeout', e.stdout or b'', e.stderr or b''
    except OSError as e:
        return 'tool-error', b'', str(e).encode()


def reference_timeout(path):
    """Only explicitly marked heavy references get more than 60 seconds."""
    with open(path, errors='replace') as source:
        src = source.read()
    m = re.search(r'/\*\s*vcc-ref-timeout:\s*(\d+)\s*\*/', src)
    seconds = int(m.group(1)) if m else 60
    if not 1 <= seconds <= 600:
        raise ValueError('vcc-ref-timeout must be between 1 and 600 seconds')
    return seconds


def oracle_error(rc, err, stage, seconds):
    """Infrastructure failures must never become unsupported-source skips."""
    if isinstance(err, bytes):
        err = err.decode(errors='replace')
    reason = None
    if rc == 'timeout':
        reason = f'timed out after {seconds}s'
    elif rc == 'tool-error':
        reason = 'could not execute tool'
    elif isinstance(rc, int) and rc < 0:
        reason = f'terminated by signal {-rc}'
    elif rc and re.search(r'PLEASE submit a bug report|frontend command failed|unable to execute command', err):
        reason = f'tool failure (exit {rc})'
    elif rc and stage in ('link', 'assemble') and not err.strip():
        reason = f'tool failure (exit {rc}, no diagnostic)'
    elif rc and stage == 'compile' and 'error:' not in err:
        reason = f'tool failure (exit {rc}, no compiler diagnostic)'
    if reason:
        detail = first_err(err) if err.strip() else ''
        return {'error': 'oracle-' + stage,
                'why': f'{stage}: {reason}' + (': ' + detail if detail else '')}
    return None

def qemu(elf, cpu='max', diagnostics=False):
    rc, out, err = sh([QEMU, '-M', 'virt', '-cpu', cpu, '-m', '256M', '-nographic',
                       '-monitor', 'none', '-nic', 'none', '-semihosting', '-kernel', elf], timeout=20,
                      stdin=subprocess.DEVNULL)
    result = (rc, out.decode('latin-1').replace('\r\n', '\n'))
    return (*result, err) if diagnostics else result

VLINK = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vlink')
def link(objs, elf):
    args = ['-T', f'{RT}/link.ld', f'{OUT}/rt/start.o', f'{OUT}/rt/rt.o'] + objs
    rc, _, err = sh([LLD] + args + ['-o', elf])
    if not rc and os.environ.get('VCC_LDCMP'):
        # VCC_LDCMP=1: vlink must give lld's loaded image byte for byte
        rc2, _, err2 = sh([VLINK] + args + ['-o', elf + '.v'])
        if rc2:
            return rc2, 'error: vlink: ' + err2.decode(errors='replace')
        oc = llvm('llvm-objcopy')
        sh([oc, '-O', 'binary', elf, elf + '.lb']); sh([oc, '-O', 'binary', elf + '.v', elf + '.vb'])
        if open(elf + '.lb', 'rb').read() != open(elf + '.vb', 'rb').read():
            return 1, 'error: vlink image differs from lld'
        # and the program headers (PT_LOAD split, PT_TLS, PT_GNU_RELRO, ...)
        rd = llvm('llvm-readelf')
        ph = [[l.split() for l in subprocess.run([rd, '-lW', e], capture_output=True, text=True).stdout.splitlines()
               if l.strip()[:4] in ('LOAD', 'TLS ', 'GNU_', 'PHDR', 'NOTE')] for e in (elf, elf + '.v')]
        if ph[0] != ph[1]:
            return 1, 'error: vlink program headers differ from lld'
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

def exec_marks(src):
    """(flags, xfail reason or None) from /* vcc-flags: */ and /* vcc-xfail: */"""
    fl = [x for m in re.finditer(r'/\*\s*vcc-flags:(.*?)\*/', src) for x in m.group(1).split()]
    m = re.search(r'/\*\s*vcc-xfail:(.*?)\*/', src, re.S)
    return fl, (' '.join(m.group(1).split()) if m else None)

def reference(suite, name, path, flags, rthash, wd):
    with open(path, 'rb') as source:
        src = source.read().decode('latin-1')
    key = hashlib.sha1(('oracle-status-v2 ' + src + ' '.join(flags) + rthash + ' '.join(CLFL)).encode()).hexdigest()[:16]
    cf = f'{OUT}/ref/{suite}/{name}.json'
    try:
        with open(cf) as cache:
            r = json.load(cache)
        if r['key'] == key and 'error' not in r:
            return r
    except (OSError, ValueError, KeyError):
        pass
    r = {'key': key}
    if suite == 'vcc' and re.search(r'/\*\s*vcc-noref:', src):
        r['rc'] = 0; r['out'] = ''
        return r
    timeout = reference_timeout(path)
    rc, _, err = sh([CL] + BASE + CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=os.path.dirname(path), timeout=timeout)
    failure = oracle_error(rc, err, 'compile', timeout)
    if failure:
        return dict(r, **failure)
    if rc:
        r['skip'] = 'clang-rejects'; r['why'] = first_err(err.decode(errors='replace'))
    else:
        rc, err = link([f'{wd}/ref.o'], f'{wd}/ref.elf')
        failure = oracle_error(rc, err, 'link', 60)
        if failure:
            return dict(r, **failure)
        if rc:
            m = re.search(r'undefined symbol: (\S+)', err)
            r['skip'] = 'ref-fail'; r['why'] = 'link: ' + (m.group(1) if m else first_err(err))
        else:
            rc, out, err = qemu(f'{wd}/ref.elf', diagnostics=True)
            failure = oracle_error(rc, err, 'run', 20)
            if failure:
                return dict(r, **failure)
            r['rc'] = rc; r['out'] = out
            if suite in ('gcc', 'vcc') and rc != 0:
                r['skip'] = 'ref-fail'; r['why'] = f'clang build exits {rc}'
                if err.strip():
                    r['why'] += ': ' + first_err(err.decode(errors='replace'))
            elif suite == 'tcc':
                exp = f'{os.path.dirname(path)}/{name}.expect'
                r['expect_ok'] = os.path.exists(exp) and norm(open(exp, errors='replace').read()) == norm(out)
    os.makedirs(os.path.dirname(cf), exist_ok=True)
    with open(cf, 'w') as cache:
        json.dump(r, cache)
    return r

def one(t):
    t, st, cat, why = one_(t)
    if t[0] != 'vcc':
        return t, st, cat, why
    _, xf = exec_marks(open(t[2], 'rb').read().decode('latin-1'))
    if st == 'skip':	# our tests must run on clang: a skip is a broken test
        return t, 'fail', 'test', f'{cat}: {why}'
    if xf and st == 'fail':
        return t, 'xfail', cat, f'{xf} [{why}]'
    if xf and st == 'pass':
        return t, 'fail', 'xpass', f'passes now: remove vcc-xfail ({xf})'
    return t, st, cat, why

def one_(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    if suite == 'tcc' and name in TCC_SKIP:
        return t, 'skip', 'tcc-specific', TCC_SKIP[name]
    if suite == 'gcc' and name in GCC_SKIP:
        return t, 'skip', 'compiler-specific', GCC_SKIP[name]
    flags = dg_flags(src) if suite == 'gcc' else exec_marks(src)[0] if suite == 'vcc' else []
    m = re.search(r'/\*\s*vcc-timeout:\s*(\d+)\s*\*/', src) if suite == 'vcc' else None
    tmo = int(m.group(1)) if m else 60
    if name.startswith('76_dollars'):
        flags = ['-fdollars-in-identifiers']
    wd = f'{OUT}/w/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    cwd = os.path.dirname(path)
    vfl = BASE + CLFL + flags   # the same flags as clang (vpp ignores what it does not know)
    if FE_ONLY:
        timeout = reference_timeout(path)
        rc, _, err = sh([CL, '-fsyntax-only'] + BASE + CLFL + flags + [path], cwd=cwd, timeout=timeout)
        failure = oracle_error(rc, err, 'compile', timeout)
        if failure:
            return t, 'error', failure['error'], failure['why']
        if rc:
            return t, 'skip', 'clang-rejects', first_err(err.decode(errors='replace'))
        rc, _, err = sh([VPP, '-fsyntax-only'] + vfl + [path], cwd=cwd, timeout=tmo)
        return (t, 'pass', '', '') if rc == 0 else (t, 'fail', 'fe', first_err(err.decode(errors='replace')))
    ref = reference(suite, name, path, flags, RTHASH, wd)
    if 'error' in ref:
        return t, 'error', ref['error'], ref['why']
    if 'skip' in ref:
        return t, 'skip', ref['skip'], ref['why']
    rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd, timeout=tmo)
    if rc:
        rc2, _, err2 = sh([VPP, '-fsyntax-only'] + vfl + [path], cwd=cwd, timeout=tmo)
        if rc2:
            return t, 'fail', 'fe', first_err(err2.decode(errors='replace'))
        e = err.decode(errors='replace')
        cat = 'fp' if re.search(r'floating point|long double', e) else 'emit'
        return t, 'fail', cat, first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + first_err(e)
    if suite == 'vcc' and re.search(r'/\*\s*vcc-threads-same\s*\*/', src):
        outs = set()
        for k, n in enumerate(['1', '8', '8', '8']):
            rc, _, err = sh([VPP, '-S'] + vfl + [path, '-o', f'{wd}/vcc{k}.s'], cwd=cwd, env=dict(os.environ, VCC_THREADS=n))
            outs.add(open(f'{wd}/vcc{k}.s', 'rb').read() if not rc else b'')
        if len(outs) != 1:
            return t, 'fail', 'emit', 'assembly differs between VCC_THREADS=1 and 8'
    # an object byte-identical to one that passed before (same runtime, same
    # reference) passes again: no link, no qemu (they are ~90% of the time)
    pk = hashlib.sha1(open(f'{wd}/vcc.o', 'rb').read() + (RTHASH + ref['key'] + os.environ.get('VCC_QEMU_CPU', 'max')).encode()).hexdigest()
    pf = f'{OUT}/pass/{suite}/{name}'
    if not NOCACHE:
        try:
            if open(pf).read() == pk:
                shutil.rmtree(wd, ignore_errors=True)
                return t, 'pass', '', ''
        except OSError:
            pass
    rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc.elf')
    if rc:
        m = re.search(r'undefined symbol: (\S+)', err)
        if m and re.match(r'link_(error|failure)', m.group(1)):
            return t, 'fail', 'opt', 'undefined ' + m.group(1)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else first_err(err)
    rc, out = qemu(f'{wd}/vcc.elf', os.environ.get('VCC_QEMU_CPU', 'max'))
    if rc == ref['rc'] and out == ref['out']:
        os.makedirs(os.path.dirname(pf), exist_ok=True)
        open(pf, 'w').write(pk)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    why = f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    open(f'{wd}/vcc.out', 'w').write(out); open(f'{wd}/ref.out', 'w').write(ref['out'])
    return t, 'fail', 'run', why

def init(fe, rth, nocache=False):
    global FE_ONLY, RTHASH, NOCACHE
    FE_ONLY, RTHASH, NOCACHE = fe, rth, nocache

if __name__ == '__main__':
    av = [part for arg in sys.argv[1:]
          for part in (('-j', arg[2:]) if re.fullmatch(r'-j[0-9]+', arg) else (arg,))]
    j, suite, show, verbose = 6, None, None, False
    fe = '--fe' in av
    nocache = '--no-cache' in av
    av = [a for a in av if a not in ('--fe', '--no-cache')]
    if '-v' in av: verbose = True; av.remove('-v')
    if '--cpu' in av:	# vcc's binaries run on this qemu CPU (cortex-a53: no LSE); clang's on max
        k = av.index('--cpu'); os.environ['VCC_QEMU_CPU'] = av[k + 1]; del av[k:k + 2]
    for opt in ('-j', '--suite', '--show'):
        if opt in av:
            k = av.index(opt); v = av[k + 1]; del av[k:k + 2]
            if opt == '-j': j = int(v)
            elif opt == '--suite': suite = v
            else: show = v
    pat = av[0] if av else None
    rth = build_rt()
    ts = tests(suite, pat)
    if not ts:
        sys.exit('no tests matched the requested suite/pattern')
    t0 = time.time()
    with Pool(j, initializer=init, initargs=(fe, rth, nocache)) as pool:
        res = pool.map(one, ts, chunksize=1)
    by = defaultdict(Counter)
    whys = defaultdict(Counter)
    for (s, n, _), st, cat, why in res:
        by[s][st] += 1
        if verbose and st == 'xfail':
            print(f'XFAIL {cat:5s} {s}/{n}: {why}')
        if st != 'pass':
            by[s][f'{st}:{cat}'] += 1
            whys[(st, cat)][why] += 1
        if st in ('fail', 'error'):
            print(f'{st.upper()} {cat:5s} {s}/{n}: {why}')
        if show and st != 'pass' and cat == show:
            print(f'{st} {cat} {s}/{n}: {why}')
    for (st, cat), c in sorted(whys.items()):
        if st in ('fail', 'error') or verbose:
            print(f'--- {st} {cat}: {sum(c.values())}')
            for w, k in c.most_common(int(os.environ.get('SHOW', 8))):
                print(f'  {k:4d}  {w[:150]}')
    tot = Counter()
    for s in sorted(by):
        c = by[s]; tot.update(c)
    for s, c in sorted(by.items()) + [('all', tot)]:
        run = c['pass'] + c['fail'] + c['error']
        det = ' '.join(f'{k}={v}' for k, v in sorted(c.items()) if ':' in k)
        xf = f', {c["xfail"]} xfail' if c['xfail'] else ''
        print(f'{s:4s} {c["pass"]}/{run} pass ({100 * c["pass"] / max(run, 1):.1f}%){xf}, {c["skip"]} skipped   {det}')
    if suite in (None, 'tcc') and not fe:
        bad = [n for (s, n, _), st, cat, _ in res if s == 'tcc' and st not in ('skip', 'error')
               and not json.load(open(f'{OUT}/ref/tcc/{n}.json')).get('expect_ok')]
        if bad:
            print(f'note: clang+rt output differs from tcc .expect for {len(bad)}: {" ".join(bad[:12])}')
    print(f'{time.time() - t0:.1f} s')
    sys.exit(1 if tot['fail'] or tot['error'] else 0)
