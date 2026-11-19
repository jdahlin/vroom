#!/usr/bin/env python3
"""The C++ front end's lexer (src/frontend/cplusplus/lexer, through the driver:
vpp -fcxx-dump=, src/frontend/cplusplus/dump/dump.h)
against clang, on clang's own lexer tests (src/test/cxx/lex/llvm) and ours.

For each %clang_cc1 RUN line of a test, with the RUN line's language options:

  raw     -fcxx-dump=raw-tokens vs clang -cc1 -dump-raw-tokens on the file:
          phase 3, every preprocessing token, white space and comment with its
          kind, spelling, location and [StartOfLine]/[UnClean] flags
  tokens  the file preprocessed by clang -E, then -fcxx-dump=tokens vs
          clang -cc1 -dump-tokens on that: phase 7 (keywords, alternative
          tokens, literals), with [LeadingSpace]
  values  each literal's type and value as vcc computes them, written as
          static_asserts (-fcxx-dump=literal-claims) that clang checks
  diags   the lexical diagnostics: clang -cc1 -fsyntax-only on the file,
          keeping only messages that are one of the diagnostics the lexer
          issues; vcc's from both runs above

Usage: bench/cxx-lex.py [-v] [-j N] [--bin-dir DIR] [PATTERN...]
Prints one line per test and RUN line that does not match (all with -v) and
a summary per comparison.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(R, 'src', 'test', 'cxx', 'lex')
FRONTEND = os.path.join(R, 'src', 'frontend', 'cplusplus')

# clang -cc1 options that change lexing (passed to both), and options taking a value
KEEP = re.compile(r'^(-D\S+|-U\S+|-std=.*|-fms-extensions|-fms-compatibility|-f(no-)?char8_t|-f?(no-)?trigraphs|'
                  r'-f(no-)?dollars-in-identifiers|-pedantic(-errors)?|-W[^l].*|-w|-fdeclspec|-fgnu-keywords)$')
VALUE = {'-triple', '-x', '-o', '-I', '-include', '-isystem', '-verify', '-target-cpu', '-D', '-U', '-main-file-name'}


def find_clang():
    for c in ('clang-23', 'clang'):
        if shutil.which(c):
            return shutil.which(c)
    sys.exit('cxx-lex: no clang')


def vpp_tools():
    """cxx-pp.py's find_vpp and vpp_command"""
    import importlib.util
    spec = importlib.util.spec_from_file_location('cxxpp', os.path.join(R, 'bench', 'cxx-pp.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CXXPP = vpp_tools()


def run_lines(path):
    """The %clang_cc1 RUN lines of a test, joined across trailing backslashes."""
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
    for line in lines:
        cmd = line.split('|')[0]
        if '%clang_cc1' not in cmd:
            continue
        args = cmd.split()
        args = args[args.index('%clang_cc1') + 1:]
        lang = None
        opts = []
        i = 0
        while i < len(args):
            a = args[i]
            if a in VALUE:
                if a == '-x':
                    lang = args[i + 1]
                i += 2
                continue
            if a.startswith('-x') and len(a) > 2:
                lang = a[2:]
            if a.startswith('-verify') or a.startswith('%'):
                i += 1
                continue
            if KEEP.match(a) or a.startswith('-std='):
                opts.append(a)
            i += 1
        if lang and 'c++' not in lang:
            continue
        if any(o.startswith('-std=') and not re.match(r'-std=(gnu|c)\+\+', o) for o in opts):
            continue
        if not any(o.startswith('-std=') for o in opts):
            opts.insert(0, '-std=gnu++17')  # clang -cc1's default for C++
        if '-E' in args and not any(a.startswith('-verify') or a == '-fsyntax-only' for a in args):
            opts = opts + ['#preprocess-only']
        out.append(opts)
    # one entry per distinct option set
    seen, unique = set(), []
    for o in out:
        if tuple(o) not in seen:
            seen.add(tuple(o))
            unique.append(o)
    return unique or [['-std=gnu++17']]


WHITE_SPACE = re.compile(r'(?: |\\n|\\t|\\r|\\013|\\014|\\\\(?=\\n))*')
TOKEN_LINE = re.compile(r"^(\S+)\s+'(.*)'\s+Loc=<(.*?):(\d+):(\d+)>(.*)$")


def parse_tokens(text, keep_leading_space):
    out = []
    for line in text.splitlines():
        m = TOKEN_LINE.match(line)
        if not m:
            continue
        name, spelling, _, row, col, flags = m.groups()
        if name.startswith('annot_'):
            continue
        if name == 'unknown' and WHITE_SPACE.fullmatch(spelling):
            # [lex.phases]/1.3: "Whether each nonempty sequence of white-space
            # characters other than new-line is retained or replaced by one
            # space character is unspecified": clang's raw lexer splits white
            # space at a splice in its own way, so it is not compared
            continue
        if name == 'eof':  # where clang puts the end of preprocessed input is its own
            out.append(('eof', '', 0, 0, ''))
            continue
        f = []
        if '[StartOfLine]' in flags:
            f.append('StartOfLine')
        if keep_leading_space and '[LeadingSpace]' in flags:
            f.append('LeadingSpace')
        # [UnClean] is clang's own bookkeeping (the token needs phases 1-2
        # applied); the spellings, which are compared, already say that
        out.append((name, spelling, int(row), int(col), ' '.join(f)))
    return out


def lexer_diagnostics():
    """name -> regex of the message of each diagnostic the lexer issues"""
    used = set()
    for path in glob.glob(os.path.join(FRONTEND, '**', '*.cpp'), recursive=True):
        # phases 1-3 and 5-7: phase 4's (preprocessor/, ext/) are bench/cxx-pp.py's
        if os.path.relpath(path, FRONTEND).split(os.sep)[0] not in ('lex', 'intro', 'basic', 'diff'):
            continue
        used.update(re.findall(r'diagnostic_id::(\w+)', open(path).read()))
    out = {}
    inc = open(os.path.join(FRONTEND, 'intro', 'diagnostic-messages.inc')).read()
    for m in re.finditer(r'^DIAGNOSTIC\((\w+), \w+, \w+, "[^"]*", "((?:[^"\\]|\\.)*)"\)$', inc, re.M):
        name, fmt = m.group(1), bytes(m.group(2), 'utf-8').decode('unicode_escape')
        if name in used:
            out[name] = format_regex(fmt)
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
            while depth:
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


def phase4_lines(test, opts, cwd, clang, tmp):
    """The lines of the test that phase 4 owns: directive lines and lines in
    groups that conditional inclusion skips ([cpp.cond]), found from clang -E -C
    (comments kept) -dD (#define kept): a line clang does not emit is one of
    them.  Diagnostics there are the preprocessor's to give, not the lexer's."""
    out = os.path.join(tmp, re.sub(r'\W', '_', test) + str(abs(hash(tuple(opts)))) + '.active')
    text, _ = run([clang, '-cc1', '-E', '-C', '-dD', '-I', '.', '-I', 'Inputs'] + opts + [os.path.basename(test)], cwd=cwd)
    open(out, 'w').write(text)
    lines = open(test, encoding='utf-8', errors='replace').read().split('\n')
    owned = {i + 1 for i, line in enumerate(lines) if line.lstrip().startswith('#')}
    if not os.path.exists(out):
        return owned, False
    active, current, main = set(), 1, True
    base = os.path.basename(test)
    for line in open(out, encoding='utf-8', errors='replace'):
        m = re.match(r'^# (\d+) "([^"]*)"', line)
        if m:
            current, main = int(m.group(1)), os.path.basename(m.group(2)) == base
            continue
        if main and line.strip():
            active.add(current)
        current += 1
    for i in range(1, len(lines) + 1):
        if i not in active:
            owned.add(i)
    return owned, True


def parse_diagnostics(text, regexes, skip_lines=()):
    out = set()
    for line in text.splitlines():
        m = DIAG_LINE.match(line)
        if not m:
            continue
        _, row, col, level, msg = m.groups()
        if int(row) in skip_lines:
            continue
        if any(re.fullmatch(r, msg) for r in regexes.values()):
            out.add((int(row), int(col), level, msg))
    return out


# vcc's implementation-defined behavior is clang's for this target
TRIPLE = ['-triple', 'aarch64-unknown-linux-gnu']


def run(cmd, cwd=None):
    if len(cmd) > 1 and cmd[1] == '-cc1' and '-triple' not in cmd:
        cmd = cmd[:2] + TRIPLE + cmd[2:]
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace', cwd=cwd, timeout=60)
    return p.stdout, p.stderr


def first_difference(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f'#{i}: vcc {x} / clang {y}'
    if len(a) != len(b):
        i = min(len(a), len(b))
        return f'#{i}: vcc {a[i] if i < len(a) else "(end)"} / clang {b[i] if i < len(b) else "(end)"}'
    return ''


def check(test, opts, clang, vpp, regexes, tmp):
    rel = os.path.relpath(test, TESTS)
    preprocess_only = '#preprocess-only' in opts
    opts = [o for o in opts if o != '#preprocess-only']
    d = os.path.dirname(test)
    name = os.path.basename(test)
    res = {}
    # raw: phase 3 on the file itself
    ours, ours_err = run(CXXPP.vpp_command(vpp, 'raw-tokens', opts, name), cwd=d)
    _, theirs = run([clang, '-cc1', '-dump-raw-tokens'] + opts + [name], cwd=d)  # clang dumps to stderr
    a, b = parse_tokens(ours, False), parse_tokens(theirs, False)
    res['raw'] = first_difference(a, b) if a != b else ''
    # tokens: phase 7 on clang -E's output
    ii = os.path.join(tmp, re.sub(r'\W', '_', rel) + '-' + str(abs(hash(tuple(opts)))) + '.ii')
    # A test without preprocessing directives is its own phase 4 output, so
    # phase 7 runs on it as it is: clang -E also miscounts the lines after a
    # raw string literal that spans lines (it does not count its new-lines).
    # Otherwise clang -E's output (from stdout: clang deletes -o's file after
    # an error, as an #error in a test makes).
    source = open(test, encoding='utf-8', errors='replace').read()
    if not re.search(r'^\s*(#|%:|\?\?=)', source, re.M) and not any(o.startswith('-D') for o in opts):
        e_out = source
    else:
        e_out, _ = run([clang, '-cc1', '-E', '-I', '.', '-I', 'Inputs'] + opts + [name], cwd=d)
    open(ii, 'w').write(e_out)
    if e_out:
        ours_t, ours_t_err = run(CXXPP.vpp_command(vpp, 'tokens', opts, ii))
        _, theirs_t = run([clang, '-cc1', '-dump-tokens'] + opts + [ii])
        a, b = parse_tokens(ours_t, True), parse_tokens(theirs_t, True)
        res['tokens'] = first_difference(a, b) if a != b else ''
        # values: clang checks each literal's type and value as vcc
        # computed them (phases 5 and 7)
        claims, _ = run(CXXPP.vpp_command(vpp, 'literal-claims', opts, ii))
        cpp = ii + '.claims.cpp'
        open(cpp, 'w').write(claims)
        # C++98 has no constant expression that reads a string literal's
        # elements: its claims are checked as C++11, where the literals C++98
        # has keep their types and values
        claim_opts = [re.sub(r'^-std=(c|gnu)\+\+(98|03)$', r'-std=\1++11', o)
                      for o in opts if o.startswith('-std=') or o.startswith('-f')]
        _, failed = run([clang, '-cc1', '-fsyntax-only', '-ferror-limit', '0',
                         '-Wno-everything'] + claim_opts + [cpp])
        errors = [line for line in failed.splitlines() if ': error: ' in line]
        res['values'] = f'{len(errors)} claims fail, first: {errors[0]}' if errors else ''
    else:
        res['tokens'] = 'clang -E failed'
        res['values'] = ''
        ours_t_err = ''
    # diags
    _, clang_err = run([clang, '-cc1', '-fsyntax-only', '-I', '.', '-I', 'Inputs'] + opts + [name], cwd=d)
    owned, _ = phase4_lines(test, opts, d, clang, tmp)
    ours_d = parse_diagnostics(ours_err, regexes, owned) | parse_diagnostics(ours_t_err, regexes, owned)
    theirs_d = parse_diagnostics(clang_err, regexes, owned)
    missing, extra = sorted(theirs_d - ours_d), sorted(ours_d - theirs_d)
    if preprocess_only:
        # a RUN line that only preprocesses (clang -E | FileCheck): the
        # test's tokens never reach phase 7 there
        missing = extra = []
    res['diags'] = '' if not missing and not extra else \
        f'missing {len(missing)} {missing[:2]} extra {len(extra)} {extra[:2]}'
    return rel, ' '.join(opts + (['-E'] if preprocess_only else [])), res


def known_differences():
    """test -> set of comparisons whose difference from clang is understood
    (src/test/cxx/lex/known-differences.txt)"""
    out = {}
    for line in open(os.path.join(TESTS, 'known-differences.txt')):
        if line.strip() and not line.startswith('#'):
            test, kinds, _ = [f.strip() for f in line.split('|', 2)]
            out[test] = set(kinds.split())  # 'path' or 'path [options]'
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--bin-dir')
    ap.add_argument('patterns', nargs='*')
    args = ap.parse_args()
    clang, vpp = find_clang(), CXXPP.find_vpp(args.bin_dir or os.environ.get('VCC_BIN'))
    tests = sorted(glob.glob(os.path.join(TESTS, '**', '*.cpp'), recursive=True))
    tests = [t for t in tests if '/Inputs/' not in t]
    if args.patterns:
        tests = [t for t in tests if any(p in t for p in args.patterns)]
    regexes = lexer_diagnostics()
    jobs = [(t, o) for t in tests for o in run_lines(t)]
    passed = {'raw': 0, 'tokens': 0, 'values': 0, 'diags': 0}
    known = known_differences()
    known_runs = 0
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        results = list(pool.map(lambda j: check(j[0], j[1], clang, vpp, regexes, tmp), jobs))
    for rel, opts, res in results:
        bad = {k: v for k, v in res.items() if v}
        excused = bad and set(bad) <= (known.get(f'{rel} [{opts}]') or known.get(rel) or set())
        known_runs += bool(excused)
        for k in passed:
            passed[k] += not res[k] or excused
        if (bad and not excused) or args.v:
            print(f'{"known" if excused else "FAIL" if bad else "ok  "} {rel} [{opts}]')
            for k, v in bad.items():
                print(f'    {k}: {v}')
    n = len(results)
    print(f'{n} runs of {len(tests)} tests: ' + ', '.join(f'{k} {v}/{n}' for k, v in passed.items()) +
          f' ({known_runs} known differences)')
    return 0 if all(v == n for v in passed.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
