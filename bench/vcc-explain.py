#!/usr/bin/env python3
"""Check the `vcc --explain` pages (doc/diag/ID.md, PLAN-diag.md D6).

  bench/vcc-explain.py [-v] [PATTERN]

For every page: ID is a diagnostic id vcc has (diag/diagnostic-ids.inc); the page has one `c wrong`
and one `c fixed` example; vpp gives the wrong one a diagnostic with that id
(-fdiagnostics-show-id) and the fixed one none with it, and no error; `vpp --explain ID`
prints the page (the compiled-in table is up to date).  Both examples use the page's
`<!-- flags: ... -->` options.  <1 s.
"""
import os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hostenv import BUILD

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = f'{R}/doc/diag'
VPP = os.path.join(os.environ.get('VCC_BIN') or BUILD, 'vpp')
FL = ['--target=aarch64-linux-gnu', '-fsyntax-only', '-fno-color-diagnostics', '-fdiagnostics-show-id']


def known_ids():
    """diag/diagnostic-ids.inc's ids and the ones the code sets itself (gen-diagids.py explicit_ids)"""
    import importlib.util
    spec = importlib.util.spec_from_file_location('gendiagids', f'{R}/src/tools/gen-diagids.py')
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)
    s = open(f'{R}/src/diag/diagnostic-ids.inc').read()
    got = set(re.findall(r'^DID\((\w+),', s, re.M)) | g.explicit_ids()
    for root, _, files in os.walk(f'{R}/src/frontend/c'):	# ids passed as strings ("ext_...")
        for f in files:
            if f.endswith('.c'):
                got |= set(re.findall(r'"((?:err|ext|warn|note)_\w+)"', open(os.path.join(root, f), errors='replace').read()))
    return got


def check(name, text, ids, verbose):
    i = name[:-3]
    errs = []
    if i not in ids:
        errs.append('not an id vcc gives')
    ex = dict()
    for m in re.finditer(r'```c (wrong|fixed)\n(.*?)```', text, re.S):
        if m.group(1) in ex:
            errs.append(f'two `c {m.group(1)}` examples')
        ex[m.group(1)] = m.group(2)
    m = re.search(r'<!--\s*flags:\s*(.*?)\s*-->', text)
    flags = m.group(1).split() if m else []
    with tempfile.TemporaryDirectory() as t:
        for kind in ('wrong', 'fixed'):
            if kind not in ex:
                errs.append(f'no `c {kind}` example')
                continue
            f = os.path.join(t, f'{kind}.c')
            open(f, 'w').write(ex[kind])
            p = subprocess.run([VPP] + FL + flags + [f], capture_output=True, text=True, errors='replace', cwd=t)
            has = re.search(r' \[' + re.escape(i) + r'\]$', p.stderr, re.M) is not None
            if verbose:
                print(f'--- {i} {kind}\n{p.stderr}', end='')
            if kind == 'wrong' and not has:
                errs.append('the wrong example does not give it')
            if kind == 'fixed' and has:
                errs.append('the fixed example still gives it')
            if kind == 'fixed' and re.search(r': (fatal )?error: ', p.stderr):
                errs.append('the fixed example has an error')
    p = subprocess.run([VPP, '--explain', i], capture_output=True, text=True)
    if p.returncode or p.stdout != text:
        errs.append('vpp --explain does not print it (run src/tools/gen-explain.py)')
    return errs


def main():
    a = sys.argv[1:]
    verbose = '-v' in a
    pat = [x for x in a if not x.startswith('-')]
    ids = known_ids()
    pages = sorted(f for f in os.listdir(DOCS) if re.fullmatch(r'[a-z]\w*\.md', f) and (not pat or re.search(pat[0], f)))
    bad = 0
    for f in pages:
        e = check(f, open(os.path.join(DOCS, f), encoding='utf-8').read(), ids, verbose)
        if e:
            bad += 1
            print(f'FAIL {f}: {", ".join(e)}')
    print(f'vcc-explain: {len(pages) - bad}/{len(pages)} pages ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
