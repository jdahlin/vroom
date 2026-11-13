#!/usr/bin/env python3
"""rename-table.py: apply a table of renames across the tree.

  src/tools/rename-table.py TABLE.tsv            dry run: counts per row, collisions
  src/tools/rename-table.py TABLE.tsv --apply    rewrite the files
  src/tools/rename-table.py TABLE.tsv --apply --move   ... and git mv the files named in path rows
  src/tools/rename-table.py TABLE.tsv --rerun ...      the table is applied already and new files came in with old
                                                       names (a merge): new names existing is then expected
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

In C files nothing inside a string literal is changed (output text stays) except on
#include lines.

Scope: src/, bench/, ci/, contrib/, AGENTS.md, README.md, TODO.md and
doc/STYLE.md; not doc/ otherwise (plans and reviews keep their history),
not build/, work/ or test inputs under src/test/ that are expected outputs
(.stderr, .expect, .s, .o).  --allow-exists W,W lets those new names exist already (words only in comments).
  --also PATH adds a file or directory.

Safety: the dry run (and --apply, which refuses) report a collision when
  - a new name is already used as a word in the scope and is not itself an
    old name of the table (rename into an existing identifier), or
  - two rows give the same new name (a merge: say so in the row's note and
    use --allow-merge, the author must make the bodies agree first), or
  - an old name occurs nowhere (a stale row or a misspelt guard),
  - an old name occurs in a string literal that looks like output (listed,
    not changed: decide by hand).
Scoped mode (--scoped LOCAL_DIRS [--users-of ROOT_DIRS], comma lists): the table's third column is a kind list.
A row whose kind has no `public` applies only to the files under LOCAL_DIRS
(repo-relative) and to the paths named by `also:PATH` in its kind; `in:SUFFIX`
narrows it to the files there whose path ends with /SUFFIX.  A `public` row
applies to non-C files and to C files that include, directly or through other
headers, a file under ROOT_DIRS (default LOCAL_DIRS).  A word is not renamed
after `#` (directive names) nor after `->`/`.` unless the row's kind has `field`;
in a comment only names with an underscore, in upper case or followed by `(`
are renamed; strings only on #include lines.  Rows whose kind contains `deferred` are skipped.  Without
--scoped every row applies everywhere (the common and assembler tables).

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


C_TOK = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'', re.S)
INC_RX = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def include_closure(texts):
    """path -> set of paths it includes, transitively (C files and headers)."""
    direct = {}
    for p, t in texts.items():
        if not p.endswith(('.c', '.h', '.inc')):
            continue
        d = []
        for m in INC_RX.finditer(t):
            for base in (os.path.dirname(p), os.path.join(R, 'src', 'include'), os.path.join(R, 'src')):
                q = os.path.normpath(os.path.join(base, m.group(1)))
                if q in texts:
                    d.append(q)
                    break
        direct[p] = d
    memo = {}

    def close(p, stack):
        if p in memo:
            return memo[p]
        out = set()
        for q in direct.get(p, []):
            if q in stack:
                continue
            out.add(q)
            out |= close(q, stack | {p})
        memo[p] = out
        return out
    return {p: close(p, frozenset()) for p in direct}


def main():
    av = sys.argv[1:]
    if not av or av[0] in ('-h', '--help'):
        sys.exit(__doc__)
    table = av[0]
    apply_ = '--apply' in av
    move = '--move' in av
    merge_ok = '--allow-merge' in av
    exists_ok = set(av[av.index('--allow-exists') + 1].split(',')) if '--allow-exists' in av else set()
    rerun = '--rerun' in av
    extra = [av[i + 1] for i, a in enumerate(av) if a == '--also']
    rows = load(table)
    scoped = [av[i + 1] for i, a in enumerate(av) if a in ('--scoped', '--users-of')][:2]
    if scoped:
        rows = [r for r in rows if 'deferred' not in r[2]]
    words, paths, pats = build(rows)
    fl = files(extra)
    texts = {p: read(p) for p in fl}
    texts = {p: t for p, t in texts.items() if t is not None}
    local = ()
    users = set()
    if scoped:
        local = tuple(os.path.join(R, d.rstrip('/')) + '/' for d in scoped[0].split(','))
        clo = include_closure(texts)
        roots = tuple(os.path.join(R, d.rstrip('/')) + '/' for d in scoped[1].split(',')) if len(scoped) > 1 else local
        users = {p for p, c in clo.items() if p.startswith(roots + local) or any(q.startswith(roots) for q in c)}
    pubw, locw, alsow = {}, {}, {}
    pubr, locr = [], []
    for o, n, note, ln in rows:
        if scoped and 'public' not in note:
            locr.append((o, n, note, ln))
            if o not in paths:
                for k in note.split(','):
                    if k.startswith('also:'):
                        alsow.setdefault(o, []).append(os.path.join(R, k[5:]))
        else:
            pubr.append((o, n, note, ln))
    scope_pats = {}
    if scoped:
        scope_pats['pub'] = build(pubr)
        scope_pats['loc'] = build(locr)

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
    pub_names = {o for o, _, _, _ in pubr}
    for p, t in texts.items():
        if not p.endswith(('.c', '.h', '.inc')):
            continue
        # scoped: a new name only collides in the files its row applies to
        tag = ('pub' if p in users else '') + ('loc' if p.startswith(local) else '') if scoped else 'all'
        for w in set(wrx.findall(t)):
            allwords.setdefault(w, {}).setdefault(tag, p)
    for o, n, note, ln in rows:
        if o in paths:
            dst = os.path.join(R, 'src', 'include', n)
            if os.path.exists(dst):
                problems.append(f'path: {n} exists already (row {ln})')
            continue
        if n in allwords and n not in words and not note.startswith('unlikely') and n not in exists_ok and not rerun:
            hits = allwords[n]
            if scoped:
                want = 'pub' if o in pub_names else 'loc'
                hit = next((f for t, f in hits.items() if want in t), None)
            else:
                hit = next(iter(hits.values()))
            if hit:
                problems.append(f'exists: {n} (new name of {o}, row {ln}) is already used, e.g. in {os.path.relpath(hit, R)}')
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
    kinds = {o: note for o, n, note, ln in rows}
    cache = {}

    def file_pats(p):
        if not scoped:
            return words, paths, pats
        isc = p.endswith(('.c', '.h', '.inc'))
        rs = []
        if not isc or p in users:
            rs += pubr
        if p.startswith(local):
            rs += [r for r in locr if not any(k.startswith('in:') for k in r[2].split(',')) or
                   any(p.endswith('/' + k[3:]) for k in r[2].split(',') if k.startswith('in:'))]
        else:
            rs += [r for r in locr if p in alsow.get(r[0], ())]
        key = tuple(r[3] for r in rs)
        if key not in cache:
            cache[key] = build(rs)
        return cache[key]

    for p, t in texts.items():
        words_f, paths_f, pats = file_pats(p)
        def sub(m, words_f=words_f, paths_f=paths_f):
            o = m.group(1)
            n = words_f.get(o) or paths_f.get(o)
            if o in words_f and scoped and 'field' not in kinds.get(o, '') and re.search(r'(->|\.)$', m.string[:m.start()]):
                return o        # a member of some struct: only field rows rename those
            if o in words_f and re.search(r'#\s*$', m.string[:m.start()]):
                return o        # a preprocessor directive's name (include, define, error ...)
            counts[o] = counts.get(o, 0) + 1
            return n
        u = t
        if scoped and p.endswith(('.c', '.h', '.inc')):
            # code gets every row; a comment only the names that cannot be English words
            # (an underscore, upper case, or a call `name(`); strings only on #include lines
            def comment_sub(m):
                o = m.group(1)
                if o in paths_f or '_' in o or o.isupper() or m.string[m.end():m.end() + 1] == '(':
                    return sub(m)
                return o
            out, pos = [], 0
            for m in C_TOK.finditer(t):
                gap = t[pos:m.start()]
                for pat in pats:
                    gap = pat.sub(sub, gap)
                out.append(gap)
                tok = m.group(0)
                if tok[0] == '/':
                    for pat in pats:
                        tok = pat.sub(comment_sub, tok)
                elif tok[0] == '"' and re.search(r'^[ \t]*#[ \t]*include[ \t]*$', t[t.rfind('\n', 0, m.start()) + 1:m.start()]):
                    for pat in pats:
                        tok = pat.sub(sub, tok)
                out.append(tok)
                pos = m.end()
            gap = t[pos:]
            for pat in pats:
                gap = pat.sub(sub, gap)
            out.append(gap)
            u = ''.join(out)
        elif p.endswith(('.c', '.h', '.inc')):
            # not inside string literals (output text stays as it is), except #include lines
            lit = re.compile(r'"(?:[^"\\\n]|\\.)*"')
            def line(l):
                if l.lstrip().startswith('#include'):
                    for pat in pats:
                        l = pat.sub(sub, l)
                    return l
                parts, pos = [], 0
                for m in lit.finditer(l):
                    seg = l[pos:m.start()]
                    for pat in pats:
                        seg = pat.sub(sub, seg)
                    parts += [seg, m.group(0)]
                    pos = m.end()
                seg = l[pos:]
                for pat in pats:
                    seg = pat.sub(sub, seg)
                return ''.join(parts) + seg
            u = '\n'.join(line(l) for l in t.split('\n'))
        else:
            for pat in pats:
                u = pat.sub(sub, u)
        if u != t:
            new_texts[p] = u

    for o, n, note, ln in rows:
        print(f'{counts.get(o, 0):6d}  {o} -> {n}')
    unused = [o for o, n, _, _ in rows if not counts.get(o)]
    print(f'{len(new_texts)} files change, {sum(counts.values())} replacements, {len(rows)} rows')
    for s in strs:
        print('string literal (not changed, decide by hand): ' + s)
    for o in ([] if rerun else unused):
        problems.append(f'unused: {o} occurs nowhere (stale row, or the guard/name is spelled differently)')
    for pr in problems:
        print('PROBLEM: ' + pr)
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
