"""Host paths shared by the bench harnesses (macOS or Linux aarch64).

  LLVM   directory of the reference LLVM tools: $LLVM/bin, else Homebrew's
         llvm on macOS, else the clang on PATH (the Linux container sets
         LLVM=/usr/lib/llvm-NN)
  WORK   work/ of this checkout, else the main checkout's ($VROOM_WORK)
  KTREE  the configured kernel build tree whose .cmd files the parity and
         sema harnesses read: work/build-o0 (macOS: vroom-cc/clang, Darwin
         host tools) or work/build-linux (Linux: clang, glibc host tools);
         override with $VCC_KTREE.  PPREF/ASREF: clang's reference outputs
         for that tree (work/ppref, work/asref; -linux suffix on Linux)
  BUILD  where `make -C src` puts vcc: build/src on macOS, build/linux/src
         on Linux (the tree may be shared with a Mac through a bind mount);
         override with $VCC_BUILD
"""
import os, platform, re, shutil

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINUX = platform.system() == 'Linux'


def _llvm_bin():
    if os.environ.get('LLVM'):
        return os.environ['LLVM'] + '/bin'
    if os.path.isdir('/opt/homebrew/opt/llvm/bin'):
        return '/opt/homebrew/opt/llvm/bin'
    cl = shutil.which('clang')
    if cl:
        return os.path.dirname(os.path.realpath(cl))
    import glob	# Debian/Ubuntu's apt.llvm.org packages without the clang alternative
    c = sorted(glob.glob('/usr/lib/llvm-*/bin/clang'), key=lambda p: int(re.sub(r'\D', '', p.split('/')[3]) or 0))
    return os.path.dirname(c[-1]) if c else '/usr/bin'


LLVM_BIN = _llvm_bin()
BUILD = os.environ.get('VCC_BUILD', f'{R}/build/linux/src' if LINUX else f'{R}/build/src')


def has_wasm(vpp):
    """False when this vpp was built without WebAssembly (the default since
    2026-11-13, doc/PLAN-wasm.md; `make VCC_WASM=1` builds it): a harness of
    several targets then leaves its wasm ones out."""
    import subprocess
    p = subprocess.run([vpp, '--target=wasm64-unknown-emscripten', '-E', os.devnull], capture_output=True, text=True)
    return 'no WebAssembly support' not in p.stderr


def llvm(tool):
    """Path of an LLVM tool (clang, llvm-objcopy, ld.lld, ...)."""
    p = f'{LLVM_BIN}/{tool}'
    return p if os.path.exists(p) else (shutil.which(tool) or p)


def _work_dir():
    """work/ of this checkout, else of the main checkout (git worktrees share it)"""
    if os.path.isdir(f'{R}/work'):
        return f'{R}/work'
    try:
        import subprocess
        c = subprocess.run(['git', '-C', R, 'rev-parse', '--git-common-dir'], capture_output=True, text=True).stdout.strip()
        w = os.path.join(os.path.dirname(os.path.abspath(os.path.join(R, c))), 'work')
        if os.path.isdir(w):
            return w
    except OSError:
        pass
    return f'{R}/work'


WORK = os.environ.get('VROOM_WORK') or _work_dir()
_SFX = '-linux' if LINUX else ''
KTREE = os.environ.get('VCC_KTREE', f'{WORK}/build-linux' if LINUX else f'{WORK}/build-o0')
PPREF = os.environ.get('VCC_PPREF', f'{WORK}/ppref{_SFX}')
ASREF = os.environ.get('VAS_REF', f'{WORK}/asref{_SFX}')


# --- Windows x64 (MinGW-w64) as a target: doc/PLAN-win64.md, bench/vcc-win64.py ---
# MINGW_SYSROOT  the llvm-mingw tree (headers, CRT, import libraries, compiler-rt
#                builtins): $VCC_MINGW_SYSROOT, else the newest
#                work/llvm-mingw/llvm-mingw-*-ucrt-*-x86_64 (bench/fetch-testsuites.sh
#                llvm-mingw).  Only its sysroot and lib/clang/N/lib/windows are
#                used; the compiler is the host's clang.
# WINE           $WINE, else wine on PATH; WINESERVER beside it


def _mingw_sysroot():
    if os.environ.get('VCC_MINGW_SYSROOT'):
        return os.environ['VCC_MINGW_SYSROOT']
    import glob
    c = sorted(glob.glob(f'{WORK}/llvm-mingw/llvm-mingw-*-ucrt-*-x86_64'))
    return c[-1] if c else f'{WORK}/llvm-mingw/llvm-mingw-ucrt-x86_64'


MINGW_SYSROOT = _mingw_sysroot()
WINE = os.environ.get('WINE') or shutil.which('wine') or '/usr/bin/wine'
WINESERVER = os.environ.get('WINESERVER') or os.path.join(os.path.dirname(WINE), 'wineserver')


def mingw_resource_dir():
    """llvm-mingw's clang resource directory (lib/clang/N): its lib/windows has
    the compiler-rt builtins for the target, which the host clang lacks"""
    import glob
    c = sorted(glob.glob(f'{MINGW_SYSROOT}/lib/clang/*'))
    return c[-1] if c else None


def mingw_missing():
    """why the MinGW chain cannot run here, or None"""
    if not os.path.isdir(f'{MINGW_SYSROOT}/x86_64-w64-mingw32/lib'):
        return f'no MinGW sysroot at {MINGW_SYSROOT} (bench/fetch-testsuites.sh llvm-mingw, or VCC_MINGW_SYSROOT)'
    if not mingw_resource_dir():
        return f'no lib/clang/N in {MINGW_SYSROOT}'
    if not os.path.exists(WINE):
        return 'no wine (set WINE)'
    return None


def wine_env(prefix):
    """the environment for running Windows programs under Wine with the prefix
    PREFIX: no debug channels, no display, no Mono/Gecko installers, no
    menu entries"""
    e = dict(os.environ, WINEPREFIX=prefix, WINEDEBUG='-all',
             WINEDLLOVERRIDES='winemenubuilder.exe,mscoree,mshtml=d')
    e.pop('DISPLAY', None); e.pop('WAYLAND_DISPLAY', None)
    return e


def wine_start(prefix, persist=60):
    """Create the Wine prefix PREFIX once (a few seconds the first time) and
    start a wineserver for it that stays PERSIST seconds after its last
    client, so that thousands of short programs do not each start one.
    Call before forking workers: creating the prefix is not concurrent-safe."""
    import subprocess
    env = wine_env(prefix)
    if not os.path.exists(f'{prefix}/system.reg'):
        os.makedirs(prefix, exist_ok=True)
        subprocess.run([WINE, 'wineboot', '-i'], env=env, stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        subprocess.run([WINESERVER, '-w'], env=env, timeout=120)
    # a no-op when a server already runs for this prefix
    subprocess.run([WINESERVER, f'-p{persist}'], env=env, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    # the first program of a new server starts Wine's services (services.exe,
    # winedevice, explorer), which inherit its stdout and stderr and live as long
    # as the server: a caller capturing them would wait for that (60 s).  Start
    # them here with no pipes to hold.
    subprocess.run([WINE, 'cmd', '/c', 'exit'], env=env, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    return env
