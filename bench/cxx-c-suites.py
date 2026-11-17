#!/usr/bin/env python3
"""cxx-c-suites.py: the C suites as C++ programs (doc/PLAN-cxx-sema.md C5.1).

Each program of the C suites bench/vcc-ctests.py runs (gcc torture, tcc
tests2, inlining, vcc's own; with its skips and per-test flags) is
compiled as C++20 for Linux arm64 (its flags without a C -std= or
-ansi).  A program belongs to the corpus when clang++ -O0 compiles and
links it.  Its C library is the C run time of src/test/rt, whose headers
have no extern "C" yet (vcc does not implement [dcl.link] yet), and the
tests declare abort, exit, printf, ... themselves: so a function of the
global namespace that the program declares with C++ linkage and that the
run time defines under its plain name is linked to it (--defsym, for
clang++'s link and vcc's alike), as a C header's extern "C" would have
named it.  Out too: a program that flows off the end of a non-void
function (undefined behaviour in C++, [stmt.return]/2; clang++'s
-Wreturn-type), and those in CXX_SKIP.  clang++ -O2's build, run under
qemu, gives the expected exit status and output.

vcc's side is the driver, `vpp -x c++ -c`, at each -O level asked for: the
program must run as clang++'s, and at -O0 its object must define clang++
-O0's functions and variables, by name and binding; a local (internal)
variable clang++ folded every use of and so did not emit is allowed.  C5.1 asked for files
inside extern "C" compared with the C front end's objects, but `main`
"shall not be declared with a linkage-specification" ([basic.start.main]/3),
so files are compiled as they are and compared with clang++.

Outcomes per program and level: pass; wrong (ran differently: a bug);
symbols (the -O0 object defines other symbols: a bug); rejected (an error
vcc gives on a program clang++ accepts that is not "not implemented yet":
a bug of our parser or analysis, or a missing extension); and not yet (the
analysis or lowering does not handle a construct yet), counted by the
subclause vcc names.

The programs that pass are kept in bench/cxx-c-suites.passing, so that
one that stops passing is an error (--bless rewrites the list after an
improvement).

    bench/cxx-c-suites.py [-j N] [-O LEVELS] [-v] [--bless] [--no-cache] [PATTERN]

-O takes the levels as one argument (default 02).  clang++'s results are
cached in build/src/cxx-c-suites/ref (keyed by the source, its flags and
the run time); a vcc object byte-identical to one that passed is not
linked or run again (--no-cache: run all).
"""
import collections, hashlib, importlib.util, json, os, re, sys
from multiprocessing import Pool

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

R = os.path.dirname(B)
OUT = f'{ct.BUILD}/cxx-c-suites'
PASSING = f'{B}/cxx-c-suites.passing'
NM = ct.llvm('llvm-nm')
FLAGS = ['--target=aarch64-linux-gnu', '-nostdinc', '-I', f'{ct.RT}/include', '-isystem', ct.RES,
         '-fno-PIE', '-fno-asynchronous-unwind-tables', '-fno-stack-protector',
         '-mno-outline-atomics', '-x', 'c++', '-std=c++20', '-fno-exceptions', '-fno-rtti',
         '-w', '-fno-strict-aliasing', '-ffp-contract=off']
# programs whose result is not C++'s to give
CXX_SKIP = {
    ('gcc', '20031003-1'): 'converts a float out of int\'s range (undefined)',
    ('gcc', 'pr79286'): 'passes an uninitialized int to printf: undefined in C++ '
                        '([basic.indet]/2), clang++ -O2 deletes main',
}
NOT_YET = re.compile(r'error: (?:the rules of \[([^\]]+)\] are not implemented yet|'
                     r'C\+\+ code generation does not handle (.*) yet)')

state = {}


def run_time_symbols():
    """the names the C run time defines"""
    names = set()
    for o in ('rt.o', 'start.o'):
        rc, out, _ = ct.sh([NM, '--defined-only', f'{ct.OUT}/rt/{o}'])
        names |= {l.split()[2] for l in out.decode().splitlines() if len(l.split()) == 3}
    return names


def link(objs, elf):
    """ct.link, each undefined C++ name of a run-time function linked to it"""
    rc, err = ct.link(objs + ['--no-demangle'], elf)
    if not rc:
        return rc, err
    undefined = set(re.findall(r'undefined symbol: (_Z\w+)', err))
    aliases = []
    for u in sorted(undefined):
        m = re.match(r'_Z(\d+)', u)
        name = u[len(m.group(0)):][:int(m.group(1))] if m else ''
        if name not in state['rt_names']:
            return rc, err
        aliases.append(f'--defsym={u}={name}')
    if not aliases:
        return rc, err
    return ct.link(objs + ['--no-demangle'] + aliases, elf)


def defined(obj):
    """name -> (global, local or weak; function or object), for each one defined"""
    rc, out, err = ct.sh([NM, '--defined-only', obj])
    symbols = {}
    for line in out.decode().splitlines():
        parts = line.split()
        if len(parts) != 3 or parts[1].upper() not in 'TDRBWV':
            continue
        if parts[2].startswith('$') or parts[2].startswith('.'):
            continue
        binding = 'global' if parts[1].isupper() else 'local'
        if parts[1] in 'WV':
            binding = 'weak'
        kind = 'function' if parts[1] in 'Tt' else 'object'
        symbols[parts[2]] = (binding, kind)
    return symbols


def test_flags(suite, src):
    flags = []
    if suite == 'gcc':
        flags = ct.dg_flags(src)
    elif suite == 'inline':
        flags = [f for f in ct.dg_flags(src) if ct.INLINE_FLAGS.match(f)]
    elif suite == 'vcc':
        flags = ct.exec_marks(src)[0]
    # (a C standard: the program is compiled as C++20)
    return [f for f in flags if not f.startswith('-std=') and f != '-ansi']


def reference(suite, name, path, flags, wd):
    """clang++'s verdict: {'valid': bool, 'why', 'run': [rc, out], 'symbols'}"""
    src = open(path, 'rb').read()
    key = hashlib.sha1(b'cxx-c-suites-v3 ' + os.path.relpath(path, R).encode() + b' ' + src
                       + ' '.join(flags + FLAGS).encode() + state['rt'].encode()).hexdigest()[:16]
    cf = f'{OUT}/ref/{suite}/{name}.json'
    try:
        r = json.load(open(cf))
        if r.get('key') == key:
            return r
    except (OSError, ValueError):
        pass
    r = {'key': key, 'valid': False}
    # (without -w: -Wreturn-type alone, as an error)
    strict = [f for f in FLAGS if f != '-w'] + ['-Wno-everything', '-Werror=return-type']
    rc, _, err = ct.sh([ct.CL] + strict + flags + ['-O0', '-c', path, '-o', f'{wd}/ref0.o'])
    if rc and 'Wreturn-type' in err.decode(errors='replace'):
        r['why'] = 'flows off the end of a non-void function'
    elif rc:
        r['why'] = 'clang++ rejects: ' + ct.first_err(err.decode(errors='replace'))
    else:
        rc, err = link([f'{wd}/ref0.o'], f'{wd}/ref0.elf')
        if rc:
            r['why'] = 'clang++ -O0 does not link: ' + ct.first_err(err)
        else:
            rc, _, err = ct.sh([ct.CL] + FLAGS + flags + ['-O2', '-c', path, '-o', f'{wd}/ref.o'])
            rc2, err2 = link([f'{wd}/ref.o'], f'{wd}/ref.elf') if not rc else (1, '')
            if rc or rc2:
                r['why'] = 'clang++ -O2 does not build'
            else:
                r['valid'] = True
                r['run'] = list(ct.qemu(f'{wd}/ref.elf'))
                r['symbols'] = defined(f'{wd}/ref0.o')
    if 'symbols' in r:
        r['symbols'] = {k: list(v) for k, v in r['symbols'].items()}
    os.makedirs(os.path.dirname(cf), exist_ok=True)
    json.dump(r, open(cf, 'w'))
    return r


def ours(suite, name, path, flags, level, ref, wd):
    """(outcome, detail) of vcc at one level"""
    o, elf = f'{wd}/O{level}.o', f'{wd}/O{level}.elf'
    rc, _, err = ct.sh([ct.VPP] + FLAGS + flags + [f'-O{level}', '-c', path, '-o', o])
    if rc:
        text = err.decode(errors='replace')
        m = NOT_YET.search(text)
        if m and m.group(1):
            return 'not yet', f'[{m.group(1)}]'
        if m:
            return 'not yet', m.group(2)
        return 'rejected', ct.first_err(text)
    if level == '0':
        got = {k: list(v) for k, v in defined(o).items()}
        theirs = ref['symbols']
        missing = sorted(set(theirs) - set(got))
        extra = sorted(k for k in set(got) - set(theirs) if got[k] != ['local', 'object'])
        bound = sorted(k for k in got if k in theirs and got[k][0] != theirs[k][0])
        if missing or extra or bound:
            return 'symbols', f'missing {missing} extra {extra} binding {bound}'
    digest = hashlib.sha1(open(o, 'rb').read()).hexdigest()
    cache = f'{wd}/O{level}.passed'
    if not state['nocache'] and os.path.exists(cache) and open(cache).read() == digest:
        return 'pass', ''
    rc, err = link([o], elf)
    if rc:
        return 'rejected', 'link: ' + ct.first_err(err)
    got = list(ct.qemu(elf))
    if [got[0], ct.norm(got[1])] != [ref['run'][0], ct.norm(ref['run'][1])]:
        return 'wrong', f'exit {got[0]}, clang++ {ref["run"][0]}'
    open(cache, 'w').write(digest)
    return 'pass', ''


def one(t):
    suite, name, path = t
    wd = f'{OUT}/{suite}/{name}'
    os.makedirs(wd, exist_ok=True)
    src = open(path, 'rb').read().decode('latin-1')
    flags = test_flags(suite, src)
    ref = reference(suite, name, path, flags, wd)
    if not ref['valid']:
        return t, None, ref['why']
    results = [(level, *ours(suite, name, path, flags, level, ref, wd))
               for level in state['levels']]
    # a known bug of vcc's code generation (/* vcc-xfail: */, TODO.md) that
    # the C suite counts as xfail is not the C++ front end's
    if suite == 'vcc' and ct.exec_marks(src)[1]:
        results = [(level, 'not yet', 'the C suite\'s vcc-xfail') if outcome == 'wrong'
                   else (level, outcome, detail) for level, outcome, detail in results]
    return t, results, None


def init(levels, nocache, rt, rt_names):
    state.update(levels=levels, nocache=nocache, rt=rt, rt_names=rt_names)


def main():
    av = sys.argv[1:]
    jobs, levels, verbose, bless, nocache, pattern = 6, '02', False, False, False, ''
    while av:
        a = av.pop(0)
        if a == '-j':
            jobs = int(av.pop(0))
        elif a.startswith('-j'):
            jobs = int(a[2:])
        elif a == '-O':
            levels = av.pop(0)
        elif a == '-v':
            verbose = True
        elif a == '--bless':
            bless = True
        elif a == '--no-cache':
            nocache = True
        else:
            pattern = a
    if not os.path.exists(ct.VPP):
        sys.exit(f'cxx-c-suites: no {ct.VPP} (make -C src vcc)')
    rt = ct.build_rt()
    skip = {('tcc', n) for n in ct.TCC_SKIP} | {('gcc', n) for n in ct.GCC_SKIP} | \
           {('inline', n) for n in ct.INLINE_SKIP} | set(CXX_SKIP)
    tests = [t for t in ct.tests(None, pattern) if (t[0], t[1]) not in skip]
    with Pool(jobs, initializer=init, initargs=(levels, nocache, rt, run_time_symbols())) as pool:
        results = pool.map(one, tests)

    outcomes = collections.Counter()
    reasons = collections.Counter()
    invalid = 0
    passing = set()
    bugs = []
    for (suite, name, _), res, why in results:
        if res is None:
            invalid += 1
            continue
        for level, outcome, detail in res:
            outcomes[outcome] += 1
            label = f'{suite}/{name} -O{level}'
            if outcome == 'pass':
                passing.add(label)
                if verbose:
                    print(f'PASS {label}')
            elif outcome == 'not yet':
                reasons[detail] += 1
                if verbose:
                    print(f'NOT YET {label}: {detail}')
            else:
                bugs.append(f'{outcome.upper()} {label}: {detail}')
    for b in bugs:
        print(b)

    expected = set()
    if os.path.exists(PASSING) and not pattern:
        expected = {l.strip() for l in open(PASSING) if l.strip() and not l.startswith('#')}
        expected = {l for l in expected if l.rsplit(' -O', 1)[-1] in levels}
    lost = sorted(expected - passing)
    gained = sorted(passing - expected)
    for l in lost:
        print(f'REGRESSION {l}: passed before (bench/cxx-c-suites.passing)')
    valid = len(tests) - invalid
    print(f'cxx-c-suites: {len(tests)} programs, {valid} valid C++ (clang++ -O0 builds them), '
          f'-O{levels}: {outcomes["pass"]} pass, {outcomes["wrong"]} wrong, '
          f'{outcomes["symbols"]} symbols, {outcomes["rejected"]} rejected, '
          f'{outcomes["not yet"]} not yet')
    if reasons:
        print('not yet, by what is missing: ' + ', '.join(f'{r} {n}' for r, n in reasons.most_common(15)))
    if bless and not pattern:
        with open(PASSING, 'w') as f:
            f.write('# bench/cxx-c-suites.py: the C suites\' programs that run as clang++\'s\n'
                    '# (rewritten by --bless; one that stops passing is an error)\n')
            for l in sorted(passing):
                f.write(l + '\n')
        print(f'cxx-c-suites: {len(passing)} passing written to {os.path.relpath(PASSING, R)}')
    elif gained and not pattern:
        print(f'cxx-c-suites: {len(gained)} pass that bench/cxx-c-suites.passing does not list (--bless)')
    sys.exit(1 if lost or outcomes['wrong'] or outcomes['symbols'] else 0)


if __name__ == '__main__':
    main()
