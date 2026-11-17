#!/usr/bin/env python3
"""The C++ front end's semantic analysis (src/frontend/cplusplus, through
cxxsema) against clang, on clang's conformance tests (doc/PLAN-cxx-sema.md,
C3).

Tests: clang/test/CXX/**/*.cpp (organized by the standard's paragraphs),
clang/test/SemaCXX and SemaTemplate when bench/fetch-testsuites.sh llvm has
them (work/testsuites), and src/test/cxx/sema/{spec,gram}/*.cpp.  For each
%clang_cc1 RUN line (the option sets bench/cxx-pp.py reads), per run:

  crash    cxxsema ends with status 0 or 1, within the time limit, never
           by a signal
  verdict  per declaration of the translation unit that starts in the test
           file, cxxsema -dump-analysis against the lines where clang
           reports an error:
             agree        well-formed and clang has no error in its lines,
                          or ill-formed and clang has one
             we reject    ill-formed, clang accepts: a failure, unless
                          src/test/cxx/sema/known-differences.txt has the
                          line with its reason (the standard's paragraph
                          or the core issue)
             we accept    well-formed, clang rejects: a rule that is not
                          enforced yet (counted)
             unsupported  rules that are not written: nothing concluded
             not parsed   the parser's (an erroneous node, or one kept as
                          tokens), or clang's error there is one of syntax

The summary ends with the subclauses most declarations are unsupported for:
the work list.  clang's side is the cache of bench/cxx-parse.py
(work/cxx-parse-ref).

Usage: bench/cxx-sema.py [-v] [-j N] [--bin-dir DIR] [--unsupported N] [PATTERN...]
A PATTERN is a part of a test's path: `basic.lookup.unqual` runs that
subclause's tests.  Prints one line per failing run (all with -v) and a
summary.
"""
import argparse
import collections
import concurrent.futures
import glob
import importlib.util
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('cxxparse', os.path.join(HERE, 'cxx-parse.py'))
cxxparse = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cxxparse)
cxxpp = cxxparse.cxxpp

R = cxxpp.R
OURS = os.path.join(R, 'src', 'test', 'cxx', 'sema')
LLVM_TEST = cxxparse.LLVM_TEST
SUITES = ('CXX', 'SemaCXX', 'SemaTemplate')


def known_differences():
    """src/test/cxx/sema/known-differences.txt: PATH:LINE reason"""
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


def tests():
    found = []
    for suite in SUITES:
        found += [f for f in glob.glob(os.path.join(LLVM_TEST, suite, '**', '*.cpp'), recursive=True)
                  if os.sep + 'cpp' + os.sep not in f and os.sep + 'lex' + os.sep not in f]
    found.sort()
    found += sorted(glob.glob(os.path.join(OURS, 'spec', '**', '*.cpp'), recursive=True) +
                    glob.glob(os.path.join(OURS, 'gram', '**', '*.cpp'), recursive=True))
    return found


def relative(test):
    rel = os.path.relpath(test, LLVM_TEST)
    if rel.startswith('..'):
        rel = os.path.relpath(test, R)
    return rel


ANALYSIS_LINE = re.compile(r'^(\S+) (.*?):(\d+):(\d+) (.*?):(\d+):(\d+) '
                           r'(well-formed|ill-formed|not-parsed|unsupported)(?:=\[([^\]]*)\])?$')
CLANG_ERROR = re.compile(r'^([^:\n]+):(\d+):\d+: (?:error|fatal error): .*?(?:\[([^\]]+)\])?$', re.M)
SYNTAX_CATEGORIES = ('Parse Issue', 'Lexical or Preprocessor Issue')


def check(test, opts, clang, cxxsema):
    cwd = os.path.dirname(test)
    name = os.path.basename(test)
    res = {'test': test, 'opts': opts, 'fail': [], 'verdict': collections.Counter(),
           'unsupported': collections.Counter()}
    ours = cxxparse.run([cxxsema, '-dump-analysis'] + opts + [name], cwd)
    if ours[0] == 2 and 'unsupported -std' in ours[2]:
        res['skip'] = ours[2].strip()
        return res
    if ours[0] not in (0, 1):
        res['fail'].append(('crash', f'status {ours[0]}: {ours[2].strip().splitlines()[-1:]}'))
        return res
    c = cxxparse.cached('diagnostics', clang, test, opts, lambda: list(
        cxxparse.run([clang, '-cc1'] + cxxparse.TRIPLE + ['-x', 'c++'] + opts +
                     ['-fsyntax-only', '-fdiagnostics-show-category', 'name', '-ferror-limit', '0', name],
                     cwd, timeout=60)))
    if c[0] == 'timeout':
        res['skip'] = 'clang timed out'
        return res
    syntax_lines, error_lines = set(), set()
    for m in CLANG_ERROR.finditer(c[2]):
        if m.group(1) != name:
            continue
        if m.group(3) in SYNTAX_CATEGORIES:
            syntax_lines.add(int(m.group(2)))
        else:
            error_lines.add(int(m.group(2)))
    known = KNOWN.get(relative(test), set())
    rejected = []
    for line in ours[1].splitlines():
        m = ANALYSIS_LINE.match(line)
        if not m:
            res['fail'].append(('crash', f'-dump-analysis: {line!r}'))
            break
        if m.group(2) != name or m.group(5) != name:
            continue
        first, last, result = int(m.group(3)), int(m.group(6)), m.group(8)
        lines = range(first, last + 1)
        if result == 'not-parsed' or any(n in syntax_lines for n in lines):
            res['verdict']['not_parsed'] += 1
        elif result == 'unsupported':
            res['verdict']['unsupported'] += 1
            res['unsupported'][m.group(9)] += 1
        else:
            clang_rejects = any(n in error_lines for n in lines)
            if (result == 'ill-formed') == clang_rejects:
                res['verdict']['agree'] += 1
            elif result == 'ill-formed':
                if any(n in known for n in lines):
                    res['verdict']['known'] += 1
                else:
                    res['verdict']['we_reject'] += 1
                    rejected.append(first)
            else:
                res['verdict']['we_accept'] += 1
    if rejected:
        res['fail'].append(('verdict', f'ill-formed for us, accepted by clang: line {rejected[0]}'
                                       f' ({len(rejected)} declarations)'))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir')
    ap.add_argument('--unsupported', type=int, default=10, metavar='N',
                    help='how many subclauses of the work list to print')
    ap.add_argument('patterns', nargs='*')
    a = ap.parse_args()
    clang = cxxpp.find_clang()
    cxxsema = cxxparse.find_tool(a.bin_dir, 'cxxsema')
    resource_include = os.path.join(
        subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip(), 'include')
    missing = [s for s in SUITES if not os.path.isdir(os.path.join(LLVM_TEST, s))]
    jobs = []
    unrunnable = 0
    for t in tests():
        if a.patterns and not any(p in t for p in a.patterns):
            continue
        text = open(t, errors='replace').read()
        if re.search(r'altivec|power8-vector|-fzvector', text):
            unrunnable += 1
            continue
        sets = cxxpp.run_lines(t)
        if sets is None:
            unrunnable += 1
            continue
        if not sets and (OURS in t):
            sets = [['-std=c++20']]
        extra = []
        if re.search(r'^\s*#\s*include\s*<', text, re.M):
            extra = ['-isystem', resource_include]
        for opts in sets:
            jobs.append((t, opts + extra))
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        results = list(ex.map(lambda j: check(j[0], j[1], clang, cxxsema), jobs))
    verdict, unsupported = collections.Counter(), collections.Counter()
    crashes = failures = skipped = 0
    seen = set()
    for r in results:
        seen.add(r['test'])
        verdict.update(r['verdict'])
        unsupported.update(r['unsupported'])
        kinds = {k for k, _ in r['fail']}
        crashes += 'crash' in kinds
        failures += bool(kinds)
        skipped += bool(r.get('skip'))
        if r['fail'] or a.v:
            for k, why in r['fail'] or [('ok', '')]:
                print(f'{k:7} {relative(r["test"])} {" ".join(r["opts"])}: {why}')
    runs = len(jobs)
    print(f'{runs} runs of {len(seen)} tests ({unrunnable} not run, {skipped} skipped): '
          f'no crash {runs - crashes}/{runs}')
    print(f'declarations: {verdict["agree"]} as clang, {verdict["we_reject"]} we reject and clang accepts '
          f'({verdict["known"]} known differences), {verdict["we_accept"]} we accept and clang rejects, '
          f'{verdict["unsupported"]} unsupported, {verdict["not_parsed"]} not parsed')
    if unsupported and a.unsupported:
        top = ', '.join(f'[{label}] {n}' for label, n in unsupported.most_common(a.unsupported))
        print(f'unsupported by subclause ({len(unsupported)}): {top}')
    if missing:
        print(f'not fetched: clang/test/{", clang/test/".join(missing)} (bench/fetch-testsuites.sh llvm)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
