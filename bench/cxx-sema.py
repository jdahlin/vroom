#!/usr/bin/env python3
"""The C++ front end's semantic analysis (src/frontend/cplusplus, through the
driver: vpp -fcxx-dump=, src/frontend/cplusplus/dump/dump.h) against clang, on clang's conformance tests (doc/PLAN-cxx-sema.md,
C3).

Tests: clang/test/CXX/**/*.cpp (organized by the standard's paragraphs),
clang/test/SemaCXX and SemaTemplate when bench/fetch-testsuites.sh llvm has
them (work/testsuites), and src/test/cxx/sema/{spec,gram}/*.cpp.  For each
%clang_cc1 RUN line (the option sets bench/cxx-pp.py reads), per run:

  crash    vpp ends with status 0 or 1, within the time limit, never
           by a signal
  verdict  per declaration of the translation unit that starts in the test
           file, -fcxx-dump=analysis against the lines where clang
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

  tree     -fcxx-dump=check-tree: the semantic tree of every well-formed
           definition is complete and agrees with each expression's
           recorded type and value category (dump/tree-check.h);
           a problem is a failure

  declared -fcxx-dump=declaration-claims gives each name a declaration
           declared and the type analysis gave it, in declarator-claims' format;
           bench/cxx-parse.py's check has clang compile
               static_assert(__is_same(decltype(NAME), CLAIM));
           after the declaration: verified, refuted (a failure), in a
           declaration clang rejects, or not checkable
  types    -fcxx-dump=type-claims gives each expression with a type its type
           and value category (dump/type-claims.h); after the
           expression's declaration clang compiles
               static_assert(__is_same(decltype((E)), CLAIM));
           verified, refuted (a failure), or not checkable (E does not
           compile at that place).  From C++11 on, a constant prvalue's
           claim has its value too, which clang checks with
               static_assert((E) == VALUE);
           `bench/cxx-sema.py selftest` checks this path on claims written
           by hand, some of them wrong.
  lookup   -fcxx-dump=lookup-claims gives each name whose lookup analysis
           resolved the declaration it found (the place of the name in the
           entity's first declaration); clang's AST (-ast-dump=json) gives
           the declaration each DeclRefExpr and MemberExpr at that place
           refers to, followed to its first redeclaration: verified,
           refuted (a failure: the gate of S2), or not checkable (clang has
           no reference there: a type name, a namespace; or an error on the
           line)
  symbols  on a run clang accepts without an error, -fcxx-dump=symbols gives
           the symbol of each function and variable a well-formed
           declaration defines (src/frontend/cplusplus/abi/itanium/); clang
           -O0 compiles the file and each must be one its object defines
           (llvm-nm): verified, refuted (a failure), or not emitted (clang
           leaves out an unused entity with internal linkage).
  layout   -fcxx-dump=layout-claims gives each class analysis completed
           its size, data size and alignment, and the offset of each direct
           base class subobject, non-static data member and bit-field
           (src/frontend/cplusplus/abi/itanium/layout.cpp); clang
           -fdump-record-layouts-complete gives the record of that name:
           verified, refuted (a failure: S4's gate), or not checkable (no
           record of that name in clang's dump or two that differ, or an
           error of clang's in the declaration).  `bench/cxx-sema.py
           selftest` checks this path too, on claims written by hand.
  lowering on the same runs, vpp -S -O0 (the tree lowered to C's AST,
           C's code generator): its assembly must assemble (clang -cc1as)
           and the object define exactly the functions and variables
           clang -O0's does: lowered, refuted (a failure), or not lowered
           with the reason (a declaration not well-formed for us, or the
           first construct lowering has no rule for), most frequent first.

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
import json
import re
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('cxxparse', os.path.join(HERE, 'cxx-parse.py'))
cxxparse = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cxxparse)
cxxpp = cxxparse.cxxpp

R = cxxpp.R
OURS = os.path.join(R, 'src', 'test', 'cxx', 'sema')
LLVM_TEST = cxxparse.LLVM_TEST
# clang -cc1 has plain char signed unless told otherwise; the driver tells
# it so for aarch64 Linux, the target vpp analyses for
TARGET = cxxparse.TRIPLE + ['-fno-signed-char']
ALIASES = ['-mconstructor-aliases']
SUITES = ('CXX', 'SemaCXX', 'SemaTemplate')
# --suites NAME,...: clang's other directories with C++ tests (CodeGenCXX,
# Parser, Sema), for a wider look than the gate's; the baselines
# (clang-failures.txt, clang-metrics.json) are of the default three
WIDER_SUITES = ('CXX', 'SemaCXX', 'SemaTemplate', 'CodeGenCXX', 'Parser',
                'Sema')
KNOWN_FAILURES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                              'src/test/cxx/sema/clang-failures.txt')
METRICS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       'src/test/cxx/sema/clang-metrics.json')
# a number that differs by this much or less from the baseline is host noise (CI's arm64 runner
# counts one declaration differently from an x86-64 host), not progress and not a regression
TOLERANCE = 3
# metric -> better direction: a number that gets worse fails --check (the rest is shown)
HIGHER_IS_BETTER = ('decl_agree', 'declared_verified', 'types_verified', 'constants_verified',
                    'lookup_verified', 'symbols_verified', 'layout_verified', 'lowering_lowered')
LOWER_IS_BETTER = ('crashes', 'decl_we_reject', 'decl_we_accept', 'declared_refuted', 'types_refuted',
                   'constants_refuted', 'lookup_refuted', 'symbols_refuted', 'layout_refuted',
                   'lowering_refuted')
KNOWN_HEADER = '''# failures of bench/cxx-sema.py on clang's C++ tests that are known (xfail): KIND TEST OPTIONS: reason.
# --check fails on a failure not listed here and on a listed one that passes now: fix it, then
# `bench/cxx-sema.py --bless` in the same commit.  Never add a line to hide a regression.
'''


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
CLANG_DIAGNOSTIC = re.compile(r'^([^:\n]+):(\d+):\d+: (error|fatal error|warning|note): (.*)$', re.M)
INSTANTIATION_NOTE = re.compile(r'^in instantiation of .* requested here')


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
    if cxxparse.has_line_directive(lines):
        counts['unchecked'] += len(claims)
        return counts, refuted
    places = {}
    for k, c in enumerate(claims):
        places.setdefault(c[:2], []).append(k)
    for row, column in sorted(places, reverse=True):
        if row > len(lines):
            continue
        block = ''
        for k in places[(row, column)]:
            e, claim, value = claims[k][2], claims[k][3], claims[k][4]
            # the file is read as latin-1: the expression's UTF-8 bytes too
            e = e.encode('utf-8', 'surrogateescape').decode('latin-1')
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
            r = subprocess.run([clang, '-cc1'] + TARGET + ['-x', 'c++'] + opts +
                               ['-fsyntax-only', '-ferror-limit', '0', '-w', '-'],
                               cwd=cwd, input=source, capture_output=True, timeout=60)
        except subprocess.TimeoutExpired:
            return None
        return r.stderr.decode('utf-8', 'replace')
    if cache:
        stderr = cxxparse.cached('type-claims:' + ''.join(TARGET) + hashlib.sha256(source).hexdigest(), clang, test, opts,
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


# the outputs check() reads, asked for in one vpp run per test and option
# set (-fcxx-dump-sections tells them apart): parsing and analysing the test
# once instead of once per output
OUTPUTS = ('analysis', 'check-tree', 'declaration-claims', 'type-claims',
           'lookup-claims', 'layout-claims', 'symbols')
RUNS = threading.local()


def ours(vpp, outputs, opts, test, raw=False):
    """(returncode, stdout, stderr) of vpp -fcxx-dump=OUTPUTS (a comma
    list of OUTPUTS) on test, from the one run of all of OUTPUTS"""
    key = (vpp, tuple(opts), test)
    if getattr(RUNS, 'key', None) != key:
        rc, out, err = cxxparse.run(cxxpp.vpp_command(vpp, ','.join(OUTPUTS), opts, os.path.basename(test),
                                                      ['-fcxx-dump-sections']),
                                    os.path.dirname(test), raw=True)
        sections = {}
        if isinstance(out, bytes):
            name = None
            for line in out.split(b'\n')[:-1]:
                if line.startswith(b'#cxx-dump '):
                    name = line[10:].decode()
                    sections[name] = []
                elif name is not None:
                    sections[name].append(line + b'\n')
        RUNS.key, RUNS.result = key, (rc, {k: b''.join(v) for k, v in sections.items()}, err)
    rc, sections, err = RUNS.result
    if not isinstance(rc, int):
        empty = b''
        if not raw:
            empty = ''
        return rc, empty, ''
    out = b''.join(sections.get(o, b'') for o in outputs.split(','))
    if raw:
        return rc, out, err
    return rc, out.decode('utf-8', 'replace'), err


def claim_output(vpp, output, opts, test):
    """cxx-parse.py's claim_lines through ours() (VPP_OUTPUT)"""
    if output not in OUTPUTS:
        out = subprocess.run(cxxpp.vpp_command(vpp, output, opts, os.path.basename(test)),
                             cwd=os.path.dirname(test), capture_output=True, timeout=cxxparse.TIMEOUT)
        return out.returncode, out.stdout
    return ours(vpp, output, opts, test, raw=True)[:2]


cxxparse.VPP_OUTPUT = claim_output


def type_claims(vpp, opts, test):
    """-fcxx-dump=type-claims: the claims about the test file itself"""
    cwd = os.path.dirname(test)
    r = ours(vpp, 'type-claims', opts, test)
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
# (expression, claim, value, expected): the encodings -fcxx-dump=selftest
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


LAYOUT_LINE = re.compile(r'^ *(\d+)?(?::(?:(\d+)-(\d+)|-))? \| (.*)$')
LAYOUT_SIZES = re.compile(r'\[sizeof=(\d+), (?:dsize=(\d+), )?align=(\d+)')
LAYOUT_UNNAMED = re.compile(r'\((?:unnamed|anonymous)(?: struct| union| class)? at [^()]*?:(\d+):(\d+)\)')
LAYOUT_KEY = re.compile(r'^(?:struct|class|union) ')
LAYOUT_BASE = re.compile(r'^(.*?) \((?:primary |virtual |primary virtual )?base\)(?: \(empty\))?$')


def parse_record_layouts(text):
    """clang -fdump-record-layouts' records: {name: [(sizes, components)]},
    sizes (sizeof, dsize, align), components the direct ones in clang's
    order: ('vptr', offset), ('base', offset, name), ('field', offset,
    name), ('bits', bit offset, width, name).  An unnamed class's place is
    written without its file: (unnamed at LINE:COLUMN)"""
    records = {}
    for block in text.split('*** Dumping AST Record Layout')[1:]:
        name, sizes, components = None, None, []
        for line in block.split('\n'):
            m = LAYOUT_LINE.match(line)
            if not m:
                continue
            what = m.group(4)
            sized = LAYOUT_SIZES.match(what)
            if sized:
                sizes = (int(sized.group(1)), int(sized.group(2) or sized.group(1)), int(sized.group(3)))
                continue
            if m.group(1) is None:
                continue
            offset = int(m.group(1))
            if name is None:
                name = LAYOUT_UNNAMED.sub(r'(unnamed at \1:\2)', LAYOUT_KEY.sub('', what))
                if name.endswith(' (empty)'):
                    name = name[:-len(' (empty)')]
                continue
            if not what.startswith('  ') or what.startswith('   '):
                continue        # a component of a component
            what = what[2:]
            if ':' in line.split('|', 1)[0]:
                bit, width = offset * 8, 0
                if m.group(2) is not None:
                    bit += int(m.group(2))
                    width = int(m.group(3)) - int(m.group(2)) + 1
                components.append(('bits', bit, width, what.rsplit(' ', 1)[-1]))
                continue
            if re.match(r'^\(.* (?:vtable|vbase) pointer\)$', what):
                components.append(('vptr', offset))
                continue
            base = LAYOUT_BASE.match(what)
            if base:
                components.append(('base', offset,
                                   LAYOUT_UNNAMED.sub(r'(unnamed at \1:\2)', LAYOUT_KEY.sub('', base.group(1)))))
                continue
            if what.endswith(' (empty)'):
                what = what[:-len(' (empty)')]
            components.append(('field', offset, what.rsplit(' ', 1)[-1]))
        if name is not None and sizes is not None:
            records.setdefault(name, [])
            if (sizes, components) not in records[name]:
                records[name].append((sizes, components))
    return records


def clang_record_layouts(test, opts, clang, cache=True):
    """parse_record_layouts of clang's dump for the test (cached), or None"""
    cwd, name = os.path.dirname(test), os.path.basename(test)

    def compute():
        r = cxxparse.run([clang, '-cc1'] + TARGET + ['-x', 'c++'] + opts +
                         ['-fsyntax-only', '-w', '-fdump-record-layouts-complete', name], cwd, timeout=60)
        if r[0] == 'timeout':
            return None
        return r[1]
    if cache:
        text = cxxparse.cached('record-layouts' + ''.join(TARGET), clang, test, opts, compute)
    else:
        text = compute()
    if text is None:
        return None
    return parse_record_layouts(text)


def layout_claims(text, test):
    """-fcxx-dump=layout-claims: [(file, line, name, sizes, components)]"""
    found = []
    for line in text.split('\n'):
        parts = line.split('\t')
        if parts[0] == 'record' and len(parts) == 6:
            f, row, _ = parts[1].rsplit(':', 2)
            found.append((os.path.normpath(f), int(row), parts[2], tuple(map(int, parts[3:6])), []))
        elif not found:
            continue
        elif parts[0] == 'vptr' and len(parts) == 2:
            found[-1][4].append(('vptr', int(parts[1])))
        elif parts[0] in ('base', 'field') and len(parts) == 3:
            found[-1][4].append((parts[0], int(parts[1]), parts[2]))
        elif parts[0] == 'bits' and len(parts) == 4:
            found[-1][4].append(('bits', int(parts[1]), int(parts[2]), parts[3]))
    return found


def compare_layouts(claims, theirs, main, unchecked_lines):
    """our claims against clang's records: a Counter, and the refuted"""
    counts, refuted = collections.Counter(), []
    for f, row, name, sizes, components in claims:
        records = theirs.get(name, [])
        if len(records) != 1 or (f == main and row in unchecked_lines):
            counts['unchecked'] += 1
            continue
        their_sizes, their_components = records[0]
        if sizes == their_sizes and components == their_components:
            counts['verified'] += 1
            continue
        counts['refuted'] += 1
        why = f'sizeof, dsize, align {sizes}, clang {their_sizes}'
        if sizes == their_sizes:
            why = next((f'{" ".join(map(str, a))}, clang {" ".join(map(str, b))}'
                        for a, b in zip(components, their_components) if a != b),
                       f'{len(components)} bases and members, clang {len(their_components)}')
        refuted.append(f'{row}: layout of `{name}`: {why}')
    return counts, refuted


def check_layout_claims(test, opts, clang, vpp, unchecked_lines):
    """-fcxx-dump=layout-claims against clang's record layouts: a Counter,
    and the refuted.  A class at one of unchecked_lines (a declaration clang
    has an error in) is not checkable"""
    cwd, name = os.path.dirname(test), os.path.basename(test)
    r = ours(vpp, 'layout-claims', opts, test)
    if r[0] not in (0, 1):
        return collections.Counter(), []
    claims = layout_claims(r[1], test)
    if not claims:
        return collections.Counter(), []
    theirs = clang_record_layouts(test, opts, clang)
    if theirs is None:
        return collections.Counter({'unchecked': len(claims)}), []
    return compare_layouts(claims, theirs, os.path.normpath(name), unchecked_lines)


LAYOUT_SELFTEST_SOURCE = """struct E {};
struct A { char c; int i; };
namespace N { struct B : A, E { short s; int p : 3; int : 0; unsigned q : 9; }; }
struct C { C(); int i; char c; };
struct D : C { char d; E e; A a[2]; void (*f)(int); };
typedef struct { long y; } T;
struct V { virtual void f(); int v; };
void f() { struct L { int a; }; }
void g() { struct L { char b; }; }
"""
# (claim lines, expected): what -fcxx-dump=layout-claims would write
LAYOUT_SELFTEST_CLAIMS = [
    ('record\tclaims.cpp:1:1\tE\t1\t1\t1', 'verified'),
    ('record\tclaims.cpp:2:1\tA\t8\t8\t4\nfield\t0\tc\nfield\t4\ti', 'verified'),
    ('record\tclaims.cpp:3:15\tN::B\t16\t14\t4\nbase\t0\tA\nbase\t0\tE\nfield\t8\ts\n'
     'bits\t80\t3\tp\nbits\t96\t0\t\nbits\t96\t9\tq', 'verified'),
    ('record\tclaims.cpp:4:1\tC\t8\t5\t4\nfield\t0\ti\nfield\t4\tc', 'verified'),
    ('record\tclaims.cpp:5:1\tD\t32\t32\t8\nbase\t0\tC\nfield\t5\td\nfield\t6\te\n'
     'field\t8\ta\nfield\t24\tf', 'verified'),
    ('record\tclaims.cpp:6:9\t(unnamed at 6:9)\t8\t8\t8\nfield\t0\ty', 'verified'),
    # the tail padding of C not reused, a wrong data size, a missing
    # virtual table pointer, a bit-field one bit off, a base left out
    ('record\tclaims.cpp:5:1\tD\t32\t32\t8\nbase\t0\tC\nfield\t8\td\nfield\t9\te\n'
     'field\t12\ta\nfield\t24\tf', 'refuted'),
    ('record\tclaims.cpp:4:1\tC\t8\t8\t4\nfield\t0\ti\nfield\t4\tc', 'refuted'),
    ('record\tclaims.cpp:7:1\tV\t16\t12\t8\nfield\t8\tv', 'refuted'),
    ('record\tclaims.cpp:3:15\tN::B\t16\t14\t4\nbase\t0\tA\nbase\t0\tE\nfield\t8\ts\n'
     'bits\t81\t3\tp\nbits\t96\t0\t\nbits\t96\t9\tq', 'refuted'),
    ('record\tclaims.cpp:3:15\tN::B\t16\t14\t4\nbase\t0\tA\nfield\t8\ts\n'
     'bits\t80\t3\tp\nbits\t96\t0\t\nbits\t96\t9\tq', 'refuted'),
    # two classes of one name in clang's dump, a class clang has none for
    ('record\tclaims.cpp:8:12\tL\t4\t4\t4\nfield\t0\ta', 'unchecked'),
    ('record\tclaims.cpp:9:1\tNone\t4\t4\t4', 'unchecked'),
]


def layout_selftest(clang):
    """the layout claims' path through clang, on claims written by hand"""
    with tempfile.TemporaryDirectory() as d:
        test = os.path.join(d, 'claims.cpp')
        open(test, 'w').write(LAYOUT_SELFTEST_SOURCE)
        theirs = clang_record_layouts(test, ['-std=c++17'], clang, cache=False)
    failed = 0
    want = collections.Counter()
    for text, expected in LAYOUT_SELFTEST_CLAIMS:
        want[expected] += 1
        counts, _ = compare_layouts(layout_claims(text, test), theirs or {}, 'claims.cpp', set())
        if counts != collections.Counter({expected: 1}):
            failed += 1
            print(f'cxx-sema selftest: FAILED: layout claim {text.splitlines()[0]!r}: {dict(counts)}, '
                  f'expected {expected}')
    if failed:
        return 1
    print(f'cxx-sema selftest: ok (layouts: {want["verified"]} verified, {want["refuted"]} refuted, '
          f'{want["unchecked"]} not checkable, as expected)')
    return 0


def clang_defined(test, opts, clang):
    """the symbols clang -O0's object of the test defines (cached), or None"""
    cwd, name = os.path.dirname(test), os.path.basename(test)

    def compile_and_list():
        with tempfile.TemporaryDirectory() as d:
            obj = os.path.join(d, 'out.o')
            # the driver's -mconstructor-aliases for Linux: an inline
            # constructor's complete object variant is an alias of the base one
            r = cxxparse.run([clang, '-cc1'] + TARGET + ALIASES + ['-x', 'c++'] + opts +
                             ['-emit-obj', '-o', obj, name], cwd, timeout=60)
            if r[0] != 0 or not os.path.exists(obj):
                return None
            nm = os.path.join(os.path.dirname(clang), 'llvm-nm')
            listed = cxxparse.run([nm, '--defined-only', '--just-symbol-name', obj], d)
            if listed[0] != 0:
                return None
            return listed[1].split()
    return cxxparse.cached('symbols' + ''.join(TARGET + ALIASES), clang, test, opts, compile_and_list)


def clang_references(test, opts, clang):
    """{(line, column): {(file, line, column), ...}}: per place in the test
    file where clang's AST has a DeclRefExpr or a MemberExpr, the places of
    redeclaration chains of what it refers to, first declaration first
    (cached)"""
    cwd, name = os.path.dirname(test), os.path.basename(test)

    def compute():
        r = cxxparse.run([clang, '-cc1'] + TARGET + ['-x', 'c++'] + opts +
                         ['-fsyntax-only', '-ast-dump=json', name], cwd, timeout=120)
        if r[0] == 'timeout' or not r[1].strip():
            return None
        try:
            root = json.loads(r[1])
        except ValueError:
            return None
        # clang writes a place's file and line only where they change from
        # the place written before it: the walk is in the order written
        state = {'file': None, 'line': None}

        def place(loc):
            """the place of a location object, None in a macro or invalid"""
            if not isinstance(loc, dict) or not loc:
                return None
            if 'spellingLoc' in loc or 'expansionLoc' in loc:
                place(loc.get('spellingLoc'))
                place(loc.get('expansionLoc'))
                return None
            if 'file' in loc:
                state['file'] = loc['file']
            if 'line' in loc:
                state['line'] = loc['line']
            if 'col' not in loc or state['file'] is None:
                return None
            return (os.path.normpath(state['file']), state['line'], loc['col'])

        declared, previous, uses = {}, {}, []
        stack = [iter([root])]
        while stack:
            try:
                node = next(stack[-1])
            except StopIteration:
                stack.pop()
                continue
            if isinstance(node, list):
                stack.append(iter(node))
                continue
            if not isinstance(node, dict):
                continue
            here = end = None
            children = []
            for key, value in node.items():
                if key == 'loc':
                    here = place(value)
                elif key == 'range' and isinstance(value, dict):
                    place(value.get('begin'))
                    end = place(value.get('end'))
                elif isinstance(value, (dict, list)) and key not in ('referencedDecl', 'type'):
                    children.append(value)
            kind = node.get('kind', '')
            if kind.endswith('Decl') and 'id' in node and here:
                declared[node['id']] = here
                if 'previousDecl' in node:
                    previous[node['id']] = node['previousDecl']
            if kind == 'DeclRefExpr' and end and isinstance(node.get('referencedDecl'), dict):
                uses.append((end, node['referencedDecl'].get('id')))
            elif kind == 'MemberExpr' and end and node.get('referencedMemberDecl'):
                uses.append((end, node['referencedMemberDecl']))
            stack.append(iter(children))
        main = os.path.normpath(name)
        found = {}
        for (f, line, column), decl in uses:
            if f != main:
                continue
            chain, seen = [], set()
            while decl in declared and decl not in seen:
                seen.add(decl)
                chain.append(list(declared[decl]))
                decl = previous.get(decl)
            if chain:
                found.setdefault(f'{line}:{column}', []).append(chain[::-1])
        return found
    return cxxparse.cached('lookup-chain' + ''.join(TARGET), clang, test, opts, compute)


def check_lookup_claims(test, opts, clang, vpp, error_lines, unanalysed_lines):
    """-fcxx-dump=lookup-claims against clang_references: a Counter, and the
    refuted.  A name whose entity clang declared first in a declaration we
    did not analyse (unsupported, ill-formed, not parsed) and we first at a
    later declaration of clang's chain is not checkable"""
    cwd, name = os.path.dirname(test), os.path.basename(test)
    counts, refuted = collections.Counter(), []
    r = ours(vpp, 'lookup-claims', opts, test)
    if r[0] not in (0, 1):
        return counts, refuted
    claims = []
    for line in r[1].split('\n'):
        parts = line.split('\t')
        if len(parts) == 5 and os.path.normpath(parts[0]) == os.path.normpath(name):
            row, column = map(int, parts[1].split(':'))
            f, drow, dcolumn = parts[4].rsplit(':', 2)
            claims.append((row, column, parts[2], parts[3], (os.path.normpath(f), int(drow), int(dcolumn))))
    if not claims:
        return counts, refuted
    lines = open(test, 'rb').read().decode('latin-1').split('\n')
    if cxxparse.has_line_directive(lines):
        counts['unchecked'] += len(claims)
        return counts, refuted
    theirs = clang_references(test, opts, clang)
    if theirs is None:
        counts['unchecked'] += len(claims)
        return counts, refuted
    for row, column, spelled, kind, declared in claims:
        chains = theirs.get(f'{row}:{column}')
        found = [c[0] for c in chains or []]
        later = [c[:c.index(list(declared))] for c in chains or [] if list(declared) in c[1:]]
        if not found or row in error_lines:
            counts['unchecked'] += 1
        elif list(declared) in found:
            counts['verified'] += 1
        elif any(all(d[0] == os.path.normpath(name) and d[1] in unanalysed_lines for d in earlier)
                 for earlier in later):
            counts['unchecked'] += 1
        else:
            counts['refuted'] += 1
            refuted.append(f'{row}:{column}: `{spelled}` finds the {kind} declared at '
                           f'{declared[1]}:{declared[2]}, clang the one at '
                           f'{", ".join(f"{d[1]}:{d[2]}" for d in found[:2])}')
    return counts, refuted


def check_symbols(test, opts, clang, vpp):
    """-fcxx-dump=symbols against the symbols clang -O0's object defines:
    a Counter, and the refuted"""
    cwd, name = os.path.dirname(test), os.path.basename(test)
    counts, refuted = collections.Counter(), []
    said = ours(vpp, 'symbols', opts, test)
    if said[0] not in (0, 1) or not said[1].strip():
        return counts, refuted
    defined = clang_defined(test, opts, clang)
    if defined is None:
        return counts, refuted
    defined = set(defined)
    for line in said[1].splitlines():
        fields = line.split('\t')
        if fields[0] == 'unsupported':
            counts['unsupported'] += 1
            continue
        if len(fields) != 3 or fields[1] != 'defined':
            continue
        symbol, linkage = fields[0], fields[2]
        if symbol in defined:
            counts['verified'] += 1
        elif linkage != 'external':
            counts['not_emitted'] += 1
        else:
            counts['refuted'] += 1
            refuted.append(f'{symbol} is not a symbol clang -O0 defines')
    return counts, refuted


# the driver's message when lowering has no rule (lower/compile.cpp)
NOT_LOWERED = re.compile(r'^[^\n]*:\d+:\d+: error: (?:the rules of (\[[^\]]*\]) are not implemented yet|'
                         r'C\+\+ code generation does not handle (.*) yet)$', re.M)


def object_symbols(nm, obj, cwd):
    """the functions and variables an object defines, without the
    assembler's local labels and mapping symbols"""
    listed = cxxparse.run([nm, '--defined-only', '--just-symbol-name', obj], cwd)
    if listed[0] != 0:
        return None
    return {s for s in listed[1].split() if not s.startswith(('$', '.L'))}


def check_lowering(test, opts, clang, vpp):
    """vpp -S -O0 on a file clang compiles: the assembly assembles and
    the object defines what clang -O0's does.  A Counter (lowered, refuted,
    incomplete: a declaration not well-formed for us, or the reason
    lowering stopped), and the refuted"""
    cwd, name = os.path.dirname(test), os.path.basename(test)
    counts, refuted = collections.Counter(), []
    ours = cxxparse.run([vpp, '-x', 'c++', '-nostdinc', '--target=' + cxxparse.TRIPLE[1], '-S', '-O0', '-o', '-'] +
                        opts + [name], cwd, timeout=60)
    if ours[0] not in (0, 1):
        refuted.append(f'vpp -S: status {ours[0]}: {ours[2].strip().splitlines()[-1:]}')
        counts['refuted'] += 1
        return counts, refuted
    if ours[0] == 1:
        m = NOT_LOWERED.search(ours[2])
        counts['not: ' + ((m.group(1) or m.group(2)) if m else 'an error of our parser or analysis')] += 1
        return counts, refuted
    defined = clang_defined(test, opts, clang)
    if defined is None:
        return counts, refuted

    def assemble():
        """[error or None, the symbols the object defines or None]: a
        function of the assembly's bytes and clang, kept by cached()"""
        with tempfile.TemporaryDirectory() as d:
            asm, obj = os.path.join(d, 'out.s'), os.path.join(d, 'out.o')
            with open(asm, 'w') as f:
                f.write(ours[1])
            r = cxxparse.run([clang, '-cc1as', '-triple', 'aarch64-unknown-linux-gnu', '-filetype', 'obj',
                              '-o', obj, asm], d, timeout=60)
            if r[0] != 0:
                return [f'the assembly of vpp -S does not assemble: {r[2].strip().splitlines()[:1]}', None]
            made = object_symbols(os.path.join(os.path.dirname(clang), 'llvm-nm'), obj, d)
            if made is None:
                return [None, None]
            return [None, sorted(made)]
    digest = hashlib.sha256(ours[1].encode()).hexdigest()
    why, made = cxxparse.cached('assembled-symbols:' + digest, clang, test, opts, assemble)
    if why:
        counts['refuted'] += 1
        refuted.append(why)
        return counts, refuted
    if made is None:
        return counts, refuted
    made = set(made)
    expected = {s for s in defined if not s.startswith(('$', '.L'))}
    if made != expected:
        counts['refuted'] += 1
        refuted.append(f'vpp -S defines {sorted(made - expected)[:3]} clang -O0 does not, '
                       f'not {sorted(expected - made)[:3]}')
        return counts, refuted
    counts['lowered'] += 1
    return counts, refuted


def check(test, opts, clang, vpp):
    cwd = os.path.dirname(test)
    name = os.path.basename(test)
    res = {'test': test, 'opts': opts, 'fail': [], 'verdict': collections.Counter(),
           'unsupported': collections.Counter(), 'types': collections.Counter()}
    analysed = ours(vpp, 'analysis,check-tree', opts, test)
    if cxxpp.unsupported_edition(analysed[0], analysed[2]):
        res['skip'] = analysed[2].strip()
        return res
    if analysed[0] not in (0, 1):
        res['fail'].append(('crash', f'status {analysed[0]}: {analysed[2].strip().splitlines()[-1:]}'))
        return res
    c = cxxparse.cached('diagnostics' + ''.join(TARGET), clang, test, opts, lambda: list(
        cxxparse.run([clang, '-cc1'] + TARGET + ['-x', 'c++'] + opts +
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
    # an error in an instantiation is clang's at the template's line; the
    # use that asked for the instantiation has its note ("in instantiation
    # of ... requested here"), and that declaration is rejected too
    # ([temp.inst]/1): a note of an error's, not of a warning's
    last_is_error = False
    for m in CLANG_DIAGNOSTIC.finditer(c[2]):
        if m.group(3) != 'note':
            last_is_error = m.group(3) in ('error', 'fatal error')
        elif last_is_error and m.group(1) == name and INSTANTIATION_NOTE.search(m.group(4)):
            error_lines.add(int(m.group(2)))
    # a fatal error (-Wfatal-errors, a missing include) ends clang's
    # analysis there: what follows it is not compared
    fatal = re.search(r'^[^:\n]+:(\d+):\d+: fatal error:', c[2], re.M)
    fatal_line = int(fatal.group(1)) if fatal else None
    known = KNOWN.get(relative(test), set())
    rejected = []
    unanalysed_lines = set()
    # the lines of the declarations clang has an error in
    rejected_lines = set()
    for line in analysed[1].splitlines():
        if line.startswith('tree '):
            res['fail'].append(('tree', line[5:]))
            continue
        m = ANALYSIS_LINE.match(line)
        if not m:
            res['fail'].append(('crash', f'-dump-analysis: {line!r}'))
            break
        if m.group(2) != name or m.group(5) != name:
            continue
        first, last, result = int(m.group(3)), int(m.group(6)), m.group(8)
        lines = range(first, last + 1)
        if result != 'well-formed':
            unanalysed_lines.update(lines)
        if any(n in error_lines or n in syntax_lines for n in lines):
            rejected_lines.update(lines)
        if fatal_line is not None and first > fatal_line:
            res['verdict']['after_fatal'] += 1
        elif result == 'not-parsed' or any(n in syntax_lines for n in lines):
            res['verdict']['not_parsed'] += 1
        elif result == 'unsupported':
            res['verdict']['unsupported'] += 1
            res['unsupported'][m.group(9)] += 1
            res.setdefault('counted', []).append((first, int(m.group(4))))
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
                res.setdefault('accepted', []).append(first)
    every_error_line = set()
    for m in CLANG_ERROR.finditer(c[2]):
        if m.group(1) == name:
            every_error_line.add(int(m.group(2)))
    cxxparse.check_claims(test, opts, clang, vpp, res, every_error_line, expression_output=None,
                          declaration_output='declaration-claims',
                          target=TARGET[len(cxxparse.TRIPLE):])
    res['types'], refuted = check_type_claims(test, opts, clang, type_claims(vpp, opts, test))
    for r in refuted:
        res['fail'].append(('types', r))
    res['lookup'], refuted = check_lookup_claims(test, opts, clang, vpp, every_error_line,
                                                    unanalysed_lines)
    for r in refuted:
        res['fail'].append(('lookup', r))
    res['layout'], refuted = check_layout_claims(test, opts, clang, vpp, rejected_lines)
    for r in refuted:
        res['fail'].append(('layout', r))
    if c[0] == 0 and not CLANG_ERROR.search(c[2]):
        res['symbols'], refuted = check_symbols(test, opts, clang, vpp)
        for r in refuted:
            res['fail'].append(('symbols', r))
        res['lowering'], refuted = check_lowering(test, opts, clang, vpp)
        for r in refuted:
            res['fail'].append(('lower', r))
    if rejected:
        res['fail'].append(('verdict', f'ill-formed for us, accepted by clang: line {rejected[0]}'
                                       f' ({len(rejected)} declarations)'))
    return res


# --why: the root causes of the unsupported declarations, by how many
# counted declarations each blocks (doc/PLAN-cxx-unsupported.md, step 0)
UNSUPPORTED_LINE = re.compile(r'^\[([^\]]*)\] (\S+) (.*?):(\d+):(\d+)\t(.*?)\t(.*)$')
HELD = re.compile(r'^(\S+) is declared by a declaration (that is not parsed|that is ill-formed|with no rules yet)$')
INSTANTIATION = re.compile(r'^instantiation refused: \[([^\]]*)\] (\S+:\d+) ?(.*)$')
WHY_DEPTH = 8
# a refusal that names the declaration (not analysed) it depends on
HELD_AT = re.compile(r'declared by the declaration at (.*?):(\d+):(\d+), not analysed')


def unwrap_instantiation(sub, detail, site):
    """an instantiation's refusal is its own analysis's: (SUB, DETAIL, SITE)
    of the innermost failure ("instantiation refused: [SUB] FILE:LINE
    DETAIL"; the detail itself may be another instantiation's)"""
    while True:
        i = INSTANTIATION.match(detail)
        if not i:
            return sub, detail, site
        sub, site, detail = i.group(1), i.group(2), i.group(3)
        site = os.path.relpath(site, R) if os.path.isabs(site) else site


def why_key(sub, detail, site):
    """(subclause, vcc's site, the detail with the name generic unless it is
    a builtin's)"""
    site = os.path.relpath(site, R) if os.path.isabs(site) else site
    sub, detail, site = unwrap_instantiation(sub, detail, site)
    if detail.startswith('evaluator '):
        # "evaluator met L1234 (KIND)": the line is the cause
        return f'[{sub}] {site} {detail}'
    m = re.match(r'^(\S+) (.*)$', detail)
    # (an extension's name, an attribute's or a builtin's, is the cause itself)
    if m and not m.group(2).startswith('is a compiler builtin') and not sub.startswith('ext.'):
        detail = 'NAME ' + m.group(2)
    detail = re.sub(r'\d+', 'N', detail)
    return f'[{sub}] {site} {detail}'.rstrip()


def source_lines(path, cache={}):
    if path not in cache:
        try:
            cache[path] = open(path, errors='replace').read().split('\n')
        except OSError:
            cache[path] = []
    return cache[path]


def declaration_text(cwd, d):
    lines = source_lines(os.path.join(cwd, d['path']))
    return '\n'.join(lines[d['l1'] - 1:d['l2']])


def root_causes(test, opts, vpp, counted):
    """{root key: [count, example]} for the counted unsupported declarations
    of one run"""
    cwd, name = os.path.dirname(test), os.path.basename(test)
    ours = cxxparse.run(cxxpp.vpp_command(vpp, 'analysis,unsupported', opts, name,
                                          ['-fcxx-debug-fallbacks']), cwd)
    decls, refusals = [], []
    # why the parser kept a declaration as tokens or erroneous: its fallback
    # line, else its first error, by (file, line)
    parse_why = {}
    for line in (ours[2] or '').splitlines():
        f = re.match(r'^vcc: declaration at (.*?):(\d+):\d+: (.*)$', line)
        if not f:
            f = re.match(r'^(.*?):(\d+):\d+: error: (.*)$', line)
        if f:
            reason = re.sub(r' at \d+:\d+', '', f.group(3))
            if not reason.startswith('later step'):
                reason = re.sub(r'`[^`]*`', '`X`', reason)
            parse_why.setdefault((f.group(1), int(f.group(2))), reason)
    for line in ours[1].splitlines():
        u = UNSUPPORTED_LINE.match(line)
        if u:
            refusals.append({'sub': u.group(1), 'node': u.group(2), 'path': u.group(3),
                             'pos': (int(u.group(4)), int(u.group(5))),
                             'detail': u.group(6), 'site': u.group(7)})
            continue
        m = ANALYSIS_LINE.match(line)
        if m:
            decls.append({'kind': m.group(1), 'path': m.group(2), 'l1': int(m.group(3)),
                          'c1': int(m.group(4)), 'l2': int(m.group(6)), 'c2': int(m.group(7)),
                          'result': m.group(8), 'sub': m.group(9)})

    def first_refusal(d):
        lo, hi = (d['l1'], d['c1']), (d['l2'], d['c2'])
        inside = [r for r in refusals if r['path'] == d['path'] and lo <= r['pos'] <= hi]
        same = [r for r in inside if r['sub'] == d['sub']]
        if same:
            return same[0]
        if inside:
            return inside[0]
        return None

    def holder(i, held):
        """the last declaration before decls[i] that is not well-formed and
        has the name"""
        word = re.compile(r'\b' + re.escape(held) + r'\b')
        for j in range(i - 1, -1, -1):
            if decls[j]['result'] != 'well-formed' and word.search(declaration_text(cwd, decls[j])):
                return j
        return None

    def root(i, depth):
        d = decls[i]
        if d['result'] != 'unsupported':
            reason = ''
            if d['result'] == 'not-parsed':
                for n in range(d['l1'], d['l2'] + 1):
                    if (d['path'], n) in parse_why:
                        reason = ' ' + parse_why[(d['path'], n)]
                        break
            return f'{d["result"]}: {d["kind"]}{reason}', d
        r = first_refusal(d)
        if r is None:
            return f'[{d["sub"]}] (no refusal line in the declaration)', d
        at = HELD_AT.search(r['detail'])
        if at and depth < WHY_DEPTH:
            pos = (int(at.group(2)), int(at.group(3)))
            for j in range(i - 1, -1, -1):
                dj = decls[j]
                if os.path.basename(dj['path']) == os.path.basename(at.group(1)) and \
                        (dj['l1'], dj['c1']) <= pos <= (dj['l2'], dj['c2']):
                    return root(j, depth + 1)
        h = HELD.match(unwrap_instantiation(r['sub'], r['detail'], r['site'])[1])
        if h and depth < WHY_DEPTH:
            j = holder(i, h.group(1))
            if j is not None:
                return root(j, depth + 1)
        return why_key(r['sub'], r['detail'], r['site']), d

    out = {}
    index = {(d['l1'], d['c1']): i for i, d in enumerate(decls) if d['path'] == name}
    for pos in counted:
        i = index.get(pos)
        if i is None:
            key, d = '(declaration not in the dump)', None
        else:
            key, d = root(i, 0)
        e = out.setdefault(key, [0, None])
        e[0] += 1
        if e[1] is None and d is not None:
            e[1] = f'{relative(test)}:{pos[0]} {" ".join(opts[:2])} (root {os.path.basename(d["path"])}:{d["l1"]})'
    return out


def why_report(results, n):
    total, examples = collections.Counter(), collections.defaultdict(list)
    for r in results:
        for key, (count, example) in r.get('why', {}).items():
            total[key] += count
            test = (example or '').split(':')[0]
            if example and len(examples[key]) < 3 and all(not e.startswith(test + ':') for e in examples[key]):
                examples[key].append(example)
    path = os.path.join(R, 'work', 'cxx-sema-why.json')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        json.dump([{'key': k, 'declarations': c, 'examples': examples[k]}
                   for k, c in total.most_common()], f, indent=1)
    print(f'root causes ({len(total)} keys, {sum(total.values())} declarations; {relative(path)}):')
    for k, c in total.most_common(n):
        print(f'{c:7d}  {k}')
        for e in examples[k]:
            print(f'           {e}')


def main():
    if sys.argv[1:] == ['selftest']:
        return selftest(cxxpp.find_clang()) | layout_selftest(cxxpp.find_clang())
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir')
    ap.add_argument('--unsupported', type=int, default=10, metavar='N',
                    help='how many subclauses of the work list to print')
    ap.add_argument('--check', action='store_true',
                    help=f'fail on a failure not listed in {relative(KNOWN_FAILURES)}, '
                         'or on a listed one that passes now')
    ap.add_argument('--bless', action='store_true',
                    help=f'rewrite {relative(KNOWN_FAILURES)} with the current failures (a full run)')
    ap.add_argument('--suites', metavar='NAME,...',
                    help='clang test directories to run (default CXX,SemaCXX,'
                         'SemaTemplate; "all": also CodeGenCXX, Parser, Sema; '
                         '--check and --bless are of the default only)')
    ap.add_argument('--why', type=int, nargs='?', const=40, metavar='N',
                    help='also the root causes of the unsupported declarations, top N '
                         '(work/cxx-sema-why.json; doc/PLAN-cxx-unsupported.md)')
    ap.add_argument('patterns', nargs='*')
    a = ap.parse_args()
    if a.suites:
        if a.check or a.bless:
            sys.exit('cxx-sema: --check and --bless are of the default suites')
        globals()['SUITES'] = (WIDER_SUITES if a.suites == 'all'
                               else tuple(a.suites.split(',')))
    clang = cxxpp.find_clang()
    vpp = cxxpp.find_vpp(a.bin_dir or os.environ.get('VCC_BIN'))
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
    # processes, not threads: the claims' checks are Python, which threads
    # do not run in parallel
    with concurrent.futures.ProcessPoolExecutor(a.j) as ex:
        results = list(ex.map(check, [j[0] for j in jobs], [j[1] for j in jobs],
                              [clang] * len(jobs), [vpp] * len(jobs), chunksize=8))
        if a.why is not None:
            counted = [r for r in results if r.get('counted')]
            whys = ex.map(root_causes, [r['test'] for r in counted], [r['opts'] for r in counted],
                          [vpp] * len(counted), [r['counted'] for r in counted], chunksize=8)
            for r, w in zip(counted, whys):
                r['why'] = w
    verdict, unsupported, types = collections.Counter(), collections.Counter(), collections.Counter()
    declared, symbols, lowering = collections.Counter(), collections.Counter(), collections.Counter()
    lookup, layout = collections.Counter(), collections.Counter()
    crashes = failures = skipped = 0
    seen = set()
    failed = set()
    for r in results:
        seen.add(r['test'])
        verdict.update(r['verdict'])
        unsupported.update(r['unsupported'])
        types.update(r['types'])
        declared.update(r.get('declarators', {}))
        symbols.update(r.get('symbols', {}))
        lookup.update(r.get('lookup', {}))
        layout.update(r.get('layout', {}))
        lowering.update(r.get('lowering', {}))
        kinds = {k for k, _ in r['fail']}
        crashes += 'crash' in kinds
        failures += bool(kinds)
        skipped += bool(r.get('skip'))
        for k, why in r['fail']:
            failed.add(failure_key(r, k, why))
        if r['fail'] or a.v:
            for k, why in r['fail'] or [('ok', '')]:
                print(f'{k:7} {relative(r["test"])} {" ".join(r["opts"])}: {why}')
        if a.v and r.get('accepted'):
            # well-formed for us, rejected by clang: not a failure (a rule
            # analysis does not check yet), listed to see which
            print(f'accepts {relative(r["test"])} {" ".join(r["opts"])}: '
                  f'lines {" ".join(str(n) for n in r["accepted"])}')
    runs = len(jobs)
    print(f'{runs} runs of {len(seen)} tests ({unrunnable} not run, {skipped} skipped): '
          f'no crash {runs - crashes}/{runs}')
    print(f'declarations: {verdict["agree"]} as clang, {verdict["we_reject"]} we reject and clang accepts '
          f'({verdict["known"]} known differences), {verdict["we_accept"]} we accept and clang rejects, '
          f'{verdict["unsupported"]} unsupported, {verdict["not_parsed"]} not parsed, '
          f'{verdict["after_fatal"]} after a fatal error')
    print(f'declared types: {declared["verified"]} verified, {declared["overload"]} as one of a name\'s overloads, '
          f'{declared["refuted"]} refuted, {declared["rejected"]} in declarations clang rejects, '
          f'{declared["unchecked"]} not checkable')
    print(f'types: {types["verified"]} claims verified, {types["refuted"]} refuted, '
          f'{types["unchecked"]} not checkable; constants: {types["value_verified"]} verified, '
          f'{types["value_refuted"]} refuted')
    print(f'lookup: {lookup["verified"]} names found as clang finds them, {lookup["refuted"]} refuted, '
          f'{lookup["unchecked"]} not checkable')
    print(f'symbols: {symbols["verified"]} verified, {symbols["refuted"]} refuted, '
          f'{symbols["not_emitted"]} not emitted by clang, {symbols["unsupported"]} unsupported')
    print(f'layout: {layout["verified"]} classes laid out as clang lays them out, {layout["refuted"]} refuted, '
          f'{layout["unchecked"]} not checkable')
    reasons = sorted(((n, k[5:]) for k, n in lowering.items() if k.startswith('not: ')), reverse=True)
    print(f'lowering: {lowering["lowered"]} files lowered as clang -O0 compiles them, '
          f'{lowering["refuted"]} refuted, {sum(n for n, _ in reasons)} not lowered'
          + (': ' + ', '.join(f'{why} {n}' for n, why in reasons[:a.unsupported]) if reasons else ''))
    if unsupported and a.unsupported:
        top = ', '.join(f'[{label}] {n}' for label, n in unsupported.most_common(a.unsupported))
        print(f'unsupported by subclause ({len(unsupported)}): {top}')
    if a.why is not None:
        why_report(results, a.why)
    if not a.patterns:
        # every declaration we accept and clang rejects, one line each:
        # a step's new ones are the diff against the previous run's file
        path = os.path.join(R, 'work', 'cxx-sema-accepted.txt')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            for line in sorted(f'{relative(r["test"])}:{first} {" ".join(r["opts"])}'
                               for r in results for first in r.get('accepted', [])):
                f.write(line + '\n')
    if missing:
        print(f'not fetched: clang/test/{", clang/test/".join(missing)} (bench/fetch-testsuites.sh llvm)')
    metrics = {
        'runs': runs, 'tests': len(seen), 'crashes': crashes,
        'decl_agree': verdict['agree'], 'decl_we_reject': verdict['we_reject'],
        'decl_we_accept': verdict['we_accept'], 'decl_unsupported': verdict['unsupported'],
        'decl_not_parsed': verdict['not_parsed'],
        'declared_verified': declared['verified'], 'declared_refuted': declared['refuted'],
        'types_verified': types['verified'], 'types_refuted': types['refuted'],
        'constants_verified': types['value_verified'], 'constants_refuted': types['value_refuted'],
        'lookup_verified': lookup['verified'], 'lookup_refuted': lookup['refuted'],
        'symbols_verified': symbols['verified'], 'symbols_refuted': symbols['refuted'],
        'layout_verified': layout['verified'], 'layout_refuted': layout['refuted'],
        'lowering_lowered': lowering['lowered'], 'lowering_refuted': lowering['refuted'],
        'known_failures': len(failed),
        'unsupported_by_subclause': dict(sorted(unsupported.items())),
    }
    if a.bless:
        if a.patterns:
            print('--bless needs a full run (no PATTERN)')
            return 1
        with open(KNOWN_FAILURES, 'w') as f:
            f.write(KNOWN_HEADER)
            for key in sorted(failed):
                f.write(key + '\n')
        print(f'{relative(KNOWN_FAILURES)}: {len(failed)} known failures')
        with open(METRICS, 'w') as f:
            json.dump(metrics, f, indent=1, sort_keys=True)
            f.write('\n')
        print(f'{relative(METRICS)}: written')
        return 0
    if a.check:
        rc = check_known(failed, {relative(t) for t in seen})
        if not a.patterns:
            rc |= check_metrics(metrics)
        return rc
    return 1 if failures else 0


def check_metrics(now):
    """The numbers against src/test/cxx/sema/clang-metrics.json, as a table (also in the
    CI step summary): one that gets worse fails, the rest is progress to review in the diff."""
    if not os.path.exists(METRICS):
        print(f'no {relative(METRICS)}: bench/cxx-sema.py --bless')
        return 1
    base = json.load(open(METRICS))
    rows, worse = [], []
    for k in HIGHER_IS_BETTER + LOWER_IS_BETTER + ('decl_unsupported', 'decl_not_parsed', 'known_failures'):
        b, n = base.get(k, 0), now[k]
        mark = ''
        if abs(n - b) <= TOLERANCE and k not in ('known_failures', 'crashes'):
            pass
        elif k in HIGHER_IS_BETTER and n < b or k in LOWER_IS_BETTER and n > b:
            mark = 'WORSE'
            worse.append(k)
        elif k in HIGHER_IS_BETTER and n > b or k in LOWER_IS_BETTER and n < b:
            mark = 'better'
        elif k in ('decl_unsupported', 'decl_not_parsed', 'known_failures') and n != b:
            mark = 'better' if n < b else 'more'
        rows.append((k, b, n, mark))
    sub_b, sub_n = base.get('unsupported_by_subclause', {}), now['unsupported_by_subclause']
    moved = sorted(((sub_n.get(k, 0) - sub_b.get(k, 0), k) for k in set(sub_b) | set(sub_n)
                    if sub_n.get(k, 0) != sub_b.get(k, 0)), key=lambda x: -abs(x[0]))
    lines = ['| metric | baseline | now | change |', '|---|---:|---:|---|']
    for k, b, n, mark in rows:
        lines.append(f'| {k} | {b} | {n} | {n - b:+d} {mark} |' if n != b else f'| {k} | {b} | {n} | |')
    if moved:
        lines += ['', 'unsupported by subclause (declarations whose first refusal it is), changed: '
                  + ', '.join(f'[{k}] {sub_b.get(k, 0)} -> {sub_n.get(k, 0)}' for _, k in moved[:12])
                  + (f' ... {len(moved)} changed' if len(moved) > 12 else '')]
    text = '\n'.join(lines)
    print(text)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a') as f:
            f.write('\n### cxx-sema against clang\'s C++ tests\n' + text + '\n')
    if worse:
        print(f'WORSE: {", ".join(worse)} (a fix that is worth it: --bless, saying why)')
        return 1
    if any(mark for _, _, _, mark in rows) or any(abs(d) > TOLERANCE for d, _ in moved):
        print(f'numbers changed: bench/cxx-sema.py --bless puts them in {relative(METRICS)}, '
              'so the PR shows the progress')
        return 1
    return 0


def failure_key(r, kind, why):
    """One failure as a line of the known-failures file: the kind, the test
    with its options and the reason, without line numbers of vcc's own source."""
    why = re.sub(r"(refused by vcc's [^ :]+):\d+ ", r'\1:N ', why)
    # a crash is a signal on one host and a timeout on another (a runaway recursion)
    why = re.sub(r'^status (-\d+|timeout)', 'status signal-or-timeout', why)
    return f'{kind} {relative(r["test"])} {" ".join(r["opts"])}: {why}'.replace('\n', ' ')


def check_known(failed, ran):
    """A failure that is not in the known list is a regression; a listed one
    that no longer happens is stale (xfail: remove it in the same commit)."""
    known = set()
    if os.path.exists(KNOWN_FAILURES):
        for line in open(KNOWN_FAILURES):
            line = line.rstrip('\n')
            if line and not line.startswith('#'):
                known.add(line)
    new = sorted(failed - known)
    # only an entry of a test that ran can be stale (a PATTERN runs a part)
    stale = sorted(k for k in known - failed if k.split(' ', 2)[1] in ran)
    for k in new:
        print(f'NEW    {k}')
    for k in stale:
        print(f'STALE  {k}')
    print(f'known failures: {len(known)} listed, {len(new)} new, {len(stale)} stale '
          f'(a fix removes its line: bench/cxx-sema.py --bless)')
    return 1 if new or stale else 0


if __name__ == '__main__':
    sys.exit(main())
