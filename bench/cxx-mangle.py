#!/usr/bin/env python3
"""
bench/cxx-mangle.py: vcc's Itanium mangling against clang's own mangling
tests (work/testsuites/llvm/clang/test/CodeGenCXX, bench/fetch-testsuites.sh
llvm).

The files are CodeGenCXX's whose name says mangling (mangle*.cpp,
*-mangle-*.cpp, *mangling*.cpp, ...), without the Microsoft ABI's
(mangle-ms*, *-ms-*, *msvc*, mingw, windows) and those whose RUN lines name
no Itanium target this harness compiles for.  The first RUN line with an
x86-64, i386 or %itanium_abi_triple target (x86_64-linux-gnu here) or an
arm64/aarch64 Linux one (aarch64-linux-gnu) gives the flags: -std=, -D, -U,
-I%S, -fms-extensions, -fblocks, -fclang-abi-compat=latest (another
-fclang-abi-compat: the next RUN line).  clang++ -O0 -c and vpp -x c++ -O0
-c compile the file with those flags for that target; the result is

  ok        the _Z symbols (defined and referenced, llvm-nm) are the same
  symbols   they differ: missing in vcc's object, extra in vcc's
  refused   vpp fails: the first error and the vcc source file:line of the
            refusal ("refused by vcc's" note)
  crash     vpp died or timed out
  skip      clang itself rejects the file with those flags (-verify tests)

Per symbol (each file vpp analyses, whether -c fails or not), vpp
-fsyntax-only -fcxx-dump=symbols names the symbol of each function and
variable of a well-formed declaration:

  verified     a symbol clang's object defines or references
  refuted      a defined external symbol clang's object has not (a mangling
               that is not clang's, as a rule)
  unsupported  a declaration whose symbol vcc does not mangle yet
  missing      a symbol clang's object defines that vcc names nowhere (the
               declaration not analysed, or mangled otherwise)

Files that pass are kept in bench/cxx-mangle.passing: one that stops
passing is an error (exit status 1); --bless rewrites the list.  Results
are cached in build/cxx-mangle.json, keyed by the file, the flags and both
compilers: a rerun compiles again only what changed.  Not in CI: the suite
is not in the checkout.

  bench/cxx-mangle.py [-j N] [--bin-dir DIR] [--force] [--bless] [-v] [PATTERN]
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

B = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(B)
SUITE = os.path.join(R, 'work', 'testsuites', 'llvm', 'clang', 'test',
                     'CodeGenCXX')
CLANG = '/usr/lib/llvm-23/bin/clang++'
NM = '/usr/lib/llvm-23/bin/llvm-nm'
CACHE = os.path.join(R, 'build', 'cxx-mangle.json')
PASSING = os.path.join(B, 'cxx-mangle.passing')

MICROSOFT = re.compile(r'(^|[-_])ms([-_]|$)|msvc|mingw|windows|win64|win-')


def candidates():
    out = []
    for f in sorted(os.listdir(SUITE)):
        if not f.endswith('.cpp') or 'mangl' not in f.lower():
            continue
        if MICROSOFT.search(f[:-4]):
            continue
        out.append(f)
    return out


def run_lines(path):
    """the RUN commands of a lit test, continuation lines joined"""
    lines = []
    cur = ''
    with open(path, errors='replace') as f:
        for line in f:
            m = re.match(r'\s*//\s*RUN:(.*)$', line)
            if not m:
                if cur:
                    lines.append(cur)
                    cur = ''
                continue
            text = m.group(1).strip()
            if text.endswith('\\'):
                cur += text[:-1] + ' '
                continue
            lines.append(cur + text)
            cur = ''
    if cur:
        lines.append(cur)
    return lines


def target_of(triple):
    if triple in ('%itanium_abi_triple', '%itanium_abi_triple-only'):
        return 'x86_64-linux-gnu'
    if re.match(r'(x86_64|i[3-6]86)-', triple) and \
            not re.search(r'windows|win32|msvc|mingw|cygwin|scei|ps4|ps5',
                          triple):
        return 'x86_64-linux-gnu'
    if re.match(r'(aarch64|arm64)(-[a-z]+)?-linux', triple):
        return 'aarch64-linux-gnu'
    return None


def configuration(path):
    """(target, flags) of the first usable RUN line, or None"""
    for line in run_lines(path):
        if '%clang_cc1' not in line or '%if' in line:
            continue
        command = line.split('|')[0]
        try:
            words = shlex.split(command)
        except ValueError:
            continue
        triple = None
        flags = []
        usable = True
        i = 0
        while i < len(words):
            w = words[i]
            value = None
            if w in ('-triple', '-std', '-D', '-U', '-I') and \
                    i + 1 < len(words):
                value = words[i + 1]
                i += 1
            elif w.startswith('-triple='):
                w, value = '-triple', w[len('-triple='):]
            if w == '-triple':
                triple = value
            elif w.startswith('-std=') or w == '-std':
                flags.append('-std=' + (value or w[5:]))
            elif w in ('-D', '-U'):
                flags.append(w + value)
            elif w.startswith('-D') or w.startswith('-U'):
                flags.append(w)
            elif w == '-I' or w.startswith('-I'):
                d = value or w[2:]
                flags.append('-I' + d.replace('%S', SUITE))
            elif w in ('-fms-extensions', '-fblocks'):
                flags.append(w)
            elif w.startswith('-fclang-abi-compat='):
                if w != '-fclang-abi-compat=latest':
                    usable = False
                flags.append(w)
            elif w in ('-cl-std=clc++', '-fsycl-is-device', '-fcuda-is-device',
                       '-fopenmp', '-fsyntax-only') or w.startswith('-cl-'):
                usable = False
            i += 1
        if not usable or not triple:
            continue
        target = target_of(triple)
        if target:
            return target, flags
    return None


NOTE = re.compile(r"note: refused by vcc's (?P<site>\S+:\d+)")
ERROR = re.compile(r'^(?P<where>[^:\s]+:\d+:\d+): error: (?P<message>.*)$')


def first_error(stderr):
    lines = stderr.splitlines()
    for i, line in enumerate(lines):
        m = ERROR.match(line)
        if not m:
            continue
        site = ''
        for later in lines[i + 1:i + 6]:
            n = NOTE.search(later)
            if n:
                site = n.group('site')
                break
            if ERROR.match(later):
                break
        where = os.path.basename(m.group('where'))
        return m.group('message'), site, where
    return '', '', ''


def symbols(obj, which=None):
    cmd = [NM, '-j']
    if which:
        cmd.append(which)
    p = subprocess.run(cmd + [obj], capture_output=True, text=True)
    return sorted(set(s for s in p.stdout.split() if s.startswith('_Z')))


def symbol_claims(vpp, common, flags, defined, referenced, timeout):
    """the per-symbol counts of -fcxx-dump=symbols against clang's object"""
    cmd = ['nice', '-n', '15', vpp, '-x', 'c++'] + \
        [w for w in common if w not in ('-c',)] + \
        ['-fsyntax-only', '-fcxx-dump=symbols']
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return {}
    known = set(defined) | set(referenced)
    named = set()
    counts = {'verified': 0, 'refuted': 0, 'unsupported': 0,
              'not_emitted': 0}
    refuted = []
    for line in p.stdout.splitlines():
        fields = line.split('\t')
        if fields[0] == 'unsupported':
            counts['unsupported'] += 1
            continue
        if len(fields) != 3:
            continue
        symbol, how, linkage = fields
        named.add(symbol)
        if symbol in known:
            counts['verified'] += 1
        elif how == 'defined' and linkage == 'external' and \
                symbol.startswith('_Z'):
            counts['refuted'] += 1
            refuted.append(symbol)
        else:
            counts['not_emitted'] += 1
    missing = sorted(s for s in defined if s not in named)
    counts['missing'] = len(missing)
    counts['clang'] = len(defined)
    return {'counts': counts, 'refuted_symbols': sorted(refuted)[:20],
            'missing_symbols': missing[:20]}


def compile_one(vpp, name, target, flags, timeout):
    src = os.path.join(SUITE, name)
    common = ['--target=' + target, '-O0', '-w', '-c', src] + flags
    with tempfile.TemporaryDirectory() as tmp:
        ref = os.path.join(tmp, 'clang.o')
        ours = os.path.join(tmp, 'vcc.o')
        try:
            p = subprocess.run(['nice', '-n', '15', CLANG] + common +
                               ['-o', ref], capture_output=True, text=True,
                               timeout=timeout)
        except subprocess.TimeoutExpired:
            return {'state': 'skip', 'message': 'clang timed out'}
        if p.returncode:
            message, _, where = first_error(p.stderr)
            return {'state': 'skip', 'message': 'clang: %s %s'
                    % (where, message)}
        defined = symbols(ref, '--defined-only')
        referenced = symbols(ref, '--undefined-only')
        claims = symbol_claims(vpp, common, flags, defined, referenced,
                               timeout)
        r = compile_vcc(vpp, common, ours, ref, timeout)
        r.update(claims)
        return r


def compile_vcc(vpp, common, ours, ref, timeout):
    """vpp -c's state against clang's object ref"""
    try:
        q = subprocess.run(['nice', '-n', '15', vpp, '-x', 'c++'] + common +
                           ['-o', ours], capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'state': 'crash', 'message': 'timeout'}
    if q.returncode:
        message, site, where = first_error(q.stderr)
        if not message:
            return {'state': 'crash', 'message': 'status %d' % q.returncode}
        return {'state': 'refused', 'message': message, 'site': site,
                'where': where}
    a = symbols(ref)
    b = symbols(ours)
    if a == b:
        return {'state': 'ok'}
    return {'state': 'symbols', 'missing': sorted(set(a) - set(b)),
            'extra': sorted(set(b) - set(a))}


def key_of(vpp, name, target, flags):
    s = os.stat(os.path.join(SUITE, name))
    v = os.stat(vpp)
    c = os.stat(os.path.realpath(CLANG))
    return json.dumps([2, name, s.st_size, int(s.st_mtime), v.st_size,
                       int(v.st_mtime), c.st_size, target, flags])


def group_of(r):
    if r['state'] == 'refused':
        text = re.sub(r'\s+', ' ', r['message'])
        if r.get('site'):
            text += ' (%s)' % r['site']
        return 'refused: ' + text[:120]
    if r['state'] in ('skip', 'crash'):
        return r['state']
    return r['state']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN') or
                    os.path.join(R, 'build', 'linux', 'src'))
    ap.add_argument('-j', type=int, default=3)
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--bless', action='store_true')
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?')
    a = ap.parse_args()
    vpp = os.path.join(a.bin_dir, 'vpp')
    if not os.path.isdir(SUITE):
        print('cxx-mangle: no %s (bench/fetch-testsuites.sh llvm)' % SUITE)
        return 0
    cache = {}
    if os.path.exists(CACHE) and not a.force:
        with open(CACHE) as f:
            cache = json.load(f)
    work = []
    results = {}
    for name in candidates():
        if a.pattern and a.pattern not in name:
            continue
        config = configuration(os.path.join(SUITE, name))
        if not config:
            results[name] = {'state': 'skip',
                             'message': 'no Itanium RUN line for x86-64 '
                                        'or aarch64 Linux'}
            continue
        target, flags = config
        key = key_of(vpp, name, target, flags)
        r = cache.get(name)
        if r and r.get('key') == key:
            results[name] = r
            continue
        work.append((name, target, flags, key))
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as pool:
        futures = {pool.submit(compile_one, vpp, n, t, f, a.timeout):
                   (n, k) for n, t, f, k in work}
        for fut in concurrent.futures.as_completed(futures):
            n, k = futures[fut]
            r = fut.result()
            r['key'] = k
            cache[n] = r
            results[n] = r
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, 'w') as f:
        json.dump(cache, f, indent=1, sort_keys=True)

    for name in sorted(results):
        r = results[name]
        if not a.v and r['state'] != 'symbols':
            continue
        if r['state'] == 'symbols':
            print('%s: %d missing in vcc, %d extra in vcc'
                  % (name, len(r['missing']), len(r['extra'])))
            if a.v:
                for s in r['missing'][:5]:
                    print('    - ' + s)
                for s in r['extra'][:5]:
                    print('    + ' + s)
        else:
            detail = r.get('message', '')
            if r.get('site'):
                detail += ' (%s)' % r['site']
            if r.get('where'):
                detail = r['where'] + ': ' + detail
            print('%s: %s %s' % (name, r['state'], detail[:160]))

    counted = {n: r for n, r in results.items() if r['state'] != 'skip'}
    totals = {}
    for n, r in sorted(counted.items()):
        c = r.get('counts', {})
        for k, v in c.items():
            totals[k] = totals.get(k, 0) + v
        if a.v and c:
            print('  %-50s clang %4d: verified %4d, refuted %3d, '
                  'unsupported %3d, missing %4d'
                  % (n, c['clang'], c['verified'], c['refuted'],
                     c['unsupported'], c['missing']))
            for x in r.get('refuted_symbols', [])[:5]:
                print('      refuted ' + x)
    if totals:
        print('cxx-mangle: symbols: clang defines %d; vcc: %d verified, '
              '%d refuted, %d unsupported, %d not emitted by clang; '
              '%d of clang\'s missing'
              % (totals['clang'], totals['verified'], totals['refuted'],
                 totals['unsupported'], totals['not_emitted'],
                 totals['missing']))
    passing = sorted(n for n, r in counted.items() if r['state'] == 'ok')
    print('cxx-mangle: %d files, %d compared: %d ok, %d symbols differ, '
          '%d refused, %d crash; %d skipped'
          % (len(results), len(counted), len(passing),
             sum(1 for r in counted.values() if r['state'] == 'symbols'),
             sum(1 for r in counted.values() if r['state'] == 'refused'),
             sum(1 for r in counted.values() if r['state'] == 'crash'),
             len(results) - len(counted)))
    groups = {}
    for n, r in counted.items():
        if r['state'] != 'ok':
            groups.setdefault(group_of(r), []).append(n)
    for g, names in sorted(groups.items(), key=lambda x: (-len(x[1]), x[0])):
        print('%4d  %s' % (len(names), g))
        if a.v:
            print('      ' + ' '.join(sorted(names)))

    if a.pattern:
        return 0
    expected = set()
    if os.path.exists(PASSING):
        with open(PASSING) as f:
            expected = set(l.strip() for l in f
                           if l.strip() and not l.startswith('#'))
    lost = sorted(expected - set(passing))
    gained = sorted(set(passing) - expected)
    for n in lost:
        print('REGRESSION %s: passed before (bench/cxx-mangle.passing)' % n)
    for n in gained:
        print('new pass %s (--bless to keep it)' % n)
    if a.bless:
        with open(PASSING, 'w') as f:
            f.write('# bench/cxx-mangle.py: clang mangling tests whose '
                    'symbols are clang\'s\n'
                    '# (rewritten by --bless; one that stops passing is an '
                    'error)\n')
            for n in passing:
                f.write(n + '\n')
        return 0
    return 1 if lost else 0


if __name__ == '__main__':
    sys.exit(main())
