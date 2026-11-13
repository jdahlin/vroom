#!/usr/bin/env python3
"""retarget-paths.py: the source tree moves of doc/PLAN-tree.md, as data.

  src/tools/retarget-paths.py move STEP     git mv the step's files, then rewrite
                                            #include lines and path references
  src/tools/retarget-paths.py text [FILE..] rewrite path references (all steps,
                                            in order) in FILEs, default every
                                            tracked text file: for a branch
                                            started before the reorganisation

Each step is a list of (old, new) repository paths; a path ending in / is a
directory prefix.  `move` resolves every quoted #include of a C/C++ file against
the includer's old directory and, when it names a tracked file, rewrites it
relative to the includer's new directory (or, with -I roots, as a root-relative
name: see ROOTS).  Text references are replaced as repository paths
(`ccs/vcc/lex/`), and inside src/ and the docs also as src-relative paths
(`lex/lex.c`) when the left neighbour is not a path character.
"""
import os, re, subprocess, sys

R = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True,
                   check=True).stdout.strip()

# text-only replacements (no files move): build directories and make -C
T1_TEXT = [('build/linux/ccs', 'build/linux/src'), ('build/ccs', 'build/src'),
           ('ccs-asan', 'src-asan'), ('ccs-tsan', 'src-tsan'), ('ccs-parts', 'src-parts'),
           ('ccs-base', 'src-base'), ('ccs-exp', 'src-exp'),
           ('make -C ccs', 'make -C src'), ('-C ccs ', '-C src ')]

PUBLIC_T4 = (
    'arch/a64/a64.h', 'arch/a64/reloc.h', 'arch/x64/reloc.h', 'arch/x64/x86features.h',
    'arch/x64/x86features-data.inc', 'arch/wasm/as/wasmas.h', 'as/as.h', 'codegen/emit.h',
    *[f'common/{h}' for h in ('arena.h', 'attr.h', 'bigint.h', 'buf.h', 'ckd.h', 'defer.h', 'f128.h',
                              'hash.h', 'intern.h', 'ldhash.h', 'tbuf.h', 'vec.h')],
    'driver/ccsnap.h', 'driver/ppdriver.h', 'frontend/c/cc.h', 'frontend/c/ccint.h',
    'frontend/lex/lex.h', 'frontend/pp/dircache.h', 'frontend/pp/pp.h', 'ld/ld.h', 'obj/obj.h',
    *[f'port/{h}' for h in ('hostcrc.h', 'mapfile.h', 'vperf.h', 'vstack.h', 'x86native.h',
                            'x86native-data.inc', 'x86native-LICENSE.txt')],
    'utils/ar.h', 'utils/fixdep.h', 'utils/rsh.h',
    'vir/alloc.h', 'vir/layout.h', 'vir/synth.h', 'vir/vir.h')

STEPS = {
    'T1': [('ccs/vcc/include/', 'src/headers/'),
           ('ccs/vcc/docs/diag/', 'doc/diag/'),
           ('ccs/vcc/', 'src/'),
           ('ccs/Makefile', 'src/Makefile'),
           ('ccs/protocol.h', 'src/include/vcc/protocol.h'),
           ('ccs/phase1/', 'contrib/phase1/'),
           ('ccs/wasm/', 'contrib/wasm/'),
           ('ccs/fuzz/', 'src/fuzz/'),
           ('docs/', 'doc/')],
    # data structures for every tool -> common/; host glue -> port/; the x86
    # target's feature table -> arch/x64/
    'T2': [*[(f'src/lib/{f}', f'src/common/{f}') for f in (
               'arena.h', 'attr.h', 'bigint.h', 'buf.h', 'ckd.h', 'defer.h', 'f128.h', 'hash.h',
               'intern.c', 'intern.h', 'ldhash.c', 'ldhash.h', 'tbuf.h', 'vec.h')],
           *[(f'src/lib/{f}', f'src/port/{f}') for f in (
               'hostcrc.h', 'mapfile.h', 'vperf.h', 'vstack.h',
               'x86native.h', 'x86native-data.inc', 'x86native-LICENSE.txt')],
           ('src/lib/x86features.h', 'src/arch/x64/x86features.h'),
           ('src/lib/x86features-data.inc', 'src/arch/x64/x86features-data.inc'),
           ('src/platform/darwin/pmc.c', 'src/port/darwin/pmc.c'),
           ('contrib/wasm/mman.c', 'src/port/wasm/mman.c'),
           ('contrib/wasm/compat/', 'src/port/wasm/compat/')],
    # the language side under frontend/, code generation as codegen/, vccd's parts
    # apart, one directory per program's main under bin/
    'T3': [('src/pp/diag.c', 'src/frontend/diag/diag.c'),
           ('src/pp/diagout.c', 'src/frontend/diag/diagout.c'),
           ('src/pp/diagtext.c', 'src/frontend/diag/diagtext.c'),
           ('src/pp/diagids.inc', 'src/frontend/diag/diagids.inc'),
           ('src/lex/lextest.c', 'src/bin/lextest/lextest.c'),
           ('src/lex/lexdiff.py', 'src/bin/lextest/lexdiff.py'),
           ('src/lex/lexpmc.sh', 'src/bin/lextest/lexpmc.sh'),
           ('src/lex/', 'src/frontend/lex/'),
           ('src/pp/', 'src/frontend/pp/'),
           ('src/cc/', 'src/frontend/c/'),
           ('src/emit/', 'src/codegen/'),
           ('src/arch/a64/emit/', 'src/arch/a64/codegen/'),
           ('src/arch/x64/emit/', 'src/arch/x64/codegen/'),
           ('src/arch/wasm/emit/', 'src/arch/wasm/codegen/'),
           ('src/vccd/shim.c', 'src/bin/vcc/shim.c'),
           *[(f'src/vccd/{f}', f'src/server/{f}') for f in (
               'vccd.c', 'dstore.c', 'dstore.h', 'caps.c', 'caps.h', 'warnings.inc', 'gen-warnings.sh')],
           *[(f'src/vccd/{f}', f'src/utils/{f}') for f in (
               'ar.c', 'ar.h', 'rsh.c', 'rsh.h', 'fixdep.c', 'fixdep.h')],
           ('src/as/vasm.c', 'src/bin/vasm/vasm.c'),
           ('src/driver/vpp.c', 'src/bin/vpp/vpp.c'),
           ('src/vir/virtest.c', 'src/bin/virtest/virtest.c'),
           ('src/vir/viralloc.c', 'src/bin/viralloc/viralloc.c')],
    # the headers other modules include -> src/include/, mirroring the tree
    # (PostgreSQL's src/include); included as "common/arena.h" with -Isrc/include
    'T4': [*[(f'src/{h}', f'src/include/{h}') for h in PUBLIC_T4],
           ('src/platform/darwin/pmc.h', 'src/include/port/darwin/pmc.h'),
           ('src/include/vcc/protocol.h', 'src/include/protocol.h')],
}
TEXT = {'T1': T1_TEXT}

# never rewritten: the plan that describes the old paths, logs
SKIP_TEXT = re.compile(r'^(doc/PLAN-tree\.md|docs/PLAN-tree\.md|results/|patches/|src/tools/retarget-paths\.py)')
ROOT_INC = 'src/include/'
C_EXT = ('.c', '.h', '.inc', '.cpp', '.cc', '.S', '.def')
PATHCH = r'A-Za-z0-9_.\-'


def git(*a, **kw):
    return subprocess.run(['git', *a], cwd=R, check=True, capture_output=True, text=True, **kw).stdout


def tracked():
    return [f for f in git('ls-files', '-z').split('\0') if f]


def mapper(pairs):
    pairs = sorted(pairs, key=lambda p: -len(p[0]))
    def m(path):
        for a, b in pairs:
            if a.endswith('/') and path.startswith(a):
                return b + path[len(a):]
            if path == a:
                return b
        return path
    return m


def is_text(path):
    try:
        with open(os.path.join(R, path), 'rb') as f:
            head = f.read(8192)
    except OSError:
        return False
    return b'\0' not in head


INC = re.compile(r'^(\s*#\s*(?:include|import|include_next)\s*")([^"]+)(")', re.M)


def rewrite_includes(newfile, oldfile, m, old_set):
    p = os.path.join(R, newfile)
    s = open(p, encoding='utf-8', errors='surrogateescape').read()
    olddir, newdir = os.path.dirname(oldfile), os.path.dirname(newfile)
    def fix(mo):
        name = mo.group(2)
        tgt = os.path.normpath(os.path.join(olddir, name))
        if tgt not in old_set:
            return mo.group(0)
        new = m(tgt)
        if (new.startswith(ROOT_INC) and newfile.startswith('src/')
                and not newfile.startswith(('src/tests/', 'src/test/'))):
            rel = new[len(ROOT_INC):]	# -Isrc/include
        else:
            rel = os.path.relpath(new, newdir or '.')
        return mo.group(1) + rel + mo.group(3) if rel != name else mo.group(0)
    t = INC.sub(fix, s)
    if t != s:
        open(p, 'w', encoding='utf-8', errors='surrogateescape').write(t)
        return True
    return False


def text_pairs(step):
    """(regex, replacement) for one step: repo paths, then src-relative paths"""
    out = []
    pairs = sorted(STEPS.get(step, []) + TEXT.get(step, []), key=lambda p: -len(p[0]))
    def rx(a, left):
        # a directory also matches without its slash at the end of a name (`{R}/ccs/vcc/docs/diag'`)
        if a.endswith('/') and '/' in a[:-1]:	# never a bare word: `lex` in prose
            return re.compile(r'(?<![' + left + r'])' + re.escape(a[:-1]) + r'(?=/|(?![' + PATHCH + r']))')
        return re.compile(r'(?<![' + left + r'])' + re.escape(a))
    for a, b in pairs:
        out.append((rx(a, PATHCH), b[:-1] if a.endswith('/') and '/' in a[:-1] else b, False))
    for a, b in pairs:
        if a.startswith('src/') and b.startswith('src/'):
            out.append((rx(a[4:], PATHCH + '/'), (b[:-1] if a.endswith('/') and '/' in a[4:-1] else b)[4:], True))
    return out


def in_relative_scope(path):
    # not the test inputs and snapshots: their text is data
    return ((path.startswith('src/') and not path.startswith(('src/tests/', 'src/test/')))
            or path in ('AGENTS.md', 'TODO.md', 'README.md') or path.startswith('doc/'))


def rewrite_text(files, steps):
    changed = []
    for f in files:
        if SKIP_TEXT.match(f) or not is_text(f):
            continue
        p = os.path.join(R, f)
        s = open(p, encoding='utf-8', errors='surrogateescape').read()
        # #include lines are rewrite_includes' (a root-relative name is not a path from here)
        lines = s.splitlines(keepends=True)
        for step in steps:
            pairs = [(rx, b) for rx, b, rel in text_pairs(step) if not rel or in_relative_scope(f)]
            for i, line in enumerate(lines):
                if f.endswith(C_EXT) and INC.match(line):
                    continue
                for rx, b in pairs:
                    line = rx.sub(lambda mo: b, line)
                lines[i] = line
        t = ''.join(lines)
        if t != s:
            open(p, 'w', encoding='utf-8', errors='surrogateescape').write(t)
            changed.append(f)
    return changed


def move(step):
    files = tracked()
    old_set = set(files)
    m = mapper(STEPS[step])
    moves = [(f, m(f)) for f in files if m(f) != f]
    for a, b in moves:
        os.makedirs(os.path.join(R, os.path.dirname(b)), exist_ok=True)
        git('mv', a, b)
    newof = {b: a for a, b in moves}
    n_inc = 0
    for f in tracked():
        if f.endswith(C_EXT) and is_text(f):
            n_inc += rewrite_includes(f, newof.get(f, f), m, old_set)
    changed = rewrite_text(tracked(), [step])
    print(f'{step}: {len(moves)} files moved, includes rewritten in {n_inc}, '
          f'references in {len(changed)} files')


def main(av):
    if len(av) >= 2 and av[0] == 'move':
        move(av[1])
    elif av and av[0] == 'text':
        files = av[1:] or tracked()
        for f in rewrite_text(files, list(STEPS)):
            print(f)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
