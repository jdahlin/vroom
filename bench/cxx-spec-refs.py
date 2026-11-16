#!/usr/bin/env python3
"""The C++ front end's citations and grammar against docs/cxx-spec
(PLAN-cxx-spec.md L2, PLAN-cxx-parse.md V1).  <1 s.

  refs      every [label]/N (and [label]/N.M) in src/frontend/cplusplus names
            a subclause of docs/cxx-spec and a paragraph N it has
  grammar   every production quoted in a comment,

                *     nonterminal:
                *         alternative
                *         ...

            is the standard's: each alternative quoted is one of the
            alternatives of that nonterminal's bnf block (spaces, the
            subscript opt written _opt, and quotes around terminals do not
            count).  Quoting some alternatives is allowed (a comment may
            say which are not written yet).
  coverage  per clause, the nonterminals of docs/cxx-spec/grammar-index.md
            that some comment quotes, of all of the clause's

Usage: bench/cxx-spec-refs.py [-v] [--missing CLAUSE] [FILE...]
  -v                 every quoted production, not only the wrong ones
  --missing CLAUSE   list the clause's nonterminals no comment quotes yet
Exit status 1 if a citation or a quoted production is wrong.
"""
import argparse
import glob
import os
import re
import sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(R, 'docs', 'cxx-spec')
FRONTEND = os.path.join(R, 'src', 'frontend', 'cplusplus')

CITATION = re.compile(r'\[([a-z][a-z0-9.]*[a-z0-9])\]/(\d+)(?:\.(\d+))?')
# clang's and other vendors' tags, and the plan's own labels, are no
# subclauses of the standard
NOT_SUBCLAUSES = {'ext.gnu', 'ext.clang', 'ext.microsoft', 'ext.vcc', 'impl.clang', 'impl.gnu',
                  'impl.microsoft', 'impl.vcc'}


def subclause_files():
    files = {}
    for path in glob.glob(os.path.join(SPEC, '**', '*.md'), recursive=True):
        label = os.path.basename(path)[:-3]
        files[label] = path
    return files


def paragraphs(path, cache={}):
    if path not in cache:
        cache[path] = {int(n) for n in re.findall(r'^\*\*(\d+)\*\*', open(path, encoding='utf-8').read(), re.M)}
    return cache[path]


ONE_OF_TEXT = {}


def normalize(alternative):
    # quotes mark terminals, one each ('#' 'include') or a run of them
    # ('# include'): the same terminals either way; a quoted terminal's
    # subscript opt stays on it ('template'ₒₚₜ is template_opt)
    a = alternative.replace("'ₒₚₜ", "ₒₚₜ'").replace("'_opt", "_opt'").replace("'", ' ')
    a = a.replace('ₒₚₜ', '_opt')
    return ' '.join(a.split())


def spec_grammar():
    """nonterminal -> (subclause, set of normalized alternatives, one_of)"""
    grammar = {}
    for path in glob.glob(os.path.join(SPEC, '**', '*.md'), recursive=True):
        rel = os.path.relpath(path, SPEC)
        # Annex A ([gram]) repeats every production; the library's grammars
        # (regular expressions, formats) are no part of the front end
        if rel.startswith(('gram' + os.sep, 'lib' + os.sep)):
            continue
        label = os.path.basename(path)[:-3]
        text = open(path, encoding='utf-8').read()
        for block in re.findall(r'^```bnf\n(.*?)^```', text, re.M | re.S):
            name, alternatives, one_of = None, [], False
            for line in block.split('\n') + ['']:
                head = re.match(r'^([A-Za-z][\w-]*):\s*(one of)?\s*$', line)
                # [expr.yield]'s block indents its head: one that opens a
                # production (first line, or after a blank one) is a head too
                if not head and name is None:
                    head = re.match(r'^\s+([A-Za-z][\w-]*):\s*(one of)?\s*$', line)
                if head:
                    if name:
                        grammar.setdefault(name, (label, set(), one_of))[1].update(alternatives)
                    name, alternatives, one_of = head.group(1), [], bool(head.group(2))
                elif line.startswith(' ') and line.strip() and name:
                    if one_of:
                        alternatives.extend(normalize(line).split())
                        ONE_OF_TEXT[name] = ONE_OF_TEXT.get(name, '') + ''.join(normalize(line).split())
                    else:
                        alternatives.append(normalize(line))
                elif not line.strip() and name:
                    grammar.setdefault(name, (label, set(), one_of))[1].update(alternatives)
                    name, alternatives = None, []
    return grammar


def comment_lines(path):
    """the lines inside /* */ comments, without the leading ' * ', with
    their line numbers"""
    out = []
    inside = False
    for n, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        s = line.rstrip('\n')
        if not inside and '/*' in s:
            inside = True
            s = s[s.index('/*') + 2:]
        if inside:
            end = '*/' in s
            if end:
                s = s[:s.index('*/')]
            s = re.sub(r'^\s*\* ?', '', s)
            out.append((n, s))
            if end:
                inside = False
    return out


def quoted_productions(path):
    """(line, nonterminal, [alternatives]) for each production a comment
    quotes: a line of four spaces, a nonterminal and a colon, then lines
    indented further"""
    lines = comment_lines(path)
    found = []
    i = 0
    while i < len(lines):
        n, s = lines[i]
        m = re.match(r'^ {4}([a-z][a-z0-9-]*):\s*(one of)?\s*$', s)
        if not m:
            i += 1
            continue
        alternatives = []
        j = i + 1
        while j < len(lines) and re.match(r'^ {8}\S', lines[j][1]) and lines[j][0] == lines[j - 1][0] + 1:
            if m.group(2):
                alternatives.extend(normalize(lines[j][1]).split())
            else:
                alternatives.append(lines[j][1].strip())
            j += 1
        if alternatives:
            found.append((n, m.group(1), alternatives))
        i = max(j, i + 1)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--missing')
    ap.add_argument('files', nargs='*')
    a = ap.parse_args()
    files = a.files or sorted(glob.glob(os.path.join(FRONTEND, '**', '*.cpp'), recursive=True) +
                              glob.glob(os.path.join(FRONTEND, '**', '*.h'), recursive=True) +
                              glob.glob(os.path.join(FRONTEND, '**', '*.inc'), recursive=True))
    subclauses = subclause_files()
    grammar = spec_grammar()
    errors = 0
    citations = productions = 0
    quoted = {}
    for path in files:
        rel = os.path.relpath(path, R)
        text = open(path, encoding='utf-8', errors='replace').read()
        for m in CITATION.finditer(text):
            label, para = m.group(1), int(m.group(2))
            if label in NOT_SUBCLAUSES:
                continue
            citations += 1
            line = text.count('\n', 0, m.start()) + 1
            if label not in subclauses:
                print(f'{rel}:{line}: [{label}] is no subclause of docs/cxx-spec')
                errors += 1
            elif para not in paragraphs(subclauses[label]):
                print(f'{rel}:{line}: [{label}] has no paragraph {para}')
                errors += 1
        for line, name, alternatives in quoted_productions(path):
            productions += 1
            if name not in grammar:
                # a nonterminal a later paper added, named near the quote
                near = '\n'.join(t for n, t in comment_lines(path) if line - 15 <= n <= line + 15)
                if re.search(r'\bP\d{4}R\d+', near):
                    continue
                print(f'{rel}:{line}: {name} is no nonterminal of n4861, and no paper is named near it')
                errors += 1
                continue
            label, spec_alternatives, one_of = grammar[name]
            quoted.setdefault(name, []).append(f'{rel}:{line}')
            # "..." stands for alternatives the comment leaves out
            wrong = [x for x in alternatives if x != '...' and normalize(x) not in spec_alternatives]
            if one_of and wrong:
                # docs/cxx-spec lost some spaces between the terminals of a
                # one-of list (`%>;:`): the same characters in the same order
                if ''.join(alternatives) == ONE_OF_TEXT.get(name):
                    wrong = []
            for x in wrong:
                print(f'{rel}:{line}: {name}: "{x}" is none of the alternatives of [{label}]')
                errors += 1
            if a.v and not wrong:
                print(f'{rel}:{line}: {name} [{label}]: {len(alternatives)} of {len(spec_alternatives)} alternatives')
    # coverage per clause
    by_clause = {}
    for name, (label, _, _) in grammar.items():
        clause = os.path.basename(os.path.dirname(os_path(subclauses, label)))
        by_clause.setdefault(clause, set()).add(name)
    print(f'{citations} citations, {productions} quoted productions, {errors} wrong')
    row = []
    for clause in sorted(by_clause):
        have = sum(1 for n in by_clause[clause] if n in quoted)
        row.append(f'{clause} {have}/{len(by_clause[clause])}')
    print('nonterminals quoted, by clause: ' + ', '.join(row))
    if a.missing:
        for n in sorted(by_clause.get(a.missing, ())):
            if n not in quoted:
                print(f'  {n} [{grammar[n][0]}]')
    return 1 if errors else 0


def os_path(subclauses, label):
    return subclauses.get(label, os.path.join(SPEC, '?', label + '.md'))


if __name__ == '__main__':
    sys.exit(main())
