#!/usr/bin/env python3
"""rename-table.py: apply a table of renames across the tree.

  src/tools/rename-table.py TABLE.tsv            dry run: counts per row, collisions
  src/tools/rename-table.py TABLE.tsv --apply    rewrite the files
  src/tools/rename-table.py TABLE.tsv --apply --move   ... and git mv the files named in path rows
  src/tools/rename-table.py TABLE.tsv --rerun ...      the table is applied already and new files came in with old
                                                       names (a merge): new names existing is then expected
  src/tools/rename-table.py TABLE.tsv --list OLD       the places where OLD is used
  src/tools/rename-table.py TABLE.tsv --fields [--apply]   the field rows, by use (see below)
  src/tools/rename-table.py --selftest [--bless]       the fixture src/test/field-uses/rename/ (--bless: write the *.after files)

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

Field rows (--fields; doc/quality/DESIGN-field-uses.md): `old new field OWNER [comment]`
(also `struct OWNER`, `union OWNER`, `OWNER, where`, `OWNER@PATH`: that definition; a kind list
containing `field` counts, `deferred` rows only with --deferred).  A field named `n` or `cur` cannot be
renamed as a word, so these rows are never part of the word pass of an unscoped table (it prints how
many it left); they are done from the report of vcc's front end (src/tools/field-uses.py, run for the
selection, or --report R.tsv, repeatable, for reports made earlier, e.g. on Linux; --commands FILE
--root DIR are passed on; --report-out F saves the merged report).  The dry run prints the uses per
row and every problem: stale or ambiguous owner, name taken in the owner (an anonymous member's too),
two rows with one new name, a macro of that name, a keyword, a position serving two owners, a macro
parameter of that name, a generated file, uses to be edited by hand (--allow-manual lists them
instead).  --apply edits the bytes at the reported positions (all of a file in one pass, each checked
to hold `old`; a stale report writes nothing).  Last it lists the whole-word occurrences of `old`
left in the scope: member-shaped code (text no configuration compiled: check them), comments, strings.
The owner's `type` rows of the same table are used to find a record before or after its rename.
--leftovers all lists every word, not only the member-shaped ones.

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
            ds[:] = [x for x in ds if x not in SKIP_DIRS and not os.path.join(d, x).endswith('src/test/field-uses/rename')]   # the --selftest fixture
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


# ---------------------------------------------------------------- field rows (--fields)

def is_field_kind(note):
    return 'field' in [k.strip() for k in note.split(',')]


def keywords():
    """C and GNU keywords: frontend/c/keywords.h of this tool's own checkout"""
    p = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'src/include/frontend/c/keywords.h')
    t = read(p) or ''
    return set(re.findall(r'X\((\w+)\)', t)) | {'__attribute__', '__asm__', 'asm', 'typeof', '__typeof__', '_Generic', '_Atomic'}


class FRow:
    def __init__(self, old, new, owner, path, ln):
        self.old, self.new, self.owner, self.path, self.ln = old, new, owner, path, ln
        self.cands = [owner]     # the owner's name before and after its own rename


def field_rows(table, deferred):
    """(rows, problems): the field rows of a table, owners resolved through its type rows"""
    rows, types, problems = [], {}, []
    for ln, line in enumerate(open(table), 1):
        line = line.rstrip('\n')
        if not line.strip() or line.startswith('#'):
            continue
        f = line.split('\t')
        if len(f) < 3:
            continue
        if f[2].strip() == 'type' or f[2].startswith('type,'):
            types[f[0]] = f[1]
        if not is_field_kind(f[2]) or ('deferred' in f[2] and not deferred):
            continue
        if len(f) < 4 or not f[3].strip():
            problems.append(f'{table}:{ln}: field row without an owner')
            continue
        owner = re.sub(r'^(struct|union) ', '', f[3].split(', ')[0].strip())
        path = None
        if '@' in owner:
            owner, path = owner.split('@', 1)
        if re.search(r'\s', owner):
            problems.append(f'{table}:{ln}: owner "{owner}" is not one name (move the note to the comment column)')
            continue
        rows.append(FRow(f[0], f[1], owner, path, ln))
    back = {v: k for k, v in types.items()}
    for r in rows:
        for o in (types.get(r.owner), back.get(r.owner)):
            if o and o not in r.cands:
                r.cands.append(o)
    return rows, problems


def selection_text(rows):
    return ''.join(f'{o}.{r.old} {r.new}\n' for r in rows for o in r.cands)


def line_col(text, off):
    return text.count('\n', 0, off) + 1, off - (text.rfind('\n', 0, off) + 1) + 1


def define_around(text, off):
    """(name, parameters or None) of the #define whose text holds offset off, else None"""
    b = text.rfind('\n', 0, off) + 1
    while b > 1 and text[b - 2] == '\\':    # the directive's first line: back over backslash-newlines
        b = text.rfind('\n', 0, b - 1) + 1
    e = b
    while True:
        e = text.find('\n', e)
        if e < 0:
            e = len(text)
            break
        if text[e - 1] == '\\':
            e += 1
        else:
            break
    if not b <= off < e:
        return None
    m = re.match(r'[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(\(([^)]*)\))?', text[b:e])
    if not m:
        return None
    params = [x.strip().rstrip('.').strip() or '__VA_ARGS__' for x in m.group(3).split(',')] if m.group(2) else None
    return m.group(1), params


def leftovers(texts, rows, edited, other):
    """whole-word occurrences of each old name that are neither edited nor another owner's:
    (path, line, col, kind, member-shaped, word, line text); kind code, comment, string or prose"""
    olds = {r.old for r in rows}
    if not olds:
        return []
    rx = re.compile(r'(?<![A-Za-z0-9_])(' + '|'.join(re.escape(w) for w in sorted(olds, key=len, reverse=True)) + r')(?![A-Za-z0-9_])')
    out = []
    for p, t in texts.items():
        isc = p.endswith(('.c', '.h', '.inc'))
        rng = [(m.start(), m.end(), 'comment' if m.group(0)[0] == '/' else 'string') for m in C_TOK.finditer(t)] if isc else []
        ri = 0
        rel = os.path.relpath(p, R)
        lines = None
        for m in rx.finditer(t):
            off = m.start()
            if (rel, off) in edited or (rel, off) in other:
                continue
            while ri < len(rng) and rng[ri][1] <= off:
                ri += 1
            kind = rng[ri][2] if ri < len(rng) and rng[ri][0] <= off else ('code' if isc else 'prose')
            pre = t[max(0, off - 60):off].rstrip()
            post = t[m.end():m.end() + 8].lstrip()
            shaped = isc and bool(re.search(r'(->|[^.]\.)$', pre))
            if kind == 'code':
                shaped = shaped or bool(re.search(r'(^|[{,])$', pre) and post.startswith(':') and not post.startswith('::')) or \
                    bool(re.search(r'offsetof\s*\([^;]*,$', pre))
            else:
                shaped = shaped or (t[off - 1:off] == '`' and t[m.end():m.end() + 1] == '`')
            line, col = line_col(t, off)
            if lines is None:
                lines = t.split('\n')
            out.append((rel, line, col, kind, shaped, m.group(1), lines[line - 1].strip()[:100]))
    return out


def fields_main(av):
    import importlib.util, tempfile
    allow_manual = '--allow-manual' in av
    apply_ = '--apply' in av
    table = av[0]

    def opt_all(name):
        return [av[i + 1] for i, a in enumerate(av) if a == name]
    reports = opt_all('--report')
    report_out = (opt_all('--report-out') or [None])[0]
    commands = (opt_all('--commands') or [None])[0]
    show = (opt_all('--leftovers') or ['member'])[0]
    rows, problems = field_rows(table, '--deferred' in av)
    if not rows:
        print(f'{table}: no field rows')
        return 1 if problems else 0
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location('field_uses', os.path.join(here, 'field-uses.py'))
    fu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fu)
    with tempfile.TemporaryDirectory() as td:
        if reports:
            seen = fu.merge_files(reports)
        else:
            sp, rp = os.path.join(td, 'selection.txt'), os.path.join(td, 'report.tsv')
            open(sp, 'w').write(selection_text(rows))
            cmd = [sys.executable, os.path.join(here, 'field-uses.py'), sp, '-o', rp, '--root', R]
            if commands:
                cmd += ['--commands', commands]
            cmd += ['--no-build'] if '--no-build' in av else []
            for k in ('--bin-dir', '--config', '-j'):
                cmd += [x for v in opt_all(k) for x in (k, v)]
            if subprocess.run(cmd).returncode:
                print('field-uses.py failed: the report would be incomplete')
                return 1
            seen = fu.merge_files([rp])
        if report_out:
            open(report_out, 'w').write('\n'.join(fu.format_report(seen)) + '\n')
    recs = [b for b, _ in seen.values()]
    uses = [r for r in recs if r[0] == 'use']
    others = [r for r in recs if r[0] == 'other']
    macros = {r[1] for r in recs if r[0] == 'macro'}
    mem_of = {}     # (owner, DEF) -> every name find_member finds from it
    for m in recs:
        if m[0] == 'members':
            mem_of.setdefault((m[1], m[2]), set()).update(m[3].split(','))
    by_row = {id(r): [] for r in rows}
    row_of = {}
    for r in rows:
        for o in r.cands:
            row_of.setdefault((o, r.old), []).append(r)
    for u in uses:
        for r in row_of.get((u[2], u[3]), []):
            if not r.path or u[4].startswith(r.path + ':'):
                by_row[id(r)].append(u)
    texts = {p: read(p) for p in files([])}
    texts = {p: t for p, t in texts.items() if t is not None}
    kw = keywords()
    positions = {}      # (path, offset) -> the rows that use it, and 'other'
    for r in rows:
        for u in by_row[id(r)]:
            if u[5] in ('file', 'macro'):
                positions.setdefault((u[6], int(u[7])), set()).add(id(r))
    for o in others:
        if o[5] in ('file', 'macro'):
            positions.setdefault((o[6], int(o[7])), set()).add('other')
    done, edits, summary = set(), {}, []
    for r in rows:
        us = by_row[id(r)]
        ds = [u for u in us if u[1] == 'decl']
        tag = f'{r.old} -> {r.new} ({r.owner}, row {r.ln})'
        defs = sorted({u[4] for u in ds})
        if not ds:
            if any(r.new in n for (o, d), n in mem_of.items() if o in r.cands and (not r.path or d.startswith(r.path + ':'))):
                summary.append(f'{0:6d}  {tag}: already applied')
            else:
                problems.append(f'unused: {tag}: no declaration of {r.owner}.{r.old} found (stale row, or in code no configuration compiles)')
            done.add(id(r))
            continue
        if len(defs) > 1:
            problems.append(f'ambiguous owner: {tag}: {len(defs)} definitions ({", ".join(defs)}); name one with OWNER@PATH')
        for d in defs:
            if os.path.isabs(d.rsplit(':', 1)[0]):
                problems.append(f'outside: {tag}: the owner is defined outside the repository ({d})')
            names = set().union(*[n for (o, dd), n in mem_of.items() if o in r.cands and dd == d])
            if r.new in names and not any(x is not r and x.owner == r.owner and x.old == r.new and x.new != r.new for x in rows):
                problems.append(f'taken: {tag}: {r.new} is already a member of {r.owner} ({d}), or of one of its anonymous members')
        for x in rows:
            if x is not r and x.owner == r.owner and x.new == r.new and x.ln > r.ln:
                problems.append(f'merge: {tag}: row {x.ln} ({x.old}) gives the same new name in the same owner')
        for n in (r.old, r.new):
            if n in macros:
                problems.append(f'macro: {tag}: {n} is a macro in a translation unit')
        drx = re.compile(r'^[ \t]*#[ \t]*define[ \t]+' + re.escape(r.new) + r'\b', re.M)
        for p, t in texts.items():
            if p.endswith(('.c', '.h', '.inc')) and '/src/test/' not in p and drx.search(t):
                problems.append(f'macro: {tag}: {os.path.relpath(p, R)} defines a macro {r.new}')
                break
        if r.new in kw:
            problems.append(f'keyword: {tag}: {r.new} is a C keyword')
        for u in us:
            where, path, off = u[5], u[6], int(u[7])
            if where in ('none', 'dirty'):
                line = f'by hand ({where}): {tag} at {u[11]}'
                summary.append(line)
                if not allow_manual:
                    problems.append(line)
                continue
            if os.path.isabs(path):
                problems.append(f'outside: {tag}: a use in {path}, outside the repository')
                continue
            if len(positions.get((path, off), ())) > 1:
                problems.append(f'shared position: {tag} at {path}:{u[8]}:{u[9]} serves another owner or row too: edit it by hand')
                continue
            t = texts.get(os.path.join(R, path))
            if t is None:
                problems.append(f'file: {tag}: cannot read {path}')
                continue
            if 'generated by' in '\n'.join(t.split('\n')[:5]):
                problems.append(f'generated: {tag}: {path} is generated: change the generator')
                continue
            if where == 'macro':
                d = define_around(t, off)
                if d and d[1] and r.new in d[1]:
                    problems.append(f'macro parameter: {tag}: the #define {d[0]} in {path} has a parameter {r.new}')
            edits.setdefault(path, {})[off] = r
        cnt = {}
        for u in us:
            cnt[u[1]] = cnt.get(u[1], 0) + 1
            cnt[u[5]] = cnt.get(u[5], 0) + 1
        summary.append(f'{len(us):6d}  {tag}: {len({u[6] for u in us})} files, ' + ' '.join(f'{k} {v}' for k, v in sorted(cnt.items())))
    problems = list(dict.fromkeys(problems))
    for s in summary:
        print(s)
    edited = {(p, o) for p, d in edits.items() for o in d}
    other = {k for k, v in positions.items() if 'other' in v}
    left = leftovers(texts, [r for r in rows if id(r) not in done], edited, other)
    shown = 0
    for rel, line, col, kind, shaped, w, text in left:
        if show == 'all' or shaped:
            print(f'to check ({kind}{", member-shaped" if shaped else ""}): {rel}:{line}:{col}: {w}: {text}')
            shown += 1
    print(f'{len(left)} other whole-word occurrences of the old names stay ({shown} listed)')
    print(f'{sum(len(d) for d in edits.values())} positions in {len(edits)} files change, {len(rows)} rows')
    for pr in problems:
        print('PROBLEM: ' + pr)
    if not apply_:
        return 1 if problems else 0
    if problems:
        sys.exit('refusing to apply with problems')
    new = {}
    for path, d in edits.items():
        data = open(os.path.join(R, path), 'rb').read()
        for off in sorted(d, reverse=True):
            r = d[off]
            if data[off:off + len(r.old)] != r.old.encode():
                sys.exit(f'{path}: offset {off} does not hold {r.old}: the report is stale, make it again (nothing written)')
            data = data[:off] + r.new.encode() + data[off + len(r.old):]
        new[os.path.join(R, path)] = data
    for full, data in new.items():
        open(full, 'wb').write(data)
    return 0


def selftest():
    """src/test/field-uses/rename: each NAME.tsv is a case on a copy of the fixture tree.
    Header lines of the table: `# expect: ok` (apply succeeds; files must equal NAME.after/),
    `# expect: problem TEXT` (the dry run prints PROBLEM with TEXT, apply refuses, nothing changes),
    `# expect: output TEXT` (the dry run's output has TEXT), `# flags: ...` (extra options),
    `# mutate: FILE|old|new` (edit a file after the report is made: the apply must fail, nothing written)."""
    import shutil, tempfile
    global R
    r0 = R
    fx = os.path.join(r0, 'src/test/field-uses/rename')
    me = os.path.abspath(__file__)
    bad = []
    cases = sorted(f for f in os.listdir(fx) if f.endswith('.tsv'))
    for c in cases:
        name = c[:-4]
        head = [l[1:].strip() for l in open(os.path.join(fx, c)) if l.startswith('#')]
        exp = [h[len('expect:'):].strip() for h in head if h.startswith('expect:')]
        flags = [x for h in head if h.startswith('flags:') for x in h[6:].split()]
        mut = [h[len('mutate:'):].strip().split('|') for h in head if h.startswith('mutate:')]
        with tempfile.TemporaryDirectory() as td:
            tree = os.path.join(td, 'tree')
            shutil.copytree(os.path.join(fx, 'tree'), tree)
            tbl = os.path.join(fx, c)
            base = [sys.executable, me, tbl, '--fields', '--root', tree, '--commands', os.path.join(fx, 'commands'), '--no-build'] + flags
            dry = subprocess.run(base + (['--report-out', os.path.join(td, 'r.tsv')] if mut else []), capture_output=True, text=True)
            out = dry.stdout + dry.stderr
            before = {os.path.relpath(os.path.join(d, f), tree): open(os.path.join(d, f), 'rb').read() for d, _, fs in os.walk(tree) for f in fs}
            for e in exp:
                if e.startswith('problem '):
                    if f'PROBLEM: {e[8:]}' not in out and not re.search(r'PROBLEM: [^\n]*' + re.escape(e[8:]), out):
                        bad.append(f'{name}: no PROBLEM containing "{e[8:]}":\n{out}')
                elif e.startswith('output '):
                    if e[7:] not in out:
                        bad.append(f'{name}: output lacks "{e[7:]}":\n{out}')
                elif e in ('ok', 'ok-dry') and 'PROBLEM' in out:
                    bad.append(f'{name}: unexpected problem:\n{out}')
            if mut:
                f, a, b = mut[0]
                p = os.path.join(tree, f)
                open(p, 'w').write(open(p).read().replace(a, b))
                before[f] = open(p, 'rb').read()
                ap = subprocess.run(base + ['--report', os.path.join(td, 'r.tsv'), '--apply'], capture_output=True, text=True)
            else:
                ap = subprocess.run(base + ['--apply'], capture_output=True, text=True)
            after = {os.path.relpath(os.path.join(d, f), tree): open(os.path.join(d, f), 'rb').read() for d, _, fs in os.walk(tree) for f in fs}
            if 'ok' in exp and '--bless' in sys.argv:
                ad = os.path.join(fx, name + '.after')
                shutil.rmtree(ad, ignore_errors=True)
                for k, v in after.items():
                    if v != before.get(k):
                        os.makedirs(os.path.dirname(os.path.join(ad, k)), exist_ok=True)
                        open(os.path.join(ad, k), 'wb').write(v)
                continue
            if 'ok' in exp:
                want = dict(before)
                ad = os.path.join(fx, name + '.after')
                for d, _, fs in os.walk(ad):
                    for f in fs:
                        want[os.path.relpath(os.path.join(d, f), ad)] = open(os.path.join(d, f), 'rb').read()
                if ap.returncode or after != want:
                    diff = [k for k in want if after.get(k) != want[k]]
                    bad.append(f'{name}: apply gave {ap.returncode}, differs in {diff}:\n{ap.stdout[-600:]}{ap.stderr[-600:]}')
            elif any(e.startswith('problem') or e.startswith('output') for e in exp) or mut:
                if ((mut or any(e.startswith('problem') for e in exp)) and ap.returncode == 0) or after != before:
                    bad.append(f'{name}: apply should refuse and write nothing (rc {ap.returncode}, changed: {[k for k in after if after[k] != before.get(k)]})')
    for b in bad:
        print('FAIL ' + b)
    print(f'rename-table --selftest: {len(cases) - len(bad)}/{len(cases)} cases ok' if not bad else f'rename-table --selftest: {len(bad)} of {len(cases)} cases failed')
    return 1 if bad else 0


def main():
    global R
    av = sys.argv[1:]
    if not av or av[0] in ('-h', '--help'):
        sys.exit(__doc__)
    if '--root' in av:
        R = os.path.abspath(av[av.index('--root') + 1])
    if '--selftest' in av:
        return selftest()
    if '--fields' in av:
        return fields_main(av)
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
    else:   # a field row is done by use (--fields): as a word it would rewrite every `n` and `cur`
        nf = sum(1 for r in rows if is_field_kind(r[2]))
        rows = [r for r in rows if not is_field_kind(r[2])]
        if nf:
            print(f'{nf} field rows: apply them with --fields')
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
