#!/usr/bin/env python3
"""rename-table.py: apply a table of renames across the tree.

  src/tools/rename-table.py TABLE.tsv            dry run: counts per row, collisions
  src/tools/rename-table.py TABLE.tsv --apply    rewrite the files
  src/tools/rename-table.py TABLE.tsv --apply --move   ... and git mv the files named in path rows
  src/tools/rename-table.py TABLE.tsv --list OLD       the places where OLD is used

TABLE.tsv: lines `old<TAB>new[<TAB>note]`; blank lines and lines starting
with # are ignored.  Two kinds of row:

  path rows   old ends in .h or .c (include/ relative, as in #include "..."):
              the path is rewritten in #include lines, in Makefile and script
              source lists and in prose (every occurrence of the text), and
              with --move the file under src/include/ or src/ is renamed too.
  word rows   anything else: an identifier, replaced as a whole word
              ([A-Za-z0-9_] boundaries) in C, headers, Python, shell, Makefiles
              and the live documents.

Scope: src/, bench/, ci/, contrib/, AGENTS.md, README.md, TODO.md and
doc/STYLE.md; not doc/ otherwise (plans and reviews keep their history),
not build/, work/ or test inputs under src/test/ that are expected outputs
(.stderr, .expect, .s, .o).  --also PATH adds a file or directory.

Safety: the dry run (and --apply, which refuses) report a collision when
  - a new name is already used as a word in the scope and is not itself an
    old name of the table (rename into an existing identifier), or
  - two rows give the same new name (a merge: say so in the row's note and
    use --allow-merge, the author must make the bodies agree first), or
  - an old name occurs in a string literal that looks like output (listed,
    not changed: decide by hand).
All replacements are done in one pass per file with one alternation, longest
old name first, so a row can never rewrite the result of another.
"""
import os, re, subprocess, sys

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCOPE = ['src', 'bench', 'ci', 'contrib', 'AGENTS.md', 'README.md', 'TODO.md', 'doc/STYLE.md']
EXT = {'.c', '.h', '.inc', '.py', '.sh', '.md', '.txt', '.tsv', '.json', '.yml', '.mk', ''}
SKIP_DIRS = {'build', 'work', '.git'}
SKIP_SUFFIX = ('.stderr', '.expect', '.s', '.o', '.bin', '.a', '.json.gz')


def load(table):
    rows = []
    for ln, line in enumerate(open(table), 1):
        line = line.rstrip('\n')
        if not line.strip() or line.startswith('#'):
            continue
        f = line.split('\t')
        if len(f) < 2 or not f[0] or not f[1]:
            sys.exit(f'{table}:{ln}: need old<TAB>new')
        rows.append((f[0], f[1], f[2] if len(f) > 2 else '', ln))
    return rows


def files(extra):
    out = []
    for s in SCOPE + extra:
        p = os.path.join(R, s)
        if os.path.isfile(p):
            out.append(p)
            continue
        for d, ds, fs in os.walk(p):
            ds[:] = [x for x in ds if x not in SKIP_DIRS]
            for f in fs:
                if f.endswith(SKIP_SUFFIX):
                    continue
                if os.path.splitext(f)[1] in EXT or f in ('Makefile', 'LICENSE'):
                    out.append(os.path.join(d, f))
    return sorted(set(out))


def read(p):
    try:
        return open(p, encoding='utf-8').read()
    except (UnicodeDecodeError, OSError):
        return None


def build(rows):
    words = {o: n for o, n, _, _ in rows if not o.endswith(('.h', '.c'))}
    paths = {o: n for o, n, _, _ in rows if o.endswith(('.h', '.c'))}
    pats = []
    if words:
        pats.append(re.compile(r'(?<![A-Za-z0-9_])(' + '|'.join(re.escape(w) for w in sorted(words, key=len, reverse=True)) + r')(?![A-Za-z0-9_])'))
    if paths:
        pats.append(re.compile('(' + '|'.join(re.escape(w) for w in sorted(paths, key=len, reverse=True)) + r')(?![A-Za-z0-9_.-])'))
    return words, paths, pats


def main():
    av = sys.argv[1:]
    if not av or av[0] in ('-h', '--help'):
        sys.exit(__doc__)
    table = av[0]
    apply_ = '--apply' in av
    move = '--move' in av
    merge_ok = '--allow-merge' in av
    extra = [av[i + 1] for i, a in enumerate(av) if a == '--also']
    rows = load(table)
    words, paths, pats = build(rows)
    fl = files(extra)
    texts = {p: read(p) for p in fl}
    texts = {p: t for p, t in texts.items() if t is not None}

    if '--list' in av:
        old = av[av.index('--list') + 1]
        rx = re.compile(r'(?<![A-Za-z0-9_])' + re.escape(old) + r'(?![A-Za-z0-9_])')
        for p, t in texts.items():
            for i, l in enumerate(t.split('\n'), 1):
                if rx.search(l):
                    print(f'{os.path.relpath(p, R)}:{i}: {l.strip()[:120]}')
        return 0

    problems = []
    # a merge: two rows, one new name
    seen = {}
    for o, n, note, ln in rows:
        if n in seen and seen[n] != o and not merge_ok:
            problems.append(f'merge: {seen[n]} and {o} both become {n} (row {ln}); --allow-merge once the bodies agree')
        seen.setdefault(n, o)
    # a new name that exists already
    allwords = {}
    wrx = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
    for p, t in texts.items():
        if not p.endswith(('.c', '.h', '.inc')):
            continue
        for w in set(wrx.findall(t)):
            allwords.setdefault(w, p)
    for o, n, note, ln in rows:
        if o in paths:
            dst = os.path.join(R, 'src', 'include', n)
            if os.path.exists(dst):
                problems.append(f'path: {n} exists already (row {ln})')
            continue
        if n in allwords and n not in words and not note.startswith('unlikely'):
            problems.append(f'exists: {n} (new name of {o}, row {ln}) is already used, e.g. in {os.path.relpath(allwords[n], R)}')
    # old names in string literals that look like output
    strrx = re.compile(r'"(?:[^"\\\n]|\\.)*"')
    strs = []
    if words:
        wrx2 = re.compile(r'(?<![A-Za-z0-9_])(' + '|'.join(re.escape(w) for w in words) + r')(?![A-Za-z0-9_])')
        for p, t in texts.items():
            if not p.endswith(('.c', '.h', '.inc')):
                continue
            for i, l in enumerate(t.split('\n'), 1):
                for m in strrx.finditer(l):
                    if wrx2.search(m.group(0)) and '#include' not in l:
                        strs.append(f'{os.path.relpath(p, R)}:{i}: {l.strip()[:100]}')

    counts = {}
    new_texts = {}
    for p, t in texts.items():
        def sub(m):
            o = m.group(1)
            n = words.get(o) or paths.get(o)
            counts[o] = counts.get(o, 0) + 1
            return n
        u = t
        for pat in pats:
            u = pat.sub(sub, u)
        if u != t:
            new_texts[p] = u

    for o, n, note, ln in rows:
        print(f'{counts.get(o, 0):6d}  {o} -> {n}')
    print(f'{len(new_texts)} files change, {sum(counts.values())} replacements, {len(rows)} rows')
    for s in strs:
        print('string literal (not changed, decide by hand): ' + s)
    for pr in problems:
        print('PROBLEM: ' + pr)
    unused = [o for o, n, _, _ in rows if not counts.get(o)]
    if unused:
        print('rows with no use (stale?): ' + ', '.join(unused))
    if not apply_:
        return 1 if problems else 0
    if problems:
        sys.exit('refusing to apply with problems')
    for p, u in new_texts.items():
        open(p, 'w', encoding='utf-8').write(u)
    if move:
        for o, n, _, _ in rows:
            if o in paths:
                src = os.path.join(R, 'src', 'include', o)
                if not os.path.exists(src):
                    src = os.path.join(R, 'src', o)
                dst = os.path.join(os.path.dirname(src), os.path.basename(n))
                if os.path.exists(src):
                    subprocess.run(['git', '-C', R, 'mv', src, dst], check=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
