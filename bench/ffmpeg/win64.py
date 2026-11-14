"""bench/ffmpeg/run.py --target win64 (doc/PLAN-win64.md WIN8): FFmpeg 8.1.3, C
fallbacks only, cross-built for x86_64-w64-mingw32 by clang (the reference) and
by vcc, its sample-free FATE run under Wine.

  bench/ffmpeg/run.py --target win64 [build|fate|all] [clang vcc vir] [-k] [--ignore T,...]

Source: the release tarball (SHA-256 pinned, the one bench/ffmpeg/linux-x86.sh
uses), unpacked into work/ffmpeg-win64.  Every tree in
build/linux/win64-projects/ffmpeg/b-NAME is configured the same way:

  --enable-cross-compile --target-os=mingw32 --arch=x86_64 --disable-asm
  --disable-autodetect --enable-w32threads --disable-doc --disable-debug
  --disable-stripping --windres=false --target-exec=WINE (tools/wine-run)
  --host-cc=HOSTCC (tools/host-cc)

--disable-asm leaves no x86 assembly or inline asm (ARCH_X86_64 is 0, the C
fallbacks of the DSP code run, as bench/ffmpeg/linux-x86.sh's noasm tree);
--disable-autodetect: no external library; Win32 threads on, so the frame and
slice threads of the codecs run; no Windows resources (vlink has no .rsrc).
The tools: clang's tree has clang + lld + compiler-rt and llvm-ar/llvm-nm;
vcc's (`vcc` direct emitter by -fno-vir, `vir` VIR) has vpp as --cc and --ld (vcc's
driver links with vlink and libvccrt.a), var as --ar against a private vccd,
vnm as --nm, `true` as ranlib and strip, PATH emptied of foreign tools
(bench/win64proj.py), and ffmpeg.exe's link map checked.

FATE's host tools (tests/tiny_psnr, audiogen, videogen, base64, ...) run on
this Linux host, so they are built by the host's gcc through tools/host-cc
in every tree, outside the emptied PATH: they compare and generate test data
and are not part of the Windows build.  `fate`: `make -k fate` (no samples)
under Wine; vcc passes when it fails no test clang's tree passes.
"""
import hashlib, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import hostenv
import win64proj as wp

TARBALL = 'ffmpeg-8.1.3.tar.xz'
SHA = '7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3'
W = os.path.join(hostenv.WORK, 'ffmpeg-win64')
SRC = os.path.join(W, 'ffmpeg-8.1.3')
JOBS = os.environ.get('JOBS', '6')
CONFIGURE = ['--enable-cross-compile', '--target-os=mingw32', '--arch=x86_64', '--disable-asm', '--disable-autodetect',
             '--enable-w32threads', '--disable-doc', '--disable-debug', '--disable-stripping', '--windres=false']
TOOLS = os.path.join(wp.OUT, 'ffmpeg', 'tools')


def fetch():
    if os.path.isdir(SRC):
        return
    os.makedirs(W, exist_ok=True)
    t = os.path.join(W, TARBALL)
    other = os.path.join(hostenv.WORK, 'ffmpeg-linux', TARBALL)
    if not os.path.exists(t):
        if os.path.exists(other):
            shutil.copy(other, t)
        else:
            subprocess.run(['curl', '-sSfL', '-o', t + '.part', f'https://ffmpeg.org/releases/{TARBALL}'], check=True)
            os.replace(t + '.part', t)
    if hashlib.sha256(open(t, 'rb').read()).hexdigest() != SHA:
        raise OSError(f'{t}: SHA-256 is not {SHA}')
    subprocess.run(['tar', 'xf', t, '-C', W], check=True)


def tools():
    """tools/host-cc (the host's gcc, normal PATH) and tools/wine-run (Wine with the runners' prefix)"""
    os.makedirs(TOOLS, exist_ok=True)
    wp.wine_env()	# the prefix and its server, once
    open(f'{TOOLS}/host-cc', 'w').write('#!/bin/sh\n# FATE host tools (tests/*.c), by the host compiler outside the emptied PATH\n'
                                        'PATH=/usr/bin:/bin exec gcc "$@"\n')
    open(f'{TOOLS}/wine-run', 'w').write('#!/bin/sh\n'
                                         f'export WINEPREFIX={wp.PREFIX} WINEDEBUG=-all '
                                         'WINEDLLOVERRIDES="winemenubuilder.exe,mscoree,mshtml,winedbg.exe=d"\n'
                                         f'unset DISPLAY WAYLAND_DISPLAY\nexec {wp.WINE} "$@"\n')
    for f in ('host-cc', 'wine-run'):
        os.chmod(f'{TOOLS}/{f}', 0o755)


def bdir(name):
    return os.path.join(wp.OUT, 'ffmpeg', 'b-' + name)


def toolchain(name):
    """configure options and environment of one tree"""
    common = CONFIGURE + [f'--host-cc={TOOLS}/host-cc', f'--target-exec={TOOLS}/wine-run']
    if name == 'clang':
        cc = ' '.join(wp.CLANG_CC)
        ld = ' '.join(wp.CLANG_LINK)
        return common + [f'--cc={cc}', f'--ld={ld}', f'--ar={hostenv.llvm("llvm-ar")}', f'--nm={hostenv.llvm("llvm-nm")}',
                         '--ranlib=true', '--strip=true'], wp.clang_env()
    cc = ' '.join(wp.vcc_cc(name))
    return common + [f'--cc={cc}', f'--ld={cc} -static', f'--ar={wp.VAR}', f'--nm={wp.BUILD}/vnm', '--ranlib=true',
                     '--strip=true'], wp.vcc_env(name)


def build(name):
    b = bdir(name)
    shutil.rmtree(b, ignore_errors=True)
    os.makedirs(b)
    opts, env = toolchain(name)
    bad = []
    with wp.Vccd(env) if name != 'clang' else _Null() as v:
        if v:
            env = dict(env, VCC_SOCK=v.sock)
        t = time.time()
        with open(os.path.join(b, 'configure.out'), 'w') as f:
            if subprocess.run([os.path.join(SRC, 'configure')] + opts, cwd=b, env=env, stdout=f, stderr=subprocess.STDOUT).returncode:
                sys.exit(f'{name}: configure failed, see {b}/configure.out and {b}/ffbuild/config.log')
        ct = time.time() - t
        t = time.time()
        mk = ['make', '-j', JOBS] + (["LDEXEFLAGS=-v -Wl,-Map=$@.map"] if name != 'clang' else [])
        with open(os.path.join(b, 'build.log'), 'w') as f:
            if subprocess.run(mk, cwd=b, env=env, stdout=f, stderr=subprocess.STDOUT).returncode:
                sys.exit(f'{name}: make failed, see {b}/build.log')
        bt = time.time() - t
    if name != 'clang':
        log = open(os.path.join(b, 'build.log'), errors='replace').read()
        links = [l for l in log.splitlines() if l.startswith('vlink ')]
        if not links:
            bad.append('no vlink line in build.log')
        for l in links:
            bad += [f'link line: {x}' for x in wp.link_line_foreign(l)]
        for exe in ('ffmpeg_g.exe', 'ffprobe_g.exe'):
            f = wp.map_foreign(os.path.join(b, exe + '.map'))
            if f:
                bad.append(f'{exe}: foreign input {" ".join(f)}')
        rc, n, ch = wp.no_clang_headers(wp.vcc_cc(name) + ['-I.', '-I' + SRC, os.path.join(SRC, 'libavutil', 'cpu.c')], b, env)
        if rc or not n or ch:
            bad.append(f'headers: {ch or "-H failed"}')
    cfiles = sum(1 for l in open(os.path.join(b, 'build.log'), errors='replace') if l.startswith('CC\t'))
    return ct, bt, cfiles, bad


class _Null:
    def __enter__(self):
        return None

    def __exit__(self, *a):
        pass


def fate(name, ignore):
    b = bdir(name)
    env = wp.vcc_env(name) if name != 'clang' else wp.clang_env()
    t = time.time()
    cmd = ['make', '-k', '-j', JOBS, 'fate'] + ([f'IGNORE_TESTS={" ".join(ignore)}'] if ignore else [])
    with open(os.path.join(b, 'fate.log'), 'w') as f:
        subprocess.run(cmd, cwd=b, env=env, stdout=f, stderr=subprocess.STDOUT)
    log = open(os.path.join(b, 'fate.log'), errors='replace').read().splitlines()
    ran = sorted(l.split()[1] for l in log if l.startswith('TEST '))
    bad = sorted(l.split()[1] for l in log if l.startswith('Test ') and l.rstrip().endswith('for details.'))
    return ran, bad, time.time() - t


def main(cmd, names, ignore):
    why = wp.missing()
    if why:
        return wp.skip('ffmpeg --target win64', why)
    try:
        fetch()
    except (subprocess.CalledProcessError, OSError) as e:
        return wp.skip('ffmpeg --target win64', f'no FFmpeg source ({e})')
    wp.ensure_vccrt()
    tools()
    names = names or ['clang', 'vcc', 'vir']
    rc = 0
    if cmd in ('build', 'all'):
        for x in names:
            ct, bt, n, bad = build(x)
            print(f'{x:6} configure {ct:5.0f} s, make -j {JOBS} {bt:6.1f} s ({n} C files){": " + "; ".join(bad) if bad else ", nothing foreign"}',
                  flush=True)
            rc |= 1 if bad else 0
    if cmd in ('fate', 'all'):
        res = {x: fate(x, ignore) for x in names}
        for x in names:
            ran, bad, t = res[x]
            print(f'{x:6} FATE {len(ran) - len(bad)}/{len(ran)} pass under Wine ({t:.0f} s)', flush=True)
        if 'clang' in res:
            refbad = set(res['clang'][1])
            for x in names:
                if x == 'clang':
                    continue
                only = sorted(set(res[x][1]) - refbad)
                miss = sorted(set(res['clang'][0]) - set(res[x][0]))
                print(f'{x}: vcc-only failures: {" ".join(only) or "none"}' + (f'; not run: {" ".join(miss[:20])}' if miss else ''))
                rc |= 1 if only or miss else 0
            if refbad:
                print(f'clang\'s own failures under Wine ({len(refbad)}, not counted): {" ".join(sorted(refbad)[:40])}')
    print('ffmpeg --target win64:', 'ok' if not rc else 'FAIL')
    return rc
