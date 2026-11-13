#!/usr/bin/env python3
"""vcc-diff.py: preprocessor parity with clang over the kernel TUs.

  bench/vcc-diff.py ref [-j N]          clang -E -P for every C TU -> work/ppref/
  bench/vcc-diff.py cmp [-j N] [PAT]    vpp --cmp against the references
  bench/vcc-diff.py text [-j N] [PAT]   vpp -E -P byte for byte against the references
  bench/vcc-diff.py textc [-j N] [PAT]  the same with -C, clang run alongside
  bench/vcc-diff.py lines [-j N] [PAT]  -E without -P (line markers) vs clang run alongside
  bench/vcc-diff.py args TU             the clang argv for one TU (for vpp)
  bench/vcc-diff.py batch [-j N] [--cmp] [PAT]
                                        all TUs in one vpp process (vpp --batch)

TUs come from work/build-o0/**/.*.o.cmd (savedcmd_X := ...); each command
runs in work/build-o0.  The comparison is a token stream (kind, spelling),
done by vpp itself (it lexes the reference with vcc's lexer).

--bin-dir DIR (or VCC_BIN=DIR) runs the tools from DIR instead of build/src,
e.g. build/src-asan or build/src-tsan (AGENTS.md, "Sanitizers").

--win64 (or VCC_WIN64=1): instead of the kernel's TUs, one TU per top-level
header of the MinGW-w64 sysroot (x86_64-w64-mingw32/include/*.h, those clang
-fsyntax-only accepts; PLAN-win64.md WIN1), plus a few flag variants, compiled
with --target=x86_64-w64-mingw32 --sysroot=SYSROOT in build/win64-headers
(references in its ref/).  SYSROOT: hostenv.MINGW_SYSROOT
($VCC_MINGW_SYSROOT).  Every mode works on them, and so do the harnesses
that import this module (vcc-sema.py run/layout/warn).
"""
import glob, hashlib, os, re, shlex, subprocess, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD, KTREE, PPREF
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = KTREE; REF = PPREF
if '--win64' in sys.argv or os.environ.get('VCC_WIN64') == '1':	# (see win64_tus)
    B = f'{R}/build/win64-headers'; REF = f'{B}/ref'
    os.makedirs(REF, exist_ok=True)
CL = llvm('clang')

def bin_dir():
    """build/src (build/linux/src on Linux), or --bin-dir DIR / VCC_BIN=DIR (removed from sys.argv)"""
    d = os.environ.get('VCC_BIN', BUILD)
    if '--bin-dir' in sys.argv:
        k = sys.argv.index('--bin-dir'); d = sys.argv[k+1]; del sys.argv[k:k+2]
        # Spawn/forkserver workers import this module after argv was consumed.
        os.environ['VCC_BIN'] = os.path.abspath(d)
    return os.path.abspath(d)
BIN = bin_dir(); VPP = os.environ.get('VPP', f'{BIN}/vpp')

if '--win64' in sys.argv:
    sys.argv.remove('--win64')
    os.environ['VCC_WIN64'] = '1'	# (spawned workers import this module after argv was consumed)
WIN64 = os.environ.get('VCC_WIN64') == '1'
WIN64_TARGET = '--target=x86_64-w64-mingw32'
# a few flag variants of the headers most programs include
WIN64_VARIANTS = [('windows.h', ['-DUNICODE', '-D_UNICODE']), ('windows.h', ['-DWIN32_LEAN_AND_MEAN', '-O2']),
                  ('windows.h', ['-D_WIN32_WINNT=0x0601', '-std=gnu99']), ('stdio.h', ['-D__USE_MINGW_ANSI_STDIO=1']),
                  ('stdlib.h', ['-std=c23']), ('math.h', ['-ffast-math', '-O2']), ('intrin.h', ['-march=haswell']),
                  ('winsock2.h', ['-municode'])]

def win64_sysroot():
    """the MinGW-w64 sysroot (llvm-mingw's tree): hostenv.MINGW_SYSROOT ($VCC_MINGW_SYSROOT)"""
    from hostenv import MINGW_SYSROOT as d
    if not d or not os.path.isdir(f'{d}/x86_64-w64-mingw32/include'):
        sys.exit('vcc-diff.py --win64: no MinGW-w64 sysroot (VCC_MINGW_SYSROOT; bench/fetch-testsuites.sh llvm-mingw)')
    return d

def win64_tus(pat=None):
    """one TU per top-level sysroot header clang accepts (+ WIN64_VARIANTS), files written into B/hdr"""
    sr = win64_sysroot()
    inc = f'{sr}/x86_64-w64-mingw32/include'
    os.makedirs(f'{B}/hdr', exist_ok=True)
    out = []
    for i, (h, fl) in enumerate([(h, []) for h in sorted(os.listdir(inc)) if h.endswith('.h') and os.path.isfile(f'{inc}/{h}')] + WIN64_VARIANTS):
        name = h[:-2] + (f'.v{i}' if fl else '')
        f = f'hdr/{name}.c'
        if not os.path.exists(f'{B}/{f}'):
            open(f'{B}/{f}', 'w').write(f'#include <{h}>\n')
        out.append((f'win64/{name}.o', [WIN64_TARGET, f'--sysroot={sr}'] + fl + [f]))
    # what clang itself accepts (cached by clang's binary and the sysroot)
    key = hashlib.sha1(repr((os.stat(CL).st_mtime_ns, sr, os.stat(inc).st_mtime_ns, len(out))).encode()).hexdigest()[:16]
    acc = f'{B}/accepted-{key}'
    try:
        ok = set(open(acc).read().split())
    except OSError:
        with Pool(int(os.environ.get('JOBS', 6))) as pool:
            ok = {o for o, good in pool.map(_win64_accepts, out, chunksize=8) if good}
        open(acc, 'w').write('\n'.join(sorted(ok)) + '\n')
    return [t for t in out if t[0] in ok and (not pat or re.search(pat, t[0]))]

def _win64_accepts(t):
    obj, a = t
    return obj, subprocess.run([CL, '-fsyntax-only', '-w'] + a, cwd=B, capture_output=True).returncode == 0

def tu_args(cmd):
    """(object, clang args without -c/-E/-o/-MMD) for a C TU, a .S file, or a
    cpp run over a linker script / device tree (kbuild's -E commands)."""
    line = open(cmd, errors='replace').readline()
    m = re.match(r'savedcmd_(\S+) := (.*)', line)
    if not m: return None
    a = shlex.split(m.group(2).split(' ; ')[0])
    if not a or not re.search(r'\.(c|S|dts)$', a[-1]): return None
    if '-c' not in a and '-E' not in a: return None
    a = [x for x in a[1:] if not x.startswith('-Wp,-MMD') and x not in ('-c', '-E')]
    if '-o' in a: i = a.index('-o'); a = a[:i] + a[i+2:]
    return m.group(1), a

def tus(pat=None):
    if WIN64:
        return win64_tus(pat)
    out = []
    cmds = []
    for d, dirs, files in os.walk(B):	# no symlinks: build-o0/source leads to other trees
        cmds += [f'{d}/{f}' for f in files if f.startswith('.') and f.endswith('.cmd')]
    for c in sorted(cmds):
        t = tu_args(c)
        if t and (not pat or re.search(pat, t[0])): out.append(t)
    return out

def refname(obj): return f'{REF}/{obj.replace("/", "__")}.i'

def mkref(t):
    obj, a = t
    p = subprocess.run([CL] + a + ['-E', '-P', '-o', refname(obj)], cwd=B, capture_output=True, text=True)
    return obj, p.returncode, p.stderr[-300:]

def cmp(t):
    obj, a = t
    p = subprocess.run([VPP, '--cmp', refname(obj)] + a, cwd=B, capture_output=True, text=True)
    return obj, p.returncode, (p.stdout + p.stderr)[-1500:]

def dm(t, fl=('-dM', '-E')):
    """-dM: the macro table at the end of the TU, bytes vs clang (deps: -M, -MM -MP)"""
    obj, a = t
    r = [subprocess.run([x] + a + list(fl), cwd=B, capture_output=True) for x in (CL, VPP)]
    if r[0].stdout == r[1].stdout: return obj, 0, ''
    import difflib
    d = difflib.unified_diff(r[0].stdout.decode(errors='replace').splitlines(),
                             r[1].stdout.decode(errors='replace').splitlines(), 'clang', 'vpp', n=0, lineterm='')
    return obj, 1, '\n'.join(list(d)[:12]) + r[1].stderr.decode(errors='replace')[-300:]

def deps(t):
    """-M and -MM -MP -MQ: the rules byte for byte, else the same files after
    normalising paths (clang also lists a guard-skipped header under each
    spelling it was included by; vpp under the first only: TODO.md)"""
    obj, a = t
    for fl in (('-E', '-M'), ('-E', '-MM', '-MP', '-MQ', '$(obj)/x.o')):
        r = [subprocess.run([x] + a + list(fl), cwd=B, capture_output=True) for x in (CL, VPP)]
        if r[0].stdout == r[1].stdout: continue
        def files(b):
            w = b.decode(errors='replace').replace('\\\n', ' ').split('\n')[0].split()[1:]
            return list(dict.fromkeys(os.path.normpath(os.path.join(B, f)) for f in w))
        if files(r[0].stdout) != files(r[1].stdout) or r[0].stdout.count(b'\n\n') != r[1].stdout.count(b'\n\n'):
            return dm(t, fl)
    return obj, 0, ''

def hdr(t):
    """-H: the include tree on stderr, lines starting with '.', vs clang"""
    obj, a = t
    r = [subprocess.run([x] + a + ['-E', '-H', '-o', '/dev/null'], cwd=B, capture_output=True) for x in (CL, VPP)]
    h = [[l for l in x.stderr.decode(errors='replace').splitlines() if l.startswith('.')] for x in r]
    if h[0] == h[1]: return obj, 0, ''
    import difflib
    return obj, 1, '\n'.join(list(difflib.unified_diff(h[0], h[1], 'clang', 'vpp', n=0, lineterm=''))[:12])

def text(t):
    """-E -P output byte for byte vs the clang reference (spacing, indentation, lines)"""
    obj, a = t
    if not os.path.exists(refname(obj)): return obj, 0, ''	# clang failed too (modpost)
    r = subprocess.run([VPP] + a + ['-E', '-P'], cwd=B, capture_output=True)
    ref = open(refname(obj), 'rb').read()
    if r.stdout == ref: return obj, 0, ''
    import difflib
    d = difflib.unified_diff(ref.decode(errors='replace').splitlines(), r.stdout.decode(errors='replace').splitlines(),
                             'clang', 'vpp', n=0, lineterm='')
    return obj, 1, '\n'.join(list(d)[:12]) + r.stderr.decode(errors='replace')[-300:]

def textc(t, fl=('-E', '-P', '-C')):
    """-E -P -C: with comments, byte for byte vs clang run now"""
    obj, a = t
    r = [subprocess.run([x] + a + list(fl), cwd=B, capture_output=True) for x in (CL, VPP)]
    if r[0].returncode: return obj, 0, ''
    if r[0].stdout == r[1].stdout: return obj, 0, ''
    import difflib
    d = difflib.unified_diff(r[0].stdout.decode(errors='replace').splitlines(),
                             r[1].stdout.decode(errors='replace').splitlines(), 'clang', 'vpp', n=0, lineterm='')
    return obj, 1, '\n'.join(list(d)[:12]) + r[1].stderr.decode(errors='replace')[-300:]

def lines(t):
    """-E without -P: line markers and blank lines byte for byte vs clang run now"""
    return textc(t, ('-E',))

def dd(t):
    """-dD: the #define/#undef lines of -E -P -dD, in order, vs clang (the
    rest of the text is what cmp checks)"""
    obj, a = t
    r = [subprocess.run([x] + a + ['-E', '-P', '-dD'], cwd=B, capture_output=True) for x in (CL, VPP)]
    h = [[l for l in x.stdout.decode(errors='replace').splitlines() if l.startswith(('#define ', '#undef '))] for x in r]
    if h[0] == h[1]: return obj, 0, ''
    import difflib
    return obj, 1, '\n'.join(list(difflib.unified_diff(h[0], h[1], 'clang', 'vpp', n=0, lineterm=''))[:12])

if __name__ == '__main__':
    av = [a for a in sys.argv[1:] if a not in ('--kernel', '--cmp')]; j = 6
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    if mode == 'args':
        for obj, a in tus(pat): print(shlex.join(a)); break
        sys.exit(0)
    ts = tus(pat)
    if '--kernel' in sys.argv: ts = [t for t in ts if not t[0].startswith('scripts/')]
    if mode == 'dm': ts = [t for t in ts if t[1][-1].endswith('.c')]	# .S: clang's -dM drops macros (TODO.md)
    if mode == 'batch':
        jf = f'/tmp/vcc-batch-{os.getpid()}.jobs'
        with open(jf, 'w') as f:
            for obj, a in ts: f.write('\x1f'.join([refname(obj)] + a) + '\n')
        r = subprocess.run([VPP, '--batch', jf, '-j', str(j), '-C', B] + (['--cmp'] if '--cmp' in sys.argv else []))
        os.unlink(jf); sys.exit(r.returncode)
    if mode == 'ref': os.makedirs(REF, exist_ok=True)
    with Pool(j) as pool:
        res = pool.map({'ref': mkref, 'dm': dm, 'deps': deps, 'hdr': hdr, 'dd': dd, 'text': text, 'textc': textc, 'lines': lines}.get(mode, cmp), ts, chunksize=4)
    bad = [r for r in res if r[1]]
    for obj, rc, err in bad[:int(os.environ.get('SHOW', 20))]:
        print(f'--- {obj} (rc {rc})\n{err}')
    print(f'{len(res) - len(bad)}/{len(res)} ok')
