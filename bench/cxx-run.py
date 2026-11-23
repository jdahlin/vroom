#!/usr/bin/env python3
"""cxx-run.py: the C++ front end's code against clang's (doc/PLAN-cxx-sema.md S1d).

Each program of src/test/cxx/run, and each generated one (GENERATED below:
too large to keep as a file), is compiled twice for Linux arm64: by
clang++ -O2, and by vcc's driver (vpp -c: frontend/cplusplus, lowered to
C's AST, C's code generator and object writer; S1e) at each -O level asked
for, with the same flags.  Both are linked with the C suites' run time (src/test/rt) and run
under qemu-system-aarch64 with semihosting, as bench/vcc-ctests.py does;
the exit status and output must be clang's.  The functions and variables
the -O0 object defines are also clang -O0's, by name and binding (global,
local, weak).  A program of several translation units names the others in
/* cxx-run-units: FILE... */ (files of src/test/cxx/run/units, which hold
their headers too); each compiler compiles them as the program and links
them with it.

    bench/cxx-run.py [-j N] [-O LEVELS] [-v] [PATTERN]

-O takes the levels as one argument (default 0123).  A program that vcc
does not compile is a failure with its reason.  A program named in
src/test/cxx/run/xfail.txt (NAME, a tab, vcc's first error) is expected to
fail at some level: it is counted, not a failure, and passing at every level
is an error (XPASS), so the fix removes its line.  old-*.cpp are the first
C++ front end's run programs (removed in 7283c431), most of them listed there.
"""
import importlib.util, os, re, shlex, subprocess, sys
from multiprocessing import Pool

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

R = os.path.dirname(B)
TESTS = f'{R}/src/test/cxx/run'
VPP = ct.VPP
OUT = f'{ct.BUILD}/cxx-run'
NM = ct.llvm('llvm-nm')
TARGET = ['--target=aarch64-linux-gnu', '-nostdinc', '-fno-PIE',
          '-fno-asynchronous-unwind-tables', '-fno-stack-protector',
          '-mno-outline-atomics']
# the C run time's headers (src/test/rt/include, extern "C" for C++)
CXX = TARGET + ['-I', f'{ct.RT}/include', '-isystem', ct.RES,
                '-x', 'c++', '-std=c++20', '-fno-exceptions', '-fno-rtti', '-w']


def defined(obj):
    """name -> 'global' or 'local' for each function and object defined"""
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
        symbols[parts[2]] = binding
    return symbols


def run(objs, elf):
    rc, err = ct.link(objs, elf)
    if rc:
        return None, 'link: ' + ct.first_err(err)
    return ct.qemu(elf), None


def macro_parameters():
    """[cpp.replace]: a function-like macro with 70,000 parameters whose
    replacement list names parameters past 32,767 and 65,535 (the front end
    once kept a parameter's index in 16 bits), also as the operand of #"""
    n = 70000
    parameters = ','.join(f'p{i}' for i in range(n))
    arguments = ','.join(str(i % 7) for i in range(n))
    return (f'#define M({parameters}) \\\n'
            f'    (p1 + p32768 * 2 + p40001 * 3 + p65536 * 5 + p{n - 1} * 7 + \\\n'
            f'     (int)sizeof(#p65540 #p{n - 2}))\n'
            f'int main() {{\n    return (M({arguments})) & 255;\n}}\n')


GENERATED = {'generated-macro-parameters': macro_parameters}


def one(job):
    name, levels = job
    src = f'{TESTS}/{name}.cpp'
    wd = f'{OUT}/{name}'
    os.makedirs(wd, exist_ok=True)
    if name in GENERATED:
        src = f'{wd}/{name}.cpp'
        with open(src, 'w') as f:
            f.write(GENERATED[name]())
    results = []
    # /* cxx-run-flags: ... */ in the test: flags for both compilers, after
    # the common ones (-std=c++17 replaces -std=c++20)
    with open(src, errors='replace') as f:
        head = f.read(2000)
    found = re.search(r'cxx-run-flags:(.*?)\*/', head)
    cxx = CXX + (shlex.split(found.group(1)) if found else [])
    found = re.search(r'cxx-run-units:(.*?)\*/', head)
    units = [f'{TESTS}/units/{u}' for u in (found.group(1).split() if found else [])]

    def others(compiler, flags, tag):
        """the other units' objects, or the first error"""
        objs = []
        for i, u in enumerate(units):
            o = f'{wd}/{tag}-unit{i}.o'
            rc, _, err = ct.sh([compiler] + cxx + flags + ['-c', u, '-o', o])
            if rc:
                return None, (err.decode().strip().splitlines() or ['exit %s' % rc])[-1]
            objs.append(o)
        return objs, None

    rc, _, err = ct.sh([ct.CL] + cxx + ['-O2', '-c', src, '-o', f'{wd}/ref.o'])
    objs, why = others(ct.CL, ['-O2'], 'ref')
    if rc or why:
        return name, [('ref', 'oracle: ' + (why or ct.first_err(err.decode())))]
    expected, why = run([f'{wd}/ref.o'] + objs, f'{wd}/ref.elf')
    if why:
        return name, [('ref', 'oracle ' + why)]
    rc, _, err = ct.sh([ct.CL] + cxx + ['-O0', '-c', src, '-o', f'{wd}/ref0.o'])
    symbols = defined(f'{wd}/ref0.o') if not rc else None
    for level in levels:
        o, elf = f'{wd}/O{level}.o', f'{wd}/O{level}.elf'
        rc, _, err = ct.sh([VPP] + cxx + [f'-O{level}', '-c', src, '-o', o])
        if rc:
            results.append((level, 'vcc: ' + (err.decode().strip().splitlines() or ['exit %s' % rc])[-1]))
            continue
        objs, why = others(VPP, [f'-O{level}'], f'O{level}')
        if why:
            results.append((level, 'vcc: ' + why))
            continue
        ours = defined(o)
        # (above -O0 an inlined static function may be gone)
        if level == '0' and symbols is not None and ours != symbols:
            missing = sorted(set(symbols) - set(ours))
            extra = sorted(set(ours) - set(symbols))
            bound = sorted(k for k in ours if k in symbols and ours[k] != symbols[k])
            results.append((level, f'symbols: missing {missing} extra {extra} binding {bound}'))
            continue
        got, why = run([o] + objs, elf)
        if why:
            results.append((level, why))
        elif got != expected:
            results.append((level, f'ran {got!r}, clang {expected!r}'))
        else:
            results.append((level, None))
    return name, results


def main():
    av = sys.argv[1:]
    jobs, levels, verbose, pattern = 6, '0123', False, ''
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
        else:
            pattern = a
    if not os.path.exists(VPP):
        sys.exit(f'cxx-run: no {VPP} (make -C src vcc)')
    ct.build_rt()
    names = sorted([f[:-4] for f in os.listdir(TESTS)
                    if f.endswith('.cpp') and re.search(pattern, f)] +
                   [n for n in GENERATED if re.search(pattern, n + '.cpp')])
    xfail = {}
    with open(f'{TESTS}/xfail.txt') as f:
        for line in f:
            if line.strip() and not line.startswith('#'):
                n, _, why = line.rstrip('\n').partition('\t')
                xfail[n] = why
    passed = failed = expected = 0
    with Pool(jobs) as pool:
        for name, results in pool.imap(one, [(n, levels) for n in names]):
            if name in xfail:
                bad = [(lv, w) for lv, w in results if w is not None]
                if bad:
                    expected += 1
                    if verbose:
                        print(f'XFAIL {name} -O{bad[0][0]}: {bad[0][1]}')
                else:
                    failed += 1
                    print(f'XPASS {name}: remove it from xfail.txt')
                continue
            for level, why in results:
                if why is None:
                    passed += 1
                    if verbose:
                        print(f'PASS {name} -O{level}')
                else:
                    failed += 1
                    print(f'FAIL {name} -O{level}: {why}')
    print(f'cxx-run: {passed} passed, {failed} failed, {expected} programs expected to fail ({len(names)} programs, -O{levels})')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
