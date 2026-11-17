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

  declared cxxsema -declaration-claims gives each name a declaration
           declared and the type analysis gave it, in cxxparse's format;
           bench/cxx-parse.py's check has clang compile
               static_assert(__is_same(decltype(NAME), CLAIM));
           after the declaration: verified, refuted (a failure), in a
           declaration clang rejects, or not checkable
  types    cxxsema -type-claims gives each expression with a type its type
           and value category (src/bin/cxxsema/type-claims.h); after the
           expression's declaration clang compiles
               static_assert(__is_same(decltype((E)), CLAIM));
           verified, refuted (a failure), or not checkable (E does not
           compile at that place).  From C++11 on, a constant prvalue's
           claim has its value too, which clang checks with
               static_assert((E) == VALUE);
           `bench/cxx-sema.py selftest` checks this path on claims written
           by hand, some of them wrong.

The summary ends with the subclauses most declarations are unsupported for:
the work list.  clang's side is the cache of bench/cxx-parse.py
(work/cxx-parse-ref).

Usage: bench/cxx-sema.py selftest
       bench/cxx-sema.py [-v] [-j N] [--bin-dir DIR] [--unsupported N] [PATTERN...]
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
import hashlib
import re
import subprocess
import sys
import tempfile

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


TYPES_FILE = 'cxx-type-claims'


def check_type_claims(test, opts, clang, claims, cache=True):
    """claims: (line, column, expression, claim, value), the place just
    after the expression's declaration; value is None or the constant the
    expression's value is claimed to equal.  Returns {'verified',
    'refuted', 'unchecked', 'value_verified', 'value_refuted'} counts and
    the refuted claims."""
    counts = collections.Counter()
    refuted = []
    if not claims:
        return counts, refuted
    cwd = os.path.dirname(test)
    name = os.path.basename(test)
    lines = open(test, 'rb').read().decode('latin-1').split('\n')
    places = {}
    for k, c in enumerate(claims):
        places.setdefault(c[:2], []).append(k)
    for row, column in sorted(places, reverse=True):
        if row > len(lines):
            continue
        block = ''
        for k in places[(row, column)]:
            e, claim, value = claims[k][2], claims[k][3], claims[k][4]
            block += (f'\n#line {3 * k + 1} "{TYPES_FILE}"\n'
                      f'static_assert(__is_same(decltype(({e})), decltype(({e}))));\n'
                      f'static_assert(__is_same(decltype(({e})), {claim}));')
            if value is not None:
                block += f'\nstatic_assert(({e}) == {value});'
        text = lines[row - 1]
        lines[row - 1] = (text[:column - 1] + block + f'\n#line {row} "{name}"\n' +
                          ' ' * (column - 1) + text[column - 1:])
    source = (f'#line 1 "{TYPES_FILE}-prelude"\n' + cxxparse.PRELUDE + f'#line 1 "{name}"\n' +
              '\n'.join(lines)).encode('latin-1')

    def compile_claims():
        try:
            r = subprocess.run([clang, '-cc1'] + cxxparse.TRIPLE + ['-x', 'c++'] + opts +
                               ['-fsyntax-only', '-ferror-limit', '0', '-w', '-'],
                               cwd=cwd, input=source, capture_output=True, timeout=60)
        except subprocess.TimeoutExpired:
            return None
        return r.stderr.decode('utf-8', 'replace')
    if cache:
        stderr = cxxparse.cached('type-claims:' + hashlib.sha256(source).hexdigest(), clang, test, opts,
                                 compile_claims)
    else:
        stderr = compile_claims()
    if stderr is None:
        counts['unchecked'] += len(claims)
        return counts, refuted
    errors = {}
    for m in re.finditer(r'^(.+?):(\d+):\d+: (?:fatal )?error: (.*)$', stderr, re.M):
        if m.group(1) == TYPES_FILE:
            errors.setdefault(int(m.group(2)), []).append(m.group(3))
    for k, (row, column, e, claim, value) in enumerate(claims):
        if errors.get(3 * k + 1):
            counts['unchecked'] += 1
            continue
        if errors.get(3 * k + 2):
            counts['refuted'] += 1
            refuted.append(f'{row}:{column}: `{e}` is no `{claim}`')
        else:
            counts['verified'] += 1
        if value is None:
            continue
        if errors.get(3 * k + 3):
            counts['value_refuted'] += 1
            refuted.append(f'{row}:{column}: `{e}` is not the constant {value}')
        else:
            counts['value_verified'] += 1
    return counts, refuted


def type_claims(cxxsema, opts, test):
    """cxxsema -type-claims: the claims about the test file itself"""
    cwd = os.path.dirname(test)
    r = cxxparse.run([cxxsema, '-type-claims'] + opts + [os.path.basename(test)], cwd)
    if r[0] not in (0, 1):
        return []
    found = []
    for line in r[1].split('\n'):
        parts = line.split('\t')
        if len(parts) in (4, 5) and os.path.normpath(os.path.join(cwd, parts[0])) == os.path.normpath(test):
            row, column = map(int, parts[1].split(':'))
            value = parts[4].strip() if len(parts) == 5 else None
            found.append((row, column, parts[2].strip(), parts[3].strip(), value))
    return found


SELFTEST_SOURCE = """int i;
const int ci = 1;
int g(...);
int *const volatile *(*fp)[3];
struct S { int m; };
"""
# (expression, claim, value, expected): the encodings cxxsema -selftest
# checks that its encoder writes, and claims that must be refuted or
# unchecked
SELFTEST_CLAIMS = [
    ('i', '::vcc_claim::lvalue_reference_to<int>', None, 'verified'),
    ('i + 1', 'int', None, 'verified'),
    ('ci + 1', 'int', '2ULL', 'verified value_verified'),
    ('-ci', 'int', '(-1LL)', 'verified value_verified'),
    ('1.0 / 4', 'double', '0x1p-2', 'verified value_verified'),
    ('ci * 3', 'int', '4ULL', 'verified value_refuted'),
    ('i * 3', 'int', '0ULL', 'verified value_refuted'),
    ('static_cast<int &&>(i)', '::vcc_claim::rvalue_reference_to<int>', None, 'verified'),
    ('ci', '::vcc_claim::lvalue_reference_to<::vcc_claim::const_<int>>', None, 'verified'),
    ('g', '::vcc_claim::lvalue_reference_to<::vcc_claim::function<int(...)>>', None, 'verified'),
    ('fp', '::vcc_claim::lvalue_reference_to<::vcc_claim::pointer_to<::vcc_claim::array_of<'
           '::vcc_claim::pointer_to<::vcc_claim::const_<::vcc_claim::volatile_<'
           '::vcc_claim::pointer_to<int>>>>, (3)>>>', None, 'verified'),
    ('S().m', '::vcc_claim::rvalue_reference_to<int>', None, 'verified'),
    ('i', 'int', None, 'refuted'),
    ('i + 1', '::vcc_claim::lvalue_reference_to<int>', None, 'refuted'),
    ('ci', '::vcc_claim::lvalue_reference_to<int>', None, 'refuted'),
    ('undeclared', 'int', None, 'unchecked'),
]


def selftest(clang):
    """the type claims' path through clang, on claims written by hand"""
    with tempfile.TemporaryDirectory() as d:
        test = os.path.join(d, 'claims.cpp')
        open(test, 'w').write(SELFTEST_SOURCE)
        claims = [(5, 22, e, c, v) for e, c, v, _ in SELFTEST_CLAIMS]
        counts, refuted = check_type_claims(test, ['-std=c++17'], clang, claims, cache=False)
    want = collections.Counter(w for _, _, _, x in SELFTEST_CLAIMS for w in x.split())
    if counts != want:
        print(f'cxx-sema selftest: FAILED: {dict(counts)}, expected {dict(want)}')
        for r in refuted:
            print(f'  refuted {r}')
        return 1
    print(f'cxx-sema selftest: ok ({want["verified"]} verified, {want["refuted"]} refuted, '
          f'{want["unchecked"]} not checkable; values {want["value_verified"]} verified, '
          f'{want["value_refuted"]} refuted, as expected)')
    return 0


def check(test, opts, clang, cxxsema):
    cwd = os.path.dirname(test)
    name = os.path.basename(test)
    res = {'test': test, 'opts': opts, 'fail': [], 'verdict': collections.Counter(),
           'unsupported': collections.Counter(), 'types': collections.Counter()}
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
    every_error_line = set()
    for m in CLANG_ERROR.finditer(c[2]):
        if m.group(1) == name:
            every_error_line.add(int(m.group(2)))
    cxxparse.check_claims(test, opts, clang, cxxsema, res, every_error_line, expression_mode=None)
    res['types'], refuted = check_type_claims(test, opts, clang, type_claims(cxxsema, opts, test))
    for r in refuted:
        res['fail'].append(('types', r))
    if rejected:
        res['fail'].append(('verdict', f'ill-formed for us, accepted by clang: line {rejected[0]}'
                                       f' ({len(rejected)} declarations)'))
    return res


def main():
    if sys.argv[1:] == ['selftest']:
        return selftest(cxxpp.find_clang())
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
    verdict, unsupported, types = collections.Counter(), collections.Counter(), collections.Counter()
    declared = collections.Counter()
    crashes = failures = skipped = 0
    seen = set()
    for r in results:
        seen.add(r['test'])
        verdict.update(r['verdict'])
        unsupported.update(r['unsupported'])
        types.update(r['types'])
        declared.update(r.get('declarators', {}))
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
    print(f'declared types: {declared["verified"]} verified, {declared["overload"]} as one of a name\'s overloads, '
          f'{declared["refuted"]} refuted, {declared["rejected"]} in declarations clang rejects, '
          f'{declared["unchecked"]} not checkable')
    print(f'types: {types["verified"]} claims verified, {types["refuted"]} refuted, '
          f'{types["unchecked"]} not checkable; constants: {types["value_verified"]} verified, '
          f'{types["value_refuted"]} refuted')
    if unsupported and a.unsupported:
        top = ', '.join(f'[{label}] {n}' for label, n in unsupported.most_common(a.unsupported))
        print(f'unsupported by subclause ({len(unsupported)}): {top}')
    if missing:
        print(f'not fetched: clang/test/{", clang/test/".join(missing)} (bench/fetch-testsuites.sh llvm)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
