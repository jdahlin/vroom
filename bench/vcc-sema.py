#!/usr/bin/env python3
"""vcc-sema.py: the vcc front end (parser + sema) over the kernel's C TUs.

  bench/vcc-sema.py run [-j N] [--kernel] [PAT]     vpp -fsyntax-only on every C TU
  bench/vcc-sema.py batch [-j N] [--kernel] [PAT]   the same in one vpp --batch process (~2 s, not ~35 s;
                                                    shared caches as in vccd, so also run `run` before a commit)
  bench/vcc-sema.py layout [-j N] [--kernel] [PAT]  vcc's layout/constant claims, checked by clang
  bench/vcc-sema.py warn [-j N] [--kernel] [PAT]    vcc's warnings (file:line:col: warning: ... [-Wflag]) against clang's
      (WFLAGS="-pedantic": extra flags for both; HIST=N: the N flags clang gives most that vcc misses)

`layout` has vpp write _Static_assert lines (sizeof/_Alignof/offsetof of every
file-scope struct, union, enum and typedef, sizeof of every global, every
enumerator's value and type) and appends them to clang's own -E output of the
TU (work/ppref, from vcc-diff.py ref); clang -fsyntax-only on that file then
fails on any claim vcc got wrong.  clang's verdict is cached (build/ccs/layout-ok,
keyed by the claims, the reference and the flags): a TU whose claims did not
change is not checked by clang again; NOCACHE=1 checks all.

--bin-dir DIR (or VCC_BIN=DIR) picks the vpp binary, as in vcc-diff.py.
"""
import hashlib, os, re, subprocess, sys, tempfile, time
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)
B, CL, VPP = vd.B, vd.CL, vd.VPP
TMP = tempfile.mkdtemp(prefix='vcc-sema-')
LCACHE = f'{vd.BIN}/layout-ok'

def c_tus(pat):
    return [(o, a) for o, a in vd.tus(pat) if a[-1].endswith('.c') and '-E' not in a]

def run(t):
    obj, a = t
    t0 = time.time()
    p = subprocess.run([VPP, '-fsyntax-only'] + a, cwd=B, capture_output=True, text=True)
    return obj, p.returncode, p.stderr[-2000:], time.time() - t0

CHECK_FLAGS = re.compile(r'^(--target=|-std=|-f(un)?signed-char|-fms-extensions|-fms-anonymous-structs|-fexperimental-late-parse-attributes|-fno-strict-aliasing|-fshort-wchar|-m)')

def layout(t):
    obj, a = t
    chk = f'{TMP}/{obj.replace("/", "__")}.chk'
    p = subprocess.run([VPP, '--vcc-layout-checks', '-o', chk] + a, cwd=B, capture_output=True, text=True)
    if p.returncode:
        return obj, p.returncode, 'vcc: ' + p.stderr[-1500:], 0
    flags = [x for x in a[:-1] if CHECK_FLAGS.match(x)]
    claims = open(chk, 'rb').read()
    n = claims.count(b'\n_Static_assert') + claims.startswith(b'_Static_assert')
    rs = os.stat(vd.refname(obj))
    key = hashlib.sha1(claims + repr((rs.st_size, rs.st_mtime_ns, flags, CL)).encode()).hexdigest()
    cf = f'{LCACHE}/{obj.replace("/", "__")}'
    try:
        if not os.environ.get('NOCACHE') and open(cf).read() == key:
            os.unlink(chk)
            return obj, 0, '', n
    except OSError:
        pass
    src = f'{TMP}/{obj.replace("/", "__")}.i'
    with open(src, 'w') as f:
        f.write(open(vd.refname(obj), errors='replace').read())
        f.write(open(chk).read())
    q = subprocess.run([CL, '-fsyntax-only', '-w', '-Wno-implicit-int', '-x', 'cpp-output', '-ferror-limit=0'] + flags + [src],
                       capture_output=True, text=True)
    os.unlink(src); os.unlink(chk)
    # naming an SDK function marked unavailable is an error in itself: not a wrong claim
    errs = [l for l in q.stderr.splitlines() if 'error:' in l and 'is unavailable' not in l]
    if not errs:
        os.makedirs(LCACHE, exist_ok=True)
        open(cf, 'w').write(key)
    return obj, len(errs) and 1, '\n'.join(errs[:8]) + (f'\n... {len(errs)} errors' if len(errs) > 8 else ''), n

WLINE = re.compile(r'^(\S+:\d+:\d+: (?:warning|error|fatal error): .*)$', re.M)

def warn(t):
    """vcc's diagnostic header lines against clang's -fsyntax-only (same flags): ok when
    every line vcc prints is one clang prints too (vcc may miss some of clang's)."""
    obj, a = t
    a = a + os.environ.get('WFLAGS', '').split()
    p = subprocess.run([VPP, '-fsyntax-only', '-fno-color-diagnostics'] + a, cwd=B, capture_output=True, text=True)
    q = subprocess.run([CL, '-fsyntax-only', '-fno-color-diagnostics'] + [x for x in a if not x.startswith('-Wp,')],
                       cwd=B, capture_output=True, text=True)
    v, c = WLINE.findall(p.stderr), WLINE.findall(q.stderr)
    extra = [l for l in v if l not in c]
    missed = [re.sub(r'.*\[-W(?:error,-W)?([^\]]*)\]$', r'\1', l) if l.endswith(']') else 'no flag'
              for l in c if l not in v and (' warning: ' in l or ' error: ' in l)]
    return obj, len(extra) and 1, '\n'.join(extra[:8]), (len(v) - len(extra), len(c), missed)

if __name__ == '__main__':
    av = [x for x in sys.argv[1:] if x != '--kernel']; j = 6
    av = [part for arg in av for part in (['-j', arg[2:]] if arg.startswith('-j') and arg[2:].isdigit() else [arg])]
    if '-j' in av: k = av.index('-j'); j = int(av[k+1]); del av[k:k+2]
    mode = av[0]; pat = av[1] if len(av) > 1 else None
    ts = c_tus(pat)
    if '--kernel' in sys.argv: ts = [t for t in ts if not t[0].startswith('scripts/')]
    if not ts:
        os.rmdir(TMP)
        sys.exit(f'No C translation units matched {pat!r} in {B}')
    if mode == 'batch':
        jf = f'{TMP}/jobs'
        with open(jf, 'w') as f:
            for obj, a in ts: f.write('\x1f'.join(['-', '-fsyntax-only'] + a) + '\n')
        r = subprocess.run([VPP, '--batch', jf, '-j', str(j), '-C', B], stdout=subprocess.PIPE, text=True)
        print(r.stdout, end='' if r.stdout.endswith('\n') else '\n')
        os.unlink(jf); os.rmdir(TMP); sys.exit(r.returncode)
    with Pool(j) as pool:
        res = pool.map(run if mode == 'run' else warn if mode == 'warn' else layout, ts, chunksize=2)
    bad = [r for r in res if r[1]]
    show = int(os.environ.get('SHOW', 10))
    if os.environ.get('FIRST'):
        # histogram of the first error line per failing TU
        from collections import Counter
        h = Counter()
        for r in bad:
            m = [l for l in r[2].splitlines() if 'error' in l]
            h[re.sub(r'^.*?error: ', '', m[0]) if m else r[2][:100]] += 1
        for k, v in h.most_common(show): print(f'{v:5d}  {k}')
    else:
        for obj, rc, err, _ in bad[:show]:
            print(f'--- {obj} (rc {rc})\n{err}')
    if mode == 'run':
        tt = sorted(r[3] for r in res)
        print(f'{len(res) - len(bad)}/{len(res)} ok; median {tt[len(tt)//2]*1e3:.0f} ms, total {sum(tt):.1f} s')
    elif mode == 'warn':
        print(f'{len(res) - len(bad)}/{len(res)} ok; vcc reproduces {sum(r[3][0] for r in res)} of clang\'s {sum(r[3][1] for r in res)} diagnostics')
        if os.environ.get('HIST'):	# clang's diagnostics vcc does not give, by flag
            from collections import Counter
            h = Counter(f for r in res for f in r[3][2])
            for k, v in h.most_common(int(os.environ.get('HIST'))): print(f'{v:7d}  {k}')
    else:
        print(f'{len(res) - len(bad)}/{len(res)} ok; {sum(r[3] for r in res)} checks')
    os.rmdir(TMP)
