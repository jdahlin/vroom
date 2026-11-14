"""What the WIN8 project runners share (doc/PLAN-win64.md WIN8): zlib, SQLite and
FFmpeg cross-built for x86_64-w64-mingw32 by clang (the reference) and by vcc,
run under Wine.  Used by bench/zlib/run.py, bench/sqlite/run.py and
bench/ffmpeg/run.py with --target win64.

Clang's side: the host's clang 23 with the llvm-mingw sysroot, its own resource
headers, lld and llvm-mingw's compiler-rt builtins (CLANG_CC, CLANG_LINK; the
line of bench/vcc-win64.py).  vcc's side has nothing from LLVM:

  - vpp (or the vcc shim against a private vccd) compiles with vcc's own
    headers (src/headers/freestanding, src/headers/x86) and the sysroot's,
    `var` archives, vcc's driver links (vlink, libvccrt.a, the CRT);
  - every command of vcc's side runs with PATH = clean_path(): the host's
    /usr/bin and /bin without any compiler, assembler, linker, archiver or
    object tool (clang*, llvm-*, lld, gcc, cc, as, ld, ar, nm, ranlib, strip,
    objcopy, objdump, windres, dlltool, any *-mingw32-* or other cross tool),
    so a build that reaches for one fails;
  - each program's link map (-Wl,-Map) must name only our objects, the
    sysroot's CRT files and archive members and libvccrt.a's members
    (map_foreign), and its -v link line no LLVM path;
  - -H of one compile shows that no header under clang's lib/clang is read
    (no_clang_headers).

Trees go to build/linux/win64-projects/PROJECT (BUILD's parent), sources stay
where the native runners put them (work/PROJECT).  A run skips (exit 0, with
the reason) when the sysroot, Wine or vcc's tools are missing, or when the
sources cannot be fetched.
"""
import glob, os, re, shutil, signal, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostenv
from hostenv import MINGW_SYSROOT as SYSROOT, WINE, BUILD

ROOT = hostenv.R
TGT = '--target=x86_64-w64-mingw32'
CL = os.environ.get('CLANG') or hostenv.llvm('clang')
VPP = os.environ.get('VPP') or f'{BUILD}/vpp'
SHIM = f'{BUILD}/vcc'
VAR = f'{BUILD}/var'
VCCD = f'{BUILD}/vccd'
VCCRT = os.environ.get('VCC_VCCRT') or f'{BUILD}/vccrt/libvccrt.a'
OUT = os.path.join(os.path.dirname(BUILD), 'win64-projects')
PREFIX = os.environ.get('VCC_WINEPREFIX', f'{BUILD}/wine')
MRES = hostenv.mingw_resource_dir()

CLANG_CC = [CL, TGT, f'--sysroot={SYSROOT}']
CLANG_LINK = [CL, TGT, f'--sysroot={SYSROOT}', f'-resource-dir={MRES}', '-rtlib=compiler-rt',
              '--unwindlib=none', '-fuse-ld=lld', '-static']
VCC_CC = [VPP, TGT, f'--sysroot={SYSROOT}']
VCC_LINK = VCC_CC + ['-static']
# the two vcc code generators, as bench/vcc-win64.py names them
VIR = {'vcc': '0', 'vir': 'l'}

# no tool of these names may be on vcc's PATH
_BLOCKED = re.compile(r'^(clang|llvm|lld|ld(\.|64|$)|wasm-ld|gcc|g\+\+|cc$|c\+\+$|c89|c99|cpp|tcc|as$|ar$|nm$|ranlib|strip|'
                      r'objcopy|objdump|readelf|windres|dlltool|gendef|widl|size$|strings$|addr2line|elfedit|gprof|'
                      r'dwp|.*-(gcc|g\+\+|cc|ld|as|ar|nm|ranlib|strip|objcopy|objdump|windres|dlltool|clang).*|'
                      r'.*mingw.*|x86_64-|i686-|aarch64-|arm-|armv7)')


def missing():
    """why the cross chain cannot run here, or None"""
    why = hostenv.mingw_missing()
    if why:
        return why
    for t in (VPP, VCCD, f'{BUILD}/vlink', SHIM):
        if not os.access(t, os.X_OK):
            return f'no {t} (make -C src vcc)'
    if not shutil.which(CL) and not os.path.exists(CL):
        return f'no clang ({CL}) for the reference side'
    return None


def skip(name, why):
    print(f'{name}: skip ({why})')
    return 0


def ensure_vccrt():
    """libvccrt.a, built by vcc (src/runtime/build.sh) when it is not there"""
    if not os.path.exists(VCCRT):
        sh(['sh', f'{ROOT}/src/runtime/build.sh', os.path.dirname(VCCRT)], env=dict(os.environ, PATH=clean_path(), VCC_BIN=BUILD))


def sh(cmd, **kw):
    print('+', ' '.join(cmd) if isinstance(cmd, list) else cmd, flush=True)
    return subprocess.run(cmd, check=True, **kw)


def clean_path():
    """a directory of links to the host's /usr/bin and /bin without any compiler,
    assembler, linker, archiver or object tool: PATH for vcc's side"""
    d = os.path.join(OUT, 'clean-path')
    stamp = os.path.join(d, '.done')
    if os.path.exists(stamp):
        return d
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for src in ('/usr/local/bin', '/usr/bin', '/bin'):
        if not os.path.isdir(src):
            continue
        for n in sorted(os.listdir(src)):
            p = os.path.join(d, n)
            if _BLOCKED.match(n) or os.path.lexists(p):
                continue
            t = os.path.join(src, n)
            if os.path.isfile(t) and os.access(t, os.X_OK):
                os.symlink(os.path.realpath(t), p)
    open(stamp, 'w').close()
    return d


def blocked_on_path(path):
    """the blocked tools a PATH would find (none for clean_path())"""
    bad = []
    for d in path.split(':'):
        if os.path.isdir(d):
            bad += [f'{d}/{n}' for n in os.listdir(d) if _BLOCKED.match(n)]
    return bad


class Vccd:
    """a private vccd (the server behind the vcc/var shims) for one runner"""

    def __init__(self, env):
        os.makedirs(f'{OUT}/tmp', exist_ok=True)
        self.dir = tempfile.mkdtemp(prefix='vccd-', dir=f'{OUT}/tmp')
        self.sock = f'{self.dir}/s'
        self.log = f'{self.dir}/vccd.log'
        self.p = subprocess.Popen([VCCD, '--sock', self.sock, '-j', os.environ.get('JOBS', '6')],
                                  stdout=open(self.log, 'w'), stderr=subprocess.STDOUT, env=env)
        for _ in range(400):
            if os.path.exists(self.sock):
                break
            if self.p.poll() is not None:
                sys.exit(f'vccd did not start: {open(self.log).read()}')
            time.sleep(0.02)
        else:
            sys.exit('vccd did not start')

    def __enter__(self):
        return self

    def __exit__(self, *a):
        if self.p.poll() is None:
            self.p.send_signal(signal.SIGTERM)
            try:
                self.p.wait(10)
            except subprocess.TimeoutExpired:
                self.p.kill()
        # its counters at exit: what it compiled, archived and linked itself
        self.summary = next((l.strip()[len('vccd: '):] for l in open(self.log, errors='replace') if 'compiled by vcc' in l), '(no counters)')
        shutil.rmtree(self.dir, ignore_errors=True)


def vcc_env(name, vccd=None, **extra):
    """the environment of vcc's side: PATH without foreign tools, VCC_VIR for
    the code generator NAME (vcc, vir), the private vccd's socket"""
    os.makedirs(f'{OUT}/tmp', exist_ok=True)
    e = dict(os.environ, PATH=clean_path(), VCC_VCCRT=VCCRT, TMPDIR=f'{OUT}/tmp', SOURCE_DATE_EPOCH='1700000000')
    e['VCC_VIR'] = VIR.get(name, '0')
    if vccd:
        e['VCC_SOCK'] = vccd.sock
    e.update(extra)
    return e


def clang_env():
    return dict(os.environ, TMPDIR=f'{OUT}/tmp', SOURCE_DATE_EPOCH='1700000000')


def map_foreign(mapfile):
    """the inputs of a vlink map that are not ours: anything but objects of the
    build tree, the sysroot's CRT files and archive members, and libvccrt.a's
    members (vcc-win64.py --own's rule)"""
    try:
        text = open(mapfile, errors='replace').read()
    except OSError:
        return ['(no map)']
    own = {os.path.basename(o) for o in glob.glob(f'{os.path.dirname(VCCRT)}/obj/*.o')}
    bad = []
    for m in re.finditer(r'\s(\S+?):\(', text):
        f = m.group(1)
        if f in own or f.startswith('lib64_') or f.startswith(SYSROOT + '/x86_64-w64-mingw32/lib/') or \
           (not f.startswith(SYSROOT) and not re.search(r'/lib/clang/|clang_rt|libgcc|libunwind|compiler-rt', f)):
            continue
        bad.append(f)
    if not re.search(r'\s\S+?:\(', text):
        bad.append('(empty map)')
    return sorted(set(bad))


def link_line_foreign(stderr):
    """LLVM, compiler-rt, libgcc or libunwind on vcc's -v link line"""
    line = next((l for l in stderr.splitlines() if l.startswith('vlink ')), None)
    if line is None:
        return ['(no link line)']
    line = line.replace(SYSROOT, 'SYSROOT')
    return sorted({m.group(0) for m in re.finditer(r'\S*(?:clang_rt|libgcc|libunwind|compiler-rt|lib/clang/|llvm)\S*', line, re.I)})


def no_clang_headers(cmd, cwd, env):
    """the headers a compile reads (-H) under clang's lib/clang: none for vcc's side"""
    p = subprocess.run(cmd + ['-H', '-fsyntax-only'], cwd=cwd, env=env, capture_output=True, text=True)
    hdrs = [l.split(' ', 1)[1] for l in p.stderr.splitlines() if re.match(r'^\.+ ', l)]
    return p.returncode, len(hdrs), [h for h in hdrs if '/lib/clang/' in h]


_wine_started = False


def wine_env():
    global _wine_started
    if not _wine_started:
        hostenv.wine_start(PREFIX, persist=120)
        _wine_started = True
    e = hostenv.wine_env(PREFIX)
    e['WINEDLLOVERRIDES'] += ';winedbg.exe=d'
    return e


def wine(exe, args=(), cwd=None, input=None, timeout=300, retry=True):
    """(returncode, stdout bytes, stderr bytes) of EXE under Wine; returncode
    'timeout'.  A timeout is tried once more (the first program after a
    prefix's server starts can hang, as bench/vcc-win64.py also found)."""
    try:
        p = subprocess.run([WINE, exe] + list(args), cwd=cwd, input=input, capture_output=True, timeout=timeout,
                           env=wine_env(), stdin=None if input is not None else subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        if retry:
            print(f'{os.path.basename(exe)}: timed out under Wine after {timeout} s, once more', flush=True)
            return wine(exe, args, cwd, input, timeout, retry=False)
        return 'timeout', e.stdout or b'', e.stderr or b''


def crlf(b):
    """a Windows program's text output with \\r\\n as \\n"""
    return b.replace(b'\r\n', b'\n')
