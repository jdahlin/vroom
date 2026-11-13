#!/usr/bin/env python3
"""vccd-test.py: vccd as a long-lived service (TODO.md T12).

  bench/vccd-test.py [--bin-dir DIR] [-v] [PAT]

Each test starts its own private vccd (socket in a fresh temporary
directory, no backend) and checks one property of the service: access to the
socket, malformed requests, idle connections, cache invalidation between
builds, the client's umask.  Requests are sent either through the real shim
(build/src/vcc) or by a raw client in this file that speaks protocol.h
directly, to send what the shim never would.

Known failures are marked xfail with their TODO.md item: a failure is
counted as xfail, a pass is XPASS (an error: remove the marker in the commit
that fixes it).  Tests that cannot run here (a second user is needed) are
skipped.  Exit status 1 on any FAIL or XPASS.

--bin-dir build/src-asan or build/src-tsan runs the sanitizer builds of
vccd; then also check the vccd log each test prints with -v.
"""
import array, os, re, shutil, signal, socket, stat, struct, subprocess, sys, tempfile, time, importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)

SHIM = f'{vd.BIN}/vcc'	# next to the vccd under test: the build ids match
MAGIC = 0x326f6f72

def hdr(magic, argc, envc, plen, build=0):
    return struct.pack('<IIIIQII', magic, argc, envc, plen, build, 0, 0)
TARGET = ['--target=aarch64-linux-gnu', '-nostdinc', '-ffreestanding']
TESTS = []

def test(xfail=None):
    def deco(f):
        TESTS.append((f.__name__.replace('_', '-'), f, xfail)); return f
    return deco

class Skip(Exception): pass

class Vccd:
    """a private vccd in its own directory"""
    def __init__(self, workers=4, umask=None, sockdir_mode=None, extra=(), env=None):
        self.dir = tempfile.mkdtemp(prefix='vccd-test-')
        if sockdir_mode is not None: os.chmod(self.dir, sockdir_mode)
        self.sock = f'{self.dir}/vccd.sock'
        self.logf = f'{self.dir}/vccd.log'
        pre = (lambda: os.umask(umask)) if umask is not None else None
        self.p = subprocess.Popen([f'{vd.BIN}/vccd', '--sock', self.sock,
                                   '-j', str(workers)] + list(extra), stdout=open(self.logf, 'w'), stderr=subprocess.STDOUT,
                                  preexec_fn=pre, env=dict(os.environ, **(env or {})))
        for _ in range(200):
            if os.path.exists(self.sock): break
            time.sleep(0.02)
        else: raise SystemExit('vccd-test: vccd did not start')
    def alive(self): return self.p.poll() is None
    def stop(self, timeout=5):
        if self.alive(): self.p.send_signal(signal.SIGTERM); self.p.wait(timeout)
    def cc(self, cwd, src, out='a.o', extra=(), umask=None, timeout=30):
        """compile through the shim: (status, stderr)"""
        pre = (lambda: os.umask(umask)) if umask is not None else None
        p = subprocess.run([SHIM] + TARGET + list(extra) + ['-c', '-o', out, src], cwd=cwd, capture_output=True,
                           timeout=timeout, env=dict(os.environ, VCC_SOCK=self.sock), preexec_fn=pre,
                           process_group=0)	# one build per call (a shell gives each make its own group)
        return p.returncode, p.stderr.decode(errors='replace')

def raw_request(sock, cwd, argv, env=(), fds=(0, 1, 2), header=None, payload=None, close_early=False, timeout=10):
    """speak protocol.h by hand: the reply's int32 status, or None if the server closed the connection"""
    if payload is None:
        payload = b''.join(s.encode() + b'\0' for s in [cwd] + list(argv) + list(env))
    if header is None:
        header = hdr(MAGIC, len(argv), len(env), len(payload))
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(sock)
    anc = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', fds))] if fds else []
    s.sendmsg([header], anc)
    if close_early:
        s.close(); return None
    s.sendall(payload)
    try:
        r = b''
        while len(r) < 4:
            c = s.recv(4 - len(r))
            if not c: return None
            r += c
        return struct.unpack('<i', r)[0]
    finally:
        s.close()

def tu(d, name='a.c', text='int v = 1;\n'):
    os.makedirs(d, exist_ok=True)
    open(f'{d}/{name}', 'w').write(text)
    return name

def obj(d, name='a.o'):
    return open(f'{d}/{name}', 'rb').read()

def check(cond, msg):
    if not cond: raise AssertionError(msg)

# ---------------- access ----------------

@test()
def socket_mode_ignores_umask(v=None):
    """started with umask 0 in a world-readable directory, the socket must still be private"""
    v = Vccd(umask=0, sockdir_mode=0o755)
    try:
        m = stat.S_IMODE(os.stat(v.sock).st_mode)
        check(m & 0o077 == 0, f'socket mode {m:o}: other users can connect')
    finally: v.stop()

@test()
def default_socket_not_in_tmp():
    """the default socket path (shim and server) is in a per-user directory, not /tmp"""
    for f in ('bin/vcc/shim.c', 'server/vccd.c'):
        src = open(f'{vd.R}/src/{f}').read()
        check('"/tmp/vccd.sock"' not in src, f'{f}: default socket /tmp/vccd.sock')

@test()
def peer_other_user():
    """a connection from another uid is refused (needs passwordless sudo -u nobody)"""
    if not shutil.which('sudo') or subprocess.run(['sudo', '-n', '-u', 'nobody', 'true'], capture_output=True).returncode:
        raise Skip('no passwordless sudo -u nobody')
    v = Vccd(sockdir_mode=0o755)
    try:
        os.chmod(v.sock, 0o777)	# even when the file mode lets it through, the uid check must not
        d = f'{v.dir}/w'; src = tu(d); os.chmod(d, 0o777)
        p = subprocess.run(['sudo', '-n', '-u', 'nobody', 'env', f'VCC_SOCK={v.sock}', SHIM] + TARGET + ['-c', src],
                           cwd=d, capture_output=True)
        check(p.returncode != 0, 'a job from uid nobody was run')
    finally: v.stop()

# ---------------- malformed requests ----------------

@test()
def wrong_fd_count():
    """requests with 0, 1, 2 or 4 fds are refused, the server stays up and serves"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; src = tu(d)
        argv = [SHIM] + TARGET + ['-c', '-o', 'x.o', src]
        r, w = os.pipe()
        for fds in ((), (r,), (r, w), (0, 1, 2, w)):
            for _ in range(5):
                try:
                    st = raw_request(v.sock, d, argv, fds=fds)
                except (BrokenPipeError, ConnectionResetError):
                    st = None	# refused before the payload was read
                check(st != 0, f'request with {len(fds)} fds accepted (status {st})')
        os.close(r); os.close(w)
        check(v.alive(), 'vccd died')
        st, err = v.cc(d, src)
        check(st == 0, f'normal job after bad requests: {st} {err[-300:]}')
    finally: v.stop()

@test()
def spawned_tool_env_clean():
    """a link spawned for a request gets the request's env minus the dynamic loader's variables"""
    d0 = tempfile.mkdtemp(prefix='vccd-ld-')
    dump = f'{d0}/env.txt'
    with open(f'{d0}/ld', 'w') as f: f.write(f'#!/bin/sh\nenv > {dump}\n')
    os.chmod(f'{d0}/ld', 0o755)
    v = Vccd(extra=['--ld', f'{d0}/ld'])
    try:
        st = raw_request(v.sock, d0, ['ld', 'x.o', '-o', 'x'], env=['DYLD_INSERT_LIBRARIES=/nonexistent',
                         'LD_PRELOAD=/nonexistent', 'VCC_TEST_KEEP=1', 'PATH=/usr/bin:/bin'])
        check(st == 0, f'link status {st}')
        e = open(dump).read()
        check('VCC_TEST_KEEP=1' in e, 'the request env did not reach the tool')
        check('DYLD_INSERT_LIBRARIES' not in e and 'LD_PRELOAD' not in e, 'loader variables reached the tool')
    finally: v.stop()

@test()
def request_2mb():
    """a 2 MB environment (Linux allows more than macOS's 1 MB ARG_MAX) is served"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; src = tu(d)
        argv = [SHIM] + TARGET + ['-c', '-o', f'{d}/big.o', src]
        st = raw_request(v.sock, d, argv, env=['BIG=' + 'x' * (2 << 20), 'PATH=/usr/bin:/bin'])
        check(st == 0, f'status {st} for a 2 MB request')
    finally: v.stop()

@test()
def autostart_default_socket():
    """no server on the default socket: concurrent shims start one between them and are served"""
    from concurrent.futures import ThreadPoolExecutor
    t = tempfile.mkdtemp(prefix='vccd-auto-')
    env = {k: x for k, x in os.environ.items() if k not in ('VCC_SOCK', 'XDG_RUNTIME_DIR')}
    env['TMPDIR'] = t
    src = tu(f'{t}/w')
    try:
        def one(i):
            return subprocess.run([SHIM] + TARGET + ['-c', '-o', f'{i}.o', src], env=env, cwd=f'{t}/w',
                                  capture_output=True, timeout=30).returncode
        with ThreadPoolExecutor(4) as ex: rcs = list(ex.map(one, range(4)))
        check(rcs == [0] * 4, f'exit statuses {rcs}')
        n = subprocess.run(['pgrep', '-f', f'vccd --sock {t}/'], capture_output=True, text=True).stdout.split()
        check(len(n) == 1, f'{len(n)} servers started')
    finally:
        subprocess.run(['pkill', '-f', f'vccd --sock {t}/'])

@test()
def sigterm_drains():
    """SIGTERM during a job: the job still completes, then the server exits and its socket is gone"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'
        src = tu(d, 'big.c', ''.join(f'int f{i}(int x) {{ return x * {i} + (x >> 3) - {i}; }}\n' for i in range(600000)))
        p = subprocess.Popen([SHIM] + TARGET + ['-c', '-o', 'big.o', src], cwd=d, env=dict(os.environ, VCC_SOCK=v.sock),
                             stderr=subprocess.PIPE)
        time.sleep(0.4)
        v.p.send_signal(signal.SIGTERM)
        rc = p.wait(timeout=60)
        check(rc == 0, f'job interrupted: rc {rc} {p.stderr.read()[-300:]}')
        check(os.path.getsize(f'{d}/big.o') > 0, 'no object')
        v.p.wait(timeout=10)
        check(not os.path.exists(v.sock), 'socket left behind')
    finally: v.stop()

@test()
def request_env_reaches_job():
    """SOURCE_DATE_EPOCH and CPATH from the client's environment, not the server's"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; os.makedirs(f'{d}/cp', exist_ok=True)
        open(f'{d}/cp/e.h', 'w').write('#define FROM_CPATH 7\n')
        src = tu(d, 'e.c', '#include "e.h"\nconst char *s = __DATE__; int k = FROM_CPATH;\n')
        env = dict(os.environ, VCC_SOCK=v.sock, SOURCE_DATE_EPOCH='0', CPATH='cp')
        p = subprocess.run([SHIM] + TARGET + ['-E', '-P', src], cwd=d, env=env, capture_output=True, text=True,
                           process_group=0)
        check(p.returncode == 0, f'rc {p.returncode} {p.stderr[-300:]}')
        check('"Jan  1 1970"' in p.stdout, f'__DATE__ not from SOURCE_DATE_EPOCH: {p.stdout[-200:]}')
        check('k = 7' in p.stdout, 'CPATH not used')
    finally: v.stop()

@test()
def compile_shapes_not_forwarded():
    """-c/-S with the input not last, -o before -c, -fPIC: vcc compiles them (there is no backend to forward to)"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; os.makedirs(d, exist_ok=True)
        src = tu(d, 's.c', 'int g; int f(void) { return g; }\n')
        env = dict(os.environ, VCC_SOCK=v.sock)
        for i, argv in enumerate([TARGET + ['-c', src, '-O2', '-o', 'x0.o'],
                                  ['-o', 'x1.o'] + TARGET + ['-c', src],
                                  TARGET + ['-fPIC', '-c', '-o', 'x2.o', src],
                                  TARGET + ['-c', src, '-MD', '-MF', 'x3.d', '-o', 'x3.o'],
                                  TARGET + ['-S', src, '-o', 'x4.o'],
                                  TARGET + ['-O2', '-S', src]]):
            out = f'{d}/x{i}.o' if i < 5 else f'{d}/s.s'	# -S without -o: the basename with .s, as clang
            p = subprocess.run([SHIM] + argv, cwd=d, env=env, capture_output=True, text=True, process_group=0)
            check(p.returncode == 0 and os.path.exists(out), f'{argv}: rc {p.returncode} {p.stderr[-300:]}')
        check(open(f'{d}/x4.o').read().lstrip().startswith('.text'), '-S did not write assembly')
    finally: v.stop()

@test()
def dumpmachine_as_clang():
    """--target=T -dumpmachine: the triple normalized as clang, answered without a backend"""
    v = Vccd()
    try:
        env = dict(os.environ, VCC_SOCK=v.sock)
        for t, want in [('aarch64-linux-gnu', 'aarch64-unknown-linux-gnu'), ('arm64-linux-gnu', 'arm64-unknown-linux-gnu'),
                        ('aarch64-unknown-linux-gnu', 'aarch64-unknown-linux-gnu')]:
            p = subprocess.run([SHIM, f'--target={t}', '-dumpmachine'], cwd=v.dir, env=env, capture_output=True,
                               text=True, process_group=0)
            check(p.returncode == 0 and p.stdout == want + '\n', f'{t}: rc {p.returncode} {p.stdout!r}')
    finally: v.stop()

@test()
def stdin_and_host_not_forwarded():
    """-E and -c of `-` (Kconfig's cc-version.sh, tools-support-relr.sh) and, on an arm64 Mac, a host -c: vcc's
    (no backend, so a forward fails); stdin's dependency file names no <stdin>, as clang's"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; os.makedirs(d, exist_ok=True)
        tu(d, 'h.h', 'int y;\n')
        env = dict(os.environ, VCC_SOCK=v.sock)
        run = lambda argv, inp=None: subprocess.run([SHIM] + argv, cwd=d, env=env, input=inp, capture_output=True,
                                                     text=True, process_group=0)
        p = run(TARGET + ['-E', '-P', '-x', 'c', '-'], '__clang_major__ __FILE__\n')
        check(p.returncode == 0 and '"<stdin>"' in p.stdout, f'-E -: rc {p.returncode} {p.stdout[-200:]} {p.stderr[-300:]}')
        big = '#include "h.h"\nint big[] = {' + ','.join(['1'] * 5000) + '};\n'	# more than a pipe buffer
        p = run(TARGET + ['-Wp,-MMD,s.d', '-c', '-x', 'c', '-', '-o', 's.o'], big)
        check(p.returncode == 0 and os.path.exists(f'{d}/s.o'), f'-c -: rc {p.returncode} {p.stderr[-300:]}')
        check(open(f'{d}/s.d').read() == 's.o: h.h\n', f'stdin deps: {open(f"{d}/s.d").read()!r}')
        if sys.platform == 'darwin' and os.uname().machine == 'arm64':
            src = tu(d, 'host.c', 'int main(void) { return 0; }\n')
            p = run(['-O2', '-c', '-o', 'host.o', src])
            check(p.returncode == 0 and obj(d, 'host.o')[:4] == b'\xcf\xfa\xed\xfe', f'host -c: rc {p.returncode} {p.stderr[-300:]}')
    finally: v.stop()

@test()
def killed_client_no_output():
    """the shim killed during a long job: no object is written, and the server keeps serving"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'
        src = tu(d, 'big.c', ''.join(f'int f{i}(int x) {{ return x * {i} + (x >> 3) - {i}; }}\n' for i in range(600000)))
        p = subprocess.Popen([SHIM] + TARGET + ['-c', '-o', 'big.o', src], cwd=d, env=dict(os.environ, VCC_SOCK=v.sock),
                             stderr=subprocess.DEVNULL, process_group=0)
        time.sleep(0.4)
        p.kill(); p.wait()
        time.sleep(3)
        check(not os.path.exists(f'{d}/big.o'), 'object written for a client that had gone')
        check(not [f for f in os.listdir(d) if f.endswith('.tmp')], 'temporary left behind')
        st, err = v.cc(d, tu(d)); check(st == 0, f'server after: {st} {err[-300:]}')
    finally: v.stop(300)	# SIGTERM waits for the abandoned job: the next request's new build no longer drains it (Q-SCHED-01)

@test()
def memory_restart_between_builds():
    """over its memory limit at a new build the server re-execs on the same socket; concurrent jobs are retried"""
    from concurrent.futures import ThreadPoolExecutor
    v = Vccd(env={'VCC_RESTART_MB': '1'})
    try:
        d = f'{v.dir}/w'; src = tu(d)
        for b in range(3):	# each v.cc is a new build: a restart before each but the first
            with ThreadPoolExecutor(6) as ex:
                rs = list(ex.map(lambda i: v.cc(d, src, out=f'{b}-{i}.o'), range(6)))
            check(all(st == 0 for st, _ in rs), f'build {b}: {[(st, e[-120:]) for st, e in rs if st]}')
            check(v.alive(), 'vccd died')
        log = open(v.logf).read()
        check(log.count('restarting') >= 2, f'{log.count("restarting")} restarts in the log')
    finally: v.stop()

@test()
def other_build_refused():
    """a request from another build of the shim gets the mismatch status, not service"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; src = tu(d)
        argv = [SHIM] + TARGET + ['-c', src]
        good = b''.join(s.encode() + b'\0' for s in [d] + argv)
        st = raw_request(v.sock, d, argv, header=hdr(MAGIC, len(argv), 0, len(good), build=12345), payload=good)
        check(st == 0x7fff0001, f'status {st} for a foreign build id (want VROOM_STATUS_MISMATCH)')
        st, err = v.cc(d, src)
        check(st == 0, f'the real shim (same build) is served: {st} {err[-300:]}')
    finally: v.stop()

@test()
def garbage_requests():
    """bad magic, huge lengths, truncated payloads, counts that do not match: refused, server up"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; src = tu(d)
        argv = [SHIM] + TARGET + ['-c', src]
        good = b''.join(s.encode() + b'\0' for s in [d] + argv)
        bad = [
            dict(header=hdr(0xdeadbeef, len(argv), 0, len(good)), payload=good),
            dict(header=hdr(MAGIC, len(argv), 0, 1 << 31), payload=good),
            dict(header=hdr(MAGIC, 1 << 30, 1 << 30, len(good)), payload=good),
            dict(header=hdr(MAGIC, len(argv) + 50, 0, len(good)), payload=good),
            dict(header=hdr(MAGIC, 0, 0, len(good)), payload=good),
            dict(header=hdr(MAGIC, len(argv), 0, len(good) + 100), payload=good, close_early=True),
            dict(header=hdr(MAGIC, len(argv), 0, len(good)), payload=good.replace(b'\0', b'x')),
            dict(header=b'\x72\x6f', payload=b'', close_early=True),
            # 1 + argc + envc wraps to 1 in 32 bits: only the cwd
            dict(header=hdr(MAGIC, 0xffffffff, 1, len(d) + 1), payload=d.encode() + b'\0'),
            dict(header=hdr(MAGIC, 0xffffffff, 2, len(good)), payload=good),
            # the payload's last string without its NUL
            dict(header=hdr(MAGIC, len(argv), 0, len(good) - 1), payload=good[:-1]),
            # pad not zero
            dict(header=struct.pack('<IIIIQII', MAGIC, len(argv), 0, len(good), 0, 0, 1), payload=good),
        ]
        for i, b in enumerate(bad):
            try:
                st = raw_request(v.sock, d, argv, timeout=5, **b)
            except (socket.timeout, OSError):
                st = None
            check(st != 0, f'garbage request {i} accepted')
            check(v.alive(), f'vccd died on garbage request {i}')
        st, err = v.cc(d, src)
        check(st == 0, f'normal job after garbage: {st} {err[-300:]}')
    finally: v.stop()

@test()
def idle_connections_do_not_stall():
    """with every worker's worth of connections open and silent, a real job still runs"""
    v = Vccd(workers=2)
    idle = []
    try:
        for _ in range(4):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(v.sock); idle.append(s)
        d = f'{v.dir}/w'; src = tu(d)
        try:
            st, err = v.cc(d, src, timeout=10)
        except subprocess.TimeoutExpired:
            raise AssertionError('job did not finish in 10 s with 4 idle connections open')
        check(st == 0, f'job failed: {st} {err[-300:]}')
    finally:
        for s in idle: s.close()
        v.stop()

# ---------------- caches across builds ----------------

def two_builds(v, d, src, change):
    """compile, change(), compile again: the two objects"""
    st, err = v.cc(d, src, out='1.o'); check(st == 0, f'first build: {st} {err[-300:]}')
    time.sleep(0.01)	# a different mtime for the edit
    change()
    st, err = v.cc(d, src, out='2.o'); check(st == 0, f'second build: {st} {err[-300:]}')
    return obj(d, '1.o'), obj(d, '2.o')

def fresh(d, src, out):
    """the same compile in a brand-new server: what the second build should produce"""
    v = Vccd()
    try:
        st, err = v.cc(d, src, out=out); check(st == 0, f'fresh build: {st} {err[-300:]}')
        return obj(d, out)
    finally: v.stop()

@test()
def edited_header_rebuilds():
    v = Vccd()
    try:
        d = f'{v.dir}/w'; tu(d, 'h.h', '#define N 1\n'); src = tu(d, 'a.c', '#include "h.h"\nint v = N;\n')
        a, b = two_builds(v, d, src, lambda: open(f'{d}/h.h', 'w').write('#define N 22222\n'))
        check(a != b, 'the object did not change after the header was edited')
        check(b == fresh(d, src, '3.o'), 'second build differs from a fresh server')
    finally: v.stop()

@test()
def edited_main_file_rebuilds():
    v = Vccd()
    try:
        d = f'{v.dir}/w'; src = tu(d, 'a.c', 'int v = 1;\n')
        a, b = two_builds(v, d, src, lambda: open(f'{d}/a.c', 'w').write('int v = 22222;\n'))
        check(a != b, 'the object did not change after the source was edited')
    finally: v.stop()

@test()
def header_created_after_miss():
    v = Vccd()
    try:
        d = f'{v.dir}/w'
        src = tu(d, 'a.c', '#if __has_include("n.h")\n#include "n.h"\n#else\n#define N 1\n#endif\nint v = N;\n')
        a, b = two_builds(v, d, src, lambda: open(f'{d}/n.h', 'w').write('#define N 22222\n'))
        check(a != b, 'the new header was not found in the second build')
    finally: v.stop()

@test()
def header_shadowed_by_new_file():
    """a new file earlier in the search path takes over from the one found before"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; os.makedirs(f'{d}/i1'); os.makedirs(f'{d}/i2')
        open(f'{d}/i2/h.h', 'w').write('#define N 1\n')
        src = tu(d, 'a.c', '#include "h.h"\nint v = N;\n')
        st, err = v.cc(d, src, out='1.o', extra=['-Ii1', '-Ii2']); check(st == 0, err[-300:])
        open(f'{d}/i1/h.h', 'w').write('#define N 22222\n')
        st, err = v.cc(d, src, out='2.o', extra=['-Ii1', '-Ii2']); check(st == 0, err[-300:])
        check(obj(d, '1.o') != obj(d, '2.o'), 'i1/h.h was not picked up')
    finally: v.stop()

@test()
def two_groups_share_caches():
    """two builds at once through one server (Q-SCHED-01): they do not flush each
    other's caches (at most the first switch, when nothing is running), every
    object is right, and a build after an edit still sees the edit"""
    from concurrent.futures import ThreadPoolExecutor
    v = Vccd(workers=4, env={'VCC_LOG': '1'})
    try:
        d = f'{v.dir}/w'; tu(d, 'h.h', '#define N 1\n')
        leaders = [subprocess.Popen(['sleep', '60'], process_group=0) for _ in range(2)]
        def one(i, pgid=None, out=None):
            src = tu(d, f'a{i}.c', f'#include "h.h"\nint v{i} = N + {i};\nint f{i}(void) {{ return v{i} * 3; }}\n')
            p = subprocess.run([SHIM] + TARGET + ['-c', '-o', out or f'a{i}.o', src], cwd=d, capture_output=True, timeout=30,
                               env=dict(os.environ, VCC_SOCK=v.sock), process_group=pgid)
            return i, p.returncode, p.stderr.decode(errors='replace')
        try:
            with ThreadPoolExecutor(6) as ex:	# jobs alternate between the two groups
                res = list(ex.map(lambda i: one(i, leaders[i % 2].pid), range(48)))
        finally:
            for l in leaders: l.kill(); l.wait()
        for i, st, err in res:
            check(st == 0, f'job {i}: {st} {err[-200:]}')
            check(f'v{i}'.encode() in obj(d, f'a{i}.o'), f'job {i}: object does not define v{i}')
        o1 = obj(d, 'a0.o')
        time.sleep(0.01)
        open(f'{d}/h.h', 'w').write('#define N 22222\n')
        _, st, err = one(0, None, 'b0.o'); check(st == 0, err[-300:])	# a new group
        check(obj(d, 'b0.o') != o1, 'a build after the header edit used the stale header')
    finally: v.stop()
    m = re.search(r'(\d+) builds \(cache flushes\)', open(v.logf).read())
    check(m is not None, 'no flush count in the vccd log')
    n = int(m.group(1))
    check(n <= 2, f'{n} cache flushes: at most one for the first switch and one for the build after the edit')

# ---------------- the client's view ----------------

@test()
def output_mode_follows_client_umask():
    v = Vccd(umask=0o022)
    try:
        d = f'{v.dir}/w'; src = tu(d)
        st, err = v.cc(d, src, umask=0o077); check(st == 0, err[-300:])
        m = stat.S_IMODE(os.stat(f'{d}/a.o').st_mode)
        check(m == 0o600, f'output mode {m:o} with client umask 077 (want 600)')
    finally: v.stop()

@test()
def concurrent_jobs_isolated():
    """many clients at once in different directories: every object is right"""
    from concurrent.futures import ThreadPoolExecutor
    v = Vccd(workers=4)
    try:
        def one(i):
            d = f'{v.dir}/w{i}'; src = tu(d, 'a.c', f'int v{i} = {i};\nint f(void) {{ return v{i} * {i}; }}\n')
            st, err = v.cc(d, src)
            return i, st, err, obj(d) if st == 0 else b''
        with ThreadPoolExecutor(8) as ex:
            res = list(ex.map(one, range(32)))
        for i, st, err, o in res:
            check(st == 0, f'job {i}: {st} {err[-200:]}')
            check(f'v{i}'.encode() in o, f'job {i}: object does not define v{i}')
    finally: v.stop()

# ---------------- diagnostics across the build (PLAN-diag.md D7) ----------------

def shim_build(v, d, cmds, env=None):
    """the shim commands run as one build (one process group): (status, stderr)"""
    sh = '; '.join(' '.join([SHIM] + TARGET + ['-fno-color-diagnostics', '-fdiagnostics-style=clang'] + c) for c in cmds)
    p = subprocess.run(['sh', '-c', sh], cwd=d, capture_output=True, text=True, timeout=60,
                       env=dict(os.environ, VCC_SOCK=v.sock, **(env or {})), process_group=0)
    return p.returncode, p.stderr

def query(v, what=None):
    p = subprocess.run([SHIM, '--vccd-diagnostics' + (f'={what}' if what else '')], capture_output=True, text=True,
                       timeout=10, env=dict(os.environ, VCC_SOCK=v.sock))
    check(p.returncode == 0, f'query {what}: {p.returncode} {p.stderr[-300:]}')
    return p.stdout

@test()
def own_warnings_reach_client():
    """a TU vcc compiles with a warning: the client sees it (vccd used to keep it in its log)"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; tu(d, 'a.c', 'int f(void) { int u; return 0; }\n')
        st, err = shim_build(v, d, [['-Wall', '-c', 'a.c']])
        check(st == 0, f'rc {st} {err[-300:]}')
        check("a.c:1:19: warning: unused variable 'u' [-Wunused-variable]" in err, f'no warning: {err[-300:]}')
    finally: v.stop()

@test()
def header_warning_dedup():
    """VCC_DIAG_DEDUP=1: a header warning another TU of the build printed is left out, once per build"""
    v = Vccd()
    try:
        d = f'{v.dir}/w'; tu(d, 'h.h', '#warning from h\n')
        tu(d, 'a.c', '#include "h.h"\nint a;\n'); tu(d, 'b.c', '#include "h.h"\nint b;\n')
        two = [['-c', 'a.c'], ['-c', 'b.c']]
        st, err = shim_build(v, d, two)
        check(st == 0 and err.count('warning: from h') == 2, f'without dedup: {st} {err[-400:]}')
        for build in (1, 2):	# the second build starts with an empty set
            st, err = shim_build(v, d, two, {'VCC_DIAG_DEDUP': '1'})
            check(st == 0 and err.count('warning: from h') == 1, f'dedup build {build}: {st} {err[-400:]}')
            check('1 header warning left out: printed by another TU of this build.' in err, f'no count: {err[-400:]}')
        check('1 header warning left out in this build' in query(v), 'summary without the count')
    finally: v.stop()

@test()
def records_replaced_per_tu():
    """the store holds each TU's records; compiling it again replaces exactly them"""
    import json
    v = Vccd()
    try:
        d = f'{v.dir}/w'
        tu(d, 'a.c', 'int f(void) { int u; return 0; }\n'); tu(d, 'b.c', 'int g(void) { int w; return 0; }\n')
        st, err = shim_build(v, d, [['-Wall', '-c', 'a.c'], ['-Wall', '-c', 'b.c']]); check(st == 0, err[-300:])
        recs = [json.loads(l) for l in query(v, 'json').splitlines()]
        tus = sorted(os.path.basename(r['tu']) for r in recs)
        check(tus == ['a.c', 'b.c'], f'records {tus}')
        check(all(r['flag'] == 'unused-variable' and r['location']['line'] == 1 for r in recs), f'records {recs}')
        s = query(v)
        check('2 warnings and 0 errors in 2 TUs' in s and '-Wunused-variable' in s, f'summary: {s}')
        tu(d, 'a.c', 'int f(void) { return 0; }\n')
        st, err = shim_build(v, d, [['-Wall', '-c', 'a.c']]); check(st == 0, err[-300:])
        recs = [json.loads(l) for l in query(v, 'json').splitlines()]
        check([os.path.basename(r['tu']) for r in recs] == ['b.c'], f'after fixing a.c: {recs}')
        # past 500 records a TU keeps counts and one "omitted" line
        tu(d, 'c.c', 'void h(void) {\n' + ''.join(f'int u{i};\n' for i in range(600)) + '}\n')
        st, err = shim_build(v, d, [['-Wall', '-c', 'c.c']]); check(st == 0, err[-300:])
        recs = [json.loads(l) for l in query(v, 'json').splitlines() if '/c.c"' in l]
        check(len(recs) == 501 and recs[-1].get('omitted') == 100, f'c.c: {len(recs)} lines, last {recs[-1]}')
        check('601 warnings and 0 errors in 2 TUs' in query(v), f'summary: {query(v)}')
        p = subprocess.run([SHIM, '--vccd-diagnostics=xml'], capture_output=True, text=True, timeout=10,
                           env=dict(os.environ, VCC_SOCK=v.sock))
        check(p.returncode == 1 and 'summary or json' in p.stderr, f'bad query: {p.returncode} {p.stderr}')
    finally: v.stop()

@test()
def links_in_process_independent():
    """links one after another in one vccd (vld in-process, state reset per link) give vlink's output"""
    v = Vccd(workers=1)
    try:
        d = f'{v.dir}/w'
        # many named sections, so the script's section-name cache grows past its first size
        n = 1500
        src = ''.join(f'__attribute__((section(".t.f{i}"))) int f{i}(void) {{ return {i}; }}\n' for i in range(n))
        src += ''.join(f'__attribute__((section("set_{k}"), used)) static const int s{k}[2] = {{ {k}, {k} }};\n'
                       for k in range(4))
        src += 'extern const int __start_set_1[], __stop_set_1[];\n'
        src += 'long span(void) { return __stop_set_1 - __start_set_1; }\n'
        tu(d, 'm.c', src)
        st, err = v.cc(d, 'm.c', 'm.o')
        check(st == 0, f'compile: {err[-300:]}')
        scripts = {
            'a.lds': 'SECTIONS { . = 0x10000; .text : { *(.t.*) *(.text*) } .data : { *(.data*) } }\n',
            'b.lds': 'SECTIONS { . = 0x40000; .tx : { *(.text*) } .t : { *(.t.f1*) } .rest : { *(.t.*) } }\n',
        }
        for name, text in scripts.items():
            open(f'{d}/{name}', 'w').write(text)
        links = [['--eh-frame-hdr', '-T', 'a.lds'], ['-T', 'b.lds'], ['-T', 'a.lds', '-e', 'f7'],
                 ['-T', 'b.lds', '-e', 'f3']]
        ref = {}	# vccd's vld adds --vroom-stubs (ld_run)
        for i, a in enumerate(links):
            r = subprocess.run([f'{vd.BIN}/vlink', '--vroom-stubs', '-o', f'ref{i}', 'm.o'] + a, cwd=d, capture_output=True,
                               text=True, timeout=30)
            check(r.returncode == 0, f'vlink {a}: {r.stderr[-300:]}')
            ref[i] = open(f'{d}/ref{i}', 'rb').read()
        for rnd in range(3):
            for i, a in enumerate(links):
                r = subprocess.run([f'{vd.BIN}/vld', '-o', f'out{i}', 'm.o'] + a, cwd=d, capture_output=True, text=True,
                                   env=dict(os.environ, VCC_SOCK=v.sock), timeout=30)
                check(v.alive(), f'vccd died in link {a} (round {rnd})')
                check(r.returncode == 0, f'link {a} (round {rnd}): {r.stderr[-300:]}')
                check(open(f'{d}/out{i}', 'rb').read() == ref[i], f'link {a} (round {rnd}) differs from vlink\'s')
    finally: v.stop()

@test()
def link_machine_reset():
    """an x86-64 link leaves nothing behind: the next arm64 link in the same vccd is vlink's"""
    v = Vccd(workers=1)
    try:
        d = f'{v.dir}/w'
        tu(d, 'm.c', 'int f(void) { return 1; }\nint _start(void) { return f(); }\n')
        for o, t in (('a.o', 'aarch64'), ('x.o', 'x86_64')):
            r = subprocess.run([vd.VPP, f'--target={t}-linux-gnu', '-nostdinc', '-ffreestanding', '-c', '-o', o, 'm.c'],
                               cwd=d, capture_output=True, text=True, timeout=30)
            check(r.returncode == 0, f'compile {t}: {r.stderr[-300:]}')
        for o in ('a.o', 'x.o'):
            r = subprocess.run([f'{vd.BIN}/vlink', '--vroom-stubs', '-o', f'ref-{o}', o], cwd=d, capture_output=True,
                               text=True, timeout=30)
            check(r.returncode == 0, f'vlink {o}: {r.stderr[-300:]}')
        for o in ('a.o', 'x.o', 'a.o'):
            r = subprocess.run([f'{vd.BIN}/vld', '-o', f'out-{o}', o], cwd=d, capture_output=True, text=True,
                               env=dict(os.environ, VCC_SOCK=v.sock), timeout=30)
            check(r.returncode == 0, f'link {o}: {r.stderr[-300:]}')
            check(obj(d, f'out-{o}') == obj(d, f'ref-{o}'), f'link {o} differs from vlink\'s')
        r = subprocess.run([f'{vd.BIN}/vld', '-o', 'mix', 'a.o', 'x.o'], cwd=d, capture_output=True, text=True,
                           env=dict(os.environ, VCC_SOCK=v.sock), timeout=30)
        check(r.returncode != 0 and 'incompatible' in r.stderr, f'mixed machines: rc {r.returncode} {r.stderr[-300:]}')
    finally: v.stop()

def main():
    av = sys.argv[1:]
    verbose = '-v' in av; av = [a for a in av if a != '-v']
    pat = av[0] if av else None
    n = dict(pass_=0, xfail=0, fail=0, skip=0)
    for name, f, xf in TESTS:
        if pat and not re.search(pat, name): continue
        t0 = time.time()
        try:
            f(); ok, why = True, ''
        except Skip as e:
            print(f'skip  {name:32} {e}'); n['skip'] += 1; continue
        except AssertionError as e:
            ok, why = False, str(e)
        except Exception as e:
            ok, why = False, f'{type(e).__name__}: {e}'
        dt = time.time() - t0
        if xf and not ok: st = 'xfail'; n['xfail'] += 1
        elif xf: st = 'XPASS'; n['fail'] += 1; why = f'remove xfail ({xf})'
        elif ok: st = 'pass'; n['pass_'] += 1
        else: st = 'FAIL'; n['fail'] += 1
        if verbose or st != 'pass':
            print(f'{st:5} {name:32} {dt:5.1f} s  {why}' + (f'  [{xf}]' if verbose and st == 'xfail' else ''), flush=True)
    print(f'vccd-test: {n["pass_"]} pass, {n["xfail"]} xfail, {n["skip"]} skip, {n["fail"]} fail')
    sys.exit(1 if n['fail'] else 0)

if __name__ == '__main__':
    main()
