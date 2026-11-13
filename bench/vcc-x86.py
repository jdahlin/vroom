#!/usr/bin/env python3
"""vcc-x86.py: the C test suites through vcc's x86-64 target (PLAN-x86.md).

  bench/vcc-x86.py [-j N] [--suite gcc|tcc|vcc] [-v] [--show CAT] [--no-cache] [PAT]

Every test of vcc-ctests.py (gcc torture, tcc tests2, our own) is built for
x86_64-linux-gnu twice: by clang (the oracle) and by vcc, each linked by
clang against this host's glibc and run natively (an x86-64 Linux host).
vcc passes when its program prints what clang's prints and exits the same
way. By default vcc's side uses `vpp -S`, assembled by clang; --obj uses
the direct `vpp -c` path, and --objcmp compares both with clang's assembler.  The test runtime headers (ccs/vcc/tests/rt/include) are used on
both sides, glibc behind them, with rt/wasm.c's __rt_assert.

Failures by stage: emit (vpp fails; "not supported yet" names the plan
step), as (clang rejects vcc's assembly), link, run.  Clang's results are
cached in build/.../x86tests/ref (keyed by the source and flags). A signal
from the native reference is an explicit ref-runtime-fail exclusion with its
signal and stderr recorded; compiler/linker failures and execution timeouts
remain fatal oracle errors. A vcc
assembly text byte-identical to one that passed before passes again
without being assembled, linked or run (--no-cache runs all).
"""
import hashlib, importlib.util, json, os, re, shutil, signal, subprocess, sys, time
from collections import Counter, defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD

_spec = importlib.util.spec_from_file_location('ctests', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-ctests.py'))
ct = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ct)

CL = ct.CL
VPP = ct.VPP
OUT = f'{BUILD}/x86tests'
TGT = '--target=x86_64-linux-gnu'
BASE = [TGT, '-nostdinc', '-I', f'{ct.RT}/include', '-isystem', ct.RES, '-fno-PIE', '-O2']
BASE += os.environ.get('VCC_CFLAGS', '').split()	# e.g. -g -fasynchronous-unwind-tables (both compilers)
LINK = [CL, TGT, '-no-pie']
RTO = f'{OUT}/rt.o'
# Header corrections must invalidate references built with the old ABI declarations.
_header_hash = hashlib.sha1()
for _directory, _, _files in sorted(os.walk(f'{ct.RT}/include')):
    for _file in sorted(_files):
        _path = os.path.join(_directory, _file)
        _header_hash.update(os.path.relpath(_path, ct.RT).encode())
        with open(_path, 'rb') as _header:
            _header_hash.update(_header.read())
HEADER_HASH = _header_hash.hexdigest()
# tests that need what the x86-64 target or glibc does not give the same way
X86_SKIP = {}

def sh(cmd, timeout=60, **kw):
    return ct.sh(cmd, timeout=timeout, **kw)

def rt_obj():
    """rt/wasm.c (__rt_assert), built by clang once per change"""
    src = f'{ct.RT}/wasm.c'
    if not os.path.exists(RTO) or os.path.getmtime(RTO) < os.path.getmtime(src):
        os.makedirs(OUT, exist_ok=True)
        subprocess.run([CL, TGT, '-O2', '-c', src, '-o', RTO + '.tmp'], check=True)
        os.replace(RTO + '.tmp', RTO)

# --pie / --pic: vcc's side compiled -fPIE / -fPIC and linked -pie (clang's
# reference stays the -fno-PIE build: the behaviour is the same)
PIC = None
OBJ = False	# --obj: vcc's side by vpp -c

def link(objs, exe, pie=False):
    rc, _, err = sh((LINK[:-1] + ['-pie'] if pie else LINK) + objs + [RTO, '-lm', '-o', exe], timeout=60)
    return rc, err.decode(errors='replace')

def run(exe, diagnostics=False):
    rc, out, err = sh([exe], timeout=20, stdin=subprocess.DEVNULL)
    if not diagnostics and isinstance(rc, int) and rc < 0:
        rc = 128 - rc	# a signal as the shell reports it (abort: 134)
    result = (rc, out.decode('latin-1'))
    return (*result, err) if diagnostics else result

def native_reference_failure(rc, err):
    """A native test is the program under test, not an oracle tool process.

    Compiler/linker, QEMU and Node signals still go through oracle_error.
    Only the directly executed native reference has this runtime outcome.
    """
    if not isinstance(rc, int) or rc >= 0:
        return None
    try:
        name = signal.Signals(-rc).name
    except ValueError:
        name = 'signal ' + str(-rc)
    detail = err.decode(errors='replace') if isinstance(err, bytes) else err
    why = f'native reference terminated by {name} (return code {rc})'
    if detail.strip():
        why += ': ' + ct.first_err(detail)
    return {'skip': 'ref-runtime-fail', 'why': why, 'phase': 'native-run',
            'rc': rc, 'signal': name, 'stderr': detail}


def reference(suite, name, path, flags, wd):
    with open(path, 'rb') as source:
        src = source.read().decode('latin-1')
    key = hashlib.sha1(('oracle-status-v3-native-runtime ' + src + ' '.join(flags) + ' '.join(ct.CLFL) + ' '.join(BASE + LINK) + 'rt1 ' + HEADER_HASH).encode()).hexdigest()[:16]
    cf = f'{OUT}/ref/{suite}/{name}.json'
    try:
        with open(cf) as cache:
            r = json.load(cache)
        if r['key'] == key and 'error' not in r:
            return r
    except (OSError, ValueError, KeyError):
        pass
    r = {'key': key}
    timeout = ct.reference_timeout(path)
    rc, _, err = sh([CL] + BASE + ct.CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=os.path.dirname(path), timeout=timeout)
    failure = ct.oracle_error(rc, err, 'compile', timeout)
    if failure:
        return dict(r, **failure)
    if rc:
        r['skip'] = 'clang-rejects'; r['why'] = ct.first_err(err.decode(errors='replace'))
    else:
        rc, err = link([f'{wd}/ref.o'], f'{wd}/ref')
        failure = ct.oracle_error(rc, err, 'link', 60)
        if failure:
            return dict(r, **failure)
        if rc:
            m = re.search(r'undefined (?:symbol|reference to) .?(\w+)', err)
            r['skip'] = 'ref-fail'; r['why'] = 'link: ' + (m.group(1) if m else ct.first_err(err))
        else:
            rc, out, err = run(f'{wd}/ref', diagnostics=True)
            runtime_failure = native_reference_failure(rc, err)
            if runtime_failure:
                r.update(runtime_failure)
                r['out'] = out
            else:
                failure = ct.oracle_error(rc, err, 'run', 20)
                if failure:
                    return dict(r, **failure)
                r['rc'] = rc; r['out'] = out
                if suite in ('gcc', 'vcc') and rc != 0:
                    r['skip'] = 'ref-fail'; r['why'] = f'clang build exits {rc}'
    os.makedirs(os.path.dirname(cf), exist_ok=True)
    with open(cf, 'w') as cache:
        json.dump(r, cache)
    return r

def one(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    if suite == 'tcc' and name in ct.TCC_SKIP and ct.TCC_SKIP[name] != 'x86 asm':	# (those run here)
        return t, 'skip', 'tcc-specific', ct.TCC_SKIP[name]
    if suite == 'gcc' and name in ct.GCC_SKIP:
        return t, 'skip', 'compiler-specific', ct.GCC_SKIP[name]
    if name in X86_SKIP:
        return t, 'skip', 'x86', X86_SKIP[name]
    flags = ct.dg_flags(src) if suite == 'gcc' else ct.exec_marks(src)[0] if suite == 'vcc' else []
    if name.startswith('76_dollars'):
        flags = ['-fdollars-in-identifiers']
    if suite == 'vcc' and re.search(r'/\*\s*vcc-noref:', src):
        return t, 'skip', 'noref', 'no clang reference'
    wd = f'{OUT}/w/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    cwd = os.path.dirname(path)
    ref = reference(suite, name, path, flags, wd)
    if 'error' in ref:
        return t, 'error', ref['error'], ref['why']
    if 'skip' in ref:
        return t, 'skip', ref['skip'], ref['why']
    vfl = [PIC if a == '-fno-PIE' and PIC else a for a in BASE] + ct.CLFL + flags
    if OBJ:
        # --obj: vpp -c (vcc's assembler and object writer), linked and run the same way
        rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
        if rc:
            e = err.decode(errors='replace')
            return t, 'fail', 'emit', ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
        open(f'{wd}/vcc.s', 'wb').write(open(f'{wd}/vcc.o', 'rb').read())	# (the pass key: the object's bytes)
    else:
        rc, _, err = sh([VPP, '-S'] + vfl + [path, '-o', f'{wd}/vcc.s'], cwd=cwd)
    if rc:
        e = err.decode(errors='replace')
        return t, 'fail', 'emit', ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
    pk = hashlib.sha1(open(f'{wd}/vcc.s', 'rb').read() + ref['key'].encode()).hexdigest()
    pf = f'{OUT}/pass{PIC or ""}{"-obj" if OBJ else ""}/{suite}/{name}'
    if not NOCACHE:
        try:
            if open(pf).read() == pk:
                shutil.rmtree(wd, ignore_errors=True)
                return t, 'pass', '', ''
        except OSError:
            pass
    rc, _, err = (0, b'', b'') if OBJ else sh([CL, TGT, '-c', f'{wd}/vcc.s', '-o', f'{wd}/vcc.o'])
    if rc:
        m = re.search(r'error: (.*)', err.decode(errors='replace'))
        return t, 'fail', 'as', re.sub(r'\d+', 'N', m.group(1)) if m else ct.first_err(err.decode(errors='replace'))
    rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc', pie=bool(PIC))
    if rc:
        m = re.search(r'undefined (?:symbol|reference to) .?(\w+)', err)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else ct.first_err(err)
    rc, out = run(f'{wd}/vcc')
    if rc == ref['rc'] and out == ref['out']:
        os.makedirs(os.path.dirname(pf), exist_ok=True)
        open(pf, 'w').write(pk)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    why = f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    open(f'{wd}/vcc.out', 'w').write(out); open(f'{wd}/ref.out', 'w').write(ref['out'])
    return t, 'fail', 'run', why

NOCACHE = False

# --fpcheck: every test compiled with -mgeneral-regs-only; a float use must fail cleanly
# (an error, not a crash), and the assembly of what compiles must name no vector or x87
# register nor an SSE/x87 instruction (the kernel traps on them)
FPREG = re.compile(r'%(?:[xyz]mm\d+|st\b|st\(\d\)|mm\d)')
FPOP = re.compile(r'^\t(?:f[a-z]+|(?:v?)(?:mov(?:ss|sd|aps|apd|ups|upd|dq[au]|q2dq)|cvt\w+|u?comis[sd]|(?:add|sub|mul|div|xor|and|or|sqrt|min|max)[ps][sd]|p(?:xor|and|or|add|sub|cmp|unpck|shuf|mov[dq]|ins|ext)\w*|ldmxcsr|stmxcsr))\b')

# --objcmp: the generated .s assembled by vasm and by clang -c, the objects compared byte
# for byte (vcc's assembler and object writer against LLVM MC, PLAN-x86.md X5/X6)
VASM = os.path.join(os.path.dirname(VPP), 'vasm')

def objcmp(t):
    suite, name, path = t
    wd = f'{OUT}/oc/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    src = open(path, 'rb').read().decode('latin-1')
    flags = ct.dg_flags(src) if suite == 'gcc' else ct.exec_marks(src)[0] if suite == 'vcc' else []
    rc, _, err = sh([VPP, '-S'] + BASE + ct.CLFL + flags + [path, '-o', f'{wd}/vcc.s'], cwd=os.path.dirname(path))
    if rc:
        return t, 'skip', 'emit', ct.first_err(err.decode(errors='replace'))
    rc, _, err = sh([CL, TGT, '-c', f'{wd}/vcc.s', '-o', f'{wd}/ref.o'])
    failure = ct.oracle_error(rc, err, 'assemble', 60)
    if failure:
        return t, 'error', failure['error'], failure['why']
    if rc:
        return t, 'skip', 'clang-as', ct.first_err(err.decode(errors='replace'))
    rc, _, err = sh([VASM, TGT, f'{wd}/vcc.s', '-o', f'{wd}/vas.o'])
    if rc:
        e = err.decode(errors='replace')
        m = re.search(r'error: (.*)', e)
        return t, 'fail', 'vasm', (m.group(1) if m else ct.first_err(e))[:120]
    a, b = open(f'{wd}/ref.o', 'rb').read(), open(f'{wd}/vas.o', 'rb').read()
    if a != b:
        return t, 'fail', 'differs', f'{len(a)} vs {len(b)} bytes'
    rc, _, err = sh([VPP, '-c'] + BASE + ct.CLFL + flags + [path, '-o', f'{wd}/direct.o'], cwd=os.path.dirname(path))
    if rc:
        return t, 'fail', 'direct', ct.first_err(err.decode(errors='replace'))
    if open(f'{wd}/direct.o', 'rb').read() != b:
        return t, 'fail', 'direct-differs', 'record path differs from text assembly'
    shutil.rmtree(wd, ignore_errors=True)
    return t, 'pass', '', ''

# --vlink: the bare runtime (tests/rt: rt.c, start-x86.S, link-x86.ld, libgcc), static: clang's
# object linked by lld is the reference; vcc's (vpp -c) is linked by lld and by vlink, whose
# loaded images and program headers must be identical, and vlink's program is run
# (PLAN-x86.md X7, as VCC_LDCMP for arm64)
RTO86 = f'{OUT}/rt86'
LIBGCC = subprocess.run(['gcc', '-print-libgcc-file-name'], capture_output=True, text=True).stdout.strip() if shutil.which('gcc') else ''
VLINK = os.path.join(os.path.dirname(VPP), 'vlink')
LLD = os.path.join(os.path.dirname(CL), 'ld.lld')

def rt86():
    global RTO86
    if PIC:     # --vlink --pie/--pic: a static PIE (start-x86.S applies its R_X86_64_RELATIVE)
        RTO86 = f'{OUT}/rt86-pie'
    os.makedirs(RTO86, exist_ok=True)
    for src, o in (('rt.c', 'rt.o'), ('start-x86.S', 'start.o')):
        if not os.path.exists(f'{RTO86}/{o}') or os.path.getmtime(f'{RTO86}/{o}') < os.path.getmtime(f'{ct.RT}/{src}'):
            subprocess.run([CL, TGT, '-O2', PIC or '-fno-PIE', '-ffreestanding', '-fno-builtin', '-I', f'{ct.RT}/include', '-c',
                            f'{ct.RT}/{src}', '-o', f'{RTO86}/{o}'], check=True)
            subprocess.run([os.path.join(os.path.dirname(CL), 'llvm-objcopy'), '--weaken', f'{RTO86}/{o}'], check=True)

def link86(ld, objs, exe):
    rc, _, err = sh([ld, '-static'] + (['-pie', '--no-dynamic-linker'] if PIC else []) + ['-T', f'{ct.RT}/link-x86.ld', f'{RTO86}/start.o'] + objs + [f'{RTO86}/rt.o', LIBGCC, '-o', exe], timeout=60)
    return rc, err.decode(errors='replace')

def vlink1(t):
    suite, name, path = t
    if suite == 'tcc' and name in ct.TCC_SKIP and ct.TCC_SKIP[name] != 'x86 asm':
        return t, 'skip', 'tcc-specific', ct.TCC_SKIP[name]
    if suite == 'gcc' and name in ct.GCC_SKIP:
        return t, 'skip', 'compiler-specific', ct.GCC_SKIP[name]
    wd = f'{OUT}/vl/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    cwd = os.path.dirname(path)
    src = open(path, 'rb').read().decode('latin-1')
    flags = ct.dg_flags(src) if suite == 'gcc' else ct.exec_marks(src)[0] if suite == 'vcc' else []
    fl = [PIC if a == '-fno-PIE' and PIC else a for a in BASE] + ct.CLFL + flags
    timeout = ct.reference_timeout(path)
    rc, _, err = sh([CL] + fl + ['-c', path, '-o', f'{wd}/ref.o'], cwd=cwd, timeout=timeout)
    failure = ct.oracle_error(rc, err, 'compile', timeout)
    if failure:
        return t, 'error', failure['error'], failure['why']
    if rc:
        return t, 'skip', 'clang-rejects', ct.first_err(err.decode(errors='replace'))
    rc, err = link86(LLD, [f'{wd}/ref.o'], f'{wd}/ref')
    failure = ct.oracle_error(rc, err, 'link', 60)
    if failure:
        return t, 'error', failure['error'], failure['why']
    if rc:
        m = re.search(r'undefined symbol: (\S+)', err)
        return t, 'skip', 'ref-fail', 'link: ' + (m.group(1) if m else ct.first_err(err))
    rrc, rout, err = run(f'{wd}/ref', diagnostics=True)
    runtime_failure = native_reference_failure(rrc, err)
    if runtime_failure:
        return t, 'skip', runtime_failure['skip'], runtime_failure['why']
    failure = ct.oracle_error(rrc, err, 'run', 20)
    if failure:	# (the bare static runtime: a reference that hangs there is no vlink question)
        return t, 'skip', 'ref-fail', 'bare runtime: ' + failure['why']
    if suite in ('gcc', 'vcc') and rrc != 0:
        return t, 'skip', 'ref-fail', f'clang build exits {rrc}'
    rc, _, err = sh([VPP, '-c'] + fl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
    if rc:
        return t, 'skip', 'emit', ct.first_err(err.decode(errors='replace'))
    rc, err = link86(LLD, [f'{wd}/vcc.o'], f'{wd}/vcc.lld')
    if rc:
        return t, 'skip', 'lld', ct.first_err(err)
    rc, err = link86(VLINK, [f'{wd}/vcc.o'], f'{wd}/vcc.vl')
    if rc:
        return t, 'fail', 'vlink', ct.first_err(err)[:120]
    oc = os.path.join(os.path.dirname(CL), 'llvm-objcopy')
    sh([oc, '-O', 'binary', f'{wd}/vcc.lld', f'{wd}/l.bin']); sh([oc, '-O', 'binary', f'{wd}/vcc.vl', f'{wd}/v.bin'])
    if open(f'{wd}/l.bin', 'rb').read() != open(f'{wd}/v.bin', 'rb').read():
        return t, 'fail', 'image', 'loaded image differs from lld'
    rd = os.path.join(os.path.dirname(CL), 'llvm-readelf')
    ph = [[l.split() for l in subprocess.run([rd, '-lW', e], capture_output=True, text=True).stdout.splitlines()
           if l.strip()[:4] in ('LOAD', 'TLS ', 'GNU_', 'PHDR', 'NOTE')] for e in (f'{wd}/vcc.lld', f'{wd}/vcc.vl')]
    if ph[0] != ph[1]:
        return t, 'fail', 'phdrs', 'program headers differ from lld'
    rc, out = run(f'{wd}/vcc.vl')
    if rc != rrc or out != rout:
        return t, 'fail', 'run', f'exit {rc} (clang {rrc})' if rc != rrc else 'output differs'
    shutil.rmtree(wd, ignore_errors=True)
    return t, 'pass', '', ''

def fpcheck(t):
    suite, name, path = t
    wd = f'{OUT}/fp/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    src = open(path, 'rb').read().decode('latin-1')
    flags = ct.dg_flags(src) if suite == 'gcc' else ct.exec_marks(src)[0] if suite == 'vcc' else []
    rc, _, err = sh([VPP, '-S'] + BASE + ct.CLFL + flags + ['-mgeneral-regs-only', path, '-o', f'{wd}/vcc.s'], cwd=os.path.dirname(path))
    if rc:
        e = err.decode(errors='replace')
        return t, ('pass' if isinstance(rc, int) and 0 < rc < 128 else 'fail'), 'rejects', ct.first_err(e)
    bad = []
    for ln in open(f'{wd}/vcc.s', errors='replace'):
        if FPREG.search(ln) or FPOP.match(ln):
            bad.append(ln.strip())
    shutil.rmtree(wd, ignore_errors=True)
    if bad:
        return t, 'fail', 'fp', bad[0]
    return t, 'pass', 'clean', ''

def init(nocache):
    global NOCACHE
    NOCACHE = nocache

if __name__ == '__main__':
    av = [part for arg in sys.argv[1:]
          for part in (('-j', arg[2:]) if re.fullmatch(r'-j[0-9]+', arg) else (arg,))]
    j, suite, show, verbose = 6, None, None, False
    nocache = '--no-cache' in av
    av = [a for a in av if a != '--no-cache']
    if '-v' in av: verbose = True; av.remove('-v')
    fpmode = '--fpcheck' in av
    ocmode = '--objcmp' in av
    vlmode = '--vlink' in av
    av = [a for a in av if a not in ('--fpcheck', '--objcmp', '--vlink')]
    for o, f in (('--pie', '-fPIE'), ('--pic', '-fPIC')):
        if o in av: PIC = f; av.remove(o)
    if vlmode:
        rt86()
    if '--obj' in av: OBJ = True; av.remove('--obj')
    for opt in ('-j', '--suite', '--show'):
        if opt in av:
            k = av.index(opt); v = av[k + 1]; del av[k:k + 2]
            if opt == '-j': j = int(v)
            elif opt == '--suite': suite = v
            else: show = v
    pat = av[0] if av else None
    ts = ct.tests(suite, pat)
    if not ts:
        sys.exit('no tests matched the requested suite/pattern')
    t0 = time.time()
    rt_obj()
    with Pool(j, initializer=init, initargs=(nocache,)) as pool:
        res = pool.map(fpcheck if fpmode else objcmp if ocmode else vlink1 if vlmode else one, ts, chunksize=1)
    by = defaultdict(Counter)
    whys = defaultdict(Counter)
    for (s, n, _), st, cat, why in res:
        by[s][st] += 1
        if st != 'pass' or fpmode:
            by[s][f'{st}:{cat}'] += 1
            whys[(st, cat)][why] += 1
        if verbose and st in ('fail', 'error'):
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
        tot.update(by[s])
    for s, c in sorted(by.items()) + [('all', tot)]:
        run_ = c['pass'] + c['fail'] + c['error']
        det = ' '.join(f'{k}={v}' for k, v in sorted(c.items()) if ':' in k)
        print(f'{s:4s} {c["pass"]}/{run_} pass ({100 * c["pass"] / max(run_, 1):.1f}%), {c["skip"]} skipped   {det}')
    print(f'{time.time() - t0:.1f} s')
    sys.exit(1 if tot['fail'] or tot['error'] else 0)
