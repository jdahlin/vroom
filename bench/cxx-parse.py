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
  claims   (from P3) cxxparse -expression-claims gives each expression and
           the same tokens with the operands of every operator in
           parentheses, as the tree groups them.  After the expression's
           declaration, clang compiles
               static_assert(__is_same(decltype((E)), decltype((P))));
               static_assert((E) == (P) || (E) != (E));     (a NaN)
           so it checks our reading of precedence, of the casts and of
           sizeof without anyone comparing trees (V4b).  A claim is verified
           by value when both hold, by type when E is no constant clang can
           compare (or no number: two array temporaries are equal in no
           reading), refuted when one fails or P does not compile where E
           does, and not checkable when E itself does not compile there
           (a parameter's name, an erroneous test).  C++11 and later runs.
           cxxparse -declaration-claims gives each declared name and the type
           its decl-specifier-seq and declarator give it ([dcl.meaning]),
           built of alias templates that say how: pointer_to<array_of<
           function<int(char)>, (3)>>.  clang compiles
               static_assert(__is_same(decltype(NAME), CLAIM));
           (NAME itself for a typedef-name), so it checks how *, &, [] and ()
           nest, the cv-qualifiers and the combinations of type specifiers.
           Verified, refuted, or not checkable: clang rejects the
           declaration, or the name has no decltype there (an overloaded
           function is verified when one of its overloads has the type).

  --mutate N   also N token mutants (a token deleted, duplicated, or a
               bracket inserted) of each test, through cxxparse only: the
               crash check alone (V5)

clang's side is cached in work/cxx-parse-ref (VCC_PARSE_NOCACHE=1 asks it
again), so a run after the first costs about cxxparse's time.
VCC_PARSE_UNCHECKED=1 lists the declared names clang has no type for.

Usage: bench/cxx-parse.py [-v] [-j N] [--bin-dir DIR] [--mutate N] [PATTERN...]
Prints one line per failing run (all with -v) and a summary.
"""
import argparse
import concurrent.futures
import glob
import hashlib
import json
import importlib.util
import os
import random
import re
import subprocess
import sys
import tempfile
import threading

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


CACHE = os.path.join(cxxpp.work_dir(), 'cxx-parse-ref')
CLANG_VERSION = {}


def cached(what, clang, test, opts, compute):
    """clang's answer for a test file and option set, which never changes
    for the same clang and file: kept in work/cxx-parse-ref/, keyed by
    clang's version, what was asked, the options and the file's bytes (not
    those of files it includes: clang's test inputs do not change under
    it).  VCC_PARSE_NOCACHE=1 asks clang again."""
    if clang not in CLANG_VERSION:
        CLANG_VERSION[clang] = subprocess.run([clang, '--version'], capture_output=True,
                                              text=True).stdout.split('\n')[0]
    h = hashlib.sha256()
    for part in (CLANG_VERSION[clang], what, '\0'.join(opts)):
        h.update(part.encode() + b'\1')
    h.update(open(test, 'rb').read())
    path = os.path.join(CACHE, h.hexdigest()[:2], h.hexdigest()[2:] + '.json')
    if not os.environ.get('VCC_PARSE_NOCACHE') and os.path.exists(path):
        try:
            return json.load(open(path))
        except ValueError:
            pass
    value = compute()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f'{path}.{os.getpid()}.{threading.get_ident()}'
    with open(tmp, 'w') as f:
        json.dump(value, f)
    os.replace(tmp, path)
    return value


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
    c = cached('diagnostics', clang, test, opts, lambda: list(
        run([clang, '-cc1'] + TRIPLE + lang + opts +
            ['-fsyntax-only', '-fdiagnostics-show-category', 'name', '-ferror-limit', '0', name], cwd, timeout=60)))
    if c[0] == 'timeout':
        res['skip'] = 'clang timed out'
        return res
    # syntax: per declaration of ours that is neither a fallback nor
    # (fully) a later step's, accepted or rejected as clang does: clang
    # rejects it when one of its Parse or Lexical errors is on its lines
    # only Parse: phases 1-4's errors are cxx-lex.py's and cxx-pp.py's
    syntax_errors = {(int(m.group(1)), int(m.group(2))) for m in
                     re.finditer(r'^' + re.escape(name) + r':(\d+):(\d+): (?:fatal )?error: .*\[Parse Issue\]',
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
    lines = open(test, 'rb').read().decode('utf-8', 'replace').split('\n')

    def recovery(b, e):
        """an erroneous declaration that is only the closing brackets and ;
        an earlier erroneous one left (clang's recovery skips them)"""
        text = '\n'.join(lines[b[0] - 1:e[0]])
        text = text.split('\n')
        text[-1] = text[-1][:e[1]]
        text[0] = text[0][b[1] - 1:]
        body = re.sub(r'//.*', '', '\n'.join(text))
        return re.fullmatch(r'[\s)\]};]*', body) is not None

    # [cpp.pragma]/1: a pragma's effect is implementation-defined, and the
    # preprocessor consumes it: clang's errors about one (on its line, or
    # on the next line for "expected a loop after the pragma") are not
    # about the grammar
    pragma_lines = {i + 1 for i, row in enumerate(lines) if re.match(r'\s*(?:/\*.*?\*/\s*)*#\s*pragma\b', row) or '_Pragma' in row}

    def about_pragma(p):
        return p[0] in pragma_lines or p[0] - 1 in pragma_lines

    known_lines = KNOWN.get(os.path.relpath(test, LLVM_TEST), set())
    for kind, b, e, flags, opaque in decls:
        if any(b[0] <= line <= e[0] for line in known_lines):
            res['syntax']['not_comparable'] += 1
            continue
        if 'erroneous' in flags and recovery(b, e):
            res['syntax']['not_comparable'] += 1
            continue
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
            if mine and all(about_pragma(p) for p in mine):
                res['syntax']['not_comparable'] += 1
                continue
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
    check_claims(test, opts, clang, cxxparse, res, {line for line, column in any_errors})
    if CLANG_SYNTAX_ERROR.search(c[2]):
        res['clang_syntax_error'] = True
        return res
    theirs = cached('declarations', clang, test, opts, lambda: clang_declarations(
        [clang, '-cc1'] + TRIPLE + lang + opts + ['-w', '-ast-dump', name], cwd, test))
    if theirs is not None:
        # JSON gives lists back: the positions are tuples
        theirs = [(tuple(b), tuple(e) if e else None, k) for b, e, k in theirs]
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


CLAIMS_FILE = 'cxx-claims'
DECLARATORS_FILE = 'cxx-declarators'
# what cxxparse -declaration-claims builds its claims of (declarator-claims.h)
PRELUDE = """namespace vcc_claim {
template <class T> using pointer_to = T *;
template <class T> using lvalue_reference_to = T &;
template <class T> using rvalue_reference_to = T &&;
template <class T, decltype(sizeof 0) N> using array_of = T[N];
template <class T> using array_of_unknown_bound = T[];
template <class T, class C> using pointer_to_member_of = T C::*;
template <class T> using const_ = const T;
template <class T> using volatile_ = volatile T;
template <class F> using function = F;
}
"""


def claim_lines(cxxparse, mode, opts, test, fields):
    """the lines of cxxparse MODE about the test file itself, split at tabs;
    bytes as characters, as the test file is read"""
    cwd = os.path.dirname(test)
    try:
        out = subprocess.run([cxxparse, mode] + opts + [os.path.basename(test)], cwd=cwd, capture_output=True,
                             timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return []
    if out.returncode not in (0, 1):
        return []
    found = []
    for line in out.stdout.decode('latin-1').split('\n'):
        parts = line.split('\t')
        if len(parts) == fields and os.path.normpath(os.path.join(cwd, parts[0])) == os.path.normpath(test):
            found.append(parts[1:])
    return found


def check_claims(test, opts, clang, cxxparse, res, error_lines):
    """V4b: see the module's docstring.  res['claims'] and
    res['declarators'] get the counts, res['fail'] one entry per refuted
    claim.  error_lines: the lines clang reports an error on in the test as
    it is."""
    counts = {'value': 0, 'type': 0, 'refuted': 0, 'unchecked': 0}
    declarators = {'verified': 0, 'overload': 0, 'refuted': 0, 'rejected': 0, 'unchecked': 0}
    res['claims'] = counts
    res['declarators'] = declarators
    std = next((o[5:] for o in opts if o.startswith('-std=')), 'c++17')
    if re.fullmatch(r'(c|gnu)\+\+(98|03)', std):
        return
    cwd = os.path.dirname(test)
    name = os.path.basename(test)
    claims = []
    for place, e, p in claim_lines(cxxparse, '-expression-claims', opts, test, 4):
        row, column = map(int, place.split(':'))
        claims.append((row, column, e.strip(), p.strip()))
    declared = []
    for said, place, kind, declared_name, claim in claim_lines(cxxparse, '-declaration-claims', opts, test, 6):
        row, column = map(int, place.split(':'))
        declared.append((row, column, tuple(map(int, said.split('-'))), kind, declared_name, claim))
    if not claims and not declared:
        return
    # bytes as characters: the columns are bytes
    lines = open(test, 'rb').read().decode('latin-1').split('\n')
    places = {}
    for k, c in enumerate(claims):
        places.setdefault(c[:2], ([], []))[0].append(k)
    for k, c in enumerate(declared):
        places.setdefault(c[:2], ([], []))[1].append(k)
    for row, column in sorted(places, reverse=True):
        if row > len(lines):
            continue
        block = ''
        for k in places[(row, column)][0]:
            e, p = claims[k][2], claims[k][3]
            block += (f'\n#line {4 * k + 1} "{CLAIMS_FILE}"\n'
                      f'static_assert(__is_same(decltype(({e})), decltype(({e}))));\n'
                      f'static_assert(__is_same(decltype(({e})), decltype(({p}))));\n'
                      f'static_assert(({e}) == ({p}) || ({e}) != ({e}));\n'
                      f'static_assert(__is_arithmetic(__remove_cvref(decltype(({e})))) || '
                      f'__is_enum(__remove_cvref(decltype(({e})))));')
        for k in places[(row, column)][1]:
            _, _, _, kind, declared_name, claim = declared[k]
            # the type the name has: of a typedef-name the name itself
            has = declared_name if kind.startswith('type') else f'decltype({declared_name})'
            asked = has
            if kind.endswith('[]'):
                # the bound an initializer or an earlier declaration gave is
                # no part of what the declarator says
                asked = f'__remove_extent({has})[]'
            overload = ';'
            if kind.startswith('function'):
                # an overloaded name has no decltype: is there a function of
                # the claimed type?
                overload = (f'static_assert(sizeof(static_cast<::vcc_claim::pointer_to<{claim}>>(&{declared_name}))'
                            f' != 0);')
            block += (f'\n#line {3 * k + 1} "{DECLARATORS_FILE}"\n'
                      f'static_assert(__is_same({asked}, {asked}));\n'
                      f'static_assert(__is_same({asked}, {claim}));\n'
                      f'{overload}')
        text = lines[row - 1]
        lines[row - 1] = (text[:column - 1] + block + f'\n#line {row} "{name}"\n' +
                          ' ' * (column - 1) + text[column - 1:])
    source = (f'#line 1 "{CLAIMS_FILE}-prelude"\n' + PRELUDE + f'#line 1 "{name}"\n' +
              '\n'.join(lines)).encode('latin-1')

    def compile_claims():
        try:
            r = subprocess.run([clang, '-cc1'] + TRIPLE + ['-x', 'c++'] + opts +
                               ['-fsyntax-only', '-ferror-limit', '0', '-w', '-'],
                               cwd=cwd, input=source, capture_output=True, timeout=60)
        except subprocess.TimeoutExpired:
            return None
        return r.stderr.decode('utf-8', 'replace')
    stderr = cached('claims:' + hashlib.sha256(source).hexdigest(), clang, test, opts, compile_claims)
    if stderr is None:
        counts['unchecked'] += len(claims)
        declarators['unchecked'] += len(declared)
        return

    def errors_of(file):
        """line of FILE -> clang's errors there; an error in a template (one
        of the prelude's: an array of references, a function returning an
        array) is where its `in instantiation ... requested here` note is"""
        errors = {}
        last = None
        for line in stderr.split('\n'):
            m = re.match(r'^(.+?):(\d+):\d+: (?:fatal )?(error|warning|note): (.*)$', line)
            if not m:
                continue
            if m.group(3) == 'error':
                last = m.group(4)
                if m.group(1) == file:
                    errors.setdefault(int(m.group(2)), []).append(last)
            elif m.group(3) == 'warning':
                last = None
            elif last and m.group(1) == file and 'in instantiation of' in m.group(4):
                errors.setdefault(int(m.group(2)), []).append(last)
        return errors
    errors = errors_of(CLAIMS_FILE)
    for k, (row, column, e, p) in enumerate(claims):
        probe, same_type, same_value, no_number = (errors.get(4 * k + i) for i in (1, 2, 3, 4))
        if probe:
            counts['unchecked'] += 1
        elif same_type or (not no_number and any('static assertion failed' in x for x in same_value or ())):
            counts['refuted'] += 1
            res['fail'].append(('claims', f'the expression before {row}:{column}: `{e}` is not `{p}`: '
                                          f'{(same_type or same_value)[0]}'))
        elif same_value:
            counts['type'] += 1
        else:
            counts['value'] += 1
    errors = errors_of(DECLARATORS_FILE)
    for k, (row, column, said, kind, declared_name, claim) in enumerate(declared):
        probe, same_type, no_overload = (errors.get(3 * k + i) for i in (1, 2, 3))
        if any(said[0] <= line <= said[1] for line in error_lines):
            # clang rejects the declaration: what it then declares is its
            # recovery's
            declarators['rejected'] += 1
        elif probe:
            if kind.startswith('function') and not no_overload:
                declarators['overload'] += 1
            else:
                declarators['unchecked'] += 1
                if os.environ.get('VCC_PARSE_UNCHECKED'):
                    res['fail'].append(('unchecked', f'{row}: {kind} {declared_name}: {probe[0]}'))
        elif same_type:
            declarators['refuted'] += 1
            res['fail'].append(('claims', f'the declaration before {row}:{column}: `{declared_name}` is no '
                                          f'`{claim}`: {same_type[0]}'))
        else:
            declarators['verified'] += 1


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
            st, _, err = run([cxxparse, '-fsyntax-only'] + opts + [path], os.path.dirname(test), timeout=10)
            if st == 2 and 'unsupported -std' in err:
                break  # an edition cxxparse does not have: the run is skipped
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
        text = open(t, errors='replace').read()
        if re.search(r'altivec|power8-vector|-fzvector', text):
            # PowerPC's and SystemZ's vector keywords: a target's extension
            # the aarch64 runs cannot have
            unrunnable += 1
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
    counts = {'tokens': 0, 'crash': 0, 'decls': 0, 'syntax': 0, 'claims': 0, 'unchecked': 0}
    claims = {'value': 0, 'type': 0, 'refuted': 0, 'unchecked': 0}
    declarators = {'verified': 0, 'overload': 0, 'refuted': 0, 'rejected': 0, 'unchecked': 0}
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
        for k in claims:
            claims[k] += r.get('claims', {}).get(k, 0)
        for k in declarators:
            declarators[k] += r.get('declarators', {}).get(k, 0)
        if r['fail'] or a.v:
            rel = os.path.relpath(r['test'], os.path.join(cxxpp.work_dir(), 'testsuites', 'llvm', 'clang', 'test'))
            if rel.startswith('..'):
                rel = os.path.relpath(r['test'], R)
            for k, why in r['fail'] or [('ok', '')]:
                print(f'{k:7} {rel} {" ".join(r["opts"])}: {why}')
    runs = len(jobs)
    print(f'{runs} runs of {len(tests_seen)} tests ({unrunnable} not run: inputs made at run time, another target\'s vector keywords): '
          f'tokens {runs - counts["tokens"]}/{runs}, no crash {len(results) - counts["crash"]}/{len(results)}, '
          f'declarations {agree}/{compared} where clang finds no syntax error ({syntax_errors} runs where it does, '
          f'{skipped} skipped); '
          f'nodes: unparsed {unparsed}, erroneous {erroneous}')
    print(f'syntax: {syntax["agree"]} declarations accepted or rejected as clang does, '
          f'{syntax["we_reject"]} we reject and clang accepts, {syntax["we_accept"]} we accept and clang rejects, '
          f'{syntax["not_comparable"]} kept as tokens (a later step\'s grammar)')
    print(f'claims: {claims["value"]} expressions verified by value, {claims["type"]} by type, '
          f'{claims["refuted"]} refuted, {claims["unchecked"]} not checkable; '
          f'{declarators["verified"]} declared types verified, {declarators["overload"]} as one of a name\'s '
          f'overloads, {declarators["refuted"]} refuted, {declarators["rejected"]} in declarations clang rejects, '
          f'{declarators["unchecked"]} not checkable')
    return 1 if any(counts.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
