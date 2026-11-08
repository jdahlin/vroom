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
"""
import glob, hashlib, os, re, shlex, subprocess, sys
from multiprocessing import Pool
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def work_dir():
    """work/ of this checkout, else of the main checkout (git worktrees share it)"""
    if os.path.isdir(f'{R}/work'): return f'{R}/work'
    try:
        c = subprocess.run(['git', '-C', R, 'rev-parse', '--git-common-dir'], capture_output=True, text=True).stdout.strip()
        w = os.path.join(os.path.dirname(os.path.abspath(os.path.join(R, c))), 'work')
        if os.path.isdir(w): return w
    except OSError: pass
    return f'{R}/work'
W = os.environ.get('VROOM_WORK') or work_dir()
B = f'{W}/build-o0'; REF = f'{W}/ppref'
CL = '/opt/homebrew/opt/llvm/bin/clang'; VPP = os.environ.get('VPP', f'{R}/build/ccs/vpp')

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

if __name__ == '__main__':
    av = [a for a in sys.argv[1:] if a not in ('--kernel', '--cmp')]; j = 6
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    if mode == 'args':
        for obj, a in tus(pat): print(shlex.join(a)); break
        sys.exit(0)
    ts = tus(pat)
    if '--kernel' in sys.argv: ts = [t for t in ts if not t[0].startswith('scripts/')]
    if mode == 'batch':
        jf = f'/tmp/vcc-batch-{os.getpid()}.jobs'
        with open(jf, 'w') as f:
            for obj, a in ts: f.write('\x1f'.join([refname(obj)] + a) + '\n')
        r = subprocess.run([VPP, '--batch', jf, '-j', str(j), '-C', B] + (['--cmp'] if '--cmp' in sys.argv else []))
        os.unlink(jf); sys.exit(r.returncode)
    with Pool(j) as pool:
        res = pool.map(mkref if mode == 'ref' else cmp, ts, chunksize=4)
    bad = [r for r in res if r[1]]
    for obj, rc, err in bad[:int(os.environ.get('SHOW', 20))]:
        print(f'--- {obj} (rc {rc})\n{err}')
    print(f'{len(res) - len(bad)}/{len(res)} ok')
