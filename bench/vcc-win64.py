#!/usr/bin/env python3
"""vcc-win64.py: the C test suites through vcc's Windows x64 target (PLAN-win64.md).

  bench/vcc-win64.py [-j N] [--suite gcc|tcc|vcc|win64|inline] [--ref-only] [--obj] [--objcmp]
                     [--own (the default)] [--clang-as] [--vir] [--seh] [--vasm-ref] [--vlink-ref] [--vccrt-ref] [--export DIR]
                     [-v] [--show CAT] [--no-cache] [PAT]

Every test of the three suites of vcc-x86.py (gcc torture, tcc tests2, our
own), and of src/test/win64 (Windows-only: the calling convention's rules,
TLS, dllimport, constructors, and the unwinder walking, longjmp-ing and
resuming out of vcc's frames; --suite inline adds the inlining suite) is built for x86_64-w64-mingw32
twice: by clang (the oracle) and by vcc, each linked by lld against the
llvm-mingw sysroot (UCRT, static: no DLL beyond the system's) and run under
Wine.  vcc passes when its program prints what clang's prints and exits the
same way.  By default vcc's side is vcc's own toolchain (--own, below);
--clang-as uses `vpp -S`, assembled by clang and linked by lld; --obj
uses the direct `vpp -c` path, and --objcmp compares both with clang's
assembler.  --ref-only builds and runs clang's side only (WIN0).  --vir:
vcc's side through VIR (VCC_VIR=l: -O2's SSA path, falling back per
function to the direct emitter).  --seh: also check every vcc object's
unwind data (seh_check below: each function's epilogue decoded as the
Windows unwinder does, llvm-readobj --unwind against the bytes).
--vasm-ref checks vcc's assembler and COFF writer alone (WIN4/WIN5): clang -S of each
test, assembled by vasm --target=x86_64-w64-mingw32, linked and run as the
reference is; it passes when the output and exit code are the reference's,
and the object is also compared with clang -c of the same .s (stage
'objdiff' when it differs but runs).
--own: vcc's own toolchain end to end, nothing from LLVM on vcc's side:
`vpp -c` (the direct object path, WIN5) compiles the test and the test
runtime, and vcc's driver links (`vpp --target=... -static ... -o x.exe`:
vlink, libvccrt.a built by vcc, the MinGW-w64 CRT), with PATH emptied so
that no tool can be found by name; the link map must name no input from
compiler-rt, libgcc, libunwind or clang's lib/ directory (`fail foreign`).
--vlink-ref (WIN7) links clang's object twice with the same command line,
by lld and by vlink (clang --ld-path, SOURCE_DATE_EPOCH fixed): vlink's
program must run as the reference does (runs-ok) and its file should be
byte-identical to lld's (image-identical; a test that runs but differs is
`fail differs`, with the first differing structure in the reason).
--vccrt-ref (WIN7R) links clang's object with an explicit link line that has
vcc's run-time library (BUILD/vccrt/libvccrt.a, src/runtime/build.sh) where
clang's has compiler-rt's builtins, once by lld and once by vlink, and runs
both: each must give the reference result, and the link map must show no
input from compiler-rt, libgcc or libunwind (`fail foreign` otherwise).

--export DIR (WIN9; any mode) copies the program of every passing test to
DIR/exe/SUITE/NAME.exe and writes DIR/vcc-win64.manifest.json: per test the
executable, its argv and stdin (none), the reference's stdout ("\n" line
ends) and exit code, and a timeout.  bench/win64-native.py runs such a
directory on real Windows (CI's `native` job).  The program exported is the
mode's own product: vcc's (default, --obj), vlink's with libvccrt.a
(--vccrt-ref), vlink's with compiler-rt (--vlink-ref) or clang's own
(--ref-only).  A test that passes from the pass cache is linked again (the
link is deterministic, SOURCE_DATE_EPOCH) but not run.

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
import glob, hashlib, importlib.util, json, os, re, shutil, subprocess, sys, time
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

# vcc's known failures for the target, each with its TODO.md item: a failure of
# vcc's side is `xfail`, a pass of one is an XPASS error (remove the entry);
# (suite/name, run|seh): the stage it fails at
XFAIL = {
    ('tcc/95_bitfields_ms', 'run'): 'MS layout of a packed record after :0 (TODO.md)',
    ('vcc/dead_results', 'seh'): 'naked is not implemented on x86-64: its asm retq sits in a frame (TODO.md)',
}

# tcc tests2 that vcc-ctests.py skips but that mean something here
TCC_RUN = {'x86 asm', 'win32', 'msvc layout'}
# tests that cannot work on Windows for a reason the reference run does not show
WIN64_SKIP = {
    # prints a field of fresh malloc memory when it happens to have a high bit
    # set: UCRT's heap leaves it differently from run to run (seen 2026-11-14:
    # the reference printed 17742, the same image later nothing)
    'gcc/941014-2': 'reads uninitialized malloc memory: output depends on the heap',
}
# Why clang's own build does not run on Windows: each test whose reference is
# excluded only for this target (it passes for x86_64-linux-gnu) has its
# category and reason here, from the reference run of 2026-11-13.  The
# exclusion itself still comes from running clang's side; an entry whose
# reference now runs is reported as stale.  Exclusions not listed here are
# the same for x86-64 Linux (gcc-only flags and builtins, clang's own limits).
WIN64_WHY = {
    # LP64: the program tests 64-bit values held in long (our tests can move to long long)
    'gcc/comp-goto-1': ('lp64', 'label addresses in an unsigned long'),
    'gcc/pr36339': ('lp64', 'pointers in an unsigned long (my_uintptr_t)'),
    # our tests moved to long long (2026-11-14); what is left is not the data model
    'vcc/asm_clobber_all': ('asm', "clang's own program faults under Wine (an asm that clobbers every register)"),
    'vcc/asm_imm_knownbits': ('asm', 'an asm operand expression clang does not accept for COFF'),
    'vcc/bit_cast': ('ms-layout', 'a bit-field record bit-cast to an integer: the MS layout differs'),
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
DWARFDUMP = hostenv.llvm('llvm-dwarfdump')
DEBUG = any(f.startswith('-g') and f != '-g0' for f in BASE)	# VCC_CFLAGS=-g...: the linked image's DWARF is verified


NM = hostenv.llvm('llvm-nm')
SYMBOLIZER = hostenv.llvm('llvm-symbolizer')


def dwarf_bad(exe, obj=None, source=None):
    """with -g: llvm-dwarfdump --verify of the linked image, and llvm-symbolizer at each function
    the test's object defines gives that function and a line of the test's source; '' when good (WIN6)"""
    if not DEBUG:
        return ''
    p = subprocess.run([DWARFDUMP, '--verify', exe], capture_output=True, text=True)
    if p.returncode or 'No errors.' not in p.stdout:
        return (p.stdout + p.stderr).strip().splitlines()[-1][:150] if (p.stdout + p.stderr).strip() else f'exit {p.returncode}'
    if not subprocess.run([DWARFDUMP, '--debug-line', exe], capture_output=True, text=True).stdout.count('end_sequence'):
        return 'no line table in the image'
    if not obj:
        return ''
    fns = {l.split()[-1] for l in subprocess.run([NM, '--defined-only', obj], capture_output=True, text=True).stdout.splitlines()
           if len(l.split()) == 3 and l.split()[1] in 'Tt'}
    at = defaultdict(list)	# name -> addresses in the image (a static name can be in the runtime too)
    for l in subprocess.run([NM, '--defined-only', exe], capture_output=True, text=True).stdout.splitlines():
        if len(l.split()) == 3:
            at[l.split()[2]].append(l.split()[0])
    src = open(source, errors='replace').read()	# (a function defined by top-level asm has no rows)
    names = sorted(n for n in fns if len(at.get(n, ())) == 1 and not re.search(rf'(^|[\s"]|\\n){re.escape(n)}:', src))
    if not names:
        return ''
    out = subprocess.run([SYMBOLIZER, f'--obj={exe}'] + ['0x' + at[n][0] for n in names], capture_output=True, text=True).stdout
    for n, res in zip(names, out.strip().split('\n\n')):
        r = res.split('\n')
        same = len(r) >= 1 and (r[0] == n or at[n][0] in at.get(r[0], ()))	# (an alias names the same address)
        if len(r) < 2 or not same or r[1].startswith('??') or r[1].rsplit(':', 2)[1] == '0':
            return f'llvm-symbolizer at {n}: {" ".join(r)[:120]}'
    return ''
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


RTO_OWN = f'{OUT}/rt-own.o'
NOPATH = f'{OUT}/empty-path'	# --own: vcc's side runs with PATH=this (an empty directory)


def own_env():
    os.makedirs(NOPATH, exist_ok=True)
    return dict(os.environ, PATH=NOPATH, VCC_VCCRT=VCCRT)


def rt_obj_own():
    """--own: rt/win64.c built by vpp -c (keyed on vpp and the source)"""
    os.makedirs(OUT, exist_ok=True)
    key = RTHASH + _file_hash(VPP)
    try:
        if open(RTO_OWN + '.key').read() == key and os.path.exists(RTO_OWN):
            return
    except OSError:
        pass
    subprocess.run([VPP, '-c'] + BASE + [RTSRC, '-o', RTO_OWN + '.tmp'], check=True, env=own_env())
    os.replace(RTO_OWN + '.tmp', RTO_OWN)
    open(RTO_OWN + '.key', 'w').write(key)


def _file_hash(f):
    try:
        return hashlib.sha1(open(f, 'rb').read()).hexdigest()[:16]
    except OSError:
        return '?'


def own_link(objs, exe):
    """--own: vcc's driver links (vlink, libvccrt.a, the CRT); (rc, stderr, foreign inputs)"""
    mp = exe + '.map'
    rc, _, err = sh([VPP, TGT, f'--sysroot={MINGW_SYSROOT}', '-static', '-v'] + objs + [RTO_OWN, '-o', exe, f'-Wl,-Map={mp}'],
                    timeout=60, env=dict(own_env(), SOURCE_DATE_EPOCH='1700000000'))
    err = err.decode(errors='replace')
    bad = []
    if not rc:
        line = next((l for l in err.splitlines() if l.startswith('vlink ')), '').replace(MINGW_SYSROOT, 'SYSROOT')	# (llvm-mingw: only its sysroot)
        bad = sorted({m.group(0) for m in re.finditer(r'\S*(?:clang_rt|libgcc|libunwind|compiler-rt|lib/clang/|llvm)\S*', line, re.I)})
        if not line:
            bad.append('(no link line)')
        # every input of the map: our objects, the sysroot's CRT files and archive members, libvccrt.a's members
        try:
            text = open(mp, errors='replace').read()
        except OSError:
            text = None
            bad.append('(no map)')
        own = {os.path.basename(o) for o in glob.glob(f'{os.path.dirname(VCCRT)}/obj/*.o')}
        for m in re.finditer(r'\s(\S+?):\(', text or ''):
            f = m.group(1)
            if f in own or f.startswith('lib64_') or f.startswith(MINGW_SYSROOT + '/x86_64-w64-mingw32/lib/') or \
               (f.startswith('/') and not f.startswith(MINGW_SYSROOT) and not re.search(r'/lib/clang/|clang_rt|libgcc|libunwind', f)):
                continue
            bad.append(f)
    return rc, err, sorted(set(bad))


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


def export(t, exe, ref):
    """--export: EXE (a passing test's program) into EXPORT/exe and its
    manifest entry into EXPORT/entries (main() gathers them)"""
    suite, name, path = t
    rel = f'exe/{suite}/{name}.exe'
    os.makedirs(os.path.dirname(f'{EXPORT}/{rel}'), exist_ok=True)
    shutil.copyfile(exe, f'{EXPORT}/{rel}')
    entry = {'name': f'{suite}/{name}', 'exe': rel, 'argv': [], 'stdin': None,
             'stdout': ref['out'], 'exit': ref['rc'], 'timeout': 20,
             'source': os.path.relpath(path, ct.R)}
    os.makedirs(f'{EXPORT}/entries/{suite}', exist_ok=True)
    with open(f'{EXPORT}/entries/{suite}/{name}.json', 'w') as f:
        json.dump(entry, f)


def write_manifest(export_dir, mode):
    """EXPORT_DIR/vcc-win64.manifest.json from the entries the workers wrote"""
    tests = []
    for directory, _, files in sorted(os.walk(f'{export_dir}/entries')):
        for f in sorted(files):
            with open(os.path.join(directory, f)) as e:
                tests.append(json.load(e))
    shutil.rmtree(f'{export_dir}/entries', ignore_errors=True)
    os.makedirs(export_dir, exist_ok=True)
    with open(f'{export_dir}/vcc-win64.manifest.json', 'w') as f:
        json.dump({'version': 1, 'producer': f'bench/vcc-win64.py {mode}', 'target': 'x86_64-w64-mingw32',
                   'tests': tests}, f, indent=0)
    return len(tests)


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
        if EXPORT:	# clang's object and link, as reference() built them
            os.makedirs(wd, exist_ok=True)
            rc, _, err = sh([CL] + BASE + ct.CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=cwd)
            if not rc:
                rc, err = link([f'{wd}/ref.o'], f'{wd}/ref.exe')
            if rc:
                return t, 'error', 'export', ct.first_err(err if isinstance(err, str) else err.decode(errors='replace'))
            export(t, f'{wd}/ref.exe', ref)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    os.makedirs(wd, exist_ok=True)
    vfl = BASE + ct.CLFL + flags
    if OBJ or OWN:
        # --obj: vpp -c (vcc's assembler and object writer), linked and run the same way
        rc, _, err = sh([VPP, '-c'] + vfl + [path, '-o', f'{wd}/vcc.o'], cwd=cwd, **({'env': own_env()} if OWN else {}))
        if not rc:
            open(f'{wd}/vcc.s', 'wb').write(open(f'{wd}/vcc.o', 'rb').read())	# (the pass key: the object's bytes)
    else:
        rc, _, err = sh([VPP, '-S'] + vfl + [path, '-o', f'{wd}/vcc.s'], cwd=cwd)
    if rc:
        e = err.decode(errors='replace')
        return t, 'fail', 'emit', ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vpp: signal/timeout {rc}: ' + ct.first_err(e)
    pk = hashlib.sha1(open(f'{wd}/vcc.s', 'rb').read() + ref['key'].encode() + (OWNKEY.encode() if OWN else b'')).hexdigest()
    pf = f'{OUT}/pass{"-own" if OWN else "-obj" if OBJ else ""}{"-seh" if SEH else ""}/{suite}/{name}'
    cached = cache_hit(pf, pk)
    if cached and not EXPORT:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    rc, _, err = (0, b'', b'') if OBJ or OWN else sh([CL, TGT, '-c', f'{wd}/vcc.s', '-o', f'{wd}/vcc.o'])
    if rc:
        m = re.search(r'error: (.*)', err.decode(errors='replace'))
        return t, 'fail', 'as', re.sub(r'\d+', 'N', m.group(1)) if m else ct.first_err(err.decode(errors='replace'))
    if SEH:
        bad = seh_check(f'{wd}/vcc.o')
        if bad:
            open(f'{wd}/seh.txt', 'w').write('\n'.join(bad) + '\n')
            return t, 'fail', 'seh', bad[0][:150]
    if OWN:
        rc, err, bad = own_link([f'{wd}/vcc.o'], f'{wd}/vcc.exe')
        if not rc and bad:
            return t, 'fail', 'foreign', ' '.join(bad)[:150]
    else:
        rc, err = link([f'{wd}/vcc.o'], f'{wd}/vcc.exe')
    if rc:
        m = re.search(r"undefined (?:symbol|reference to):? ['`]?(\w+)", err)
        return t, 'fail', 'link', 'undefined ' + m.group(1) if m else ct.first_err(err)
    bad = '' if cached else dwarf_bad(f'{wd}/vcc.exe', f'{wd}/vcc.o', path)
    if bad:
        return t, 'fail', 'dwarf', bad
    rc, out, err = (ref['rc'], ref['out'], '') if cached else run(f'{wd}/vcc.exe')
    if rc == 'timeout':	# once more, as for the reference
        rc, out, err = run(f'{wd}/vcc.exe')
    if rc == ref['rc'] and out == ref['out']:
        os.makedirs(os.path.dirname(pf), exist_ok=True)
        open(pf, 'w').write(pk)
        if EXPORT:
            export(t, f'{wd}/vcc.exe', ref)
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


def vasmref(t):
    """--vasm-ref: clang -S, vasm, lld, Wine against the reference"""
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    s = static_skip(suite, name, src)
    if s:
        return t, 'skip', s[0], s[1]
    flags = flags_of(suite, name, src)
    wd = f'{OUT}/va/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    ref = reference(suite, name, path, flags, wd)
    if 'error' in ref:
        return t, 'error', ref['error'], ref['why']
    if 'skip' in ref:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'skip', ref['skip'], ref['why']
    os.makedirs(wd, exist_ok=True)
    rc, _, err = sh([CL] + BASE + ct.CLFL + flags + ['-S', path, '-o', f'{wd}/ref.s'], cwd=os.path.dirname(path))
    if rc:
        return t, 'skip', 'clang-S', ct.first_err(err.decode(errors='replace'))
    rc, _, err = sh([VASM, TGT, f'{wd}/ref.s', '-o', f'{wd}/vas.o'])
    if rc:
        e = err.decode(errors='replace')
        m = re.search(r'error: (.*)', e)
        return t, 'fail', 'vasm', (m.group(1) if m else ct.first_err(e))[:120]
    pk = hashlib.sha1(open(f'{wd}/vas.o', 'rb').read() + ref['key'].encode()).hexdigest()
    pf = f'{OUT}/pass-vasm/{suite}/{name}'
    if not NOCACHE:
        try:
            if open(pf).read() == pk:
                shutil.rmtree(wd, ignore_errors=True)
                return t, 'pass', '', ''
        except OSError:
            pass
    rc, err = link([f'{wd}/vas.o'], f'{wd}/vas.exe')
    if rc:
        return t, 'fail', 'link', ct.first_err(err)
    rc, out, err = run(f'{wd}/vas.exe')
    if rc != ref['rc'] or out != ref['out']:
        m = EXC.search(err) if rc == 255 else None
        return t, 'fail', 'run', f'exception {m.group(1)}' if m else f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
    rc, _, _ = sh([CL, TGT, '-c', f'{wd}/ref.s', '-o', f'{wd}/clang.o'])
    if rc or open(f'{wd}/clang.o', 'rb').read() != open(f'{wd}/vas.o', 'rb').read():
        return t, 'fail', 'objdiff', 'runs, but the object is not clang -c\'s'
    os.makedirs(os.path.dirname(pf), exist_ok=True)
    open(pf, 'w').write(pk)
    shutil.rmtree(wd, ignore_errors=True)
    return t, 'pass', '', ''


# --vlink-ref: clang's object linked by lld and by vlink with the same link line
VLINK = os.path.join(os.path.dirname(VPP), 'vlink')
VLENV = dict(os.environ, SOURCE_DATE_EPOCH='1700000000')


def _vlink_hash():
    try:
        return hashlib.sha1(open(VLINK, 'rb').read()).hexdigest()[:16]
    except OSError:
        return '?'


def pe_differs(a, b):
    """where two PE files differ first: the headers, a section (by name), the symbol table"""
    import struct
    why = '' if len(a) == len(b) else f'size {len(b)} vs lld {len(a)}'
    try:
        pe = struct.unpack_from('<I', a, 60)[0]
        nsec = struct.unpack_from('<H', a, pe + 6)[0]
        sh_ = pe + 24 + struct.unpack_from('<H', a, pe + 20)[0]
        end = sh_ + 40 * nsec
        if a[:end] != b[:end]:
            off = next(i for i in range(min(end, len(b))) if a[i] != b[i])
            return f'headers (offset {off:#x}) {why}'.strip()
        for i in range(nsec):
            name, vs, va, rs, ro = struct.unpack_from('<8sIIII', a, sh_ + 40 * i)
            if a[ro:ro + rs] != b[ro:ro + rs]:
                return f'section {name.rstrip(bytes(1)).decode(errors="replace")} {why}'.strip()
        return f'symbol table {why}'.strip()
    except (struct.error, StopIteration, IndexError):
        return f'unreadable {why}'.strip()


def vlinkref(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    s = static_skip(suite, name, src)
    if s:
        return t, 'skip', s[0], s[1]
    flags = flags_of(suite, name, src)
    wd = f'{OUT}/vl/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    ref = reference(suite, name, path, flags, wd)
    if 'error' in ref:
        return t, 'error', ref['error'], ref['why']
    if 'skip' in ref:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'skip', ref['skip'], ref['why']
    os.makedirs(wd, exist_ok=True)
    rc, _, err = sh([CL] + BASE + ct.CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=os.path.dirname(path))
    if rc:
        return t, 'error', 'compile', ct.first_err(err.decode(errors='replace'))
    obj = open(f'{wd}/ref.o', 'rb').read()
    pk = hashlib.sha1(obj + ref['key'].encode() + VLHASH.encode()).hexdigest()
    pf = f'{OUT}/pass-vlink/{suite}/{name}'
    objs = [f'{wd}/ref.o', RTO]
    if cache_hit(pf, pk):
        if EXPORT:	# the same link again (deterministic), not run
            rc, _, err = sh(LINK + [f'--ld-path={VLINK}'] + objs + ['-o', f'{wd}/vlink.exe'], timeout=60, env=VLENV)
            if rc:
                return t, 'fail', 'vlink', ct.first_err(err.decode(errors='replace'))[:150]
            export(t, f'{wd}/vlink.exe', ref)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    rc, _, err = sh(LINK + objs + ['-o', f'{wd}/lld.exe'], timeout=60, env=VLENV)
    if rc:
        return t, 'error', 'lld', ct.first_err(err.decode(errors='replace'))
    rc, _, err = sh(LINK + [f'--ld-path={VLINK}'] + objs + ['-o', f'{wd}/vlink.exe'], timeout=60, env=VLENV)
    if rc:
        e = err.decode(errors='replace')
        return t, 'fail', 'vlink', (ct.first_err(e) if isinstance(rc, int) and rc > 0 else f'vlink: signal/timeout {rc}: ' + ct.first_err(e))[:150]
    a, b = open(f'{wd}/lld.exe', 'rb').read(), open(f'{wd}/vlink.exe', 'rb').read()
    bad = dwarf_bad(f'{wd}/vlink.exe', f'{wd}/ref.o', path)
    if bad:
        return t, 'fail', 'dwarf', bad
    rc, out, err = run(f'{wd}/vlink.exe')
    if rc == 'timeout':	# once more, as for the reference: Wine's first start in a worker
        rc, out, err = run(f'{wd}/vlink.exe')
    if rc != ref['rc'] or out != ref['out']:
        m = EXC.search(err) if rc == 255 else None
        why = f'exception {m.group(1)}' if m else f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
        return t, 'fail', 'run', why + ('' if a == b else ' [image differs: ' + pe_differs(a, b) + ']')
    if a != b:
        return t, 'fail', 'differs', pe_differs(a, b)
    os.makedirs(os.path.dirname(pf), exist_ok=True)
    open(pf, 'w').write(pk)
    if EXPORT:
        export(t, f'{wd}/vlink.exe', ref)
    shutil.rmtree(wd, ignore_errors=True)
    return t, 'pass', '', ''


# --vccrt-ref: the link line clang -v shows for the target, with libvccrt.a in
# place of libclang_rt.builtins-x86_64.a and no -L for compiler-rt's directory
MLIB = f'{MINGW_SYSROOT}/x86_64-w64-mingw32/lib'
VCCRT = os.environ.get('VCC_VCCRT', f'{BUILD}/vccrt/libvccrt.a')
LLD = hostenv.llvm('ld.lld')


def vccrt_link(objs, exe, runtime=None, linker=None, map_file=None):
    """the explicit MinGW static link line with RUNTIME (default libvccrt.a)
    as the run-time library, run by LINKER (ld.lld, or vlink); (rc, stderr)"""
    cmd = [linker or LLD, f'--sysroot={MINGW_SYSROOT}', '-m', 'i386pep', '-Bstatic', '-o', exe,
           f'{MLIB}/crt2.o', f'{MLIB}/crtbegin.o', f'-L{MLIB}'] + objs + \
          ['--start-group', '-lmingw32', runtime or VCCRT, '-lmoldname', '-lmingwex', '-lmsvcrt',
           '-ladvapi32', '-lshell32', '-luser32', '-lkernel32', '--end-group', f'{MLIB}/crtend.o']
    if map_file:
        cmd += [f'-Map={map_file}']
    rc, _, err = sh(cmd, timeout=60, env=VLENV)
    return rc, err.decode(errors='replace')


def foreign_inputs(map_file):
    """the input files of a link map that come from compiler-rt, libgcc or libunwind"""
    try:
        text = open(map_file, errors='replace').read()
    except OSError:
        return ['(no map)']
    return sorted({m.group(0) for m in re.finditer(r'\S*(?:clang_rt|libgcc|libunwind|compiler-rt)\S*', text, re.I)})


def _vccrt_hash():
    try:
        return hashlib.sha1(open(VCCRT, 'rb').read()).hexdigest()[:16]
    except OSError:
        return '?'


def vccrtref(t):
    suite, name, path = t
    src = open(path, 'rb').read().decode('latin-1')
    s = static_skip(suite, name, src)
    if s:
        return t, 'skip', s[0], s[1]
    flags = flags_of(suite, name, src)
    wd = f'{OUT}/rt/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    ref = reference(suite, name, path, flags, wd)
    if 'error' in ref:
        return t, 'error', ref['error'], ref['why']
    if 'skip' in ref:
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'skip', ref['skip'], ref['why']
    os.makedirs(wd, exist_ok=True)
    rc, _, err = sh([CL] + BASE + ct.CLFL + flags + ['-c', path, '-o', f'{wd}/ref.o'], cwd=os.path.dirname(path))
    if rc:
        return t, 'error', 'compile', ct.first_err(err.decode(errors='replace'))
    pk = hashlib.sha1(open(f'{wd}/ref.o', 'rb').read() + ref['key'].encode() + VLHASH.encode() + VRHASH.encode()).hexdigest()
    pf = f'{OUT}/pass-vccrt/{suite}/{name}'
    if cache_hit(pf, pk):
        if EXPORT:	# vlink's link again (deterministic), not run
            rc, err = vccrt_link([f'{wd}/ref.o', RTO], f'{wd}/vlink.exe', linker=VLINK)
            if rc:
                return t, 'fail', 'link-vlink', ct.first_err(err)[:150]
            export(t, f'{wd}/vlink.exe', ref)
        shutil.rmtree(wd, ignore_errors=True)
        return t, 'pass', '', ''
    for which, linker in (('lld', None), ('vlink', VLINK)):
        exe, mp = f'{wd}/{which}.exe', f'{wd}/{which}.map'
        rc, err = vccrt_link([f'{wd}/ref.o', RTO], exe, linker=linker, map_file=mp)
        if rc:
            m = re.search(r"undefined (?:symbol|reference to):? ['`]?(\w+)", err)
            return t, 'fail', f'link-{which}', 'undefined ' + m.group(1) if m else ct.first_err(err)[:150]
        bad = foreign_inputs(mp)
        if bad:
            return t, 'fail', 'foreign', f'{which}: ' + ' '.join(bad)[:150]
        rc, out, err = run(exe)
        if rc == 'timeout':	# once more, as for the reference: Wine's first start in a worker
            rc, out, err = run(exe)
        if rc != ref['rc'] or out != ref['out']:
            m = EXC.search(err) if rc == 255 else None
            why = f'exception {m.group(1)}' if m else f'exit {rc} (clang {ref["rc"]})' if rc != ref['rc'] else 'output differs'
            return t, 'fail', f'run-{which}', why
    os.makedirs(os.path.dirname(pf), exist_ok=True)
    open(pf, 'w').write(pk)
    if EXPORT:
        export(t, f'{wd}/vlink.exe', ref)
    shutil.rmtree(wd, ignore_errors=True)
    return t, 'pass', '', ''


VLHASH = _vlink_hash()
OWNKEY = os.environ.get('VCC_OWN_KEY', '')
VRHASH = os.environ.get('VCC_VCCRT_HASH') or _vccrt_hash()
NOCACHE = REF_ONLY = OBJ = SEH = OWN = False
EXPORT = None


def cache_hit(pf, pk):
    """the pass cache PF holds PK (and --no-cache is off)"""
    if NOCACHE:
        return False
    try:
        return open(pf).read() == pk
    except OSError:
        return False


def init(nocache, ref_only, obj, seh=False, export_dir=None, own=False):
    global NOCACHE, REF_ONLY, OBJ, SEH, EXPORT, OWN
    NOCACHE, REF_ONLY, OBJ, SEH, EXPORT, OWN = nocache, ref_only, obj, seh, export_dir, own


if __name__ == '__main__':
    av = [part for arg in sys.argv[1:]
          for part in (('-j', arg[2:]) if re.fullmatch(r'-j[0-9]+', arg) else (arg,))]
    j, suite, show, verbose = 6, None, None, False
    flags = {a for a in av if a in ('--no-cache', '--ref-only', '--obj', '--objcmp', '-v', '--vir', '--seh', '--vasm-ref', '--vlink-ref', '--vccrt-ref', '--own', '--clang-as')}
    av = [a for a in av if a not in flags]
    nocache, ref_only, obj, ocmode, verbose, vir, seh = (f in flags for f in ('--no-cache', '--ref-only', '--obj', '--objcmp', '-v', '--vir', '--seh'))
    vlmode = '--vlink-ref' in flags
    vamode = '--vasm-ref' in flags
    rtmode = '--vccrt-ref' in flags
    if vir:
        os.environ['VCC_VIR'] = 'l'	# (inherited by every vpp the workers run)
    # the default is --own (vcc's toolchain only); --clang-as: vpp -S assembled by clang, linked by lld
    export_dir, own = None, '--own' in flags or not flags & {'--obj', '--objcmp', '--ref-only', '--vasm-ref', '--vlink-ref', '--vccrt-ref', '--clang-as'}
    for opt in ('-j', '--suite', '--show', '--export'):
        if opt in av:
            k = av.index(opt); v = av[k + 1]; del av[k:k + 2]
            if opt == '-j': j = int(v)
            elif opt == '--suite': suite = v
            elif opt == '--export': export_dir = os.path.abspath(v)
            else: show = v
    if export_dir:
        if ocmode:
            sys.exit('vcc-win64.py: --export with --objcmp: no program to export')
        shutil.rmtree(f'{export_dir}/exe', ignore_errors=True)
        shutil.rmtree(f'{export_dir}/entries', ignore_errors=True)
    pat = av[0] if av else None
    why = hostenv.mingw_missing()
    if why:
        sys.exit(f'vcc-win64.py: {why}')
    ts = tests(suite, pat)
    if not ts:
        sys.exit('no tests matched the requested suite/pattern')
    t0 = time.time()
    if rtmode or own:	# the library first: the workers key their pass cache on it (--own: built by vcc)
        r = subprocess.run(['sh', f'{ct.R}/src/runtime/build.sh', os.path.dirname(VCCRT)], capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'vcc-win64.py: src/runtime/build.sh failed:\n{r.stdout}{r.stderr}')
        os.environ['VCC_VCCRT_HASH'] = _vccrt_hash()
    rt_obj()
    if own:
        rt_obj_own()
        os.environ['VCC_OWN_KEY'] = _file_hash(VPP) + _file_hash(VLINK) + _vccrt_hash() + _file_hash(RTO_OWN)
    hostenv.wine_start(PREFIX)
    with Pool(j, initializer=init, initargs=(nocache, ref_only, obj, seh, export_dir, own)) as pool:
        res = pool.map(vasmref if vamode else vccrtref if rtmode else vlinkref if vlmode else objcmp if ocmode else one, ts, chunksize=1)
    if not (ref_only or vlmode or rtmode or vamode or ocmode):	# modes that build vcc's side
        known = {k[0] for k in XFAIL}
        def judged(r):
            (s, n, p), st, cat, why = r
            k = f'{s}/{n}'
            if st == 'fail' and (k, cat) in XFAIL:
                return (s, n, p), 'xfail', cat, XFAIL[(k, cat)]
            if st == 'pass' and k in known and any(c == 'run' or (c == 'seh' and seh) for kk, c in XFAIL if kk == k):
                return (s, n, p), 'error', 'xpass', 'a known failure passes: remove it from XFAIL'
            return r
        res = [judged(r) for r in res]
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
            xf = f', {c["xfail"]} xfail' if c['xfail'] else ''
            print(f'{s:4s} {c["pass"]}/{run_} pass ({100 * c["pass"] / max(run_, 1):.1f}%){xf}, {c["skip"]} skipped   {det}')
    if rtmode:
        print(f'vccrt: runs-ok with lld and with vlink {tot["pass"]}/{tot["pass"] + tot["fail"]}, '
              f'linked from compiler-rt/libgcc/libunwind {tot["fail:foreign"]}')
    if vlmode:
        print(f'vlink: runs-ok {tot["pass"] + tot["fail:differs"]}/{tot["pass"] + tot["fail"]}, '
              f'image-identical {tot["pass"]}/{tot["pass"] + tot["fail"]}')
    if export_dir:
        mode = ' '.join(sorted(f for f in flags if f not in ('-v', '--no-cache'))) or '(vcc)'
        print(f'export: {write_manifest(export_dir, mode)} programs in {export_dir}')
    print(f'{time.time() - t0:.1f} s')
    sys.exit(1 if tot['fail'] or tot['error'] else 0)
