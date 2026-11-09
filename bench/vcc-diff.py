#!/usr/bin/env python3
"""vcc-diff.py: preprocessor parity with clang over the kernel TUs.

  bench/vcc-diff.py ref [-j N]          clang -E -P for every C TU -> work/ppref/
  bench/vcc-diff.py cmp [-j N] [PAT]    vpp --cmp against the references
  bench/vcc-diff.py args TU             the clang argv for one TU (for vpp)
  bench/vcc-diff.py batch [-j N] [--cmp] [PAT]
                                        all TUs in one vpp process (vpp --batch)

TUs come from work/build-o0/**/.*.o.cmd (savedcmd_X := ...); each command
runs in work/build-o0.  The comparison is a token stream (kind, spelling),
done by vpp itself (it lexes the reference with vcc's lexer).

--bin-dir DIR (or VCC_BIN=DIR) runs the tools from DIR instead of build/ccs,
e.g. build/ccs-asan or build/ccs-tsan (AGENTS.md, "Sanitizers").
"""
import glob, hashlib, os, re, shlex, subprocess, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD, KTREE, PPREF
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = KTREE; REF = PPREF
CL = llvm('clang')

def bin_dir():
    """build/ccs (build/linux/ccs on Linux), or --bin-dir DIR / VCC_BIN=DIR (removed from sys.argv)"""
    d = os.environ.get('VCC_BIN', BUILD)
    if '--bin-dir' in sys.argv:
        k = sys.argv.index('--bin-dir'); d = sys.argv[k+1]; del sys.argv[k:k+2]
    return os.path.abspath(d)
BIN = bin_dir(); VPP = os.environ.get('VPP', f'{BIN}/vpp')

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
        res = pool.map({'ref': mkref, 'dm': dm, 'deps': deps, 'hdr': hdr}.get(mode, cmp), ts, chunksize=4)
    bad = [r for r in res if r[1]]
    for obj, rc, err in bad[:int(os.environ.get('SHOW', 20))]:
        print(f'--- {obj} (rc {rc})\n{err}')
    print(f'{len(res) - len(bad)}/{len(res)} ok')
