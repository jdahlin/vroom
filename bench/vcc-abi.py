#!/usr/bin/env python3
"""vcc-abi.py: cross-compiler calls (TODO.md T2), Linux under qemu.

  bench/vcc-abi.py [-v] [PAT]

Every ccs/vcc/tests/abi/NAME/ has caller.c (main, the checks) and callee.c
(the functions it calls, and callbacks into the caller); a header there is
on the include path.  Each half is built by clang and by vcc, and all four
combinations are linked with the bare-metal runtime (tests/rt) and run as
bench/vcc-ctests.py does.  A test passes when every combination exits 0
and prints what clang x clang prints; clang x clang failing is a broken
test.  A line `/* vcc-xfail: TODO item */` in caller.c marks a known bug
(a pass is XPASS, an error).  Exit status 1 on any failure.
The Darwin host half of T2 (native, system libc) is not here yet.
"""
import importlib.util, os, re, shutil, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec); spec.loader.exec_module(ct)
D = f'{ct.T}/abi'


def compile_(cc, src, obj, inc):
    if cc == 'clang':
        cmd = [ct.CL] + ct.BASE + ct.CLFL + ['-I', inc, '-c', src, '-o', obj]
    else:
        cmd = [ct.VPP] + ct.BASE + ['-I', inc, '-c', src, '-o', obj]
    rc, _, err = ct.sh(cmd)
    return rc, err.decode(errors='replace')


def one(name, wd, verbose):
    d = f'{D}/{name}'
    xf = re.search(r'/\*\s*vcc-xfail:(.*?)\*/', open(f'{d}/caller.c').read(), re.S)
    xf = ' '.join(xf.group(1).split()) if xf else None
    objs = {}
    for half in ('caller', 'callee'):
        for cc in ('clang', 'vcc'):
            o = f'{wd}/{half}.{cc}.o'
            rc, err = compile_(cc, f'{d}/{half}.c', o, d)
            if rc:
                return ('test' if cc == 'clang' else 'fail'), f'{cc} {half}.c: {ct.first_err(err)}', xf
            objs[half, cc] = o
    ref = None
    bad = []
    for a in ('clang', 'vcc'):
        for b in ('clang', 'vcc'):
            elf = f'{wd}/{a}-{b}.elf'
            rc, err = ct.link([objs['caller', a], objs['callee', b]], elf)
            if rc:
                return 'fail', f'link {a}x{b}: {ct.first_err(err)}', xf
            rc, out = ct.qemu(elf)
            if (a, b) == ('clang', 'clang'):
                if rc != 0:
                    return 'test', f'clang x clang exits {rc}: {out.strip()[:200]}', xf
                ref = out
            elif rc != 0 or out != ref:
                bad.append(f'caller {a} x callee {b}: exit {rc}' + (f': {out.strip()[:300]}' if verbose or out != ref else ''))
    return ('fail' if bad else 'pass'), '; '.join(bad), xf


def main():
    av = sys.argv[1:]
    verbose = '-v' in av
    av = [a for a in av if a != '-v']
    pat = av[0] if av else None
    ct.build_rt()
    names = sorted(n for n in os.listdir(D) if os.path.isdir(f'{D}/{n}') and (not pat or re.search(pat, n)))
    tmp = tempfile.mkdtemp(prefix='vcc-abi-')
    fails = passes = xfails = 0
    for n in names:
        wd = f'{tmp}/{n}'
        os.makedirs(wd)
        st, why, xf = one(n, wd, verbose)
        if st == 'test':
            st = 'FAIL'; why = 'broken test: ' + why; fails += 1
        elif xf and st == 'fail':
            st = 'xfail'; why = f'{xf} [{why}]'; xfails += 1
        elif xf and st == 'pass':
            st = 'XPASS'; why = f'passes now: remove vcc-xfail ({xf})'; fails += 1
        elif st == 'fail':
            st = 'FAIL'; fails += 1
        else:
            passes += 1
        if st != 'pass' or verbose:
            print(f'{st:5} {n}: {why}', flush=True)
    shutil.rmtree(tmp, ignore_errors=True)
    print(f'abi: {passes} pass, {xfails} xfail, {fails} fail')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
