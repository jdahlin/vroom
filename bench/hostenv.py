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
  BUILD  where `make -C ccs` puts vcc: build/ccs on macOS, build/linux/ccs
         on Linux (the tree may be shared with a Mac through a bind mount);
         override with $VCC_BUILD
"""
import os, platform, shutil

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINUX = platform.system() == 'Linux'


def _llvm_bin():
    if os.environ.get('LLVM'):
        return os.environ['LLVM'] + '/bin'
    if os.path.isdir('/opt/homebrew/opt/llvm/bin'):
        return '/opt/homebrew/opt/llvm/bin'
    cl = shutil.which('clang')
    return os.path.dirname(os.path.realpath(cl)) if cl else '/usr/bin'


LLVM_BIN = _llvm_bin()
BUILD = os.environ.get('VCC_BUILD', f'{R}/build/linux/ccs' if LINUX else f'{R}/build/ccs')


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
