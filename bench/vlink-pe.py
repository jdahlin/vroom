#!/usr/bin/env python3
"""vlink-pe.py: vlink's PE/COFF linker (MinGW x86-64) against ld.lld -m i386pep (doc/PLAN-win64.md WIN7).

  bench/vlink-pe.py [-v] [--run] [--keep] [PAT]

Each case of CASES links small objects compiled by clang for x86_64-w64-mingw32
with the mingw-w64 sysroot's CRT, once by ld.lld and once by vlink, with the
same arguments (clang's -static link line plus the case's options).  Both
must succeed or both fail; when both fail, vlink's first error must be lld's
(the program name aside); when both succeed, every output (the image, an
import library) must be byte-identical (SOURCE_DATE_EPOCH is fixed).  --run
also runs each pair of programs under Wine and compares exit code and output.
"""
import os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostenv
from hostenv import BUILD, MINGW_SYSROOT, WINE, llvm

VLINK = f'{BUILD}/vlink'
LLD = llvm('ld.lld')
CL = llvm('clang')
M = MINGW_SYSROOT
L = f'{M}/x86_64-w64-mingw32/lib'
RES = hostenv.mingw_resource_dir()
RT = f'{RES}/lib/windows/libclang_rt.builtins-x86_64.a'
TGT = ['--target=x86_64-w64-mingw32', f'--sysroot={M}']

# the sources: name -> C text (compiled with -O1)
SRC = {
    'main': '#include <stdio.h>\nint main(void){ printf("hello %d\\n", 42); return 7; }\n',
    'weak': '#include <stdio.h>\n__attribute__((weak)) int wf(void){ return 1; }\n'
            'extern __attribute__((weak)) int wundef(void);\n'
            'int main(void){ printf("%d %d\\n", wf(), wundef ? 1 : 0); return 0; }\n',
    'strong': 'int wf(void){ return 2; }\n',
    'tls': '#include <stdio.h>\n__thread int t = 5; __thread char big[64] __attribute__((aligned(32)));\n'
           'int main(void){ t++; big[3] = 1; printf("%d %d\\n", t, big[3]); return 0; }\n',
    'ctor': '#include <stdio.h>\nstatic int v;\n__attribute__((constructor)) static void c1(void){ v += 1; }\n'
            '__attribute__((constructor(200))) static void c2(void){ v *= 10; }\n'
            '__attribute__((destructor)) static void d1(void){ printf("bye\\n"); }\n'
            'int main(void){ printf("%d\\n", v); return 0; }\n',
    'sel1': '#include <stdio.h>\n__declspec(selectany) int shared = 11;\nint get2(void);\n'
            'int main(void){ printf("%d %d\\n", shared, get2()); return 0; }\n',
    'sel2': '__declspec(selectany) int shared = 11;\nint get2(void){ return shared + 1; }\n',
    'common1': '#include <stdio.h>\nint cv;\nint cv2[4];\nint main(void){ printf("%d\\n", cv + cv2[1]); return 0; }\n',
    'common2': 'int cv;\nint cv2[16];\n',
    'dup1': 'int dup(void){ return 1; }\nint main(void){ return dup(); }\n',
    'dup2': 'int dup(void){ return 2; }\n',
    'undef': 'int missing(void);\nint main(void){ return missing(); }\n',
    'gc': '#include <stdio.h>\nint unused_fn(int x){ return x * 3; }\nint used(int x){ return x + 1; }\n'
          'int main(void){ printf("%d\\n", used(1)); return 0; }\n',
    'gui': '#include <windows.h>\nint WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d){ return 3; }\n',
    'lib1': 'int libf(void){ return 41; }\n',
    'lib2': 'int libg(void){ return 1; }\n',
    'uselib': '#include <stdio.h>\nint libf(void);\nint main(void){ printf("%d\\n", libf()); return 0; }\n',
    'dll': '__declspec(dllexport) int add(int a, int b){ return a + b; }\nint counter = 5;\n',
    'dllall': 'int add(int a, int b){ return a + b; }\nint counter = 5;\nint bump(void){ return ++counter; }\n',
    'impdata': '#include <stdio.h>\nextern int counter;\nint bump(void);\n'
               'int main(void){ bump(); printf("%d\\n", counter); return counter; }\n',
    'bigframe': '#include <stdio.h>\n#include <string.h>\nint main(void){ char b[100000]; memset(b, 1, sizeof b); '
                'printf("%d\\n", b[99999]); return 0; }\n',
}
FLAGS = {'gc': ['-ffunction-sections', '-fdata-sections'], 'common1': ['-fcommon'], 'common2': ['-fcommon']}


def link_line(objs, extra, shared=False):
    crt = [f'{L}/dllcrt2.o' if shared else f'{L}/crt2.o', f'{L}/crtbegin.o']
    libs = ['-lmingw32', RT, '-lmoldname', '-lmingwex', '-lmsvcrt', '-ladvapi32', '-lshell32', '-luser32', '-lkernel32']
    return ([f'--sysroot={M}', '-m', 'i386pep'] + (['--shared', '-Bdynamic', '-e', 'DllMainCRTStartup'] if shared else ['-Bstatic'])
            + crt + [f'-L{L}', f'-L{M}/lib', f'-L{RES}/lib/windows'] + objs + extra
            + ['--start-group'] + libs + ['--end-group', f'{L}/crtend.o'])


# (name, objects, extra options, shared, outputs besides the image)
CASES = [
    ('plain', ['main'], [], False, []),
    ('-s', ['main'], ['-s'], False, []),
    ('-S', ['main'], ['-S'], False, []),
    ('--strip-all', ['main'], ['--strip-all'], False, []),
    ('--gc-sections', ['gc'], ['--gc-sections'], False, []),
    ('--no-gc-sections', ['gc'], ['--gc-sections', '--no-gc-sections'], False, []),
    ('--subsystem windows', ['gui'], ['--subsystem', 'windows'], False, []),
    ('--subsystem=console:6.2', ['main'], ['--subsystem=console:6.2'], False, []),
    ('--subsystem bad', ['main'], ['--subsystem', 'frob'], False, []),
    ('--image-base', ['main'], ['--image-base', '0x10000000'], False, []),
    ('--image-base=', ['main'], ['--image-base=0x200000000'], False, []),
    ('--stack', ['bigframe'], ['--stack', '0x200000'], False, []),
    ('--stack R,C', ['main'], ['--stack', '0x200000,0x2000'], False, []),
    ('--heap', ['main'], ['--heap', '0x300000,0x3000'], False, []),
    ('os/image/subsystem versions', ['main'], ['--major-os-version', '10', '--minor-os-version', '1',
                                               '--major-image-version', '2', '--minor-image-version', '3',
                                               '--major-subsystem-version', '6', '--minor-subsystem-version', '2'], False, []),
    ('--disable-dynamicbase', ['main'], ['--disable-dynamicbase'], False, []),
    ('--disable-high-entropy-va', ['main'], ['--disable-high-entropy-va'], False, []),
    ('--disable-nxcompat', ['main'], ['--disable-nxcompat'], False, []),
    ('--disable-tsaware', ['main'], ['--disable-tsaware'], False, []),
    ('--disable-large-address-aware', ['main'], ['--disable-large-address-aware'], False, []),
    ('--no-seh', ['main'], ['--no-seh'], False, []),
    ('--disable-reloc-section', ['main'], ['--disable-reloc-section'], False, []),
    ('--no-insert-timestamp', ['main'], ['--no-insert-timestamp'], False, []),
    ('--build-id=none', ['main'], ['--build-id=none'], False, []),
    ('--build-id', ['main'], ['--build-id'], False, []),
    ('--file-alignment', ['main'], ['--file-alignment', '0x1000'], False, []),
    ('--section-alignment', ['main'], ['--section-alignment', '0x2000'], False, []),
    ('--entry', ['main'], ['--entry', 'mainCRTStartup'], False, []),
    ('-e missing', ['main'], ['-e', 'nosuchentry'], False, []),
    ('--export-all-symbols exe', ['main'], ['--export-all-symbols'], False, []),
    ('weak', ['weak'], [], False, []),
    ('weak overridden', ['weak', 'strong'], [], False, []),
    ('tls', ['tls'], [], False, []),
    ('ctors dtors', ['ctor'], [], False, []),
    ('selectany', ['sel1', 'sel2'], [], False, []),
    ('common', ['common1', 'common2'], [], False, []),
    ('duplicate', ['dup1', 'dup2'], [], False, []),
    ('--allow-multiple-definition', ['dup1', 'dup2'], ['--allow-multiple-definition'], False, []),
    ('undefined', ['undef'], [], False, []),
    ('--require-defined', ['main'], ['--require-defined', 'nosuchsym'], False, []),
    ('-u', ['main'], ['-u', 'nosuchsym'], False, []),
    ('archive', ['uselib', 'LIB'], [], False, []),
    ('--whole-archive', ['uselib'], ['--whole-archive', 'LIB', '--no-whole-archive'], False, []),
    ('-l missing', ['main'], ['-lnosuchlib'], False, []),
    ('missing input', ['main', 'nosuch.o'], [], False, []),
    ('unknown option', ['main'], ['--frobnicate'], False, []),
    ('-m i386pe', ['main'], ['-m', 'i386pe'], False, []),
    ('-shared', ['dllall'], ['--out-implib', 'OUT.a'], True, ['OUT.a']),
    ('-shared dllexport', ['dll'], ['--out-implib', 'OUT.a'], True, ['OUT.a']),
    ('-shared --exclude-all-symbols', ['dllall'], ['--exclude-all-symbols'], True, []),
    ('-shared --image-base', ['dllall'], ['--image-base', '0x6f000000'], True, []),
    ('auto-import (DLL)', ['impdata', 'DLLLIB'], [], False, []),
    ('--disable-auto-import', ['impdata', 'DLLLIB'], ['--disable-auto-import'], False, []),
    ('--disable-runtime-pseudo-reloc', ['impdata', 'DLLLIB'], ['--disable-runtime-pseudo-reloc'], False, []),
]


def first_err(s):
    for l in s.splitlines():
        if 'error: ' in l:
            return l.split('error: ', 1)[1].strip()
    return s.strip().splitlines()[0] if s.strip() else ''


def main():
    av = sys.argv[1:]
    verbose = '-v' in av
    run = '--run' in av
    keep = '--keep' in av
    pats = [a for a in av if not a.startswith('-')]
    why = hostenv.mingw_missing()
    if why:
        sys.exit(f'vlink-pe.py: {why}')
    tmp = tempfile.mkdtemp(prefix='vlink-pe-')
    env = dict(os.environ, SOURCE_DATE_EPOCH='1700000000')
    for n, src in SRC.items():
        open(f'{tmp}/{n}.c', 'w').write(src)
        subprocess.run([CL] + TGT + ['-O1', '-c', f'{tmp}/{n}.c', '-o', f'{tmp}/{n}.o'] + FLAGS.get(n, []), check=True)
    subprocess.run([llvm('llvm-ar'), 'rcs', f'{tmp}/libx.a', f'{tmp}/lib1.o', f'{tmp}/lib2.o'], check=True)
    # the DLL the auto-import cases link against (and its import library), linked by lld
    subprocess.run([LLD] + link_line([f'{tmp}/dllall.o'], ['--out-implib', f'{tmp}/libd.dll.a'], True) + ['-o', f'{tmp}/d.dll'],
                   check=True, capture_output=True, env=env)
    wenv = hostenv.wine_env(f'{BUILD}/wine')
    if run:
        hostenv.wine_start(f'{BUILD}/wine')
    ok = bad = 0
    for name, objs, extra, shared, outs in CASES:
        if pats and not any(p in name for p in pats):
            continue
        res = {}
        for who, prog in (('lld', LLD), ('vlink', VLINK)):
            d = f'{tmp}/{who}'
            os.makedirs(d, exist_ok=True)
            ob = [f'{tmp}/libx.a' if o == 'LIB' else f'{tmp}/libd.dll.a' if o == 'DLLLIB' else
                  f'{tmp}/{o}' if o.endswith('.o') else f'{tmp}/{o}.o' for o in objs]
            ex = [f'{tmp}/libx.a' if e == 'LIB' else f'{d}/{e}' if e.startswith('OUT') else e for e in extra]
            out = f'{d}/out.dll' if shared else f'{d}/out.exe'
            for f in [out] + [f'{d}/{o}' for o in outs]:
                if os.path.exists(f):
                    os.remove(f)
            p = subprocess.run([prog] + link_line(ob, ex, shared) + ['-o', out], capture_output=True, text=True, env=env)
            res[who] = (p.returncode, p.stderr, [out] + [f'{d}/{o}' for o in outs], d)
        (lrc, lerr, lfiles, ld), (vrc, verr, vfiles, vd) = res['lld'], res['vlink']
        msg = ''
        if bool(lrc) != bool(vrc):
            msg = f'lld rc {lrc}, vlink rc {vrc}: ' + (first_err(verr) if vrc else first_err(lerr))
        elif lrc:
            le, ve = first_err(lerr), first_err(verr)
            if le != ve:
                msg = f'errors differ:\n    lld:   {le}\n    vlink: {ve}'
        else:
            for a, b in zip(lfiles, vfiles):
                if open(a, 'rb').read() != open(b, 'rb').read():
                    msg = f'{os.path.basename(a)} differs'
                    break
            if msg and keep:
                print(f'  kept: {a} {b}')
            if not msg and run and not shared:
                outs_ = []
                for f, d in ((lfiles[0], ld), (vfiles[0], vd)):
                    shutil.copy(f'{tmp}/d.dll', f'{d}/d.dll')
                    try:
                        p = subprocess.run([WINE, f], capture_output=True, env=wenv, cwd=d, timeout=30)
                        outs_.append((p.returncode, p.stdout))
                    except subprocess.TimeoutExpired:
                        outs_.append(('timeout', b''))
                if outs_[0] != outs_[1]:
                    msg = f'run differs: lld {outs_[0]}, vlink {outs_[1]}'
        if msg:
            bad += 1
            print(f'FAIL {name}: {msg}')
        else:
            ok += 1
            if verbose:
                print(f'ok   {name}' + (f'   ({first_err(lerr)})' if lrc else ''))
    if not keep:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'vlink-pe: {ok}/{ok + bad} ok')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
