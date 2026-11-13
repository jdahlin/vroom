#!/usr/bin/env python3
"""vcc-abi.py: cross-compiler calls (TODO.md T2), Linux under qemu or the Darwin host.

  bench/vcc-abi.py [-v] [--host | --target wasm64|wasm32|x86_64 [--vlink]] [PAT]

--generated dispatches to vcc-abi-generated.py for the pinned GCC/LLVM
cross-call generators. It accepts --count, --start, --suite, -j and Linux
--target triples (or x86_64). See that harness's --help for coverage.

A caller comment `/* abi-targets: linux; reason */` requires the fixture only
on Linux; other targets report the explicit contract exclusion, never a pass.

Every src/tests/abi/NAME/ has caller.c (main, the checks) and callee.c
(the functions it calls, and callbacks into the caller); a header there is
on the include path. Each half can specify extra compiler options with
`/* vcc-flags: ... */`, as in the C runtime suite.
Each half is built by clang and by vcc, and all four
combinations are linked with the bare-metal runtime (tests/rt) and run as
bench/vcc-ctests.py does.  A test passes when every combination exits 0
and prints what clang x clang prints; clang x clang failing is a broken
test.  A line `/* vcc-xfail: TODO item */` in caller.c marks a known bug
(a pass is XPASS, an error); `/* vcc-xfail-host: ... */` the same for
--host only.  Exit status 1 on any failure.
--host (macOS only): both halves built for the host (Apple arm64 ABI,
system headers), linked by clang and run natively.
--target wasm64 or wasm32: both halves built for wasm*-unknown-emscripten as
bench/vcc-wasm.py builds them (vcc's side by `vpp -c`, its own assembler and
object writer), linked by emcc (with --vlink: vlink as wasm-ld) and run under
node.  `/* vcc-xfail-wasm: ... */` marks a known bug there, `/* vcc-skip-wasm32:
reason */` a test that does not apply to ILP32.
--target x86_64 (an x86-64 Linux host): both halves built for x86_64-linux-gnu
as bench/vcc-x86.py builds them (vcc's side by `vpp -S` and clang's
assembler), linked against glibc and run natively; `/* vcc-xfail-x86: ... */`
marks a known bug there.
"""
import importlib.util, os, re, shutil, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec); spec.loader.exec_module(ct)
D = f'{ct.T}/abi'


HOST = False
WASM = None	# the vcc-wasm.py module, with its target set, for --target wasm64/wasm32
X86 = None	# the vcc-x86.py module, for --target x86_64
VLINK = False
HOSTFL = ['-O2', '-fno-stack-protector']


def compile_(cc, src, obj, inc):
    base = HOSTFL if HOST else WASM.BASE if WASM else X86.BASE if X86 else ct.BASE
    flags, _ = ct.exec_marks(open(src).read())
    base = [*base, *flags]
    if X86 and cc != 'clang':
        rc, _, err = ct.sh([ct.VPP, '-S'] + base + ['-I', inc, src, '-o', obj + '.s'])
        if not rc:
            rc, _, err = ct.sh([ct.CL, X86.TGT, '-c', obj + '.s', '-o', obj])
        return rc, err.decode(errors='replace')
    if cc == 'clang':
        cmd = [ct.CL] + base + ct.CLFL + ['-I', inc, '-c', src, '-o', obj]
    else:
        cmd = [ct.VPP] + base + ['-I', inc, '-c', src, '-o', obj]
    rc, _, err = ct.sh(cmd)
    return rc, err.decode(errors='replace')


def one(name, wd, verbose):
    d = f'{D}/{name}'
    src = open(f'{d}/caller.c').read()
    contract = re.search(r'/\*\s*abi-targets:\s*linux;(.*?)\*/', src, re.S)
    if contract and (HOST or WASM):
        return 'skip', 'Linux-only ABI contract: ' + ' '.join(contract.group(1).split()), None
    xf = re.search(r'/\*\s*vcc-xfail:(.*?)\*/', src, re.S) or (HOST and re.search(r'/\*\s*vcc-xfail-host:(.*?)\*/', src, re.S)) \
        or (WASM and re.search(r'/\*\s*vcc-xfail-wasm:(.*?)\*/', src, re.S)) \
        or (X86 and re.search(r'/\*\s*vcc-xfail-x86:(.*?)\*/', src, re.S))
    xf = ' '.join(xf.group(1).split()) if xf else None
    if WASM and 'wasm32' in WASM.TGT:
        sk = re.search(r'/\*\s*vcc-skip-wasm32:(.*?)\*/', src, re.S)
        if sk:
            return 'skip', ' '.join(sk.group(1).split()), None
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
            if HOST:
                rc, _, err = ct.sh([ct.CL, objs['caller', a], objs['callee', b], '-o', elf])
                err = err.decode(errors='replace')
            elif WASM:
                elf = f'{wd}/{a}-{b}.js'
                rc, err = WASM.link([objs['caller', a], objs['callee', b]], elf, VLINK)
            elif X86:
                rc, err = X86.link([objs['caller', a], objs['callee', b]], elf)
            else:
                rc, err = ct.link([objs['caller', a], objs['callee', b]], elf)
            if rc:
                return 'fail', f'link {a}x{b}: {ct.first_err(err)}', xf
            if WASM:
                rc, out = WASM.run(elf)
            elif X86:
                rc, out = X86.run(elf)
            elif HOST:
                rc, out, _ = ct.sh([elf], timeout=20)
                out = out.decode('latin-1')
            else:
                rc, out = ct.qemu(elf)
            if (a, b) == ('clang', 'clang'):
                if rc != 0:
                    return 'test', f'clang x clang exits {rc}: {out.strip()[:200]}', xf
                ref = out
            elif rc != 0 or out != ref:
                bad.append(f'caller {a} x callee {b}: exit {rc}' + (f': {out.strip()[:300]}' if verbose or out != ref else ''))
    return ('fail' if bad else 'pass'), '; '.join(bad), xf


def main():
    global HOST
    av = sys.argv[1:]
    if '--generated' in av:
        av.remove('--generated')
        if '--target' in av:
            i = av.index('--target') + 1
            if i < len(av) and av[i] == 'x86_64':
                av[i] = 'x86_64-linux-gnu'
        os.execv(sys.executable, [sys.executable, f'{B}/vcc-abi-generated.py', *av])
    verbose = '-v' in av
    HOST = '--host' in av
    av = [a for a in av if a not in ('-v', '--host')]
    global WASM, VLINK
    VLINK = '--vlink' in av
    av = [a for a in av if a != '--vlink']
    if '--target' in av:
        i = av.index('--target')
        t = av[i + 1]
        del av[i:i + 2]
        if t not in ('wasm64', 'wasm32', 'x86_64'):
            sys.exit(f'vcc-abi: --target {t}: wasm64, wasm32 or x86_64')
        if t == 'x86_64':
            global X86
            spec = importlib.util.spec_from_file_location('vx', f'{B}/vcc-x86.py')
            X86 = importlib.util.module_from_spec(spec); spec.loader.exec_module(X86)
            X86.rt_obj()
        else:
            spec = importlib.util.spec_from_file_location('vw', f'{B}/vcc-wasm.py')
            WASM = importlib.util.module_from_spec(spec); spec.loader.exec_module(WASM)
            WASM.set_target(t == 'wasm32')
            WASM.rt_obj()
            if VLINK:
                WASM.VLENV = WASM.vlink_env()
    pat = av[0] if av else None
    if not HOST and not WASM and not X86:
        ct.build_rt()
    names = sorted(n for n in os.listdir(D) if os.path.isdir(f'{D}/{n}') and (not pat or re.search(pat, n)))
    tmp = tempfile.mkdtemp(prefix='vcc-abi-')
    fails = passes = xfails = skips = 0
    for n in names:
        wd = f'{tmp}/{n}'
        os.makedirs(wd)
        st, why, xf = one(n, wd, verbose)
        if st == 'skip':
            skips += 1
            print(f'skip  {n}: {why}', flush=True)
            continue
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
    print(f'abi{" (host)" if HOST else f" ({WASM.TGT[9:]})" if WASM else " (x86_64)" if X86 else ""}: {passes} pass, {xfails} xfail, {fails} fail' + (f', {skips} skipped' if skips else ''))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
