#!/usr/bin/env python3
"""
Diagnostics: vcc's own expected output, with clang as the reference point
(PLAN-diag.md test layers 1 and 4).

Every src/tests/diag/*.c is a small piece of broken C.  vcc runs
-fsyntax-only on it (no colours, same target) from that directory; a test
passes when
  - layer 4: vcc's stderr is exactly X.c.stderr (vcc's own snapshot: the text
    may be better than clang's), and
  - layer 2: with -fdiagnostics-show-id every diagnostic has an id
    (bench/gen-diagids.py), and
  - layer 5a: the -fdiagnostics-format=json records rebuild that text
    exactly (bench/diag-render.py), and the SARIF log passes
    bench/sarif-check.py, and
  - layer 1: the diagnostics' first lines name the same (file, line, column,
    level, -W flag) as clang's, in order, and the exit status agrees.
A deliberate difference from clang in layer 1 (a better place, fewer
follow-on errors) is declared in the case: /* diag-differs: why */.
A test with /* diag-mode: -c */ is compiled (-c -o /dev/null) instead, for
diagnostics that come from code generation (the error attribute).
The summary also counts the cases whose text in -fdiagnostics-style=clang is
identical to clang's.

  bench/vcc-diag.py [-v] [-j N] [--bin-dir DIR] [PATTERN]   -v: the diff of each failure
  bench/vcc-diag.py --bless [PATTERN]       write X.c.stderr from vcc's output
"""
import difflib, importlib.util, json, os, re, shutil, subprocess, sys, tempfile
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import llvm, BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = f'{R}/src/tests/diag'
CL = llvm('clang')
# --bin-dir DIR or VCC_BIN=DIR: the tools of a sanitizer build (PLAN-diag layer 9)
_BIN = os.environ.get('VCC_BIN', BUILD)
if '--bin-dir' in sys.argv:
    _k = sys.argv.index('--bin-dir'); _BIN = os.path.abspath(sys.argv[_k + 1]); del sys.argv[_k:_k + 2]
    os.environ['VCC_BIN'] = _BIN	# the pool's workers import this again (spawn)
VPP = f'{_BIN}/vpp'
FL = ['--target=aarch64-linux-gnu', '-fsyntax-only', '-fno-color-diagnostics']


def _mod(name, file):
    spec = importlib.util.spec_from_file_location(name, f'{R}/bench/{file}')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


render = _mod('diagrender', 'diag-render.py')
sarif = _mod('sarifcheck', 'sarif-check.py')


def run(cmd, name, extra=()):
    fl = FL
    src = open(os.path.join(D, name), errors='replace').read()
    if re.search(r'/\*\s*diag-mode:\s*-c\s*\*/', src):
        fl = [f for f in FL if f != '-fsyntax-only'] + ['-c', '-o', '/dev/null']
    m = re.search(r'/\*\s*diag-flags:\s*(.*?)\s*\*/', src)	# flags for both compilers
    if m:
        fl = fl + m.group(1).split()
    fl = fl + list(extra)
    p = subprocess.run(cmd + fl + [name], cwd=D, capture_output=True, timeout=30)
    return p.returncode, p.stderr.decode(errors='replace')


HEAD = re.compile(r'^(\S+):(\d+):(\d+): (error|warning|note|fatal error|remark): (.*?)(?: \[(?:-Werror,)?-W([^\],]+)\])?$', re.M)


def heads(out):
    """layer 1: (file, line, col, level, flag) of every diagnostic"""
    return [(m.group(1), m.group(2), m.group(3), m.group(4), m.group(6) or '') for m in HEAD.finditer(out)]


def sarif_norm(text):
    """the SARIF log without what depends on the machine (working directory, compiler path)"""
    try:
        doc = json.loads(text)
    except ValueError:
        return text
    run = doc['runs'][0]
    run['originalUriBaseIds'] = {'%SRCROOT%': {'uri': 'file:///SRCROOT/'}}
    for inv in run.get('invocations', []):
        inv['arguments'] = ['vpp'] + inv['arguments'][1:]
    return json.dumps(doc, indent=1) + '\n'


def no_virtual_snippets(t):
    """the text without the snippet lines of a <built-in> location: JSON names the
    place, but a reader of the records has no source for a virtual buffer"""
    out, skip = [], False
    for l in t.split('\n'):
        if HEAD.match(l) or not l.startswith(' '):
            skip = l.startswith('<built-in>:')
        elif skip:
            continue
        out.append(l)
    return '\n'.join(out)


def one(name, bless=False):
    rc_c, out_c = run([CL], name)
    rc_v, out_v = run([VPP], name)
    src = open(os.path.join(D, name), errors='replace').read()
    snap = os.path.join(D, name + '.stderr')
    if bless:
        open(snap, 'w').write(out_v)
    want = open(snap, errors='replace').read() if os.path.exists(snap) else None
    differs = re.search(r'/\*\s*diag-differs:', src) is not None
    l1 = differs or heads(out_c) == heads(out_v)
    ok = want == out_v and l1 and (rc_c == 0) == (rc_v == 0)
    why = []
    if want is None:
        why.append('no .stderr (run --bless)')
    elif want != out_v:
        why.append('text differs from .stderr')
    if not l1:
        why.append('locations/levels/flags differ from clang')
    if (rc_c == 0) != (rc_v == 0):
        why.append(f'exit {rc_v}, clang {rc_c}')
    # layer 2: every diagnostic carries an id from pp/diagids.inc (clang's name or vcc_*)
    _, out_i = run([VPP], name, ['-fdiagnostics-show-id'])
    noid = [l for l in out_i.splitlines() if HEAD.match(l) and (not re.search(r' \[(\w+)\]$', l) or l.endswith('[vcc_unknown]'))]
    if noid:
        ok = False
        why.append(f'diagnostic without an id: {noid[0]}')
    # layer 5a: the JSON records rebuild the text exactly (bench/diag-render.py)
    _, out_j = run([VPP], name, ['-fdiagnostics-format=json'])
    pfx = '-fdiagnostics-parseable-fixits' in open(os.path.join(D, name), errors='replace').read()
    try:
        rt = render.render(out_j.splitlines(), D, parseable=pfx).decode(errors='replace')
    except (ValueError, KeyError) as e:
        rt = f'(bad JSON: {e})'
    if no_virtual_snippets(rt) != no_virtual_snippets(out_v):
        ok = False
        why.append('JSON does not rebuild the text (layer 5a)')
    # clang's style (-fdiagnostics-style=clang): rebuilt from the same records; it is
    # what the "text-identical to clang" count compares
    _, out_k = run([VPP], name, ['-fdiagnostics-style=clang'])
    try:
        rk = render.render(out_j.splitlines(), D, style='clang', parseable=pfx).decode(errors='replace')
    except (ValueError, KeyError) as e:
        rk = f'(bad JSON: {e})'
    if no_virtual_snippets(rk) != no_virtual_snippets(out_k):
        ok = False
        why.append('JSON does not rebuild clang\'s style (layer 5a)')
    # D3.2: the SARIF log is well formed
    _, out_s = run([VPP], name, ['-fdiagnostics-format=sarif'])
    try:
        sp = sarif.check(json.loads(out_s))
    except ValueError as e:
        sp = [f'bad JSON: {e}']
    if sp:
        ok = False
        why.append(f'SARIF: {sp[0]}')
    # layer 4 for the other renderers: /* diag-formats */ keeps X.c.json and X.c.sarif
    if re.search(r'/\*\s*diag-formats', src):
        _, out_n = run([VPP], name, ['-fdiagnostics-style=narrated'])
        for ext, got in (('json', out_j), ('sarif', sarif_norm(out_s)), ('narrated', out_n)):
            snapf = os.path.join(D, f'{name}.{ext}')
            if bless:
                open(snapf, 'w').write(got)
            if not os.path.exists(snapf) or open(snapf).read() != got:
                ok = False
                why.append(f'{ext} differs from .{ext}')
    # layer 5b: /* diag-fix */: --fix on a copy, repeated while it changes something (a fix can
    # uncover a diagnostic error recovery hid), gives X.c.fixed within 4 passes, and clang
    # compiles it with fewer errors
    if re.search(r'/\*\s*diag-fix\s*\*/', src):
        fixed = os.path.join(D, name + '.fixed')
        with tempfile.TemporaryDirectory() as t:
            for h in os.listdir(D):	# the headers next to it
                if h.endswith('.h'):
                    shutil.copy(os.path.join(D, h), t)
            shutil.copy(os.path.join(D, name), t)
            fl = [f for f in FL] + (re.search(r'/\*\s*diag-flags:\s*(.*?)\s*\*/', src).group(1).split()
                                     if re.search(r'/\*\s*diag-flags:', src) else [])
            sub = lambda cmd: subprocess.run(cmd + fl + [name], cwd=t, capture_output=True, timeout=30)
            for _ in range(4):
                p2 = sub([VPP, '--fix'])
                if b'note: applied' not in p2.stderr:
                    break
            got = open(os.path.join(t, name), errors='replace').read()
            if bless:
                open(fixed, 'w').write(got)
            nerr = lambda p: len(re.findall(r': (?:fatal )?error: ', p.stderr.decode(errors='replace')))
            pc0 = subprocess.run([CL] + fl + [name], cwd=D, capture_output=True)
            pc1 = sub([CL])
        if not os.path.exists(fixed) or open(fixed, errors='replace').read() != got:
            ok = False
            why.append('--fix differs from .fixed')
        if b'note: applied' in p2.stderr:
            ok = False
            why.append('--fix still changes the file after 4 passes')
        if nerr(pc0) and nerr(pc1) >= nerr(pc0):
            ok = False
            why.append(f'clang: {nerr(pc1)} errors after --fix, {nerr(pc0)} before')
    return name, ok, out_c, out_v, want or '', ', '.join(why), out_c == out_k


def main():
    args = sys.argv[1:]
    verbose = '-v' in args
    j = 6
    if '-j' in args:
        j = int(args[args.index('-j') + 1])
    pat = [a for a in args if not a.startswith('-') and not a.isdigit()]
    names = sorted(f for f in os.listdir(D) if f.endswith('.c') and (not pat or re.search(pat[0], f)))
    bless = '--bless' in args
    with Pool(j) as pool:
        res = pool.starmap(one, [(n, bless) for n in names])
    fails = [r for r in res if not r[1]]
    for name, ok, out_c, out_v, want, why, same in res:
        if ok:
            continue
        print(f'FAIL {name}: {why}')
        if verbose:
            if want != out_v:
                for l in difflib.unified_diff(want.splitlines(), out_v.splitlines(), '.stderr', 'vcc', lineterm='', n=2):
                    print('    ' + l)
            if heads(out_c) != heads(out_v):
                for l in difflib.unified_diff([' '.join(h) for h in heads(out_c)], [' '.join(h) for h in heads(out_v)],
                                              'clang', 'vcc', lineterm='', n=1):
                    print('    ' + l)
    same = sum(r[6] for r in res)
    print(f'{len(res) - len(fails)}/{len(res)} pass: vcc\'s snapshot, clang\'s locations; {same} text-identical to clang')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
