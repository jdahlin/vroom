#!/usr/bin/env python3
"""The C++ front end's preprocessor (src/frontend/cplusplus/cpp, through
cxxlex) against clang, on clang's preprocessor tests and ours.

Tests: clang/test/Preprocessor/*.{c,cpp} and clang/test/CXX/cpp of the
llvm-project checkout bench/fetch-testsuites.sh llvm makes (work/testsuites,
of the main checkout from a git worktree), run as C++ (a C test with -x c++),
and src/test/cxx/cpp/spec (the standard's examples).  For each %clang_cc1
RUN line, with its preprocessor and language options:

  tokens  cxxlex -dump-tokens vs clang -cc1 -dump-tokens: phases 1-7, each
          token's kind, spelling, place (the macro invocation's for a
          replaced token), spelling place (not for clang's scratch space)
          and [StartOfLine]/[LeadingSpace]
  errors  the lines with an error, of clang's Lex and Common components
          (those the front end could issue), from both runs: [intro.compliance]/2
          asks for "at least one diagnostic message" for an ill-formed
          program, so where an error is, not its text, is compared; warnings
          are counted but do not fail a run
  diags   the diagnostics the front end issues somewhere (its own, already
          written): place, level and text as clang's, warnings included
  macros  cxxlex -E -dM vs clang -cc1 -E -dM: the macros defined at the end

Usage: bench/cxx-pp.py [-v] [-j N] [--bin-dir DIR] [--list] [PATTERN...]
Prints one line per failing run (all with -v) and a summary.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRONTEND = os.path.join(R, 'src', 'frontend', 'cplusplus')
SPEC = os.path.join(R, 'src', 'test', 'cxx', 'cpp')
TRIPLE = ['-triple', 'aarch64-unknown-linux-gnu']

# clang -cc1 options passed to both; options taking a value (kept or dropped)
KEEP = re.compile(r'^(-std=.*|-fms-extensions|-fms-compatibility|-f(no-)?char8_t|-f?(no-)?trigraphs|'
                  r'-f(no-)?dollars-in-identifiers|-pedantic(-errors)?|-W[^l].*|-w|-undef|'
                  r'-[DUI].+|--embed-dir=.*)$')
VALUE_KEEP = {'-D', '-U', '-I', '-isystem', '-iquote', '-idirafter'}
VALUE_DROP = {'-triple', '-x', '-o', '-include', '-verify', '-target-cpu', '-main-file-name', '-imacros',
              '-fmodule-name', '-fmodules-cache-path', '-ffile-prefix-map', '-target-feature', '-isysroot',
              '-iprefix', '-iwithprefix', '-F', '-ivfsoverlay', '-fmacro-prefix-map', '-std'}


def work_dir():
    w = os.path.join(R, 'work')
    if os.path.isdir(os.path.join(w, 'testsuites', 'llvm')):
        return w
    common = subprocess.run(['git', '-C', R, 'rev-parse', '--git-common-dir'], capture_output=True,
                            text=True).stdout.strip()
    return os.path.join(os.path.dirname(os.path.abspath(os.path.join(R, common))), 'work')


def find_clang():
    for c in ('clang-23', 'clang'):
        for d in os.environ.get('PATH', '').split(':'):
            if os.path.exists(os.path.join(d, c)):
                return os.path.join(d, c)
    sys.exit('cxx-pp: no clang')


def find_cxxlex(bin_dir):
    for d in ([bin_dir] if bin_dir else []) + [os.path.join(R, 'build', 'linux', 'src'), os.path.join(R, 'build', 'src')]:
        p = os.path.join(d, 'cxxlex')
        if os.path.exists(p):
            return p
    sys.exit('cxx-pp: no cxxlex; make -C src cxxlex')


def run_lines(path):
    """the distinct option sets of the %clang_cc1 RUN lines that are C++ or
    can be run as C++"""
    lines, cur = [], ''
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'RUN:(.*)$', line)
        if not m:
            continue
        cur += m.group(1).rstrip()
        if cur.endswith('\\'):
            cur = cur[:-1] + ' '
            continue
        lines.append(cur.strip())
        cur = ''
    out = []
    # a test that makes its inputs at run time (split-file, files under %t)
    # cannot run here
    if any('split-file' in l or re.search(r'%clang_cc1[^|]*%t', l) for l in lines):
        return None
    for line in lines:
        cmd = line.split('|')[0]
        if '%clang_cc1' not in cmd or 'not ' in cmd.split('%clang_cc1')[0].replace('not %', ''):
            pass
        if '%clang_cc1' not in cmd:
            continue
        args = cmd.replace('%S', '.').replace('%p', '.').split()
        args = args[args.index('%clang_cc1') + 1:]
        lang, opts, i = None, [], 0
        while i < len(args):
            a = args[i]
            if a in VALUE_KEEP and i + 1 < len(args):
                opts.append(a + args[i + 1])
                i += 2
                continue
            if a in VALUE_DROP:
                if a == '-x' and i + 1 < len(args):
                    lang = args[i + 1]
                i += 2
                continue
            if a.startswith('-x') and len(a) > 2:
                lang = a[2:]
            if any(a.startswith(p) for p in ('-fmodules', '-fobjc', '-fopenmp', '-fcuda', '-fsycl', '-cl-',
                                              '-fopencl', '-emit-pch', '-include-pch', '-fhlsl')):
                break
            if KEEP.match(a) and '%' not in a:
                opts.append(a)
            i += 1
        else:
            if lang and lang not in ('c++', 'c', 'c++-header', 'c-header'):
                continue
            stds = [o for o in opts if o.startswith('-std=')]
            if stds and not re.match(r'-std=(gnu|c)\+\+', stds[-1]):
                opts = [o for o in opts if not o.startswith('-std=')]
            if not any(o.startswith('-std=') for o in opts):
                opts.insert(0, '-std=gnu++17')  # clang -cc1's default for C++
            out.append(opts)
    seen, unique = set(), []
    for o in out:
        if tuple(o) not in seen:
            seen.add(tuple(o))
            unique.append(o)
    return unique


TOKEN_LINE = re.compile(r"^(\S+)\s+'(.*)'\s+Loc=<(.*?):(\d+):(\d+)(?: <Spelling=(.*?):(\d+):(\d+)>)?>(.*)$")


def parse_tokens(text):
    out = []
    for line in text.splitlines():
        m = TOKEN_LINE.match(line)
        if not m:
            continue
        name, spelling, f, row, col, sf, srow, scol, flags = m.groups()
        if name.startswith('annot_'):
            continue
        if name == 'eof':
            out.append(('eof',))
            continue
        f = os.path.normpath(f)
        spelled = '' if not sf or sf.startswith('<scratch') else f'{os.path.normpath(sf)}:{srow}:{scol}'
        fl = ' '.join(x for x in ('StartOfLine', 'LeadingSpace') if f'[{x}]' in flags)
        out.append((name, spelling, f'{f}:{row}:{col}', spelled, fl))
    return out


def lex_diagnostics(used_only=False):
    """regexes of the text of every diagnostic of clang's Lex and Common, or
    of those the front end issues somewhere"""
    used = set()
    for path in glob.glob(os.path.join(FRONTEND, '**', '*.cpp'), recursive=True):
        used.update(re.findall(r'diagnostic_id::(\w+)', open(path).read()))
    inc = open(os.path.join(FRONTEND, 'intro', 'diagnostic-messages.inc')).read()
    out = []
    for m in re.finditer(r'^DIAGNOSTIC\((\w+), (\w+), \w+, "[^"]*", "((?:[^"\\]|\\.)*)"\)$', inc, re.M):
        if m.group(2) == 'NOTE' or (used_only and m.group(1) not in used):
            continue
        fmt = bytes(m.group(3), 'utf-8').decode('unicode_escape')
        if fmt in ('%0',):  # #error's, #pragma message's: any text
            if not used_only:
                out.append(re.compile(r'.*'))
            continue
        out.append(re.compile(format_regex(fmt)))
    return out


def format_regex(fmt):
    out, i = '', 0
    while i < len(fmt):
        c = fmt[i]
        if c != '%':
            out += re.escape(c)
            i += 1
            continue
        m = re.match(r'%(\w*)(\{)?', fmt[i:])
        name = m.group(1).rstrip('0123456789')
        j = i + 1 + len(name)
        body = None
        if j < len(fmt) and fmt[j] == '{':
            depth, k = 1, j + 1
            while depth and k < len(fmt):
                depth += {'{': 1, '}': -1}.get(fmt[k], 0)
                k += 1
            body, j = fmt[j + 1:k - 1], k
        while j < len(fmt) and fmt[j].isdigit():
            j += 1
        if name == 'select' and body is not None:
            out += '(?:' + '|'.join(format_regex(a) for a in body.split('|')) + ')'
        elif name == 's':
            out += 's?'
        else:
            out += '.*?'
        i = j
    return out


DIAG_LINE = re.compile(r'^(.*?):(\d+):(\d+): (error|warning|fatal error): (.*?)(?: \[-W[^\]]*\])?$')


def parse_diagnostics(text, regexes):
    out = set()
    for line in text.splitlines():
        m = DIAG_LINE.match(line)
        if not m:
            continue
        f, row, col, level, msg = m.groups()
        if any(r.fullmatch(msg) for r in regexes):
            out.add((os.path.normpath(f), int(row), int(col), level, msg))
    return out


def run(cmd, cwd=None, timeout=20):
    if len(cmd) > 1 and cmd[1] == '-cc1':
        cmd = cmd[:2] + TRIPLE + cmd[2:]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, errors='replace', cwd=cwd, timeout=timeout)
        return p.stdout, p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        return '', 'TIMEOUT', -1


def first_difference(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f'#{i}: cxxlex {x} / clang {y}'
    if len(a) != len(b):
        i = min(len(a), len(b))
        return f'#{i}: cxxlex {a[i] if i < len(a) else "(end)"} / clang {b[i] if i < len(b) else "(end)"}'
    return ''


def check(test, opts, clang, cxxlex, regexes, issued):
    d, name = os.path.dirname(test), os.path.basename(test)
    lang = ['-x', 'c++'] if not name.endswith('.cpp') else []
    res = {}
    ours, ours_err, rc = run([cxxlex, '-dump-tokens'] + opts + [name], cwd=d)
    if rc == 2 and 'unsupported -std' in ours_err:
        return test, ' '.join(opts), None  # an edition the front end does not have
    if rc < 0 or rc > 1:
        res['tokens'] = f'cxxlex failed ({rc}): {ours_err[-200:]}'
        res['errors'] = res['diags'] = res['macros'] = 'not run'
        res['#warnings'] = 0
        return test, ' '.join(opts), res
    _, theirs, _ = run([clang, '-cc1', '-dump-tokens'] + lang + opts + [name], cwd=d)
    a, b = parse_tokens(ours), parse_tokens(theirs)
    res['tokens'] = first_difference(a, b) if a != b else ''
    mine, theirs_d = parse_diagnostics(ours_err, regexes), parse_diagnostics(theirs, regexes)
    errors = lambda d: {(f, row) for f, row, _, level, _ in d if level != 'warning'}
    missing, extra = sorted(errors(theirs_d) - errors(mine)), sorted(errors(mine) - errors(theirs_d))
    res['errors'] = '' if not missing and not extra else \
        f'clang only {len(missing)} {missing[:3]} cxxlex only {len(extra)} {extra[:3]}'
    warned = lambda d: {(f, row, msg) for f, row, _, level, msg in d if level == 'warning'}
    res['#warnings'] = len(warned(theirs_d) ^ warned(mine))
    own = lambda d: {x for x in d if any(r.fullmatch(x[4]) for r in issued)}
    missing, extra = sorted(own(theirs_d) - own(mine)), sorted(own(mine) - own(theirs_d))
    res['diags'] = '' if not missing and not extra else \
        f'missing {len(missing)} {missing[:2]} extra {len(extra)} {extra[:2]}'
    ours_m, _, _ = run([cxxlex, '-E', '-dM'] + opts + [name], cwd=d)
    theirs_m, _, _ = run([clang, '-cc1', '-E', '-dM'] + lang + opts + [name], cwd=d)
    x = sorted(l for l in ours_m.splitlines() if l.startswith('#define'))
    y = sorted(l for l in theirs_m.splitlines() if l.startswith('#define'))
    if x == y:
        res['macros'] = ''
    else:
        sx, sy = set(x), set(y)
        res['macros'] = f'cxxlex only {sorted(sx - sy)[:2]} clang only {sorted(sy - sx)[:2]}'
    return test, ' '.join(opts), res


def known_differences():
    path = os.path.join(SPEC, 'known-differences.txt')
    out = {}
    if os.path.exists(path):
        for line in open(path):
            if line.strip() and not line.startswith('#'):
                test, kinds, _ = [f.strip() for f in line.split('|', 2)]
                out[test] = set(kinds.split())
    return out


def tests():
    llvm = os.path.join(work_dir(), 'testsuites', 'llvm', 'clang', 'test')
    found = sorted(glob.glob(os.path.join(llvm, 'Preprocessor', '*.c')) +
                   glob.glob(os.path.join(llvm, 'Preprocessor', '*.cpp')) +
                   glob.glob(os.path.join(llvm, 'CXX', 'cpp', '**', '*.cpp'), recursive=True))
    found += sorted(glob.glob(os.path.join(SPEC, 'spec', '*.cpp')))
    return found, llvm


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir')
    ap.add_argument('patterns', nargs='*')
    args = ap.parse_args()
    clang, cxxlex = find_clang(), find_cxxlex(args.bin_dir or os.environ.get('VCC_BIN'))
    found, llvm = tests()
    if args.patterns:
        found = [t for t in found if any(p in t for p in args.patterns)]
    regexes = lex_diagnostics()
    issued = lex_diagnostics(used_only=True)
    runnable = {t: run_lines(t) for t in found}
    unrunnable = [t for t, r in runnable.items() if r is None]
    found = [t for t in found if runnable[t] is not None]
    jobs = [(t, o) for t in found for o in runnable[t] or [['-std=gnu++17']]]
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        results = list(pool.map(lambda j: check(j[0], j[1], clang, cxxlex, regexes, issued), jobs))
    known = known_differences()
    kinds = ('tokens', 'errors', 'diags', 'macros')
    warnings_differ = 0
    passed = dict.fromkeys(kinds, 0)
    all_ok = known_runs = 0
    per_test = {}
    skipped = sum(1 for _, _, res in results if res is None)
    results = [r for r in results if r[2] is not None]
    for test, opts, res in results:
        rel = os.path.relpath(test, llvm) if test.startswith(llvm) else os.path.relpath(test, SPEC)
        warnings_differ += res.pop('#warnings')
        bad = {k: v for k, v in res.items() if v}
        excused = bad and set(bad) <= (known.get(f'{rel} [{opts}]') or known.get(rel) or set())
        known_runs += bool(excused)
        for k in kinds:
            passed[k] += not res[k] or bool(excused)
        ok = not bad or excused
        all_ok += ok
        per_test[rel] = per_test.get(rel, True) and ok
        if not ok or args.v:
            print(f'{"known" if excused else "FAIL" if bad else "ok  "} {rel} [{opts}]')
            for k, v in bad.items():
                print(f'    {k}: {v[:300]}')
    n = len(results)
    print(f'{n} runs of {len(found)} tests: ' + ', '.join(f'{k} {v}/{n}' for k, v in passed.items()) +
          f'; all three {all_ok}/{n} ({known_runs} known differences); '
          f'tests with every run passing {sum(per_test.values())}/{len(per_test)}' +
          (f' ({skipped} runs in editions cxxlex lacks)' if skipped else '') +
          f'; warnings differing (not compared): {warnings_differ}' +
          f'; {len(unrunnable)} tests make their inputs at run time (not run)')
    return 0 if all_ok == n else 1


if __name__ == '__main__':
    sys.exit(main())
