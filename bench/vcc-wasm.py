#!/usr/bin/env python3
"""vcc-wasm.py: the C test suites through vcc's WebAssembly target (PLAN-wasm.md).

  bench/vcc-wasm.py [-j N] [--suite gcc|tcc|vcc] [-v] [--show CAT] [--obj] [--objcmp] [--vlink] [--wasm32] [--no-cache] [PAT]

Every test of vcc-ctests.py (gcc torture, tcc tests2, our own) is built for
wasm64-unknown-emscripten twice: by clang (the oracle) and by vcc, each
linked by emcc (emscripten's libc, -sMEMORY64) and run under node.  vcc
passes when its program prints what clang's prints and exits the same way.

vcc's side: `vpp -S` (the assembly text) assembled by clang; with --obj,
`vpp -c` (vcc's own assembler and object writer, W5); with --objcmp, both,
and `vpp -c`'s object must be byte-identical to clang's for the same text.
With --vlink, emcc links vcc's side with vlink (W8: build/ccs/vlink as
wasm-ld, through a directory of emscripten's LLVM tools with that one
replaced); --obj --vlink is vcc's whole chain.
Failures by stage: emit (vpp fails), as (clang rejects vcc's assembly),
objcmp, link (emcc), run.
The test runtime headers (ccs/vcc/tests/rt/include) are used on both sides,
emscripten's libc behind them.  Clang's results are cached in
build/ccs/wasmtests/ref (keyed by the source and flags).  A vcc object
byte-identical to one that passed before (build/ccs/wasmtests/pass, keyed by
the object, the link and the reference) is not linked or run again;
--no-cache runs all.
--wasm32: wasm32-unknown-emscripten (ILP32, emcc without -sMEMORY64), in
build/ccs/wasmtests32 (PLAN-wasm.md W7).
"""
import hashlib, importlib.util, json, os, re, shutil, subprocess, sys, time
from collections import Counter, defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

_spec = importlib.util.spec_from_file_location('ctests', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-ctests.py'))
ct = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ct)

R = ct.R
T = ct.T
CL = ct.CL
VPP = ct.VPP
EMCC = shutil.which('emcc') or '/opt/homebrew/bin/emcc'
NODE = shutil.which('node') or '/opt/homebrew/bin/node'
OUT = f'{BUILD}/wasmtests'
TGT = '--target=wasm64-unknown-emscripten'
BASE = [TGT, '-nostdinc', '-I', f'{ct.RT}/include', '-isystem', ct.RES, '-O2']
LINK = [EMCC, '-sMEMORY64=1', '-sEXIT_RUNTIME=1', '-sASSERTIONS=0', '-sSTACK_SIZE=4MB', '-Wno-deprecated']
# tests that need what the wasm target or emscripten does not have
WASM_SKIP = {}
# known vcc bugs (TODO.md "wasm: the optimizations the gcc torture tests
# assume"): a failure counts as xfail, a pass as XPASS (an error: drop it here)
_WOPT = 'wasm: no inliner or dead-code removal (TODO.md)'
WASM_XFAIL = {n: _WOPT for n in ('20020720-1', '20030216-1', '20041114-1', 'compare-3',
                                 'pure-1', 'shiftopt-1', 'bcp-1')}
# ... and tests of our own suite (ccs/vcc/tests/vcc) the wasm emitter does not handle yet
WASM_XFAIL.update({
    'fp_class_runtime': 'wasm: __builtin_isfpclass/__builtin_fpclassify not lowered (PLAN-wasm.md W4)',
    'inline_walk': 'wasm: inlining (const parameters, labels per instance) only in the shared walker, not the wasm emitter',
    'vec_wide_lanes': 'wasm: __int128/double vector lanes (TODO.md tier 1, as arm64)',
    'switch_const_fold': 'wasm: a constant switch keeps every case (the x86 walker folds it)',
    'and_zero_fold': 'wasm: x & 0 with x not constant is not folded (the x86 walker folds it)',
    'if_stmtexpr_const': 'wasm: a statement expression constant does not decide an if (the x86 walker folds it)',
    'dead_after_inline_return': 'wasm: statements after an inlined return are generated (the x86 walker drops them)',
    'inline_unused_arg': 'wasm: an inlined function evaluates its unused arguments (the x86 walker skips them)',
    'warn_once_const': 'wasm: a statement expression constant does not decide an if (the x86 walker folds it)',
    'dead_closed_label': 'wasm: an unreachable labelled statement reached only from inside it is kept (the x86 walker drops it)',
    'dead_after_const_or': 'wasm: an if that never reaches its else keeps what follows a returning then (the x86 walker drops it)',
    'autoinit_zero': 'wasm: -ftrivial-auto-var-init=zero is x86-64 only so far (TODO.md tier 1)',
})

# New coverage of existing Wasm gaps; never skip the compile/run or accept XPASS.
# See TODO.md "wasm: Linux-correctness coverage gaps" for baseline and probes.
WASM_XFAIL.update({
    'array_constants': 'TODO.md wasm: Linux-correctness coverage gaps: array-address constant conditions leave unreachable calls',
    'atomic_record': 'TODO.md wasm: Linux-correctness coverage gaps: 128-bit atomic record value conversions are not lowered',
    'complex_div_runtime': 'TODO.md wasm: Linux-correctness coverage gaps: long-double classification builtins used by complex division are not lowered',
    'dead_branch_entries': 'TODO.md wasm: Linux-correctness coverage gaps: constant branches retain unreachable prefixes before live labels',
    'deep_conditional': 'TODO.md wasm: Linux-correctness coverage gaps: nested GNU omitted-middle conditionals mishandle __int128 values',
    'fp_classify_builtin': 'TODO.md wasm: Linux-correctness coverage gaps: runtime __builtin_isnormal/__builtin_fpclassify are not lowered',
    'fp_compare_builtin': 'TODO.md wasm: Linux-correctness coverage gaps: long-double ordered/unordered comparison builtins are not lowered',
    'fpclassify': 'TODO.md wasm: Linux-correctness coverage gaps: runtime __builtin_isnormal/__builtin_fpclassify are not lowered',
    'generic_bitcount': 'TODO.md wasm: Linux-correctness coverage gaps: _BitInt runtime precision and generic bitcount lowering are not implemented',
    'gnu_conditional_float': 'TODO.md wasm: Linux-correctness coverage gaps: GNU omitted-middle floating conditional mishandles truth/value conversion',
    'inline_constants': 'TODO.md wasm: Linux-correctness coverage gaps: no inliner for __builtin_constant_p of call arguments',
    'int128_shift_count': 'TODO.md wasm: Linux-correctness coverage gaps: __int128 shift-count operands are not narrowed correctly',
    'isfpclass': 'TODO.md wasm: Linux-correctness coverage gaps: runtime __builtin_isfpclass/__builtin_issignaling are not lowered',
    'optimized_conditions': 'TODO.md wasm: Linux-correctness coverage gaps: constant-condition optimization leaves unreachable calls',
    'parenthesized_function': 'TODO.md wasm: Linux-correctness coverage gaps: nested function-pointer return call has an incorrect Wasm signature',
    'signaling_nan': 'TODO.md wasm: Linux-correctness coverage gaps: runtime __builtin_issignaling is not lowered',
    'transparent_union_redecl': 'TODO.md wasm: Linux-correctness coverage gaps: transparent-union redeclarations do not use the member calling convention',
    'void_lvalue': 'TODO.md wasm: Linux-correctness coverage gaps: void lvalue evaluation leaves unwanted Wasm stack values',
})

def unsupported_requirement(suite, src):
    """Honor an explicit upstream contract, never infer support from a failure.

    LLVM Wasm cannot represent escaping GNU label addresses (PLAN-wasm.md,
    "Referenskontrakt"). Some examples optimize away, but that does not provide
    the label_values target capability required by these GCC tests.
    """
    if suite == 'gcc' and re.search(
            r'(?m)^\s*/\*\s*\{\s*dg-require-effective-target\s+label_values\s*\}\s*\*/', src):
        return 'wasm: dg-require-effective-target label_values is unsupported (PLAN-wasm.md: Referenskontrakt)'
    return None


def report_case(suite, name, status, category, why, verbose=False, show=None):
    # Unexpected results must identify the source even in the default CI log.
    if status in ('fail', 'error') or (verbose and status == 'xfail'):
        print(f'{status.upper()} {category:5s} {suite}/{name}: {why}')
    if show and status != 'pass' and category == show:
        print(f'{status} {category} {suite}/{name}: {why}')


def set_target(w32):
    """wasm32 (W7) instead of wasm64: the target, emcc's memory, the work directory"""
    global OUT, TGT, BASE, LINK, RTO
    if not w32:
        return
    OUT = f'{BUILD}/wasmtests32'
    TGT = '--target=wasm32-unknown-emscripten'
    BASE = [TGT] + BASE[1:]
    LINK = [a for a in LINK if a != '-sMEMORY64=1']
    RTO = f'{OUT}/rt.o'
    # Baseline f866a31 already passes this on wasm32; its remaining
    # function-pointer signature failure is specific to wasm64.
    WASM_XFAIL.pop('parenthesized_function', None)

def sh(cmd, timeout=60, **kw):
    return ct.sh(cmd, timeout=timeout, **kw)

RTO = f'{OUT}/rt.o'

def rt_obj():
    """rt/wasm.c (__rt_assert ..), built by emcc once per change"""
    src = f'{ct.RT}/wasm.c'
    if not os.path.exists(RTO) or os.path.getmtime(RTO) < os.path.getmtime(src):
        os.makedirs(OUT, exist_ok=True)
        subprocess.run([EMCC, TGT, '-O2', '-c', src, '-o', RTO + '.tmp'], check=True)
        os.replace(RTO + '.tmp', RTO)

# setjmp/longjmp through wasm exceptions, as emcc -sSUPPORT_LONGJMP=wasm compiles and links
SJLJ = ['-mllvm', '-wasm-enable-sjlj']
SJLJ_CL = ['-mllvm', '-wasm-use-legacy-eh', '-mexception-handling']
SJLJ_LINK = ['-sSUPPORT_LONGJMP=wasm']

def uses_sjlj(src):
    return re.search(r'\b_?(setjmp|longjmp)\b', src) is not None

def link(objs, js, vlink=False, extra=()):
    rc, _, err = sh(LINK + list(extra) + objs + [RTO, '-o', js], timeout=120, env=VLENV if vlink else None)
    return rc, err.decode(errors='replace')

def vlink_env():
    # emscripten's LLVM tools with wasm-ld -> vlink; the shared cache is only read
    llvm_bin = os.path.join(os.path.dirname(os.path.realpath(EMCC)), '..', 'libexec', 'llvm', 'bin')
    if not os.path.isdir(llvm_bin):
        llvm_bin = '/opt/homebrew/opt/emscripten/libexec/llvm/bin'
    shim = f'{OUT}/llvmshim'
    if not os.path.isdir(shim):
        os.makedirs(shim + '.tmp', exist_ok=True)
        for f in os.listdir(llvm_bin):
            d = os.path.join(shim + '.tmp', f)
            if not os.path.lexists(d):
                os.symlink(f'{BUILD}/vlink' if f == 'wasm-ld' else os.path.join(llvm_bin, f), d)
        os.rename(shim + '.tmp', shim)
    return dict(os.environ, EM_LLVM_ROOT=shim, EM_FROZEN_CACHE='1', EM_IGNORE_SANITY='1')

def run(js, diagnostics=False):
    rc, out, err = sh([NODE, js], timeout=20, stdin=subprocess.DEVNULL)
    result = (rc, out.decode('latin-1'))
    return (*result, err) if diagnostics else result

def reference(suite, name, path, flags, wd):
    with open(path, 'rb') as source:
        src = source.read().decode('latin-1')
    key = hashlib.sha1(('oracle-status-v2 ' + src + ' '.join(flags) + ' '.join(ct.CLFL) + ' '.join(LINK) + 'rt1').encode()).hexdigest()[:16]
    cf = f'{OUT}/ref/{suite}/{name}.json'
    try:
        with open(cf) as cache:
            r = json.load(cache)
        if r['key'] == key and 'error' not in r:
            return r
    except (OSError, ValueError, KeyError):
        pass
    r = {'key': key}
    sj = SJLJ in [flags[i:i + 2] for i in range(len(flags))]
    timeout = ct.reference_timeout(path)
    rc, _, err = sh([CL] + BASE + ct.CLFL + flags + (SJLJ_CL if sj else []) + ['-c', path, '-o', f'{wd}/ref.o'],
                    cwd=os.path.dirname(path), timeout=timeout)
    failure = ct.oracle_error(rc, err, 'compile', timeout)
    if failure:
        return dict(r, **failure)
    if rc:
        r['skip'] = 'clang-rejects'; r['why'] = ct.first_err(err.decode(errors='replace'))
    else:
        rc, err = link([f'{wd}/ref.o'], f'{wd}/ref.js', extra=SJLJ_LINK if sj else ())
        failure = ct.oracle_error(rc, err, 'link', 120)
        if failure:
            return dict(r, **failure)
        if rc:
            m = re.search(r'undefined symbol: (\S+)', err)
            r['skip'] = 'ref-fail'; r['why'] = 'link: ' + (m.group(1) if m else ct.first_err(err))
        else:
            rc, out, err = run(f'{wd}/ref.js', diagnostics=True)
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
    t, st, cat, why = one_(t)
    xf = WASM_XFAIL.get(t[1]) if t[0] in ('gcc', 'vcc') else None
    if t[0] == 'vcc':
        marker = re.search(r'/\*\s*vcc-xfail-wasm:(.*?)\*/', open(t[2]).read(), re.S)
        if marker:
            xf = ' '.join(marker.group(1).split())
    if xf and st == 'fail':
        return t, 'xfail', cat, f'{xf} [{why}]'
    if xf and st == 'pass':
        return t, 'fail', 'xpass', f'passes now: remove it from WASM_XFAIL ({xf})'
    return t, st, cat, why

def one_(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    if suite == 'tcc' and name in ct.TCC_SKIP:
        return t, 'skip', 'tcc-specific', ct.TCC_SKIP[name]
    if suite == 'gcc' and name in ct.GCC_SKIP:
        return t, 'skip', 'compiler-specific', ct.GCC_SKIP[name]
    unsupported = unsupported_requirement(suite, src)
    if unsupported:
        return t, 'skip', 'unsupported-target', unsupported
    if name in WASM_SKIP:
        return t, 'skip', 'wasm', WASM_SKIP[name]
    flags = ct.dg_flags(src) if suite == 'gcc' else ct.exec_marks(src)[0] if suite == 'vcc' else []
    if name.startswith('76_dollars'):
        flags = ['-fdollars-in-identifiers']
    sj = uses_sjlj(src)
    if sj:
        flags = flags + SJLJ
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
    vfl = BASE + ct.CLFL + flags
    if OBJ:
        rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
        if rc:
            e = err.decode(errors='replace')
            return t, 'fail', 'emit', ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
    else:
        rc, _, err = sh([VPP, '-S'] + vfl + [path, '-o', f'{wd}/vcc.s'], cwd=cwd)
        if rc:
            e = err.decode(errors='replace')
            return t, 'fail', 'emit', ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
        eh = ['-mexception-handling'] if sj else []
        rc, _, err = sh([CL, TGT] + eh + ['-c', f'{wd}/vcc.s', '-o', f'{wd}/vcc.o'])
        if rc and all('call_indirect' in l for l in re.findall(r'error: .*\n(.*)', err.decode(errors='replace'))):
            # LLVM's assembler checks call_indirect's index against an i32 table (clang's own -S fails the same way)
            rc, _, err = sh([CL, TGT] + eh + ['-Wa,--no-type-check', '-c', f'{wd}/vcc.s', '-o', f'{wd}/vcc.o'])
        quoted = False
        if rc and re.search(r'error: (Unexpected token in operand|Expected label after \.type directive, got): "', err.decode(errors='replace')):
            # quoted (non-ASCII) names, as clang -S writes them, which LLVM's wasm
            # assembler does not take back: vcc's own assembler builds the object
            rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
            quoted = True
        if rc:
            e = err.decode(errors='replace')
            m = re.search(r'error: (.*)', e)
            return t, 'fail', 'as', re.sub(r'\d+', 'N', m.group(1)) if m else ct.first_err(e)
        if OBJCMP and not quoted:
            # vcc's object for the same text, byte for byte against clang's
            rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vccobj.o'], cwd=cwd)
            if rc:
                return t, 'fail', 'objcmp', 'vpp -c: ' + ct.first_err(err.decode(errors='replace'))
            if open(f'{wd}/vccobj.o', 'rb').read() != open(f'{wd}/vcc.o', 'rb').read():
                return t, 'fail', 'objcmp', 'object differs from clang\'s for the same .s'
    # an object byte-identical to one that passed before (same link, same
    # reference) passes again: no emcc, no node
    pk = hashlib.sha1(open(f'{wd}/vcc.o', 'rb').read() + (ref['key'] + str(VLENV is not None) + str(sj)).encode()).hexdigest()
    pf = f'{OUT}/pass/{suite}/{name}'
    if not NOCACHE:
        try:
            if open(pf).read() == pk:
                shutil.rmtree(wd, ignore_errors=True)
                return t, 'pass', '', ''
        except OSError:
            pass
    rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc.js', VLENV is not None, SJLJ_LINK if sj else ())
    if rc:
        m = re.search(r'undefined symbol: (\S+)', err)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else ct.first_err(err)
    rc, out = run(f'{wd}/vcc.js')
    if rc == ref['rc'] and out == ref['out']:
        os.makedirs(os.path.dirname(pf), exist_ok=True)
        open(pf, 'w').write(pk)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    why = f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    open(f'{wd}/vcc.out', 'w').write(out); open(f'{wd}/ref.out', 'w').write(ref['out'])
    return t, 'fail', 'run', why

VLENV = None

NOCACHE = False

def init(obj, objcmp=False, vlenv=None, w32=False, nocache=False):
    global OBJ, OBJCMP, VLENV, NOCACHE
    set_target(w32)
    NOCACHE = nocache
    OBJ = obj
    OBJCMP = objcmp
    VLENV = vlenv

if __name__ == '__main__':
    av = sys.argv[1:]
    j, suite, show, verbose = 6, None, None, False
    obj = '--obj' in av
    objcmp = '--objcmp' in av
    w32 = '--wasm32' in av
    nocache = '--no-cache' in av
    set_target(w32)
    vlenv = vlink_env() if '--vlink' in av else None
    av = [a for a in av if a not in ('--obj', '--objcmp', '--vlink', '--wasm32', '--no-cache')]
    if '-v' in av: verbose = True; av.remove('-v')
    for opt in ('-j', '--suite', '--show'):
        if opt in av:
            k = av.index(opt); v = av[k + 1]; del av[k:k + 2]
            if opt == '-j': j = int(v)
            elif opt == '--suite': suite = v
            else: show = v
    pat = av[0] if av else None
    ts = ct.tests(suite, pat)
    t0 = time.time()
    rt_obj()
    with Pool(j, initializer=init, initargs=(obj, objcmp, vlenv, w32, nocache)) as pool:
        res = pool.map(one, ts, chunksize=1)
    by = defaultdict(Counter)
    whys = defaultdict(Counter)
    for (s, n, _), st, cat, why in res:
        by[s][st] += 1
        if st != 'pass':
            by[s][f'{st}:{cat}'] += 1
            whys[(st, cat)][why] += 1
        report_case(s, n, st, cat, why, verbose, show)
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
        xf = f', {c["xfail"]} xfail' if c['xfail'] else ''
        print(f'{s:4s} {c["pass"]}/{run_} pass ({100 * c["pass"] / max(run_, 1):.1f}%){xf}, {c["skip"]} skipped   {det}')
    print(f'{time.time() - t0:.1f} s')
    sys.exit(1 if tot['fail'] or tot['error'] else 0)
