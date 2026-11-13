#!/usr/bin/env python3
"""
Diagnostic coverage: vcc's messages against clang's table (PLAN-diag.md D0.1).

Extracts the format string of every diagnostic call in vcc's compiler
(frontend/pp/, diag/, frontend/c/, codegen/, arch/*/codegen/, driver/, frontend/lex/), matches each against
clang's message templates (bench/clangdiag.py) and reports:

  - per language bucket and class: clang's count, how many vcc covers;
  - vcc messages that match no clang diagnostic (vcc-only, or worded
    differently), and calls whose format is not a literal (dynamic);
  - with --missing [REGEX]: clang's C diagnostics vcc lacks, by -W group.

Matching is by text, so it is a bootstrap: once diagnostics carry ids
(D1.3), coverage reads the ids instead and wording may differ freely.

  bench/vcc-diagcov.py [-v] [--missing [REGEX]] [--json FILE]
"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clangdiag

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V = f'{R}/src'
DIRS = ['frontend/pp', 'diag', 'frontend/c', 'codegen', 'arch/a64/codegen', 'driver', 'frontend/lex']
# function: (index of the format argument, kind or index of the kind argument)
CALLS = {'cc_error': (2, 'error'), 'cc_errorx': (6, 'error'), 'fatal': (2, 'error'), 'note': (2, 'note'), 'report': (7, 1),
         'cc_warnx': (9, 'warning'), 'rt_defer': (8, 'warning'), 'fmt_report': (7, 'warning'), 'cc_warn_begin': (5, 'warning'), 'warn_at': (4, 'warning'), 'pp_diag': (3, 2), 'pp_diagx': (2, '?'), 'pp_warnx': (5, 'warning'), 'EXT_WARN': (4, 'warning'), 'EXT_WARN_MS': (5, 'warning'), 'EXT_WARN_DEL': (6, 'warning'), 'ext_warn': (4, 'warning'),
         'ediag': (2, '?'), 'eerror': (2, 'error'), 'efatal': (2, 'error'), 'macro_note_': (3, 'note'),
         'pp_diag_begin': (3, '?'), 'pp_diag_note': (3, 'note'), 'error': (2, 'error'), 'warn': (2, 'warning')}
# pp's error()/warn()/fatal() macros (ppimpl.h): only in the pp module (frontend/pp/, diag/)
PPONLY = {'error', 'warn'}
MARK = '\x01'		# a printf conversion in a vcc format


def split_args(s, i):
    """s[i] == '(': (args, end)"""
    args, cur, d, j, n = [], [], 0, i, len(s)
    while j < n:
        c = s[j]
        if c in '"\'':
            k = j + 1
            while k < n and s[k] != c:
                k += 2 if s[k] == '\\' else 1
            cur.append(s[j:k + 1])
            j = k + 1
            continue
        if c in '([{':
            d += 1
            if d == 1 and c == '(':
                j += 1
                continue
        elif c in ')]}':
            d -= 1
            if d == 0:
                args.append(''.join(cur).strip())
                return args, j
        elif c == ',' and d == 1:
            args.append(''.join(cur).strip())
            cur = []
            j += 1
            continue
        cur.append(c)
        j += 1
    return args, n


def literal(a):
    """the string literal an argument is made of, or None"""
    a = re.sub(r'/\*.*?\*/', '', a, flags=re.S).strip()
    if not re.fullmatch(r'(?:"(?:[^"\\]|\\.)*"\s*)+', a, re.S):
        return None
    return ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', a)).encode().decode('unicode_escape')


def literals(a):
    """the string literals an argument is made of: one, or both arms of a ?: of literals"""
    one = literal(a)
    if one is not None:
        return [one]
    a = re.sub(r'/\*.*?\*/', '', a, flags=re.S).strip()
    m = re.fullmatch(r'[^"?]*\?\s*((?:"(?:[^"\\]|\\.)*"\s*)+):\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', a, re.S)
    return [literal(m.group(1)), literal(m.group(2))] if m else []


def vcc_messages():
    out = []
    call = re.compile(r'\b(' + '|'.join(CALLS) + r')\s*\(')
    for d in DIRS:
        for root, _, files in os.walk(f'{V}/{d}'):
            for f in sorted(files):
                if not f.endswith('.c'):
                    continue
                path = os.path.join(root, f)
                src = open(path, errors='replace').read()
                for m in call.finditer(src):
                    pre = src[max(0, m.start() - 40):m.start()]
                    if re.search(r'(void|bool|int|vdiag)\s+\**$', pre):	# the definition itself
                        continue
                    name = m.group(1)
                    if name in PPONLY and d not in ('frontend/pp', 'diag'):
                        continue
                    args, _ = split_args(src, m.end() - 1)
                    fi, ki = CALLS[name]
                    if len(args) <= fi:
                        continue
                    if re.fullmatch(r'\s*(fmt|msg)\s*', args[fi]):	# a wrapper passing its format on
                        continue
                    fmts = literals(args[fi])
                    kind = ki if isinstance(ki, str) else (literal(args[ki]) or '?')
                    line = src.count('\n', 0, m.start()) + 1
                    for fmt in fmts or [None]:
                        out.append({'where': f'{os.path.relpath(path, V)}:{line}', 'call': name, 'kind': kind,
                                    'fmt': fmt})
    return out


def normalize(fmt):
    """a vcc format as text: conversions -> MARK, clang's suffixes dropped"""
    s = re.sub(r'%%', '\x02', fmt)
    s = re.sub(r'%[-+ #0]*(\*|\d+)?(\.(\*|\d+))?(hh|h|ll|l|z|j|t|L)?[diouxXeEfgGcsp]', MARK, s)
    s = s.replace('\x02', '%')
    s = re.sub(r'\s*\[(-W[^\]]*|' + MARK + r')\]$', '', s)
    return s


def words(s):
    return set(w.lower() for w in re.findall(r'[A-Za-z_]{3,}', s))


def matcher(diags, subst):
    """[name, regex source, compiled regex or None, samples or None, nodes, words] per clang diagnostic"""
    tbl = []
    for r in diags.values():
        if not r['text']:
            continue
        nodes = clangdiag.parse(r['text'], subst)
        txt = r['text'] + ' ' + ' '.join(subst.get(x, '') for x in re.findall(r'%sub\{(\w+)\}', r['text']))
        tbl.append([r['name'], clangdiag.to_regex(nodes), None, None, nodes, words(txt)])
    return tbl


def literal_words(s):
    return len(re.findall(r'[A-Za-z]{3,}', s.replace(MARK, ' ')))


def match(msg, tbl):
    """clang names whose template matches msg (either direction)"""
    w = words(msg.replace(MARK, ' '))
    if not w:
        return []
    need = max(1, (len(w) * 4 + 4) // 5)	# 80 % of vcc's words appear in clang's template
    cand = [t for t in tbl if len(w & t[5]) >= need]
    hits = []
    for t in cand:
        if t[2] is None:
            try:
                t[2] = re.compile(t[1], re.S)
            except re.error:
                t[2] = re.compile(r'(?!)')
        if t[2].fullmatch(msg):
            hits.append(t[0])
    if hits or MARK not in msg or literal_words(msg) < 2:
        return hits
    # vcc formats a part clang spells out ("%s by zero"): vcc's pattern against clang's samples
    vrx = re.compile(re.escape(msg).replace(re.escape(MARK), '.+?'), re.S)
    for t in cand:
        if t[3] is None:
            t[3] = clangdiag.samples(t[4])
        if any(vrx.fullmatch(x) for x in t[3]):
            hits.append(t[0])
    return hits


def partial(msg, tbl, diags):
    """a clang diagnostic saying more than vcc: all of vcc's words (at least 3) in its first alternative"""
    w = words(msg.replace(MARK, ' '))
    if len(w) < 3:
        return None
    c = [t[0] for t in tbl if w <= t[5] and w <= words(diags[t[0]]['first'] + ' ' + diags[t[0]]['text'])]
    if not c:
        return None
    return min(c, key=lambda h: len(words(diags[h]['first']) - w))


def best(msg, kind, hits, diags):
    """the closest of several matches: same class, most shared words, fewest extra words"""
    w = words(msg.replace(MARK, ' '))

    def score(h):
        r = diags[h]
        tw = words(r['first'])
        same = r['class'] == kind or (kind == 'warning' and r['class'] == 'extension')
        return (same, r['bucket'] == 'c', len(w & tw) - 0.5 * len(tw - w), -len(r['text']))
    return max(hits, key=score)


def main():
    a = sys.argv[1:]
    verbose = '-v' in a
    diags, groups, subst = clangdiag.load()
    tbl = matcher(diags, subst)
    msgs = vcc_messages()
    dyn = [m for m in msgs if m['fmt'] is None]
    lit = [m for m in msgs if m['fmt'] is not None]
    covered = {}
    nomatch, partials = [], []
    for m in lit:
        s = normalize(m['fmt'])
        hits = match(s, tbl)
        if len(hits) > 1:
            hits = [best(s, m['kind'], hits, diags)]
        m['clang'] = hits
        for h in hits:
            covered.setdefault(h, []).append(m['where'])
        if not hits:
            p = partial(s, tbl, diags)
            if p:
                m['partial'] = p
                partials.append(m)
            else:
                nomatch.append(m)

    from collections import Counter
    bs = ['c', 'cxx', 'objc', 'omp', 'gpu', 'target', 'tool']
    cl = ['error', 'warning', 'extension', 'note']
    tot = Counter((r['bucket'], r['class']) for r in diags.values())
    cov = Counter((diags[h]['bucket'], diags[h]['class']) for h in covered)
    print(f'clang {clangdiag.clang_version()}: {len(diags)} diagnostics; vcc: {len(msgs)} diagnostic calls, '
          f'{len(set(m["fmt"] for m in lit))} distinct literal formats, {len(dyn)} dynamic')
    print(f'\n{"covered/clang":14}' + ''.join(f'{c:>14}' for c in cl))
    for b in bs:
        print(f'{b:14}' + ''.join(f'{cov[b, c]:>6}/{tot[b, c]:<7}' for c in cl))
    nc = sum(cov[b, c] for b in bs for c in cl)
    print(f'\n{len(covered)} clang diagnostics matched ({nc} in error/warning/extension/note); '
          f'{len(set(normalize(m["fmt"]) for m in nomatch))} vcc formats match nothing')
    print(f'{len(set(normalize(m["fmt"]) for m in partials))} vcc formats say less than a clang diagnostic '
          f'(all their words in its message): a missing type or detail')
    odd = sorted(h for h in covered if diags[h]['bucket'] != 'c')
    if odd:
        print(f'matched outside the C bucket (bucket heuristic or matching to check): {", ".join(odd)}')
    if verbose:
        print('\nvcc formats with no clang match:')
        seen = set()
        for m in sorted(nomatch, key=lambda m: m['where']):
            s = normalize(m['fmt'])
            if s in seen:
                continue
            seen.add(s)
            print(f'  {m["where"]:28} {m["kind"]:8} {s.replace(MARK, "%")}')
        print('\nvcc formats that say less than clang:')
        for m in sorted(partials, key=lambda m: m['where']):
            print(f'  {m["where"]:28} {normalize(m["fmt"]).replace(MARK, "%"):60} | {diags[m["partial"]]["first"][:80]}')
        print('\ndynamic formats:')
        for m in dyn:
            print(f'  {m["where"]:28} {m["call"]}')
    if '--missing' in a:
        i = a.index('--missing')
        pat = a[i + 1] if i + 1 < len(a) and not a[i + 1].startswith('-') else '.'
        miss = [r for r in diags.values() if r['bucket'] == 'c' and r['class'] in ('error', 'warning', 'extension')
                and r['name'] not in covered and (re.search(pat, r['name']) or re.search(pat, r['text']))]
        byg = {}
        for r in miss:
            byg.setdefault(r['group'] or f'({r["class"]}, no group)', []).append(r)
        print(f'\n{len(miss)} C diagnostics vcc lacks, by -W group:')
        for g, rs in sorted(byg.items(), key=lambda kv: -len(kv[1])):
            print(f'  {g} ({len(rs)})')
            if verbose:
                for r in rs:
                    print(f'      {r["name"]:48} {r["text"][:100]}')
    if '--json' in a:
        json.dump({'covered': covered, 'nomatch': [m['where'] for m in nomatch],
                   'messages': msgs}, open(a[a.index('--json') + 1], 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
