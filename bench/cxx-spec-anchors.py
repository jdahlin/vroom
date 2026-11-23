#!/usr/bin/env python3
"""Whether the C++ front end's rules stay anchored to the standard.  <1 s.

The front end is written against docs/cxx-spec: a function implements a
paragraph and cites it ([label]/N), and where it follows clang instead of
the standard it says so with [ext:NAME] or [impl.clang].  Two checks keep
new code that way:

  uncited   a function definition of at least LINES lines (default 40) in
            the analysis directories whose body and leading comment cite no
            [label] at all
  clang     a comment that says what clang does ("clang accepts", "as
            clang", ...) with no [ext:...] or [impl...] label in the same
            comment: a deviation from the standard that is not marked

What is there today is listed in bench/cxx-spec-anchors.baseline, one
"check file function" per line.  --check (CI) fails on a finding not
listed, and on a listed one that is gone (a fix removes its line);
--bless rewrites the list.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'frontend', 'cplusplus')
BASELINE = os.path.join(ROOT, 'bench', 'cxx-spec-anchors.baseline')
# the directories of analysis by the standard's clauses (not the lexer,
# parser, dumps, ABI or lowering, which have their own references)
DIRS = ('basic', 'class', 'declaration', 'exception', 'expression', 'module',
        'overload', 'sema', 'statement', 'template')

# a subclause's label; [ext...] and [impl...] mark choices, not citations
CITATION = re.compile(r'\[(?!ext[.:]|impl[.:])[a-z][a-z0-9_.:-]*\]')
MARKED = re.compile(r'\[(ext[.:]|impl[.:])')
CLANG = re.compile(r"\b(as clang(?!::)|like clang(?!::)|clang(?:'s)? (?:does|accepts|rejects|"
                   r"answers|counts|keeps|treats|reports|gives|has it))\b",
                   re.IGNORECASE)
DEFINITION = re.compile(r'^[A-Za-z_][\w:<>,\s*&]*?\b((?:\w+::)?)(~?\w+)\s*\(')


def sources():
    for d in DIRS:
        top = os.path.join(SRC, d)
        if not os.path.isdir(top):
            continue
        for dirpath, _, names in os.walk(top):
            for n in sorted(names):
                if n.endswith('.cpp'):
                    yield os.path.join(dirpath, n)


def functions(lines):
    """(name, first line of its leading comment, first line, last line) of
    each function defined at the start of a line"""
    i = 0
    while i < len(lines):
        line = lines[i]
        m = DEFINITION.match(line)
        if not m or line.rstrip().endswith(';') or line.startswith(
                ('if', 'for', 'while', 'switch', 'return', 'else')):
            i += 1
            continue
        # the body: from the first line that is "{" to the line that is "}"
        j = i
        while j < len(lines) and lines[j] != '{' and not lines[j].endswith(
                ') {') and not lines[j].rstrip().endswith(';'):
            j += 1
        if j >= len(lines) or lines[j].rstrip().endswith(';'):
            i += 1
            continue
        end = j
        while end < len(lines) and lines[end] != '}':
            end += 1
        start = i
        # a return type on a line of its own
        if start > 0 and re.match(r'^[A-Za-z_][\w:<>,\s*&]*$', lines[start - 1]):
            start -= 1
        while start > 0 and lines[start - 1].strip() and (
                lines[start - 1].lstrip().startswith(('/*', '*', '//')) or
                lines[start - 1].rstrip().endswith('*/') or
                lines[start - 1].startswith('   ')):
            start -= 1
        i_next = end + 1
        # the parser's functions are cited by the grammar check
        # (bench/cxx-spec-refs.py), not here
        if m.group(1) == 'parser::':
            i = i_next
            continue
        yield m.group(2), start, i, end
        i = i_next


def comments(text):
    """(line, text) of each /* */ comment"""
    for m in re.finditer(r'/\*.*?\*/', text, re.S):
        yield text.count('\n', 0, m.start()) + 1, m.group(0)


def findings(min_lines):
    out = []
    for path in sources():
        rel = os.path.relpath(path, SRC)
        with open(path, encoding='utf-8') as f:
            text = f.read()
        lines = text.split('\n')
        owner = {}
        for name, start, first, end in functions(lines):
            for k in range(start, end + 1):
                owner[k + 1] = name
            if end - first + 1 < min_lines:
                continue
            if not CITATION.search('\n'.join(lines[start:end + 1])):
                out.append(('uncited', rel, name, first + 1))
        for line, c in comments(text):
            if CLANG.search(c) and not MARKED.search(c):
                out.append(('clang', rel, owner.get(line, '-'), line))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--bless', action='store_true')
    ap.add_argument('--lines', type=int, default=40)
    args = ap.parse_args()
    found = findings(args.lines)
    keys = sorted({f'{k} {f} {n}' for k, f, n, _ in found})
    if args.bless:
        with open(BASELINE, 'w') as f:
            f.write('# bench/cxx-spec-anchors.py --bless: what is there today\n')
            for k in keys:
                f.write(k + '\n')
        print(f'cxx-spec-anchors: {len(keys)} findings written')
        return 0
    listed = set()
    if os.path.exists(BASELINE):
        with open(BASELINE) as f:
            listed = {l.strip() for l in f if l.strip() and not l.startswith('#')}
    new = [f for f in found if f'{f[0]} {f[1]} {f[2]}' not in listed]
    stale = sorted(listed - set(keys))
    for k, f, n, line in new:
        what = {'uncited': f'function {n} of {args.lines}+ lines cites no [label]',
                'clang': 'a comment says what clang does without [ext:...] or [impl...]'}[k]
        print(f'src/frontend/cplusplus/{f}:{line}: {what}')
    for s in stale:
        print(f'stale: {s} (fixed: remove its line, or --bless)')
    counts = {}
    for k, *_ in found:
        counts[k] = counts.get(k, 0) + 1
    print(f'cxx-spec-anchors: uncited {counts.get("uncited", 0)}, '
          f'clang {counts.get("clang", 0)}; {len(new)} new, {len(stale)} stale')
    if args.check and (new or stale):
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
