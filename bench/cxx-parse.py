#!/usr/bin/env python3
"""The C++ front end's parser (src/frontend/cplusplus, through cxxparse)
against clang, on clang's parser tests (doc/PLAN-cxx-parse.md, V4-V6).

Tests: clang/test/Parser/*.cpp and clang/test/CXX/**/*.cpp of the
llvm-project checkout bench/fetch-testsuites.sh llvm makes (work/testsuites),
and src/test/cxx/parse/{spec,gram}/*.cpp.  For each %clang_cc1 RUN line (the
option sets bench/cxx-pp.py reads), per run:

  tokens   cxxparse -dump-tokens is cxxlex -dump-tokens byte for byte: the
           parser's token stream adds and loses nothing (V6)
  crash    cxxparse -fsyntax-only ends with status 0 or 1, within the time
           limit, never by a signal (V5; run it with --bin-dir build/ccs-asan)
  decls    where each declaration of the translation unit starts and ends:
           cxxparse -dump-declarations against the top-level declarations of
           clang -cc1 -ast-dump=json, for runs where clang reports no error of
           its Parse or Lexical categories.  Every one of clang's
           declarations lies inside one of ours, and every one of ours starts
           where one of clang's does (or is a lone ';').  Only declarations
           that start in the test file and not in a #define line are
           compared (clang -ast-dump.  -ast-dump=json would give expansion
           locations, but holding six of them for a header-heavy test is
           more memory than the machine has).
  syntax   (from P1) declarations without an (unparsed) node: accepted or
           rejected as clang does (V4a); counted, not compared, in P0

  --mutate N   also N token mutants (a token deleted, duplicated, or a
               bracket inserted) of each test, through cxxparse only: the
               crash check alone (V5)

Usage: bench/cxx-parse.py [-v] [-j N] [--bin-dir DIR] [--mutate N] [PATTERN...]
Prints one line per failing run (all with -v) and a summary.
"""
import argparse
import concurrent.futures
import glob
import importlib.util
import os
import random
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('cxxpp', os.path.join(HERE, 'cxx-pp.py'))
cxxpp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cxxpp)

R = cxxpp.R
OURS = os.path.join(R, 'src', 'test', 'cxx', 'parse')
TRIPLE = cxxpp.TRIPLE
TIMEOUT = 20


LLVM_TEST = os.path.join(cxxpp.work_dir(), 'testsuites', 'llvm', 'clang', 'test')


def known_differences():
    """src/test/cxx/parse/known-differences.txt: PATH:LINE reason, a line
    where our declaration and clang's AST are known to differ"""
    known = {}
    path = os.path.join(OURS, 'known-differences.txt')
    if not os.path.exists(path):
        return known
    for line in open(path):
        m = re.match(r'^(\S+):(\d+)\s', line)
        if m:
            known.setdefault(m.group(1), set()).add(int(m.group(2)))
    return known


KNOWN = known_differences()


def find_tool(bin_dir, name):
    for d in ([bin_dir] if bin_dir else []) + [os.path.join(R, 'build', 'linux', 'src'), os.path.join(R, 'build', 'src')]:
        p = os.path.join(d, name)
        if os.path.exists(p):
            # each run's directory is its test's: the path must not be relative
            return os.path.abspath(p)
    sys.exit(f'cxx-parse: no {name}; make -C src {name}')


def tests():
    llvm = LLVM_TEST
    found = sorted(glob.glob(os.path.join(llvm, 'Parser', '*.cpp')) +
                   [f for f in glob.glob(os.path.join(llvm, 'CXX', '**', '*.cpp'), recursive=True)
                    if os.sep + 'cpp' + os.sep not in f and os.sep + 'lex' + os.sep not in f])
    found += sorted(glob.glob(os.path.join(OURS, 'spec', '**', '*.cpp'), recursive=True) +
                    glob.glob(os.path.join(OURS, 'gram', '**', '*.cpp'), recursive=True))
    return found


def run(cmd, cwd, timeout=TIMEOUT):
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout)
        return r.returncode, r.stdout.decode('utf-8', 'replace'), r.stderr.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        return 'timeout', '', ''


LOCATION = re.compile(r'<invalid sloc>|\bline:(\d+):(\d+)|\bcol:(\d+)|((?:<[^<>]+>|[^\s<>,()\'"]+)):(\d+):(\d+)')
TOP = re.compile(r'^[|`]-(\w+) 0x[0-9a-f]+ (?:(?:prev|parent) 0x[0-9a-f]+ )*<([^>]*(?:<[^>]*>[^>]*)*)>')


def clang_declarations(cmd, cwd, main):
    """the top-level declarations of clang's -ast-dump, read as it is
    written (a dump with a header in it is large): (begin, end, kind) as
    (line, column) of the first and the last token, for those that start
    and end in the main file and not inside a macro definition, whose
    tokens the dump shows where they are spelled.  The dump leaves out a
    location's file and line when they are the previous location's, so
    every location of every line is followed.  None if clang fails."""
    defines = set()
    continued = False
    for i, row in enumerate(open(main, 'rb').read().split(b'\n')):
        if continued or re.match(rb'\s*#\s*define\b', row):
            defines.add(i + 1)
            continued = row.rstrip(b'\r').endswith(b'\\')
    state = {'file': None, 'line': 0}

    def follow(text):
        out = []
        for m in LOCATION.finditer(text):
            if m.group(0) == '<invalid sloc>':
                out.append(None)
            elif m.group(1):
                state['line'] = int(m.group(1))
                out.append((state['file'], state['line'], int(m.group(2))))
            elif m.group(3):
                out.append((state['file'], state['line'], int(m.group(3))))
            else:
                state['file'], state['line'] = m.group(4), int(m.group(5))
                out.append((state['file'], state['line'], int(m.group(6))))
        return out

    try:
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    main_name = os.path.basename(main)
    found = []
    for raw in proc.stdout:
        line = raw.decode('utf-8', 'replace')
        top = TOP.match(line)
        if not top:
            follow(line)
            continue
        kind = top.group(1)
        rng = follow('<' + top.group(2) + '>')
        follow(line[top.end():])
        if ' implicit ' in line or not rng or not rng[0]:
            continue
        begin, end = rng[0], rng[-1]
        if len(rng) < 2 or not end:
            # a range that ends in <built-in> or <scratch space>
            end = (None, 0, 0)
        if os.path.basename(begin[0] or '') != main_name or begin[1] in defines:
            continue
        # an end inside a macro's replacement (`using T = __SIZE_TYPE__;`)
        # is where the macro is defined: only the beginning is known
        if os.path.basename(end[0] or '') != main_name or end[1] in defines or end < begin:
            end = None
        else:
            end = (end[1], end[2])
        found.append(((begin[1], begin[2]), end, kind))
    try:
        proc.wait(timeout=60)
    except subprocess.TimeoutExpired:
        proc.kill()
        return None
    if proc.returncode not in (0, 1):
        return None
    return found


DECL_LINE = re.compile(r'^(\S+) (.*?):(\d+):(\d+) (.*?):(\d+):(\d+)((?: erroneous| fallback)*)(?: opaque=(\S+))?$')


def our_declarations(text, main):
    out = []
    for line in text.splitlines():
        m = DECL_LINE.match(line)
        if not m:
            continue
        if os.path.normpath(os.path.join(os.path.dirname(main), m.group(2))) != os.path.normpath(main):
            continue
        opaque = []
        for r in (m.group(9) or '').split(','):
            if r:
                a, b = r.split('-')
                opaque.append((tuple(map(int, a.split(':'))), tuple(map(int, b.split(':')))))
        out.append((m.group(1), (int(m.group(3)), int(m.group(4))), (int(m.group(6)), int(m.group(7))),
                    set(m.group(8).split()), opaque))
    return out


CLANG_SYNTAX_ERROR = re.compile(r'^[^:\n]+:\d+:\d+: (?:error|fatal error): .*\[(?:Parse|Lexical or Preprocessor) Issue\]',
                                re.M)


MODULE_LINE = re.compile(rb'\s*(?:export\s+)?(?:module|import)\b')


def compare_declarations(ours, theirs, main, error_lines, macros):
    """'' if the boundaries agree, else the first disagreement.  A
    declaration of ours that is none of clang's is no disagreement when
    clang reported an error on one of its lines (clang leaves an invalid
    declaration out of its AST), or when it is a module-declaration or a
    module-import-declaration, which have no node in clang's dump, or when
    it invokes one of the macros."""
    lines = open(main, 'rb').read().split(b'\n')

    def lone_semicolon(begin, end):
        if begin != end:
            return False
        row = lines[begin[0] - 1] if begin[0] - 1 < len(lines) else b''
        return row[begin[1] - 1:begin[1]] == b';'

    ours = [o[:3] for o in ours]  # kind, begin, end
    for b, e, kind in theirs:
        inside = [o for o in ours if o[1] <= b <= o[2]]
        if not inside:
            return f'clang {kind} at {b[0]}:{b[1]} starts in none of ours'
        o = inside[0]
        if e and not (o[1] <= e <= o[2]):
            return (f'clang {kind} {b[0]}:{b[1]}-{e[0]}:{e[1]} ends outside ours '
                    f'{o[1][0]}:{o[1][1]}-{o[2][0]}:{o[2][1]}')
    begins = {b for b, e, kind in theirs}
    for kind, b, e in ours:
        if b in begins or lone_semicolon(b, e):
            continue
        # clang's range of a declaration starts after a leading attribute or
        # `export` in some cases: a declaration of clang's inside ours that
        # starts later on the same tokens counts
        if any(b < cb <= e for cb in begins):
            continue
        if any(b[0] <= line <= e[0] for line in error_lines):
            continue
        if b[0] - 1 < len(lines) and MODULE_LINE.match(lines[b[0] - 1][b[1] - 1:]):
            continue
        words = set(re.findall(rb'[A-Za-z_]\w*', b'\n'.join(lines[b[0] - 1:e[0]])))
        if any(m.encode() in words for m in macros):
            continue
        return f'ours at {b[0]}:{b[1]}-{e[0]}:{e[1]} is none of clang\'s'
    return ''


def check(test, opts, clang, cxxlex, cxxparse):
    cwd = os.path.dirname(test)
    name = os.path.basename(test)
    lang = ['-x', 'c++']
    res = {'test': test, 'opts': opts, 'fail': []}
    # tokens: the stream through the parser is the lexer's
    lt = run([cxxlex, '-dump-tokens', '-phase4-diagnostics-only'] + opts + [name], cwd)
    pt = run([cxxparse, '-dump-tokens', '-phase4-diagnostics-only'] + opts + [name], cwd)
    if pt[0] == 2 and 'unsupported -std' in pt[2]:
        res['skip'] = pt[2].strip()
        return res
    if pt[0] not in (0, 1):
        res['fail'].append(('crash', f'-dump-tokens: status {pt[0]}'))
    elif lt[1] != pt[1]:
        a, b = lt[1].splitlines(), pt[1].splitlines()
        i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
        res['fail'].append(('tokens', f'line {i + 1}: cxxlex {a[i] if i < len(a) else "<end>"!r} '
                                      f'cxxparse {b[i] if i < len(b) else "<end>"!r}'))
    # crash, and our declarations
    ours = run([cxxparse, '-dump-declarations', '-stats'] + opts + [name], cwd)
    if ours[0] not in (0, 1):
        res['fail'].append(('crash', f'status {ours[0]}: {ours[2].strip().splitlines()[-1:]}'))
        return res
    stats = dict(re.findall(r'^([a-z-]+) (\d+)$', ours[1], re.M))
    res['unparsed'] = int(stats.get('unparsed', 0))
    res['erroneous'] = int(stats.get('erroneous', 0))
    # clang's declarations, when it found the syntax well-formed
    c = run([clang, '-cc1'] + TRIPLE + lang + opts +
            ['-fsyntax-only', '-fdiagnostics-show-category', 'name', '-ferror-limit', '0', name], cwd, timeout=60)
    if c[0] == 'timeout':
        res['skip'] = 'clang timed out'
        return res
    # syntax: per declaration of ours that is neither a fallback nor
    # (fully) a later step's, accepted or rejected as clang does: clang
    # rejects it when one of its Parse or Lexical errors is on its lines
    syntax_errors = {(int(m.group(1)), int(m.group(2))) for m in
                     re.finditer(r'^' + re.escape(name) + r':(\d+):(\d+): (?:fatal )?error: .*\[(?:Parse|Lexical or Preprocessor) Issue\]',
                                 c[2], re.M)}
    decls = our_declarations(ours[1], test)
    res['syntax'] = {'agree': 0, 'we_reject': [], 'we_accept': [], 'not_comparable': 0}
    # clang's split of the standard's rules into Parse and Sema is not the
    # standard's: a declaration we reject is agreed on when clang reports
    # any error on its lines (i = 4; at namespace scope is no declaration;
    # clang parses it as one, then rejects it in Sema)
    any_errors = {(int(m.group(1)), int(m.group(2))) for m in
                  re.finditer(r'^' + re.escape(name) + r':(\d+):(\d+): (?:fatal )?error:', c[2], re.M)}
    # an error belongs to the declaration whose tokens it is among: from
    # where the declaration starts to where the next one starts (clang
    # reports a missing ';' after the declaration's last token)
    starts = sorted(d[1] for d in decls)
    for kind, b, e, flags, opaque in decls:
        later = [x for x in starts if x > b]
        until = later[0] if later else (1 << 30, 0)

        def within(p):
            return b <= p < until
        if 'fallback' in flags:
            res['syntax']['not_comparable'] += 1
            continue
        we_reject = 'erroneous' in flags
        if we_reject:
            clang_rejects = any(within(p) for p in any_errors)
        else:
            mine = [p for p in syntax_errors if within(p)]
            # an error in a part kept as tokens (a body, an initializer) is
            # about grammar a later step parses
            inside = [p for p in mine if any(lo <= p <= hi for lo, hi in opaque)]
            if mine and len(inside) == len(mine):
                res['syntax']['not_comparable'] += 1
                continue
            clang_rejects = bool(mine)
        if clang_rejects == we_reject:
            res['syntax']['agree'] += 1
        elif we_reject:
            res['syntax']['we_reject'].append(b)
        else:
            res['syntax']['we_accept'].append(b)
    for b in res['syntax']['we_reject']:
        res['fail'].append(('syntax', f'we reject the declaration at {b[0]}:{b[1]}, clang accepts it'))
    for b in res['syntax']['we_accept']:
        res['fail'].append(('syntax', f'we accept the declaration at {b[0]}:{b[1]}, clang rejects it'))
    if CLANG_SYNTAX_ERROR.search(c[2]):
        res['clang_syntax_error'] = True
        return res
    theirs = clang_declarations([clang, '-cc1'] + TRIPLE + lang + opts + ['-w', '-ast-dump', name], cwd, test)
    if theirs is None:
        res['skip'] = 'clang gave no AST'
        return res
    # a diagnostic on a line: clang found something wrong there, and may
    # have left the declaration out of its AST
    error_lines = {int(m.group(1)) for m in
                   re.finditer(r'^' + re.escape(name) + r':(\d+):\d+: (?:fatal error|error|warning|note):', c[2],
                               re.M)}
    # a declaration written as a macro invocation is where clang's dump
    # cannot say (the dump shows where the macro's tokens are spelled)
    macros = {m.group(1) for m in re.finditer(rb'^\s*#\s*define\s+(\w+)', open(test, 'rb').read(), re.M)}
    macros = {m.decode() for m in macros} | {o[2:].split('=')[0] for o in opts if o.startswith('-D')}
    known = KNOWN.get(os.path.relpath(test, LLVM_TEST), set())
    error_lines |= known
    why = compare_declarations(our_declarations(ours[1], test), theirs, test, error_lines, macros)
    res['compared'] = True
    if why:
        res['fail'].append(('decls', why))
    return res


def mutants(test, n, seed):
    """n token mutants of test's text: a token deleted, duplicated, or a
    bracket inserted at a token boundary"""
    text = open(test, encoding='utf-8', errors='replace').read()
    spans = [m.span() for m in re.finditer(r'[A-Za-z_]\w*|\d[\w.]*|"(?:[^"\\\n]|\\.)*"|\S', text)]
    rnd = random.Random(f'{seed}:{os.path.basename(test)}')
    for k in range(n):
        if not spans:
            return
        a, b = spans[rnd.randrange(len(spans))]
        op = rnd.randrange(3)
        if op == 0:
            yield k, text[:a] + text[b:]
        elif op == 1:
            yield k, text[:b] + ' ' + text[a:b] + text[b:]
        else:
            yield k, text[:a] + rnd.choice('([{)]}<>;') + text[a:]


def check_mutants(test, opts, cxxparse, n):
    fails = []
    with tempfile.TemporaryDirectory() as td:
        for k, text in mutants(test, n, 0):
            path = os.path.join(td, f'm{k}.cpp')
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text)
            st = run([cxxparse, '-fsyntax-only'] + opts + [path], os.path.dirname(test), timeout=10)[0]
            if st not in (0, 1):
                keep = os.path.join(R, 'build', 'cxx-parse-mutants')
                os.makedirs(keep, exist_ok=True)
                kept = os.path.join(keep, f'{os.path.basename(test)}.{k}.cpp')
                with open(kept, 'w', encoding='utf-8') as f:
                    f.write(text)
                fails.append(('crash', f'mutant {k} ({kept}): status {st}'))
    return {'test': test, 'opts': opts, 'fail': fails}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir')
    ap.add_argument('--mutate', type=int, default=0)
    ap.add_argument('patterns', nargs='*')
    a = ap.parse_args()
    clang = cxxpp.find_clang()
    cxxlex = find_tool(a.bin_dir, 'cxxlex')
    cxxparse = find_tool(a.bin_dir, 'cxxparse')
    jobs = []
    unrunnable = 0
    for t in tests():
        if a.patterns and not any(p in t for p in a.patterns):
            continue
        sets = cxxpp.run_lines(t)
        if sets is None:
            unrunnable += 1
            continue
        if not sets and (OURS in t):
            sets = [['-std=c++20']]
        for opts in sets:
            jobs.append((t, opts))
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        results = list(ex.map(lambda j: check(j[0], j[1], clang, cxxlex, cxxparse), jobs))
        if a.mutate:
            results += list(ex.map(lambda j: check_mutants(j[0], j[1], cxxparse, a.mutate),
                                   [j for i, j in enumerate(jobs) if j[0] != jobs[i - 1][0] or i == 0]))
    counts = {'tokens': 0, 'crash': 0, 'decls': 0, 'syntax': 0}
    syntax = {'agree': 0, 'we_reject': 0, 'we_accept': 0, 'not_comparable': 0}
    compared = agree = syntax_errors = 0
    unparsed = erroneous = skipped = 0
    tests_seen = set()
    for r in results:
        tests_seen.add(r['test'])
        kinds = {k for k, _ in r['fail']}
        for k in kinds:
            counts[k] += 1
        if r.get('compared'):
            compared += 1
            if 'decls' not in kinds:
                agree += 1
        if r.get('skip'):
            skipped += 1
        if r.get('clang_syntax_error'):
            syntax_errors += 1
        for k in syntax:
            v = r.get('syntax', {}).get(k, 0)
            syntax[k] += v if isinstance(v, int) else len(v)
        unparsed += r.get('unparsed', 0)
        erroneous += r.get('erroneous', 0)
        if r['fail'] or a.v:
            rel = os.path.relpath(r['test'], os.path.join(cxxpp.work_dir(), 'testsuites', 'llvm', 'clang', 'test'))
            if rel.startswith('..'):
                rel = os.path.relpath(r['test'], R)
            for k, why in r['fail'] or [('ok', '')]:
                print(f'{k:7} {rel} {" ".join(r["opts"])}: {why}')
    runs = len(jobs)
    print(f'{runs} runs of {len(tests_seen)} tests ({unrunnable} make their inputs at run time, not run): '
          f'tokens {runs - counts["tokens"]}/{runs}, no crash {len(results) - counts["crash"]}/{len(results)}, '
          f'declarations {agree}/{compared} where clang finds no syntax error ({syntax_errors} runs where it does, '
          f'{skipped} skipped); '
          f'nodes: unparsed {unparsed}, erroneous {erroneous}')
    print(f'syntax: {syntax["agree"]} declarations accepted or rejected as clang does, '
          f'{syntax["we_reject"]} we reject and clang accepts, {syntax["we_accept"]} we accept and clang rejects, '
          f'{syntax["not_comparable"]} kept as tokens (a later step\'s grammar)')
    return 1 if any(counts.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
