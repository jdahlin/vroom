#!/usr/bin/env python3
"""Cross-check the facts vcc keeps about each builtin (doc/PLAN-builtins.md).

For every builtin spelling in vcc's C table (frontend/c/builtin-table.cpp,
target-builtins.inc, integer-builtins.inc, libbuiltins.inc; not the stub
catalog) run vpp, with no clang, and observe per language (C, C++) and target
(aarch64-linux-gnu, x86_64-linux-gnu):

  has      __has_builtin(NAME)
  call     a correctly typed call passes -fsyntax-only
  compile  the call compiles to an object at -O0, -O1 and -O2
  cexpr    a call on constant arguments is a constant expression, against
           __has_constexpr_builtin(NAME)

and assert the invariants below.  Violations that exist today are in
src/test/builtins/consistency-failures.txt, one line per
`INVARIANT NAME LANGUAGE/TARGET[ detail]`; --check fails on a violation not
listed and on a listed one that no longer happens, --bless rewrites the file.

  claimed-not-callable    __has_builtin is 1 but the call is rejected
  c-only-callable         callable in C, rejected in C++ (same target)
  not-compiled            callable but -c fails (at -O0/-O1/-O2)
  crash                   vpp died with a signal (or timed out) on the call
  constexpr-claim         __has_constexpr_builtin and the call folding differ
  fold-differs            folds in C but not in C++ or the other way around

Usage: bench/vcc-builtins-consistency.py [--vpp PATH] [-j N] [--check|--bless] [-v] [NAME...]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRONT = ROOT / 'src/frontend/c'
FAILURES = ROOT / 'src/test/builtins/consistency-failures.txt'
TARGETS = ('aarch64-linux-gnu', 'x86_64-linux-gnu')
# what a builtin of the target's extensions needs to be callable at all
TARGET_FLAGS = {'aarch64-linux-gnu': [], 'x86_64-linux-gnu': ['-msse4.2', '-mcrc32']}
LANGUAGES = ('c', 'c++')
LEVELS = (0, 1, 2)

# ---------------------------------------------------------------- the names

SYNC_SIZES = re.compile(r'CC_BUILTIN_SYNC_SIZES\(\s*"([^"]+)"')


def table_rows():
    """(name, flags, signature, encoding) of every row; encoding 'v' is vcc's
    letters, 'c' clang's Builtins encoding."""
    rows = {}
    text = (FRONT / 'builtin-table.cpp').read_text()
    start = text.index('cc_builtin_table[] = {')
    end = text.index('{NULL, 0, 0, NULL}')
    body = text[start:end]
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    for name in SYNC_SIZES.findall(body):
        for size in (1, 2, 4, 8, 16):
            rows[f'{name}_{size}'] = ('0', 's.', 'v')
    for match in re.finditer(r'\{\s*"([A-Za-z_0-9]+)"\s*,\s*[A-Z_0-9]+\s*,'
                             r'\s*([^"]*?)\s*,\s*"([^"]*)"\s*\}', body):
        rows[match.group(1)] = (match.group(2), match.group(3), 'v')
    for path in (FRONT / 'target-builtins.inc',
                 ROOT / 'src/include/frontend/integer-builtins.inc'):
        for match in re.finditer(r'"([A-Za-z_0-9]+)"\s*,\s*[A-Z_0-9]+\s*,\s*(\d+)\s*,'
                                 r'\s*"([^"]*)"', path.read_text()):
            rows[match.group(1)] = (match.group(2), match.group(3), 'v')
    library = (FRONT / 'libbuiltins.inc').read_text()
    for match in re.finditer(r'\{\s*"([A-Za-z_0-9]+)"\s*,\s*"[^"]*"\s*,\s*"([^"]*)"\s*\}', library):
        rows.setdefault(match.group(1), ('0', match.group(2), 'c'))
    return rows


# ------------------------------------------------------------ the signatures

VCC_LETTERS = {
    'v': 'void', 'b': 'VB', 'C': 'unsigned char', 'h': 'unsigned short', 'i': 'int',
    'u': 'unsigned', 'l': 'long', 'L': 'unsigned long', 'w': 'long long',
    'W': 'unsigned long long', 'z': '__SIZE_TYPE__', 'd': 'double', 'f': 'float',
    'e': '_Float16', 'F': 'V4SF', 'c': 'char', 'B': 'V16QI', 'I': 'V4SI',
    'Q': 'V2DI', 'E': 'V2DF', 'N': 'short', 'H': 'V8HI', 'D': 'long double',
    'p': 'void *', 'P': 'const void *', 'R': 'unsigned long long *', 's': 'char *',
    'S': 'const char *', 'M': 'char *', 'V': '__builtin_va_list',
}
SCALARS = {'b', 'C', 'h', 'i', 'u', 'l', 'L', 'w', 'W', 'z', 'd', 'f', 'e', 'c', 'N', 'D'}


class Sig:
    """return type and parameter types as C spellings, plus what is needed to
    build a constant call: `consts` has a literal for each parameter or None."""

    def __init__(self, ret, params, variadic):
        self.ret, self.params, self.variadic = ret, params, variadic


def decode_vcc(sig):
    ret = VCC_LETTERS[sig[0]]
    params, variadic = [], False
    for ch in sig[1:]:
        if ch == '.':
            variadic = True
            break
        params.append(VCC_LETTERS[ch])
    return Sig(ret, params, variadic)


CLANG_BASE = {'v': 'void', 'b': 'VB', 'c': 'char', 's': 'short', 'i': 'int', 'f': 'float',
              'd': 'double', 'z': '__SIZE_TYPE__', 'w': 'VWCHAR', 'h': '_Float16',
              'a': '__builtin_va_list', 'F': 'void', 'J': 'void', 'P': 'void'}


def decode_clang(sig):
    items, i, variadic = [], 0, False
    while i < len(sig):
        if sig[i] == '.':
            variadic = True
            i += 1
            continue
        longs = unsigned = 0
        complex_ = False
        const = False
        while i < len(sig) and sig[i] in 'LUSIXW':
            if sig[i] == 'L':
                longs += 1
            elif sig[i] == 'U':
                unsigned = 1
            elif sig[i] == 'X':
                complex_ = True
            i += 1
        if i >= len(sig):
            return None
        base = sig[i]
        i += 1
        if base == 'd' and longs:
            text, longs = 'long double', 0
        elif base == 'i' and longs:
            text = {1: 'long', 2: 'long long', 3: '__int128'}[longs]
        elif base in CLANG_BASE:
            text = CLANG_BASE[base]
        else:
            return None
        if unsigned:
            text = 'unsigned ' + text
        if complex_:
            text = '_Complex ' + text
        while i < len(sig) and sig[i] in '*CDR&':
            if sig[i] == '*':
                text = (('const ' if const else '') + text + ' *')
                const = False
            elif sig[i] == 'C':
                const = True
            i += 1
        if const:
            text = 'const ' + text
        items.append(text)
    if not items:
        return None
    return Sig(items[0], items[1:], variadic)


# ------------------------------------------------- generic ("." and "s") rows
# Rows whose signature the table leaves open get a hand-written typed call:
# (name regex, wrapper parameters, call, constant call or None).  {N} is the
# builtin, {T} the type of the row's first parameter.  First match wins.
V = 'V4SI'
GENERIC = [
    (r'__builtin_(add|sub|mul)_overflow$', 'int a, int b, int *r', '{N}(a, b, r)', '{N}(1, 2, (int *)0)'),
    (r'__builtin_[su](add|sub|mul)(l|ll)?_overflow$', '{T} a, {T} b, {T} *r', '{N}(a, b, r)', None),
    (r'__builtin_(add|sub)c[bsl]?l?$', '{T} a, {T} b, {T} c, {T} *r', '{N}(a, b, c, r)', None),
    (r'__atomic_load_n$', 'int *p', '{N}(p, 5)', None),
    (r'__atomic_(store|exchange)_n$', 'int *p, int v', '{N}(p, v, 5)', None),
    (r'__atomic_compare_exchange_n$', 'int *p, int *e, int v', '{N}(p, e, v, 0, 5, 5)', None),
    (r'__atomic_(load|store)$', 'int *p, int *r', '{N}(p, r, 5)', None),
    (r'__atomic_exchange$', 'int *p, int *v, int *r', '{N}(p, v, r, 5)', None),
    (r'__atomic_compare_exchange$', 'int *p, int *e, int *d', '{N}(p, e, d, 0, 5, 5)', None),
    (r'__atomic_(fetch_(add|sub|and|or|xor|nand|max|min)|(add|sub|and|or|xor|nand|max|min)_fetch)$',
     'int *p, int v', '{N}(p, v, 5)', None),
    (r'__atomic_test_and_set$', 'char *p', '{N}(p, 5)', None),
    (r'__atomic_clear$', 'char *p', '{N}(p, 5)', None),
    (r'__c11_atomic_load$', '_Atomic(int) *p', '{N}(p, 5)', None),
    (r'__c11_atomic_(store|exchange)$', '_Atomic(int) *p, int v', '{N}(p, v, 5)', None),
    (r'__c11_atomic_fetch_\w+$', '_Atomic(int) *p, int v', '{N}(p, v, 5)', None),
    (r'__c11_atomic_compare_exchange_(strong|weak)$', '_Atomic(int) *p, int *e, int v',
     '{N}(p, e, v, 5, 5)', None),
    (r'__c11_atomic_init$', '_Atomic(int) *p, int v', '{N}(p, v)', None),
    (r'__sync_(fetch_and_(add|sub|and|or|xor|nand|max|min)|(add|sub|and|or|xor|nand)_and_fetch|'
     r'lock_test_and_set|swap)(_\d+)?$', 'int *p, int v', '{N}(p, v)', None),
    (r'__sync_fetch_and_(umax|umin)$', 'unsigned *p, unsigned v', '{N}(p, v)', None),
    (r'__sync_(val|bool)_compare_and_swap(_\d+)?$', 'int *p, int o, int n', '{N}(p, o, n)', None),
    (r'__sync_lock_release(_\d+)?$', 'int *p', '{N}(p)', None),
    (r'__sync_synchronize$', '', '{N}()', None),
    (r'__builtin_elementwise_abs$', V + ' a', '{N}(a)', None),
    (r'__builtin_elementwise_\w+$', V + ' a, ' + V + ' b', '{N}(a, b)', None),
    (r'__builtin_(constant_p|classify_type)$', 'int a', '{N}(a)', '{N}(1)'),
    (r'__builtin_(issignaling|isnan|isinf|isfinite|signbit|isinf_sign|isnormal|issubnormal|iszero)$',
     'double a', '{N}(a)', '{N}(1.0)'),
    (r'__builtin_isfpclass$', 'double a', '{N}(a, 3)', '{N}(1.0, 3)'),
    (r'__builtin_(isgreater|isgreaterequal|isless|islessequal|islessgreater|isunordered)$',
     'double a, double b', '{N}(a, b)', '{N}(1.0, 2.0)'),
    (r'__builtin_fpclassify$', 'double a', '{N}(0, 1, 2, 3, 4, a)', '{N}(0, 1, 2, 3, 4, 1.0)'),
    (r'__builtin_complex$', 'double a, double b', '{N}(a, b)', None),
    (r'__builtin_(conj|creal|cimag)$', '_Complex double a', '{N}(a)', None),
    (r'__builtin_(conj|creal|cimag)f$', '_Complex float a', '{N}(a)', None),
    (r'__builtin_(conj|creal|cimag)l$', '_Complex long double a', '{N}(a)', None),
    (r'__builtin_(clzg|ctzg|popcountg)$', 'unsigned a', '{N}(a)', '{N}(1u)'),
    (r'__builtin_stdc_(count_ones|trailing_zeros)$', 'unsigned a', '{N}(a)', '{N}(1u)'),
    (r'__builtin_stdc_rotate_(left|right)$', 'unsigned a', '{N}(a, 1u)', '{N}(1u, 1u)'),
    (r'__builtin_prefetch$', 'int *p', '{N}(p)', None),
    (r'__builtin_assume_aligned$', 'int *p', '{N}(p, 8)', None),
    (r'__builtin_va_start$|__builtin_c23_va_start$',
     'int n, ...', '__builtin_va_list ap; {N}(ap, n); __builtin_va_end(ap)', None),
    (r'__builtin_ms_va_start$',
     'int n, ...', '__builtin_ms_va_list ap; {N}(ap, n); __builtin_ms_va_end(ap)', None),
]
# Rows with no typed call at all, each with its reason.
SPECIAL_FORMS = {
    '__builtin_counted_by_ref': 'needs a flexible array member with counted_by',
    '__builtin_os_log_format': 'Darwin os_log buffer layout, no portable typed call',
    '__builtin_os_log_format_buffer_size': 'Darwin os_log, no portable typed call',
    '__builtin_matrix_column_major_load': 'needs a matrix type (-fenable-matrix)',
    '__builtin_matrix_column_major_store': 'needs a matrix type (-fenable-matrix)',
    '__builtin_matrix_transpose': 'needs a matrix type (-fenable-matrix)',
}


class Sample:
    """variant A passes literals (what an immediate operand needs), variant B
    passes every argument as a parameter (the optimizer cannot fold it away);
    `constant` is a call on literals for the constant-expression probe."""

    def __init__(self, a, b, constant):
        self.a, self.b, self.constant = a, b, constant


TYPEDEFS = (
    'typedef float V4SF __attribute__((vector_size(16)));\n'
    'typedef char V16QI __attribute__((vector_size(16)));\n'
    'typedef int V4SI __attribute__((vector_size(16)));\n'
    'typedef long long V2DI __attribute__((vector_size(16)));\n'
    'typedef double V2DF __attribute__((vector_size(16)));\n'
    'typedef short V8HI __attribute__((vector_size(16)));\n'
    '#ifdef __cplusplus\n#define VB bool\n#define VWCHAR wchar_t\n#else\n#define VB _Bool\n#define VWCHAR __WCHAR_TYPE__\n#endif\n'
)
PRELUDE_LINES = TYPEDEFS.count('\n')
VECTOR_TYPES = {'V4SF', 'V16QI', 'V4SI', 'V2DI', 'V2DF', 'V8HI'}


def literal(type_text, letter):
    """a constant argument of a parameter type, or None when there is none."""
    if type_text in VECTOR_TYPES or type_text == '__builtin_va_list':
        return None
    if type_text.endswith('*'):
        return '(%s)0' % type_text
    if type_text in ('float', 'double', 'long double', '_Float16'):
        return '(%s)1.5' % type_text
    return '(%s)1' % type_text


def build_sample(name, row):
    """(Sample, reason) for a row: reason is why there is none."""
    flags, sig, encoding = row
    if name in SPECIAL_FORMS:
        return None, SPECIAL_FORMS[name]
    decoded = decode_vcc(sig) if encoding == 'v' else decode_clang(sig)
    if decoded is None:
        return None, 'signature not decodable'
    first = decoded.params[0] if decoded.params else 'int'
    for pattern, params, call, constant in GENERIC:
        if re.match(pattern, name):
            sub = lambda text: text and text.replace('{N}', name).replace('{T}', first)
            variant = (sub(params), sub(call))
            return Sample(variant, variant, sub(constant)), None
    if sig in ('v.', 's.', 'i.', 'u.', 'd.', 'f.', 'D.', 'p.') or decoded.params[:1] == ['void'] or \
            re.search(r'\.\.', sig):
        return None, 'generic signature %r with no typed call' % sig
    params, args, constants, plain = [], [], [], []
    for k, type_text in enumerate(decoded.params):
        params.append('%s p%d' % (type_text, k))
        plain.append('p%d' % k)
        value = literal(type_text, None)
        if value is None:
            args.append('p%d' % k)
            constants.append(None)
            continue
        if type_text in ('const char *', 'const void *'):
            constants.append('"abc"')
        else:
            constants.append(value)
        args.append(value)
    if decoded.variadic:
        for target in (args, plain, constants):
            target.append('1')
    constant = None
    if None not in constants and decoded.ret in SCALAR_RETURNS:
        constant = '%s(%s)' % (name, ', '.join(constants))
    declared_a = [p for p, a in zip(params, args) if a.startswith('p') and a[1:].isdigit()]
    return Sample((', '.join(declared_a), '%s(%s)' % (name, ', '.join(args))),
                  (', '.join(params), '%s(%s)' % (name, ', '.join(plain))), constant), None


SCALAR_RETURNS = {'VB', 'unsigned char', 'unsigned short', 'int', 'unsigned', 'long', 'unsigned long',
                  'long long', 'unsigned long long', '__SIZE_TYPE__', 'double', 'float', '_Float16',
                  'char', 'short', 'long double', 'VWCHAR'}


# ------------------------------------------------------------------- running

def vpp_run(vpp, args, source, suffix):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / ('probe' + suffix)
        path.write_text(source)
        try:
            proc = subprocess.run([str(vpp), *args, str(path)], capture_output=True, text=True,
                                  timeout=120, cwd=directory)
        except subprocess.TimeoutExpired:
            return 'crash', {}, 'timeout'
        errors = {}
        for match in re.finditer(r':(\d+):\d+: error: (.*)', proc.stderr):
            errors.setdefault(int(match.group(1)), match.group(2).strip())
        if proc.returncode < 0 or proc.returncode > 1:
            return 'crash', errors, 'signal %d' % -proc.returncode if proc.returncode < 0 \
                else 'exit %d' % proc.returncode
        return ('ok' if proc.returncode == 0 else 'error'), errors, proc.stderr


def probe(vpp, target, language, level, mode, lines, refusals=False):
    """Run one TU of one-line items, bisecting on a failure that cannot be
    attributed by line.  lines: {key: source line}.  Result: {key: (status,
    detail)} with status ok, error or crash.  refusals: the translation unit
    is a C++ one where a refusal ("not implemented yet") matters, see go."""
    suffix = '.c' if language == 'c' else '.cpp'
    flags = [f'--target={target}', *TARGET_FLAGS[target], '-x', language, '-w', '-ferror-limit=0']
    if mode == 'syntax':
        flags.append('-fsyntax-only')
    else:
        flags += ['-c', f'-O{level}', '-o', os.devnull]
    keys = list(lines)
    result = {}

    def go(batch):
        source = TYPEDEFS + '\n'.join(lines[k] for k in batch) + '\n'
        status, errors, detail = vpp_run(vpp, flags, source, suffix)
        if status == 'ok':
            for key in batch:
                result[key] = ('ok', '')
            return
        if len(batch) == 1:
            result[batch[0]] = (status, detail if status == 'crash' else
                                next(iter(errors.values()), detail.strip().splitlines()[-1:] and
                                     detail.strip().splitlines()[-1] or ''))
            return
        if status == 'error' and mode == 'syntax' and errors and all(
                PRELUDE_LINES < n <= PRELUDE_LINES + len(batch) for n in errors):
            # vcc's C++ front end says that it does not implement a construct
            # only when it reported no other error: a line that was refused
            # (not implemented yet) looks ok in a translation unit with other
            # errors.  With `refusals` the lines without an error are probed
            # again together, until a translation unit has none (C reports
            # every line).
            rest = []
            for index, key in enumerate(batch):
                line = PRELUDE_LINES + 1 + index
                if line in errors:
                    result[key] = ('error', errors[line])
                else:
                    rest.append(key)
            if refusals and language == 'c++':
                go(rest)
            else:
                for key in rest:
                    result[key] = ('ok', '')
            return
        middle = len(batch) // 2
        go(batch[:middle])
        go(batch[middle:])

    go(keys)
    return result


def has_builtin(vpp, target, language, names):
    source = ''.join(f'{i} __has_builtin({n}) __has_constexpr_builtin({n})\n'
                     for i, n in enumerate(names))
    flags = [f'--target={target}', *TARGET_FLAGS[target], '-x', language, '-E', '-P']
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / ('has.c' if language == 'c' else 'has.cpp')
        path.write_text(source)
        proc = subprocess.run([str(vpp), *flags, str(path)], capture_output=True, text=True,
                              cwd=directory)
    rows = [line.split() for line in proc.stdout.splitlines() if line.strip()]
    if proc.returncode or len(rows) != len(names):
        raise RuntimeError(f'__has_builtin query failed for {target} {language}\n{proc.stderr}')
    return {n: (int(r[1]) != 0, int(r[2]) != 0) for n, r in zip(names, rows)}


def special_wrapper(index, params, call):
    """one line, so that a diagnostic's line number names the builtin"""
    return f'void f{index}({params}) {{ {call}; }}'


SHORT = {'aarch64-linux-gnu': 'arm64', 'x86_64-linux-gnu': 'x86_64'}


def compile_probe(vpp, target, language, level, names, samples, a, b):
    """-c on the variant that type-checks, parameters first (nothing folds away);
    a name that fails that way gets its literal variant (an immediate operand
    must be a constant) and counts as compiled when that compiles."""
    first, second = {}, {}
    for i, n in enumerate(names):
        if b[n][0] == 'ok':
            first[n] = special_wrapper(i, *samples[n].b)
            if a[n][0] == 'ok' and samples[n].a != samples[n].b:
                second[n] = special_wrapper(i, *samples[n].a)
        elif a[n][0] == 'ok':
            first[n] = special_wrapper(i, *samples[n].a)
    result = probe(vpp, target, language, level, 'compile', first)
    retry = {n: second[n] for n in second if result[n][0] != 'ok'}
    if retry:
        for n, outcome in probe(vpp, target, language, level, 'compile', retry).items():
            if outcome[0] == 'ok':
                result[n] = outcome
    return result


def collect(vpp, rows, jobs, verbose):
    """every observation, keyed (what, language, target, ...)."""
    samples, excluded = {}, {}
    for name in sorted(rows):
        sample, reason = build_sample(name, rows[name])
        if sample is None:
            excluded[name] = reason
        else:
            samples[name] = sample
    names = sorted(samples)
    pool = ThreadPoolExecutor(max_workers=jobs)
    futures = {}
    for target in TARGETS:
        for language in LANGUAGES:
            futures[('has', language, target)] = pool.submit(has_builtin, vpp, target, language,
                                                             sorted(rows))
            for variant in 'ab':
                lines = {n: special_wrapper(i, *getattr(samples[n], variant))
                         for i, n in enumerate(names)}
                futures[('call' + variant, language, target)] = pool.submit(
                    probe, vpp, target, language, 0, 'syntax', lines, True)
            fold = [n for n in names if samples[n].constant]
            if language == 'c':
                lines = {n: f'char c{i}[1 + 0 * (long)({samples[n].constant})];'
                         for i, n in enumerate(fold)}
            else:
                lines = {n: f'constexpr long c{i} = (long)({samples[n].constant});'
                         for i, n in enumerate(fold)}
            futures[('fold', language, target)] = pool.submit(probe, vpp, target, language, 0,
                                                              'syntax', lines, True)
    observed = {key: future.result() for key, future in futures.items()}
    # the compile probes need the syntax results: the variant that type-checks
    compile_futures = {}
    for target in TARGETS:
        for language in LANGUAGES:
            a = observed[('calla', language, target)]
            b = observed[('callb', language, target)]
            for level in LEVELS:
                compile_futures[('compile', language, target, level)] = pool.submit(
                    compile_probe, vpp, target, language, level, names, samples, a, b)
    for key, future in compile_futures.items():
        observed[key] = future.result()
    pool.shutdown()
    return names, excluded, observed


def violations(names, rows, observed):
    """{(invariant, name, tag): detail}"""
    found = {}

    def add(invariant, name, tag, detail=''):
        found[(invariant, name, tag)] = detail

    for target in TARGETS:
        short = SHORT[target]
        callable_ = {}
        for language in LANGUAGES:
            a = observed[('calla', language, target)]
            b = observed[('callb', language, target)]
            for n in names:
                ok = a[n][0] == 'ok' or b[n][0] == 'ok'
                callable_[(language, n)] = ok
                if a[n][0] == 'crash' or b[n][0] == 'crash':
                    add('crash', n, f'{language}/{short}/syntax',
                        a[n][1] if a[n][0] == 'crash' else b[n][1])
                    callable_[(language, n)] = False
                    continue
                claimed = observed[('has', language, target)][n][0]
                if claimed and not ok:
                    add('claimed-not-callable', n, f'{language}/{short}', a[n][1])
        for n in names:
            if callable_[('c', n)] and not callable_[('c++', n)]:
                add('c-only-callable', n, f'c++/{short}',
                    observed[('calla', 'c++', target)][n][1])
        for language in LANGUAGES:
            for level in LEVELS:
                done = observed[('compile', language, target, level)]
                for n in names:
                    if n in done and done[n][0] != 'ok':
                        kind = 'crash' if done[n][0] == 'crash' else 'not-compiled'
                        add(kind, n, f'{language}/{short}/O{level}', done[n][1])
        folds = {}
        for language in LANGUAGES:
            fold = observed[('fold', language, target)]
            for n, (status, detail) in fold.items():
                if status == 'crash':
                    add('crash', n, f'{language}/{short}/fold', detail)
                    continue
                if not callable_[(language, n)]:
                    continue
                folds[(language, n)] = status == 'ok'
                claim = observed[('has', language, target)][n][1]
                if claim and status != 'ok':
                    add('constexpr-claimed-not-folded', n, f'{language}/{short}', detail)
                if not claim and status == 'ok':
                    add('constexpr-folds-unclaimed', n, f'{language}/{short}')
        for n in names:
            c, x = folds.get(('c', n)), folds.get(('c++', n))
            if c is None or x is None or c == x:
                continue
            if c:
                add('fold-c-only', n, f'c++/{short}')
            else:
                add('fold-cxx-only', n, f'c/{short}')
    return found


def line_of(key, detail=''):
    text = ' '.join(key)
    if detail:
        detail = detail.replace('\n', ' ').strip()
        text += ' # ' + detail[:150]
    return text


HEADER = (
    '# Known violations of bench/vcc-builtins-consistency.py: INVARIANT NAME LANGUAGE/TARGET[/LEVEL] [# what vpp said].\n'
    '# --check fails on a violation not listed here and on a listed one that no longer happens: fix it, then\n'
    '# `bench/vcc-builtins-consistency.py --bless` in the same commit.  Never add a line to hide a regression.\n'
    '# The text after # is informational and not compared.\n')


def read_known():
    known = {}
    if FAILURES.exists():
        for line in FAILURES.read_text().splitlines():
            if not line.strip() or line.startswith('#'):
                continue
            body = line.split(' # ', 1)[0].split()
            known[tuple(body)] = line
    return known


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--vpp', type=Path, default=None)
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 4)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='fail on a difference from the known list')
    mode.add_argument('--bless', action='store_true', help='rewrite the known-failures file')
    parser.add_argument('-v', '--verbose', action='store_true')
    parser.add_argument('names', nargs='*', help='only these builtins (no --check/--bless)')
    args = parser.parse_args()
    vpp = args.vpp
    if vpp is None:
        base = Path(os.environ.get('VCC_BIN') or ROOT / 'build/linux/src')
        vpp = base / 'vpp'
    vpp = vpp.resolve()
    start = time.time()
    rows = table_rows()
    if args.names:
        for n in args.names:
            if n not in rows:
                parser.error(f'{n}: not in the table')
        rows = {n: rows[n] for n in args.names}
    names, excluded, observed = collect(vpp, rows, args.jobs, args.verbose)
    found = violations(names, rows, observed)
    elapsed = time.time() - start
    counts = {}
    for invariant, _, _ in found:
        counts[invariant] = counts.get(invariant, 0) + 1
    print(f'{len(rows)} builtins in the C table, {len(names)} with a typed call, '
          f'{len(excluded)} excluded as special forms or untyped; '
          f'{len(TARGETS) * len(LANGUAGES)} language/target pairs, {elapsed:.1f} s')
    if args.verbose:
        for n, reason in sorted(excluded.items()):
            print(f'  excluded {n}: {reason}')
    for invariant in sorted(counts):
        print(f'  {invariant}: {counts[invariant]}')
    if args.names:
        for key in sorted(found):
            print(line_of(key, found[key]))
        return 0
    known = read_known()
    new = sorted(set(found) - set(known))
    stale = sorted(set(known) - set(found))
    if args.bless:
        FAILURES.parent.mkdir(parents=True, exist_ok=True)
        FAILURES.write_text(HEADER + ''.join(line_of(k, found[k]) + '\n' for k in sorted(found)))
        print(f'{FAILURES.relative_to(ROOT)}: {len(found)} known violations')
        return 0
    print(f'known violations: {len(known)} listed, {len(new)} new, {len(stale)} stale')
    for key in new:
        print('NEW   ' + line_of(key, found[key]))
    for key in stale:
        print('STALE ' + known[key] + '   (passes now: --bless)')
    return 1 if (new or stale) and args.check else 0


if __name__ == '__main__':
    sys.exit(main())
