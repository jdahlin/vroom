"""Host paths shared by the bench harnesses (macOS or Linux aarch64).

  LLVM   directory of the reference LLVM tools: $LLVM/bin, else Homebrew's
         llvm on macOS, else the clang on PATH (the Linux container sets
         LLVM=/usr/lib/llvm-NN)
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
