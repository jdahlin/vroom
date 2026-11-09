#!/usr/bin/env python3
"""vcc-wasm.py: the C test suites through vcc's WebAssembly target (PLAN-wasm.md).

  bench/vcc-wasm.py [-j N] [--suite gcc|tcc|vcc] [-v] [--show CAT] [--obj] [--objcmp] [--vlink] [PAT]

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
build/ccs/wasmtests/ref (keyed by the source and flags).
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

def sh(cmd, timeout=60, **kw):
    return ct.sh(cmd, timeout=timeout, **kw)

def link(objs, js, vlink=False):
    rc, _, err = sh(LINK + objs + ['-o', js], timeout=120, env=VLENV if vlink else None)
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

def run(js):
    rc, out, err = sh([NODE, js], timeout=20, stdin=subprocess.DEVNULL)
    return rc, out.decode('latin-1')

def reference(suite, name, path, flags, wd):
    key = hashlib.sha1((open(path, 'rb').read().decode('latin-1') + ' '.join(flags) + ' '.join(ct.CLFL) + ' '.join(LINK)).encode()).hexdigest()[:16]
    cf = f'{OUT}/ref/{suite}/{name}.json'
    try:
        r = json.load(open(cf))
        if r['key'] == key:
            return r
    except (OSError, ValueError, KeyError):
        pass
    r = {'key': key}
    rc, _, err = sh([CL] + BASE + ct.CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=os.path.dirname(path))
    if rc:
        r['skip'] = 'clang-rejects'; r['why'] = ct.first_err(err.decode(errors='replace'))
    else:
        rc, err = link([f'{wd}/ref.o'], f'{wd}/ref.js')
        if rc:
            m = re.search(r'undefined symbol: (\S+)', err)
            r['skip'] = 'ref-fail'; r['why'] = 'link: ' + (m.group(1) if m else ct.first_err(err))
        else:
            rc, out = run(f'{wd}/ref.js')
            r['rc'] = rc; r['out'] = out
            if suite in ('gcc', 'vcc') and rc != 0:
                r['skip'] = 'ref-fail'; r['why'] = f'clang build exits {rc}'
    os.makedirs(os.path.dirname(cf), exist_ok=True)
    json.dump(r, open(cf, 'w'))
    return r

def one(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    if suite == 'tcc' and name in ct.TCC_SKIP:
        return t, 'skip', 'tcc-specific', ct.TCC_SKIP[name]
    if suite == 'gcc' and name in ct.GCC_SKIP:
        return t, 'skip', 'compiler-specific', ct.GCC_SKIP[name]
    if name in WASM_SKIP:
        return t, 'skip', 'wasm', WASM_SKIP[name]
    flags = ct.dg_flags(src) if suite == 'gcc' else ct.exec_marks(src)[0] if suite == 'vcc' else []
    if name.startswith('76_dollars'):
        flags = ['-fdollars-in-identifiers']
    if suite == 'vcc' and re.search(r'/\*\s*vcc-noref:', src):
        return t, 'skip', 'noref', 'no clang reference'
    wd = f'{OUT}/w/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    cwd = os.path.dirname(path)
    ref = reference(suite, name, path, flags, wd)
    if 'skip' in ref:
        return t, 'skip', ref['skip'], ref['why']
    vfl = BASE + ct.CLFL + flags
    if OBJ:
        rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
        if rc:
            e = err.decode(errors='replace')
            return t, 'fail', 'emit', ct.first_err(e) if rc != 'timeout' and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
    else:
        rc, _, err = sh([VPP, '-S'] + vfl + [path, '-o', f'{wd}/vcc.s'], cwd=cwd)
        if rc:
            e = err.decode(errors='replace')
            return t, 'fail', 'emit', ct.first_err(e) if rc != 'timeout' and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
        rc, _, err = sh([CL, TGT, '-c', f'{wd}/vcc.s', '-o', f'{wd}/vcc.o'])
        if rc and all('call_indirect' in l for l in re.findall(r'error: .*\n(.*)', err.decode(errors='replace'))):
            # LLVM's assembler checks call_indirect's index against an i32 table (clang's own -S fails the same way)
            rc, _, err = sh([CL, TGT, '-Wa,--no-type-check', '-c', f'{wd}/vcc.s', '-o', f'{wd}/vcc.o'])
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
    rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc.js', VLENV is not None)
    if rc:
        m = re.search(r'undefined symbol: (\S+)', err)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else ct.first_err(err)
    rc, out = run(f'{wd}/vcc.js')
    if rc == ref['rc'] and out == ref['out']:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    why = f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    open(f'{wd}/vcc.out', 'w').write(out); open(f'{wd}/ref.out', 'w').write(ref['out'])
    return t, 'fail', 'run', why

VLENV = None

def init(obj, objcmp=False, vlenv=None):
    global OBJ, OBJCMP, VLENV
    OBJ = obj
    OBJCMP = objcmp
    VLENV = vlenv

if __name__ == '__main__':
    av = sys.argv[1:]
    j, suite, show, verbose = 6, None, None, False
    obj = '--obj' in av
    objcmp = '--objcmp' in av
    vlenv = vlink_env() if '--vlink' in av else None
    av = [a for a in av if a not in ('--obj', '--objcmp', '--vlink')]
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
    with Pool(j, initializer=init, initargs=(obj, objcmp, vlenv)) as pool:
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
        tot.update(by[s])
    for s, c in sorted(by.items()) + [('all', tot)]:
        run_ = c['pass'] + c['fail']
        det = ' '.join(f'{k}={v}' for k, v in sorted(c.items()) if ':' in k)
        print(f'{s:4s} {c["pass"]}/{run_} pass ({100 * c["pass"] / max(run_, 1):.1f}%), {c["skip"]} skipped   {det}')
    print(f'{time.time() - t0:.1f} s')
    sys.exit(1 if tot['fail'] else 0)
