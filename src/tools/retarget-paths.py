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
}
TEXT = {'T1': T1_TEXT}

# never rewritten: the plan that describes the old paths, logs
SKIP_TEXT = re.compile(r'^(doc/PLAN-tree\.md|docs/PLAN-tree\.md|results/|patches/)')
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
        rel = os.path.relpath(m(tgt), newdir or '.')
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
    return path.startswith('src/') or path in ('AGENTS.md', 'TODO.md') or path.startswith('doc/')


def rewrite_text(files, steps):
    changed = []
    for f in files:
        if SKIP_TEXT.match(f) or not is_text(f):
            continue
        p = os.path.join(R, f)
        s = open(p, encoding='utf-8', errors='surrogateescape').read()
        t = s
        for step in steps:
            for rx, b, rel in text_pairs(step):
                if rel and not in_relative_scope(f):
                    continue
                t = rx.sub(lambda mo: b, t)
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
