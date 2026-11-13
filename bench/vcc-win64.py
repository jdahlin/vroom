#!/usr/bin/env python3
"""vcc-win64.py: the C test suites through vcc's Windows x64 target (PLAN-win64.md).

  bench/vcc-win64.py [-j N] [--suite gcc|tcc|vcc|win64|inline] [--ref-only] [--obj] [--objcmp]
                     [--vir] [--seh] [-v] [--show CAT] [--no-cache] [PAT]

Every test of the three suites of vcc-x86.py (gcc torture, tcc tests2, our
own), and of src/test/win64 (Windows-only: the calling convention's rules,
TLS, dllimport, constructors, and the unwinder walking, longjmp-ing and
resuming out of vcc's frames; --suite inline adds the inlining suite) is built for x86_64-w64-mingw32
twice: by clang (the oracle) and by vcc, each linked by lld against the
llvm-mingw sysroot (UCRT, static: no DLL beyond the system's) and run under
Wine.  vcc passes when its program prints what clang's prints and exits the
same way.  By default vcc's side uses `vpp -S`, assembled by clang; --obj
uses the direct `vpp -c` path, and --objcmp compares both with clang's
assembler.  --ref-only builds and runs clang's side only (WIN0).  --vir:
vcc's side through VIR (VCC_VIR=l: -O2's SSA path, falling back per
function to the direct emitter).  --seh: also check every vcc object's
unwind data (seh_check below: each function's epilogue decoded as the
Windows unwinder does, llvm-readobj --unwind against the bytes).

The chain (bench/hostenv.py: MINGW_SYSROOT, WINE): the host's clang compiles
with the test runtime headers (src/test/rt/include-win64 before
src/test/rt/include, the clang resource headers behind them) and links with
  clang --target=x86_64-w64-mingw32 --sysroot=SYSROOT -resource-dir=SYSROOT/lib/clang/N
        -rtlib=compiler-rt --unwindlib=none -fuse-ld=lld -static
(llvm-mingw's resource dir only for its lib/windows compiler-rt builtins;
--unwindlib=none: C needs no libunwind, and the default would look for
libgcc_s).  src/test/rt/win64.c is linked into both sides: stdin/stdout/
stderr as variables, the printf family through mingw-w64's __mingw_v*printf
(the 80-bit long double, as __USE_MINGW_ANSI_STDIO=1 would select), abort()
exiting 3 silently, and an unhandled exception printed as
"rt: unhandled exception CODE" with exit 255.  Programs run with stdout as a
pipe (UCRT's text mode: "\\r\\n" is read back as "\\n"), in a Wine prefix per
build tree (BUILD/wine, created once, one persistent wineserver).

Clang's results are cached in BUILD/win64tests/ref (keyed by the source,
flags, runtime, sysroot and Wine version), so a rerun only does vcc's side.
A test whose clang build cannot run on Windows is excluded with the reason
recorded: clang-rejects (it does not compile for the target), ref-fail (link:
a function UCRT lacks; or the program exits non-zero: an LP64 assumption, a
CRT difference), ref-runtime-fail (an exception or a timeout under Wine), or
the static WIN64_SKIP list.  Failures of vcc's side by stage: emit (vpp fails),
as (clang rejects vcc's assembly), link, run.  A vcc assembly text
byte-identical to one that passed before passes again without being
assembled, linked or run (--no-cache runs all).
"""
import hashlib, importlib.util, json, os, re, shutil, subprocess, sys, time
from collections import Counter, defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostenv
from hostenv import BUILD, MINGW_SYSROOT, WINE

_spec = importlib.util.spec_from_file_location('ctests', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-ctests.py'))
ct = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ct)

CL = ct.CL
VPP = ct.VPP
OUT = f'{BUILD}/win64tests'
PREFIX = os.environ.get('VCC_WINEPREFIX', f'{BUILD}/wine')
TGT = '--target=x86_64-w64-mingw32'
BASE = [TGT, '-nostdinc', '-I', f'{ct.RT}/include-win64', '-I', f'{ct.RT}/include', '-isystem', ct.RES, '-O2']
BASE += os.environ.get('VCC_CFLAGS', '').split()	# extra flags for both compilers
MRES = hostenv.mingw_resource_dir()
LINK = [CL, TGT, f'--sysroot={MINGW_SYSROOT}', f'-resource-dir={MRES}', '-rtlib=compiler-rt',
        '--unwindlib=none', '-fuse-ld=lld', '-static']
RTO = f'{OUT}/rt.o'
RTSRC = f'{ct.RT}/win64.c'
WENV = hostenv.wine_env(PREFIX)
WENV['WINEDLLOVERRIDES'] += ';winedbg.exe=d'	# a crash never waits in the debugger


def _runtime_hash():
    """the runtime headers and win64.c: a change invalidates the references"""
    h = hashlib.sha1()
    for d in ('include-win64', 'include'):
        for directory, _, files in sorted(os.walk(f'{ct.RT}/{d}')):
            for f in sorted(files):
                p = os.path.join(directory, f)
                h.update(os.path.relpath(p, ct.RT).encode())
                h.update(open(p, 'rb').read())
    h.update(open(RTSRC, 'rb').read())
    return h.hexdigest()


def _toolfp():
    """what else decides the reference: clang, the sysroot, Wine"""
    v = []
    for cmd in ([CL, '--version'], [WINE, '--version']):
        try:
            v.append(subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout.split('\n')[0])
        except (OSError, subprocess.TimeoutExpired):
            v.append('?')
    return ' '.join(v + [os.path.basename(MINGW_SYSROOT)])


RTHASH = _runtime_hash()
TOOLFP = _toolfp()

# tcc tests2 that vcc-ctests.py skips but that mean something here
TCC_RUN = {'x86 asm', 'win32', 'msvc layout'}
# tests that cannot work on Windows for a reason the reference run does not show
WIN64_SKIP = {}
# Why clang's own build does not run on Windows: each test whose reference is
# excluded only for this target (it passes for x86_64-linux-gnu) has its
# category and reason here, from the reference run of 2026-11-13.  The
# exclusion itself still comes from running clang's side; an entry whose
# reference now runs is reported as stale.  Exclusions not listed here are
# the same for x86-64 Linux (gcc-only flags and builtins, clang's own limits).
_LP64 = 'assumes a 64-bit long (Windows is LLP64)'
WIN64_WHY = {
    # LP64: the program tests 64-bit values held in long (our tests can move to long long)
    'gcc/comp-goto-1': ('lp64', 'label addresses in an unsigned long'),
    'gcc/pr36339': ('lp64', 'pointers in an unsigned long (my_uintptr_t)'),
    **{f'vcc/{n}': ('lp64', _LP64) for n in (
        'mem_builtin_inline', 'vir_low32', 'jump_table', 'asm_x86_integer', 'vir_ext_add_facts',
        'vir_scratch_fold', 'vir_shrink_wrap', 'vir_x86_bytes', 'vir_x86_ext', 'vir_x86_index',
        'vir_x86_nsw_index', 'vir_x86_testmem', 'asm_clobber_all', 'asm_goto_out', 'asm_imm_knownbits',
        'atomic_ops', 'bit_cast', 'c23_keywords', 'vector_builtins', 'vir_x86_dispatch')},
    # MinGW lays records out with the MS rules (-mms-bitfields is its default)
    'vcc/ms_struct_layout': ('ms-layout', 'expects the GCC layout without ms_struct (MinGW defaults to MS)'),
    'vcc/pragma_pack_pop': ('ms-layout', 'bit-field sizes under #pragma pack follow the MS rules on MinGW'),
    # clang's code for the target
    'gcc/20031003-1': ('ub-trap', 'undefined behaviour reached: unreachable is int3 on Windows targets (TrapUnreachable)'),
    'gcc/921202-1': ('ub-trap', 'undefined behaviour reached: unreachable is int3 on Windows targets (TrapUnreachable)'),
    'gcc/pr79286': ('ub-trap', 'undefined behaviour reached: unreachable is int3 on Windows targets (TrapUnreachable)'),
    'gcc/built-in-setjmp': ('sjlj', "__builtin_setjmp/__builtin_longjmp: clang's lowering for the target does not come back"),
    'gcc/pr84521': ('sjlj', "__builtin_setjmp/__builtin_longjmp: clang's lowering for the target does not come back"),
    'vcc/builtin_setjmp': ('sjlj', "__builtin_setjmp/__builtin_longjmp: clang's lowering for the target does not come back"),
    'vcc/register_locals_setjmp': ('sjlj', '__builtin_setjmp with SysV callee-saved register variables'),
    'vcc/frame_depth': ('frame', '__builtin_frame_address(N): rbp is not the saved-rbp slot (.seh_setframe offset)'),
    'vcc/asm_inline_imm': ('asm', 'an asm "i" operand that is a symbol address: not accepted for COFF'),
    # the C library: UCRT, not glibc
    'vcc/vir_x86_frame': ('libc', 'backtrace() is glibc (execinfo.h)'),
    'vcc/x86_abs_mullo': ('libc', "immintrin.h's mm_malloc.h includes <malloc.h> on _WIN32 (not in the test runtime)"),
    'tcc/129_scopes': ('abi', 'asm labels with a leading underscore on _WIN32 (i386 only; x64 has none)'),
}


def sh(cmd, timeout=60, **kw):
    return ct.sh(cmd, timeout=timeout, **kw)


# ---------------- --seh: the unwind data of vcc's objects ----------------
#
# What the Windows unwinder relies on (PLAN-win64.md WIN3): every function that
# moves rsp has a RUNTIME_FUNCTION; its prolog codes describe its first
# PrologSize bytes exactly (push -> PUSH_NONVOL, sub rsp -> ALLOC, mov to
# [rsp] -> SAVE_NONVOL, movaps to [rsp] -> SAVE_XMM128, lea/mov rbp ->
# SET_FPREG, each at the offset after its instruction); and an epilogue is
# found by decoding, so every ret or tail jmp must follow the canonical form:
# add rsp/lea rsp from the frame register, pops, then the ret/jmp.  A
# function without unwind data must be a leaf that never moves rsp.

OBJDUMP = hostenv.llvm('llvm-objdump')
READOBJ = hostenv.llvm('llvm-readobj')
_X86REG = {'RAX': 'rax', 'RCX': 'rcx', 'RDX': 'rdx', 'RBX': 'rbx', 'RSP': 'rsp', 'RBP': 'rbp', 'RSI': 'rsi', 'RDI': 'rdi',
           **{f'R{k}': f'r{k}' for k in range(8, 16)}, **{f'XMM{k}': f'xmm{k}' for k in range(16)}}


def _num(t):
    t = t.strip().lstrip('$')
    neg = t.startswith('-')
    v = int(t.lstrip('-'), 0) if t.lstrip('-') else 0
    return -v if neg else v


def seh_functions(obj):
    """{start symbol: (end offset, prolog size, [(offset, op, reg, value)])} from llvm-readobj --unwind"""
    rc, out, err = sh([READOBJ, '--unwind', obj], timeout=60)
    fns, cur = {}, None
    for line in out.decode(errors='replace').splitlines():
        line = line.strip()
        m = re.match(r'StartAddress: (\S+)', line)
        if m:
            cur = {'sym': m.group(1), 'codes': []}
            continue
        if cur is None:
            continue
        m = re.match(r'EndAddress: (\S+)(?: \+(0x[0-9A-Fa-f]+))?', line)
        if m:
            cur['end'] = int(m.group(2), 16) if m.group(2) else 0
            continue
        m = re.match(r'PrologSize: (\d+)', line)
        if m:
            cur['prolog'] = int(m.group(1))
            continue
        m = re.match(r'0x([0-9A-Fa-f]+): (\w+)(.*)', line)
        if m:
            off, op, rest = int(m.group(1), 16), m.group(2), m.group(3)
            reg = re.search(r'reg=(\w+)', rest)
            val = re.search(r'(?:size|offset)=(0x[0-9A-Fa-f]+|\d+)', rest)
            cur['codes'].append((off, op, _X86REG.get(reg.group(1), reg.group(1).lower()) if reg else None,
                                 int(val.group(1), 0) if val else 0))
            continue
        if line.startswith('}') and 'end' in cur and 'prolog' in cur:
            fns[cur['sym']] = cur
            cur = None
    return fns


def seh_check(obj):
    """the problems of OBJ's unwind data and epilogues (empty: none)"""
    fns = seh_functions(obj)
    rc, out, err = sh([OBJDUMP, '-d', '--no-show-raw-insn', obj], timeout=60)
    if rc:
        return ['llvm-objdump failed']
    funcs, name = {}, None
    for line in out.decode(errors='replace').splitlines():
        m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
        if m:
            name = m.group(1)
            funcs[name] = []
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*?)\s*(?:#.*)?$', line)
        if m and name is not None:
            funcs[name].append((int(m.group(1), 16), m.group(2), m.group(3)))
    bad = []
    for fn, ins in funcs.items():
        if not ins:
            continue
        base = ins[0][0]
        while ins and ins[-1][1] in ('nop', 'nopw', 'nopl', 'int3', 'data16', 'cs'):
            ins = ins[:-1]	# (alignment padding after the function)
        u = fns.get(fn)
        moves = [i for i in ins if i[1] in ('pushq', 'popq', 'callq') or re.search(r',\s*%rsp$', i[2])]
        if not u:
            if moves:
                bad.append(f'{fn}: moves rsp ({moves[0][1]} {moves[0][2]}) without unwind data')
            continue
        # the prolog: each instruction its code, at the offset after it
        want = sorted(u['codes'], key=lambda c: c[0])
        got = []
        for k, (a, mn, ops) in enumerate(ins):
            if a - base >= u['prolog']:
                break
            after = (ins[k + 1][0] if k + 1 < len(ins) else a + 1) - base
            if mn == 'pushq':
                got.append((after, 'PUSH_NONVOL', ops.lstrip('%'), 0))
            elif mn == 'subq' and ops.endswith('%rsp'):
                got.append((after, 'ALLOC', None, _num(ops.split(',')[0])))
            elif mn in ('movq', 'movaps') and re.search(r'\(%rsp\)$', ops):
                r, mem = ops.split(',', 1)
                got.append((after, 'SAVE_NONVOL' if mn == 'movq' else 'SAVE_XMM128', r.strip().lstrip('%'),
                            _num(mem.strip().split('(')[0] or '0')))
            elif (mn == 'leaq' and ops.endswith('%rbp')) or (mn == 'movq' and ops == '%rsp, %rbp'):
                got.append((after, 'SET_FPREG', 'rbp', _num(ops.split('(')[0]) if mn == 'leaq' else 0))
            elif mn == 'callq' and '___chkstk_ms' in ops:
                continue
            elif mn in ('movl', 'movabsq') and ops.endswith(('%eax', '%rax')):
                continue	# (___chkstk_ms's size)
            else:
                bad.append(f'{fn}: {mn} {ops} in the prolog')
        norm = [(o, 'ALLOC' if op.startswith('ALLOC') else op, r, v) for o, op, r, v in want]
        if [(o, op, r) for o, op, r, _ in norm] != [(o, op, r) for o, op, r, _ in got] or \
           any(op in ('ALLOC', 'SAVE_NONVOL', 'SAVE_XMM128', 'SET_FPREG') and gv != wv
               for (_, op, _, wv), (_, _, _, gv) in zip(norm, got)):
            bad.append(f'{fn}: prolog codes {norm} do not describe its instructions {got}')
        # the epilogues: what precedes each ret and each jump out of the function
        end = base + u['end'] if u['end'] else ins[-1][0] + 1
        pushed = sum(1 for c in want if c[1] == 'PUSH_NONVOL')
        for k, (a, mn, ops) in enumerate(ins):
            # a jump out of the function (a tail call; an indirect jump through a register is a
            # switch or computed goto inside it)
            out_jump = mn in ('jmp', 'jmpq') and (ops.startswith('*(') or ops.startswith('*0x') or
                                                  (not ops.startswith('*') and not re.search(r'<' + re.escape(fn) + r'[+>]', ops)))
            if mn not in ('retq', 'ret') and not out_jump:
                continue
            j, pops = k - 1, 0
            while j >= 0 and ins[j][1] == 'popq':
                j, pops = j - 1, pops + 1
            restore = j >= 0 and ((ins[j][1] == 'leaq' and re.match(r'-?(0x[0-9a-f]+)?\(%rbp\), %rsp$', ins[j][2])) or
                                  (ins[j][1] == 'addq' and ins[j][2].endswith(', %rsp')))
            if pops != pushed:
                bad.append(f'{fn}+{a - base:#x}: {pops} pops before {mn}, the prolog pushes {pushed}')
            elif pushed and not restore and any(c[1] in ('ALLOC_SMALL', 'ALLOC_LARGE', 'SET_FPREG') for c in want):
                bad.append(f'{fn}+{a - base:#x}: epilogue without add/lea rsp ({ins[j][1] if j >= 0 else "-"} {ins[j][2] if j >= 0 else ""})')
            if a >= end:
                bad.append(f'{fn}+{a - base:#x}: {mn} past the RUNTIME_FUNCTION end')
    return bad


def rt_obj():
    """rt/win64.c, built by clang once per change"""
    os.makedirs(OUT, exist_ok=True)
    stamp = f'{RTO}.key'
    try:
        if open(stamp).read() == RTHASH + TOOLFP and os.path.exists(RTO):
            return
    except OSError:
        pass
    subprocess.run([CL] + BASE + ['-c', RTSRC, '-o', RTO + '.tmp'], check=True)
    os.replace(RTO + '.tmp', RTO)
    open(stamp, 'w').write(RTHASH + TOOLFP)


def link(objs, exe):
    rc, _, err = sh(LINK + objs + [RTO, '-o', exe], timeout=60)
    return rc, err.decode(errors='replace')


EXC = re.compile(r'rt: unhandled exception ([0-9a-f]{8})')
EXC_NAMES = {'c0000005': 'access violation', 'c00000fd': 'stack overflow', 'c0000094': 'integer divide by zero',
             'c0000095': 'integer overflow', 'c000001d': 'illegal instruction', '80000003': 'breakpoint',
             'c0000409': 'fast fail', 'c0000096': 'privileged instruction', '80000002': 'misaligned data'}


def run(exe, timeout=20):
    """(rc, stdout, stderr) of EXE under Wine; rc 'timeout' when it did not finish"""
    rc, out, err = sh([WINE, exe], timeout=timeout, stdin=subprocess.DEVNULL, env=WENV, cwd=os.path.dirname(exe))
    return rc, out.decode('latin-1').replace('\r\n', '\n'), err.decode(errors='replace')


def wine_failure(rc, err, timeout):
    """an exception or a timeout of the reference program under Wine: the
    program, not the oracle, so an exclusion with its reason (as vcc-x86.py's
    ref-runtime-fail), or None"""
    if rc == 'timeout':
        return {'skip': 'ref-runtime-fail', 'why': f'timed out under Wine after {timeout}s', 'phase': 'wine-run'}
    m = EXC.search(err)
    if rc == 255 and m:
        code = m.group(1)
        return {'skip': 'ref-runtime-fail', 'why': f'exception {code} ({EXC_NAMES.get(code, "?")}) under Wine',
                'phase': 'wine-run', 'stderr': err[-2000:]}
    return None


def flags_of(suite, name, src):
    flags = ct.dg_flags(src) if suite == 'gcc' else \
        [f for f in ct.dg_flags(src) if ct.INLINE_FLAGS.match(f)] if suite == 'inline' else \
        ct.exec_marks(src)[0] if suite in ('vcc', 'win64') else []
    if name.startswith('76_dollars'):
        flags = ['-fdollars-in-identifiers']
    return flags


def reference(suite, name, path, flags, wd):
    with open(path, 'rb') as source:
        src = source.read().decode('latin-1')
    # the path too: a test that prints __FILE__ changes when its file moves
    key = hashlib.sha1(('win64-ref-v1 ' + os.path.relpath(path, ct.R) + ' ' + src + ' '.join(flags) + ' '.join(ct.CLFL)
                        + ' '.join(BASE + LINK) + RTHASH + TOOLFP).encode()).hexdigest()[:16]
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
        rc, err = link([f'{wd}/ref.o'], f'{wd}/ref.exe')
        failure = ct.oracle_error(rc, err, 'link', 60)
        if failure:
            return dict(r, **failure)
        if rc:
            m = re.search(r"undefined (?:symbol|reference to):? ['`]?(\w+)", err)
            r['skip'] = 'ref-fail'; r['why'] = 'link: undefined ' + m.group(1) if m else 'link: ' + ct.first_err(err)
        else:
            rc, out, err = run(f'{wd}/ref.exe')
            if rc == 'timeout':	# once more: a loaded machine, or Wine's first start
                rc, out, err = run(f'{wd}/ref.exe')
            failure = wine_failure(rc, err, 20)
            if failure:
                r.update(failure)
                r['out'] = out
            elif not isinstance(rc, int) or rc < 0:
                return dict(r, error='oracle-run', why=f'wine: {rc}: ' + ct.first_err(err))
            else:
                r['rc'] = rc; r['out'] = out
                if suite in ('gcc', 'vcc', 'win64', 'inline') and rc != 0:
                    r['skip'] = 'ref-fail'
                    r['why'] = 'clang build aborts (exit 3)' if rc == 3 else f'clang build exits {rc}'
                    if err.strip():
                        r['why'] += ': ' + ct.first_err(err)
    os.makedirs(os.path.dirname(cf), exist_ok=True)
    with open(cf, 'w') as cache:
        json.dump(r, cache)
    shutil.rmtree(wd, ignore_errors=True)
    return r


def tests(suite, pat):
    """vcc-ctests.py's suites and src/test/win64 (the Windows-only tests)"""
    ts = [] if suite == 'win64' else ct.tests(suite, pat) if suite else [t for s in ('gcc', 'tcc', 'vcc') for t in ct.tests(s, pat)]
    if suite in (None, 'win64'):
        d = f'{ct.T}/win64'
        ts += [('win64', f[:-2], f'{d}/{f}') for f in sorted(os.listdir(d)) if f.endswith('.c')
               if not pat or re.search(pat, f'win64/{f[:-2]}')]
    return ts


def static_skip(suite, name, src):
    if suite == 'tcc' and name in ct.TCC_SKIP and ct.TCC_SKIP[name] not in TCC_RUN:
        return 'tcc-specific', ct.TCC_SKIP[name]
    if suite == 'gcc' and name in ct.GCC_SKIP:
        return 'compiler-specific', ct.GCC_SKIP[name]
    if suite == 'inline' and name in ct.INLINE_SKIP:
        return 'compiler-specific', ct.INLINE_SKIP[name]
    if f'{suite}/{name}' in WIN64_SKIP:
        return 'win64', WIN64_SKIP[f'{suite}/{name}']
    if suite == 'vcc' and re.search(r'/\*\s*vcc-noref:', src):
        return 'noref', 'no clang reference'
    return None


def one(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    s = static_skip(suite, name, src)
    if s:
        return t, 'skip', s[0], s[1]
    flags = flags_of(suite, name, src)
    wd = f'{OUT}/w/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    cwd = os.path.dirname(path)
    ref = reference(suite, name, path, flags, wd)
    if 'error' in ref:
        return t, 'error', ref['error'], ref['why']
    if 'skip' in ref:
        shutil.rmtree(wd, ignore_errors=True)
        w = WIN64_WHY.get(f'{suite}/{name}')
        if w:
            return t, 'skip', 'win64-' + w[0], f'{w[1]} [{ref["why"]}]'
        return t, 'skip', ref['skip'], ref['why']
    if f'{suite}/{name}' in WIN64_WHY:
        return t, 'error', 'stale', 'the reference runs now: remove it from WIN64_WHY'
    if REF_ONLY:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    os.makedirs(wd, exist_ok=True)
    vfl = BASE + ct.CLFL + flags
    if OBJ:
        # --obj: vpp -c (vcc's assembler and object writer), linked and run the same way
        rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd)
        if not rc:
            open(f'{wd}/vcc.s', 'wb').write(open(f'{wd}/vcc.o', 'rb').read())	# (the pass key: the object's bytes)
    else:
        rc, _, err = sh([VPP, '-S'] + vfl + [path, '-o', f'{wd}/vcc.s'], cwd=cwd)
    if rc:
        e = err.decode(errors='replace')
        return t, 'fail', 'emit', ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
    pk = hashlib.sha1(open(f'{wd}/vcc.s', 'rb').read() + ref['key'].encode()).hexdigest()
    pf = f'{OUT}/pass{"-obj" if OBJ else ""}{"-seh" if SEH else ""}/{suite}/{name}'
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
    if SEH:
        bad = seh_check(f'{wd}/vcc.o')
        if bad:
            open(f'{wd}/seh.txt', 'w').write('\n'.join(bad) + '\n')
            return t, 'fail', 'seh', bad[0][:150]
    rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc.exe')
    if rc:
        m = re.search(r"undefined (?:symbol|reference to):? ['`]?(\w+)", err)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else ct.first_err(err)
    rc, out, err = run(f'{wd}/vcc.exe')
    if rc == ref['rc'] and out == ref['out']:
        os.makedirs(os.path.dirname(pf), exist_ok=True)
        open(pf, 'w').write(pk)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    m = EXC.search(err) if rc == 255 else None
    why = f'exception {m.group(1)}' if m else f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    open(f'{wd}/vcc.out', 'w').write(out); open(f'{wd}/ref.out', 'w').write(ref['out'])
    return t, 'fail', 'run', why


# --objcmp: vcc's .s assembled by vasm and by clang -c, the objects compared byte
# for byte, and vpp -c's object against both (WIN4/WIN5)
VASM = os.path.join(os.path.dirname(VPP), 'vasm')


def objcmp(t):
    suite, name, path = t
    wd = f'{OUT}/oc/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    src = open(path, 'rb').read().decode('latin-1')
    flags = flags_of(suite, name, src)
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


NOCACHE = REF_ONLY = OBJ = SEH = False


def init(nocache, ref_only, obj, seh=False):
    global NOCACHE, REF_ONLY, OBJ, SEH
    NOCACHE, REF_ONLY, OBJ, SEH = nocache, ref_only, obj, seh


if __name__ == '__main__':
    av = [part for arg in sys.argv[1:]
          for part in (('-j', arg[2:]) if re.fullmatch(r'-j[0-9]+', arg) else (arg,))]
    j, suite, show, verbose = 6, None, None, False
    flags = {a for a in av if a in ('--no-cache', '--ref-only', '--obj', '--objcmp', '-v', '--vir', '--seh')}
    av = [a for a in av if a not in flags]
    nocache, ref_only, obj, ocmode, verbose, vir, seh = (f in flags for f in ('--no-cache', '--ref-only', '--obj', '--objcmp', '-v', '--vir', '--seh'))
    if vir:
        os.environ['VCC_VIR'] = 'l'	# (inherited by every vpp the workers run)
    for opt in ('-j', '--suite', '--show'):
        if opt in av:
            k = av.index(opt); v = av[k + 1]; del av[k:k + 2]
            if opt == '-j': j = int(v)
            elif opt == '--suite': suite = v
            else: show = v
    pat = av[0] if av else None
    why = hostenv.mingw_missing()
    if why:
        sys.exit(f'vcc-win64.py: {why}')
    ts = tests(suite, pat)
    if not ts:
        sys.exit('no tests matched the requested suite/pattern')
    t0 = time.time()
    rt_obj()
    hostenv.wine_start(PREFIX)
    with Pool(j, initializer=init, initargs=(nocache, ref_only, obj, seh)) as pool:
        res = pool.map(objcmp if ocmode else one, ts, chunksize=1)
    by = defaultdict(Counter)
    whys = defaultdict(Counter)
    for (s, n, _), st, cat, why in res:
        by[s][st] += 1
        if st != 'pass':
            by[s][f'{st}:{cat}'] += 1
            whys[(st, cat)][re.sub(r'\b\d{3,}\b', 'N', why) if st == 'skip' else why] += 1
        if verbose and st != 'pass':
            print(f'{st.upper()} {cat:5s} {s}/{n}: {why}')
        if show and st != 'pass' and cat == show:
            print(f'{st} {cat} {s}/{n}: {why}')
    for (st, cat), c in sorted(whys.items()):
        if st in ('fail', 'error') or verbose or ref_only:
            print(f'--- {st} {cat}: {sum(c.values())}')
            for w, k in c.most_common(int(os.environ.get('SHOW', 8))):
                print(f'  {k:4d}  {w[:150]}')
    tot = Counter()
    for s in sorted(by):
        tot.update(by[s])
    for s, c in sorted(by.items()) + [('all', tot)]:
        run_ = c['pass'] + c['fail'] + c['error']
        det = ' '.join(f'{k}={v}' for k, v in sorted(c.items()) if ':' in k)
        if ref_only:
            print(f'{s:4s} {c["pass"] + c["fail"] + c["error"] + c["skip"]} tests, {c["pass"]} reference-ok, '
                  f'{c["skip"]} excluded, {c["error"]} oracle errors   {det}')
        else:
            print(f'{s:4s} {c["pass"]}/{run_} pass ({100 * c["pass"] / max(run_, 1):.1f}%), {c["skip"]} skipped   {det}')
    print(f'{time.time() - t0:.1f} s')
    sys.exit(1 if tot['fail'] or tot['error'] else 0)
