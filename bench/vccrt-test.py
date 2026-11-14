#!/usr/bin/env python3
"""vccrt-test.py: vcc's run-time support library for Windows x64 (src/runtime/, PLAN-win64.md WIN7R).

  bench/vccrt-test.py needs [-j N]      which helpers code for the target calls, from data
  bench/vccrt-test.py [diff] [-n N] [-v] [HELPER...]   libvccrt.a against compiler-rt under Wine
  bench/vccrt-test.py native [-n N]     the same C sources against the host's compiler-rt (x86-64 Linux)
  bench/vccrt-test.py chkstk            ___chkstk_ms: registers, pages touched, frames on a small-stack thread
  ... diff|chkstk --export DIR          also export the libvccrt program for real Windows (WIN9)

`needs` lists every symbol of compiler-rt's builtins archive that a link for
x86_64-w64-mingw32 can take: (a) the undefined symbols of clang's object for
every reference-ok test of the three suites (bench/vcc-win64.py's reference
cache; run `vcc-win64.py --ref-only` first) at -O0 and at -O2, (b) the
undefined symbols of the mingw-w64 CRT (crt2.o, crtbegin.o, crtend.o and the
libraries of the default link line) and of the test runtime, and (c) the
helpers vcc's code generator calls (src/codegen/type.c's list, the complex
division of src/frontend/c/complex.c, __builtin_powi*).  It then checks that
libvccrt.a defines all of them and that the archive needs nothing but itself.

`diff` builds src/test/vccrt/differential.c twice, linked with libvccrt.a
(the explicit link line of vcc-win64.py --vccrt-ref; its map must show no
compiler-rt, libgcc or libunwind) and with compiler-rt's builtins (clang's
own line), runs both under Wine and compares, per helper, a digest of every
result for the same generated inputs (special values, powers of two +-1,
boundaries, NaNs with payloads, ties for every narrowing, random bits; only
inputs whose result C defines).  A mismatch is rerun with the helper's every
call printed, and the first differing calls are shown.  `-n N` sets the
random inputs per helper (default 20000).  The helpers in EXACT are checked
against the exact result computed here instead (compiler-rt lacks one, or is
wrong for it).  Complex results compare NaN parts as NaN (see the driver).

`native` compiles the same driver for x86_64-linux-gnu and links it with
src/runtime's C files built for the host (SysV ABI there; ___chkstk_ms is
Windows-only) by clang and by vcc itself (vpp; every helper must give the
same bits), with the host's compiler-rt builtins and with its libgcc;
differences other than those listed in NATIVE_KNOWN / LIBGCC_KNOWN fail.

`chkstk` runs src/test/vccrt/stack-probe.c linked with each runtime: every
register preserved for sizes 0 to 250000, every page of [rsp - size, rsp)
written (GetWriteWatch on a region the probe runs in: Wine commits thread
stacks itself, so a probe that skips pages does not fault there), and
frames of one page to 950000 bytes, fixed and alloca'd, on a thread with a
1 MiB stack reservation.

--export DIR (diff, chkstk; WIN9) copies the program linked with libvccrt.a
to DIR/exe/vccrt/ and adds it to DIR/vccrt-MODE.manifest.json with the argv
used here and the stdout and exit code it gave under Wine (after passing the
check), for bench/win64-native.py to run on real Windows.

libvccrt.a is built by src/runtime/build.sh (CC and AR as parameters; clang
for the target until vcc's Windows code generator is in, see that script).
"""
import hashlib, importlib.util, json, os, re, subprocess, sys, time
from collections import defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostenv
from hostenv import BUILD, MINGW_SYSROOT, WINE, llvm

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CL = llvm('clang')
NM = llvm('llvm-nm')
OUT = f'{BUILD}/vccrt'
LIB = f'{OUT}/libvccrt.a'
TGT = '--target=x86_64-w64-mingw32'
MLIB = f'{MINGW_SYSROOT}/x86_64-w64-mingw32/lib'
MRES = hostenv.mingw_resource_dir()
BUILTINS = f'{MRES}/lib/windows/libclang_rt.builtins-x86_64.a' if MRES else None
TEST = f'{R}/src/test/vccrt'
PREFIX = os.environ.get('VCC_WINEPREFIX', f'{BUILD}/wine')


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


def archive_symbols(path):
    """{member: (defined, undefined)} of an archive or object, by llvm-nm"""
    out = sh([NM, '-g', '-A', path], text=True).stdout
    members = defaultdict(lambda: (set(), set()))
    for line in out.splitlines():
        head, _, rest = line.rpartition(': ')
        parts = rest.split()
        if len(parts) == 2 and parts[0] == 'U':
            members[head][1].add(parts[1])
        elif len(parts) == 3 and parts[1] != 'U':
            members[head][0].add(parts[2])
    return members


def build_library():
    """libvccrt.a for the target (src/runtime/build.sh with its defaults)"""
    r = sh(['sh', f'{R}/src/runtime/build.sh', OUT], text=True)
    if r.returncode:
        sys.exit(f'vccrt-test.py: src/runtime/build.sh failed:\n{r.stdout}{r.stderr}')
    return LIB


# ---------------------------------------------------------------- needs

def _win64():
    spec = importlib.util.spec_from_file_location('win64', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-win64.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


W = None


def _load_win64():
    global W
    if W is None:
        W = _win64()


def _needs_one(job):
    """the undefined symbols of clang's object for one test at one -O level"""
    (suite, name, path), level = job
    src = open(path, 'rb').read().decode('latin-1')
    flags = W.flags_of(suite, name, src)
    wd = f'{OUT}/needs/{suite}/{name}{level}'
    os.makedirs(os.path.dirname(wd), exist_ok=True)
    base = [f for f in W.BASE if f != '-O2'] + [level]
    cmd = [CL] + base + W.ct.CLFL + flags + ['-c', path, '-o', wd + '.o']
    key = hashlib.sha1((src + ' '.join(cmd) + W.TOOLFP).encode()).hexdigest()
    try:
        cached = json.load(open(wd + '.json'))
        if cached['key'] == key:
            return job[0], level, cached['undefined']
    except (OSError, ValueError, KeyError):
        pass
    r = sh(cmd, cwd=os.path.dirname(path), timeout=300)
    if r.returncode:
        return job[0], level, None
    und = sh([NM, '-u', '-j', wd + '.o'], text=True).stdout.split()
    os.unlink(wd + '.o')
    json.dump({'key': key, 'undefined': und}, open(wd + '.json', 'w'))
    return job[0], level, und


# helpers vcc's code generator emits calls to (src/codegen/type.c's libcall
# list, src/frontend/c/complex.c and library-function.c); written out so that
# a helper added there shows up: `needs` fails on a name these files have and
# this list lacks
VCC_CODEGEN = '''__udivti3 __divti3 __umodti3 __modti3 __ashlti3 __lshrti3 __ashrti3
__floattisf __floattidf __floatuntisf __floatuntidf __fixsfti __fixdfti __fixunssfti __fixunsdfti
__addtf3 __subtf3 __multf3 __divtf3 __eqtf2 __netf2 __lttf2 __letf2 __gttf2 __getf2 __unordtf2
__extenddftf2 __extendsftf2 __trunctfdf2 __trunctfsf2 __floatsitf __floatditf __floatunsitf
__floatunditf __floattitf __floatuntitf __fixtfsi __fixtfdi __fixunstfsi __fixunstfdi
__fixtfti __fixunstfti __extendxftf2 __trunctfxf2
__extendbfsf2 __truncsfbf2 __truncdfbf2 __truncxfbf2 __trunctfbf2
__extendhfsf2 __truncsfhf2 __truncdfhf2 __truncxfhf2 __trunctfhf2
__floattixf __floatuntixf __fixxfti __fixunsxfti
__divsc3 __divdc3 __divxc3 __divtc3 __powisf2 __powidf2 __powixf2 __powitf2'''.split()


def vcc_codegen_helpers():
    """(c): the names in vcc's sources, to check VCC_CODEGEN against"""
    names = set()
    for f in ('src/codegen/type.c', 'src/frontend/c/complex.c', 'src/frontend/c/library-function.c'):
        names |= set(re.findall(r'"(__[a-z0-9_]+[0-9])"', open(f'{R}/{f}').read()))
    return names


def needs(j):
    _load_win64()
    builtins = archive_symbols(BUILTINS)
    provider = {s: m.split(':')[-1] for m, (d, _) in builtins.items() for s in d}
    seen = defaultdict(set)	# symbol -> where the need was observed
    # (a) clang's objects of the reference-ok tests
    ts = [t for s in ('gcc', 'tcc', 'vcc') for t in W.ct.tests(s, None)]
    ok = []
    for t in ts:
        try:
            r = json.load(open(f'{W.OUT}/ref/{t[0]}/{t[1]}.json'))
        except (OSError, ValueError):
            continue
        if 'skip' not in r and 'error' not in r:
            ok.append(t)
    if not ok:
        sys.exit('vccrt-test.py needs: no reference cache; run bench/vcc-win64.py --ref-only first')
    with Pool(j, initializer=_load_win64) as pool:
        for (suite, name, _), level, und in pool.imap_unordered(_needs_one, [(t, lv) for t in ok for lv in ('-O0', '-O2')], chunksize=4):
            for s in und or ():
                if s in provider:
                    seen[s].add(f'test{level}:{suite}/{name}')
    # (b) the CRT of the default link line and the test runtime
    for f in ('crt2.o', 'crtbegin.o', 'crtend.o', 'libmingw32.a', 'libmingwex.a', 'libmoldname.a', 'libmsvcrt.a'):
        for m, (_, und) in archive_symbols(f'{MLIB}/{f}').items():
            for s in und & set(provider):
                seen[s].add(f'crt:{f}({m.split(":")[-1]})')
    W.rt_obj()
    for _, und in archive_symbols(W.RTO).values():
        for s in und & set(provider):
            seen[s].add('crt:src/test/rt/win64.c')
    # (c) vcc's code generator
    for s in VCC_CODEGEN:
        seen[s].add('vcc-codegen')
    stale = vcc_codegen_helpers() - set(VCC_CODEGEN)
    print(f'reference-ok tests: {len(ok)} (each at -O0 and -O2)')
    lib = archive_symbols(build_library())
    have = {s for d, _ in lib.values() for s in d}
    lib_und = {s for _, u in lib.values() for s in u} - have
    missing = []
    for s in sorted(seen):
        where = sorted(seen[s])
        tests = [w for w in where if w.startswith('test')]
        other = [w for w in where if not w.startswith('test')]
        desc = (f'{len(tests)} test objects (e.g. {tests[0]}) ' if tests else '') + ' '.join(other)
        mark = 'ok' if s in have else 'MISSING'
        if s not in have:
            missing.append(s)
        print(f'  {mark:7s} {s:24s} {provider.get(s, "(not in compiler-rt)"):28s} {desc}')
    print(f'helpers needed: {len(seen)}; in libvccrt.a: {len(seen) - len(missing)}; library defines {len(have)}')
    if stale:
        print('vcc names a helper missing from VCC_CODEGEN here: ' + ' '.join(sorted(stale)))
    if lib_und:
        print('libvccrt.a needs symbols it does not define: ' + ' '.join(sorted(lib_und)))
    return 1 if missing or stale or lib_und else 0


# ---------------------------------------------------------------- diff

def target_compile(src, obj, extra=()):
    r = sh([CL, TGT, f'--sysroot={MINGW_SYSROOT}', '-O1', '-ffp-contract=off', '-w', *extra, '-c', src, '-o', obj], text=True)
    if r.returncode:
        sys.exit(f'vccrt-test.py: compiling {src} failed:\n{r.stderr}')


def link_both(objs, name):
    """the program from OBJS linked with libvccrt.a (explicit line, lld, map
    checked) and with compiler-rt (clang's own line); the two paths"""
    vexe, cexe = f'{OUT}/{name}-vccrt.exe', f'{OUT}/{name}-compiler-rt.exe'
    rc, err = W.vccrt_link(objs[0], vexe, runtime=LIB, map_file=vexe + '.map')
    if rc:
        sys.exit(f'vccrt-test.py: linking {vexe} failed:\n{err}')
    bad = W.foreign_inputs(vexe + '.map')
    if bad:
        sys.exit(f'vccrt-test.py: {vexe} has inputs from another runtime: {" ".join(bad)}')
    r = sh(W.LINK + objs[1] + ['-o', cexe], text=True)
    if r.returncode:
        sys.exit(f'vccrt-test.py: linking {cexe} failed:\n{r.stderr}')
    return vexe, cexe


def wine_up():
    """the prefix and its server, and Wine's own services started once with
    no pipe of ours: a service started by a captured run would hold its
    stdout open until the server exits"""
    hostenv.wine_start(PREFIX)
    subprocess.run([WINE, 'cmd', '/c', 'exit'], env=WENV, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)


def wine(exe, *args, timeout=600):
    r = sh([WINE, exe, *args], env=WENV, stdin=subprocess.DEVNULL, timeout=timeout)
    return r.returncode, r.stdout.decode('latin-1').replace('\r\n', '\n')


def export(exe, argv, out, rc, mode, timeout=300):
    """--export: EXE with ARGV and the stdout/exit code it gave under Wine"""
    if not EXPORT:
        return
    import shutil
    name = os.path.basename(exe)
    os.makedirs(f'{EXPORT}/exe/vccrt', exist_ok=True)
    shutil.copyfile(exe, f'{EXPORT}/exe/vccrt/{name}')
    test = {'name': f'vccrt/{mode}', 'exe': f'exe/vccrt/{name}', 'argv': list(argv), 'stdin': None,
            'stdout': out, 'exit': rc, 'timeout': timeout, 'source': f'src/test/vccrt ({mode})'}
    with open(f'{EXPORT}/vccrt-{mode}.manifest.json', 'w') as f:
        json.dump({'version': 1, 'producer': f'bench/vccrt-test.py {mode}', 'target': 'x86_64-w64-mingw32',
                   'tests': [test]}, f, indent=0)


def digests(out):
    return {f[0]: (f[1], f[2]) for f in (line.split() for line in out.splitlines()) if len(f) == 3}


def show_calls(run_a, run_b, name, n, label_b, limit=6):
    """the first calls of NAME whose results differ"""
    a, b = run_a(str(n), name).splitlines(), run_b(str(n), name).splitlines()
    shown = 0
    for x, y in zip(a, b):
        if x != y:
            print(f'    vccrt       {x}\n    {label_b:11s} {y}')
            shown += 1
            if shown == limit:
                break
    if len(a) != len(b):
        print(f'    {len(a)} calls against {len(b)}')


# helpers checked against the exact result instead of compiler-rt, and why
EXACT = {
    '__truncxfbf2': 'not in compiler-rt',
    '__truncxfhf2': "compiler-rt's rounds twice, through double",
    '__extendbfsf2': "compiler-rt's takes the __bf16 in %cx; clang passes it in %xmm0",
}
FORMATS = {'xf': (64, 16383, 80), 'bf': (8, 127, 16), 'hf': (11, 15, 16), 'sf': (24, 127, 32)}


def decode(bits, fmt):
    """(sign, exact Fraction | 'inf' | 'nan', payload below the quiet bit, its width)"""
    from fractions import Fraction
    precision, emax, width = FORMATS[fmt]
    explicit = fmt == 'xf'
    fraction_bits = precision - 1
    sign = bits >> (width - 1)
    exponent = (bits >> (fraction_bits + explicit)) & (2 * emax + 1)
    fraction = bits & ((1 << fraction_bits) - 1)
    payload_bits = fraction_bits - 1
    if exponent == 2 * emax + 1:
        return sign, ('nan' if fraction else 'inf'), fraction & ((1 << payload_bits) - 1), payload_bits
    significand = bits & ((1 << 64) - 1) if explicit else fraction | (1 << fraction_bits if exponent else 0)
    return sign, Fraction(significand) * Fraction(2) ** ((exponent or 1) - emax - fraction_bits), 0, 0


def encode_rounded(sign, v, fmt):
    """the bits of an exact nonzero finite value, rounded to nearest, ties to even"""
    from fractions import Fraction
    precision, emax, width = FORMATS[fmt]
    if v == 0:
        return sign << (width - 1)
    e = v.numerator.bit_length() - v.denominator.bit_length()
    while Fraction(2) ** e > v:
        e -= 1
    while Fraction(2) ** (e + 1) <= v:
        e += 1
    e = max(e, 1 - emax)		# subnormals share the smallest exponent
    scaled = v / Fraction(2) ** (e - precision + 1)
    q, r = divmod(scaled.numerator, scaled.denominator)
    if 2 * r > scaled.denominator or (2 * r == scaled.denominator and q & 1):
        q += 1
    if q == 1 << precision:
        q, e = q >> 1, e + 1
    fraction_bits = precision - 1
    if e > emax:
        return sign << (width - 1) | (2 * emax + 1) << fraction_bits
    biased = e + emax if q >> fraction_bits else 0
    return sign << (width - 1) | biased << fraction_bits | (q & ((1 << fraction_bits) - 1))


def exact_result(name, a):
    """what helper NAME must return for the input with hex bits A"""
    src, dst = name[-5:-3], name[-3:-1]
    sign, v, payload, payload_bits = decode(int(a, 16), src)
    precision, emax, width = FORMATS[dst]
    if v == 'inf':
        return sign << (width - 1) | (2 * emax + 1) << (precision - 1)
    if v == 'nan':
        dst_payload_bits = precision - 2
        quiet = (int(a, 16) >> payload_bits) & 1
        if dst_payload_bits <= payload_bits:	# narrowing: the top payload bits, quieted
            p, quiet = payload >> (payload_bits - dst_payload_bits), 1
        else:	# widening: the payload left-aligned, the quiet bit kept
            p = payload << (dst_payload_bits - payload_bits)
        return sign << (width - 1) | (2 * emax + 1) << (precision - 1) | quiet << (precision - 2) | p
    return encode_rounded(sign, v, dst)


def check_exact(run_v, name, n):
    """each call of NAME against exact_result"""
    bad = total = 0
    for line in run_v(str(n), name).splitlines():
        a, r = line.split(' -> ')
        got, want = int(r, 16), exact_result(name, a)
        total += 1
        if got != want:
            bad += 1
            if bad <= 5:
                print(f'    {name}({a}) = {got:x}, exact {want:x}')
    return total, bad


def diff(n, names, verbose):
    _load_win64()
    build_library()
    wine_up()
    src = f'{TEST}/differential.c'
    target_compile(src, f'{OUT}/differential-vccrt.o')
    target_compile(src, f'{OUT}/differential-compiler-rt.o', ['-DNO_TRUNCXFBF2'])
    vexe, cexe = link_both(([f'{OUT}/differential-vccrt.o'], [f'{OUT}/differential-compiler-rt.o']), 'differential')
    rv, out_v = wine(vexe, str(n))
    rc, out_c = wine(cexe, str(n))
    if rv or rc:
        sys.exit(f'vccrt-test.py: the driver failed (exit {rv} with libvccrt, {rc} with compiler-rt)')
    dv, dc = digests(out_v), digests(out_c)
    failed = calls = 0
    for name in sorted(set(dv) | set(dc)):
        if names and name not in names:
            continue
        if name in EXACT:
            continue	# checked below
        calls += int(dv[name][0])
        if dv.get(name) == dc[name]:
            if verbose:
                print(f'  ok   {name} ({dv[name][0]} calls)')
            continue
        failed += 1
        print(f'  FAIL {name}: {dv.get(name)} against compiler-rt {dc[name]}')
        show_calls(lambda *a: wine(vexe, *a)[1], lambda *a: wine(cexe, *a)[1], name, n, 'compiler-rt')
    for name in sorted(EXACT):
        if names and name not in names:
            continue
        total, bad = check_exact(lambda *a: wine(vexe, *a)[1], name, n)
        calls += total
        failed += bad != 0
        print(f'  {"FAIL" if bad else "ok  "} {name}: {total - bad}/{total} equal to the exact result ({EXACT[name]})')
    tested = len([x for x in dv if not names or x in names])
    print(f'differential: {tested - failed}/{tested} helpers bit-identical to compiler-rt (or exact), {calls} calls, '
          f'{n} random inputs per helper')
    if not failed:
        export(vexe, [str(n)], out_v, rv, 'diff')
    return 1 if failed else 0


# ---------------------------------------------------------------- native

def native(n):
    """the same driver on this x86-64 Linux host (SysV ABI): src/runtime's C
    files built for the host against the host's compiler-rt and libgcc"""
    d = f'{OUT}/native'
    os.makedirs(f'{d}/vcc', exist_ok=True)
    objs, vcc_objs = [], []
    vpp = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
    for f in sorted(os.listdir(f'{R}/src/runtime')):
        if f.endswith('.c'):
            flags = ['-O2', '-ffreestanding', '-fno-builtin', '-ffp-contract=off', '-c', f'{R}/src/runtime/{f}']
            o = f'{d}/{f[:-2]}.o'
            r = sh([CL] + flags + ['-o', o], text=True)
            if r.returncode:
                sys.exit(f'vccrt-test.py native: {f}:\n{r.stderr}')
            objs.append(o)
            if os.path.exists(vpp):	# the library compiled by vcc itself (for this host's target)
                o = f'{d}/vcc/{f[:-2]}.o'
                r = sh([vpp, '--target=x86_64-linux-gnu'] + flags + ['-o', o], text=True)
                if r.returncode:
                    print(f'  vcc cannot compile {f}: {r.stderr.strip()[:200]}')
                    return 1
                vcc_objs.append(o)
    drv = [CL, '-O1', '-ffp-contract=off', '-w', '-c', f'{TEST}/differential.c']
    for extra, o in (([], f'{d}/driver.o'), (['-DNO_TRUNCXFBF2'], f'{d}/driver-nobf.o')):
        r = sh(drv + extra + ['-o', o], text=True)
        if r.returncode:
            sys.exit(f'vccrt-test.py native: differential.c:\n{r.stderr}')
    exes = {'vccrt': [CL, f'{d}/driver.o'] + objs, 'compiler-rt': [CL, '--rtlib=compiler-rt', f'{d}/driver-nobf.o'],
            'libgcc': [CL, '--rtlib=libgcc', f'{d}/driver.o']}
    if vcc_objs:
        exes['vccrt-by-vcc'] = [CL, f'{d}/driver.o'] + vcc_objs
    outs = {}
    for k, cmd in exes.items():
        r = sh(cmd + ['-o', f'{d}/{k}', '-lm'], text=True)
        if r.returncode:
            print(f'  {k}: link failed: {r.stderr.strip().splitlines()[-1] if r.stderr.strip() else ""}')
            continue
        r = sh([f'{d}/{k}', str(n)], timeout=600)
        outs[k] = r.stdout.decode()
    dv = digests(outs['vccrt'])
    rc = 0
    if 'vccrt-by-vcc' in outs:
        dvv = digests(outs['vccrt-by-vcc'])
        differ = sorted(x for x in dv if dvv.get(x) != dv[x])
        print(f'native vccrt built by vcc: {len(dv) - len(differ)}/{len(dv)} helpers bit-identical to the clang-built library'
              + (f'; differ: {" ".join(differ)}' if differ else ''))
        if differ:
            rc = 1
            for name in differ[:4]:
                show_calls(lambda *a: sh([f'{d}/vccrt', *a]).stdout.decode(),
                           lambda *a: sh([f'{d}/vccrt-by-vcc', *a]).stdout.decode(), name, n, 'by vcc')
    for k in ('compiler-rt', 'libgcc'):
        if k not in outs:
            continue
        dk = digests(outs[k])
        same = [x for x in dk if dv.get(x) == dk[x]]
        differ = sorted(x for x in dk if x in dv and dv[x] != dk[x])
        known = NATIVE_KNOWN if k == 'compiler-rt' else LIBGCC_KNOWN
        print(f'native {k}: {len(same)}/{len(dk)} helpers bit-identical')
        for name in differ:
            print(f'  differs {name}: {known.get(name, "UNEXPECTED")}')
            if name not in known:
                rc = 1
                show_calls(lambda *a: sh([f'{d}/vccrt', *a]).stdout.decode(), lambda *a: sh([f'{d}/{k}', *a]).stdout.decode(), name, n, k)
    return rc


# where the host's runtimes are known to differ from libvccrt (and from the
# target's compiler-rt, which libvccrt matches): `native` reports these and
# fails on any other difference
NATIVE_KNOWN = {
    '__divxc3': "this compiler-rt build gives NaN for finite / (inf + NaN i); the target's gives 0 as Annex G's example does",
    '__extendbfsf2': EXACT['__extendbfsf2'],
    '__truncxfhf2': EXACT['__truncxfhf2'],
}
_SOFTFP = 'libgcc soft-fp: the default NaN is negative and NaN operands propagate differently'
LIBGCC_KNOWN = {
    **{h: _SOFTFP for h in ('__addtf3', '__subtf3', '__multf3', '__divtf3')},
    **{h: 'libgcc: equality tests return 0 or 1 (not -1 for less)' for h in ('__eqtf2', '__netf2')},
    **{h: 'libgcc: unordered gives 2 or -2, not 1 or -1' for h in ('__lttf2', '__letf2', '__gttf2', '__getf2')},
    **{h: 'libgcc quiets a signalling NaN when it widens one' for h in
       ('__extendsftf2', '__extenddftf2', '__extendxftf2', '__extendhfsf2', '__extendbfsf2')},
    **{h: 'libgcc quiets every signalling NaN it narrows' for h in ('__truncsfbf2', '__trunctfxf2')},
    **{h: "libgcc's own complex algorithm (not Annex G's example): other rounding and NaN results" for h in
       ('__divsc3', '__divdc3', '__divxc3', '__divtc3')},
}


# ---------------------------------------------------------------- chkstk

def chkstk():
    _load_win64()
    build_library()
    wine_up()
    objs = []
    for f in ('stack-probe.c', 'stack-probe-check.S'):
        o = f'{OUT}/{f.split(".")[0]}{"-asm" if f.endswith(".S") else ""}.o'
        target_compile(f'{TEST}/{f}', o, ['-O2'])
        objs.append(o)
    vexe, cexe = link_both((objs, objs), 'stack-probe')
    rv, out_v = wine(vexe, 'strict')
    rc, out_c = wine(cexe)
    print(''.join('  ' + line + '\n' for line in out_v.splitlines()), end='')
    # the page check sees only writes, and compiler-rt's probe reads: compared without it
    same = [x for x in out_v.splitlines() if not x.startswith('pages')] == \
        [x for x in out_c.splitlines() if not x.startswith('pages')]
    ok = rv == 0 and out_v.endswith('ok\n') and rc == 0 and same
    print(f'chkstk: {"ok" if ok else "FAILED"} (libvccrt exit {rv}, compiler-rt exit {rc}, '
          f'register and frame checks {"equal" if same else "differ"})')
    if ok:
        export(vexe, ['strict'], out_v, rv, 'chkstk')
    return 0 if ok else 1


WENV = hostenv.wine_env(PREFIX)
WENV['WINEDLLOVERRIDES'] += ';winedbg.exe=d'
EXPORT = None

if __name__ == '__main__':
    av = sys.argv[1:]
    j, n, verbose = 4, 20000, '-v' in av
    av = [a for a in av if a != '-v']
    if '--export' in av:
        k = av.index('--export'); EXPORT = os.path.abspath(av[k + 1]); del av[k:k + 2]
    for opt in ('-j', '-n'):
        if opt in av:
            k = av.index(opt); v = int(av[k + 1]); del av[k:k + 2]
            if opt == '-j':
                j = v
            else:
                n = v
    why = hostenv.mingw_missing()
    if why:
        sys.exit(f'vccrt-test.py: {why}')
    os.makedirs(OUT, exist_ok=True)
    mode = av.pop(0) if av and av[0] in ('needs', 'diff', 'native', 'chkstk') else 'diff'
    t0 = time.time()
    rc = needs(j) if mode == 'needs' else diff(n, set(av), verbose) if mode == 'diff' else \
        native(n) if mode == 'native' else chkstk()
    print(f'{time.time() - t0:.1f} s')
    sys.exit(rc)
