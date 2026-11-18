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


def type_claims(vpp, opts, test):
    """-fcxx-dump=type-claims: the claims about the test file itself"""
    cwd = os.path.dirname(test)
    r = cxxparse.run(cxxpp.vpp_command(vpp, 'type-claims', opts, os.path.basename(test)), cwd)
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
    ours = cxxparse.run(cxxpp.vpp_command(vpp, 'layout-claims', opts, name), cwd)
    if ours[0] not in (0, 1):
        return collections.Counter(), []
    claims = layout_claims(ours[1], test)
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
    r = cxxparse.run(cxxpp.vpp_command(vpp, 'lookup-claims', opts, name), cwd)
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
    ours = cxxparse.run(cxxpp.vpp_command(vpp, 'symbols', opts, name), cwd)
    if ours[0] not in (0, 1) or not ours[1].strip():
        return counts, refuted
    defined = clang_defined(test, opts, clang)
    if defined is None:
        return counts, refuted
    defined = set(defined)
    for line in ours[1].splitlines():
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
    with tempfile.TemporaryDirectory() as d:
        asm, obj = os.path.join(d, 'out.s'), os.path.join(d, 'out.o')
        with open(asm, 'w') as f:
            f.write(ours[1])
        r = cxxparse.run([clang, '-cc1as', '-triple', 'aarch64-unknown-linux-gnu', '-filetype', 'obj',
                          '-o', obj, asm], d, timeout=60)
        if r[0] != 0:
            counts['refuted'] += 1
            refuted.append(f'the assembly of vpp -S does not assemble: {r[2].strip().splitlines()[:1]}')
            return counts, refuted
        made = object_symbols(os.path.join(os.path.dirname(clang), 'llvm-nm'), obj, d)
    if made is None:
        return counts, refuted
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
    ours = cxxparse.run(cxxpp.vpp_command(vpp, 'analysis,check-tree', opts, name), cwd)
    if cxxpp.unsupported_edition(ours[0], ours[2]):
        res['skip'] = ours[2].strip()
        return res
    if ours[0] not in (0, 1):
        res['fail'].append(('crash', f'status {ours[0]}: {ours[2].strip().splitlines()[-1:]}'))
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
    # a fatal error (-Wfatal-errors, a missing include) ends clang's
    # analysis there: what follows it is not compared
    fatal = re.search(r'^[^:\n]+:(\d+):\d+: fatal error:', c[2], re.M)
    fatal_line = int(fatal.group(1)) if fatal else None
    known = KNOWN.get(relative(test), set())
    rejected = []
    unanalysed_lines = set()
    # the lines of the declarations clang has an error in
    rejected_lines = set()
    for line in ours[1].splitlines():
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


def main():
    if sys.argv[1:] == ['selftest']:
        return selftest(cxxpp.find_clang()) | layout_selftest(cxxpp.find_clang())
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir')
    ap.add_argument('--unsupported', type=int, default=10, metavar='N',
                    help='how many subclauses of the work list to print')
    ap.add_argument('patterns', nargs='*')
    a = ap.parse_args()
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
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        results = list(ex.map(lambda j: check(j[0], j[1], clang, vpp), jobs))
    verdict, unsupported, types = collections.Counter(), collections.Counter(), collections.Counter()
    declared, symbols, lowering = collections.Counter(), collections.Counter(), collections.Counter()
    lookup, layout = collections.Counter(), collections.Counter()
    crashes = failures = skipped = 0
    seen = set()
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
    if missing:
        print(f'not fetched: clang/test/{", clang/test/".join(missing)} (bench/fetch-testsuites.sh llvm)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
