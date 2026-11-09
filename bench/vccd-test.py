#!/usr/bin/env python3
"""vccd-test.py: vccd as a long-lived service (TODO.md T12).

  bench/vccd-test.py [--bin-dir DIR] [-v] [PAT]

Each test starts its own private vccd (socket in a fresh temporary
directory, no backend) and checks one property of the service: access to the
socket, malformed requests, idle connections, cache invalidation between
builds, the client's umask.  Requests are sent either through the real shim
(build/ccs/vcc) or by a raw client in this file that speaks protocol.h
directly, to send what the shim never would.

Known failures are marked xfail with their TODO.md item: a failure is
counted as xfail, a pass is XPASS (an error: remove the marker in the commit
that fixes it).  Tests that cannot run here (a second user is needed) are
skipped.  Exit status 1 on any FAIL or XPASS.

--bin-dir build/ccs-asan or build/ccs-tsan runs the sanitizer builds of
vccd; then also check the vccd log each test prints with -v.
"""
import array, os, re, signal, socket, stat, struct, subprocess, sys, tempfile, time, importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)

SHIM = f'{vd.R}/build/ccs/vcc'
MAGIC = 0x326f6f72

def hdr(magic, argc, envc, plen, build=0):
    return struct.pack('<IIIIQ', magic, argc, envc, plen, build)
TARGET = ['--target=aarch64-linux-gnu', '-nostdinc', '-ffreestanding']
TESTS = []

def test(xfail=None):
    def deco(f):
        TESTS.append((f.__name__.replace('_', '-'), f, xfail)); return f
    return deco

class Skip(Exception): pass

class Vccd:
    """a private vccd in its own directory"""
    def __init__(self, workers=4, umask=None, sockdir_mode=None, extra=()):
        self.dir = tempfile.mkdtemp(prefix='vccd-test-')
        if sockdir_mode is not None: os.chmod(self.dir, sockdir_mode)
        self.sock = f'{self.dir}/vccd.sock'
        self.logf = f'{self.dir}/vccd.log'
        pre = (lambda: os.umask(umask)) if umask is not None else None
        self.p = subprocess.Popen([f'{vd.BIN}/vccd', '--sock', self.sock, '--backend-sock', f'{self.dir}/none.sock',
                                   '-j', str(workers)] + list(extra), stdout=open(self.logf, 'w'), stderr=subprocess.STDOUT,
                                  preexec_fn=pre)
        for _ in range(200):
            if os.path.exists(self.sock): break
            time.sleep(0.02)
        else: raise SystemExit('vccd-test: vccd did not start')
    def alive(self): return self.p.poll() is None
    def stop(self):
        if self.alive(): self.p.send_signal(signal.SIGTERM); self.p.wait(5)
    def cc(self, cwd, src, out='a.o', extra=(), umask=None, timeout=30):
        """compile through the shim: (status, stderr)"""
        pre = (lambda: os.umask(umask)) if umask is not None else None
        p = subprocess.run([SHIM] + TARGET + list(extra) + ['-c', '-o', out, src], cwd=cwd, capture_output=True,
                           timeout=timeout, env=dict(os.environ, VCC_SOCK=self.sock), preexec_fn=pre)
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
    for f in ('shim.c', 'vccd.c'):
        src = open(f'{vd.R}/ccs/vcc/{f}').read()
        check('"/tmp/vccd.sock"' not in src, f'{f}: default socket /tmp/vccd.sock')

@test()
def peer_other_user():
    """a connection from another uid is refused (needs passwordless sudo -u nobody)"""
    if subprocess.run(['sudo', '-n', '-u', 'nobody', 'true'], capture_output=True).returncode:
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
                st = raw_request(v.sock, d, argv, fds=fds)
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

@test(xfail='Caches never invalidated (edited header)')
def edited_header_rebuilds():
    v = Vccd()
    try:
        d = f'{v.dir}/w'; tu(d, 'h.h', '#define N 1\n'); src = tu(d, 'a.c', '#include "h.h"\nint v = N;\n')
        a, b = two_builds(v, d, src, lambda: open(f'{d}/h.h', 'w').write('#define N 22222\n'))
        check(a != b, 'the object did not change after the header was edited')
        check(b == fresh(d, src, '3.o'), 'second build differs from a fresh server')
    finally: v.stop()

@test(xfail='Caches never invalidated (edited main file)')
def edited_main_file_rebuilds():
    v = Vccd()
    try:
        d = f'{v.dir}/w'; src = tu(d, 'a.c', 'int v = 1;\n')
        a, b = two_builds(v, d, src, lambda: open(f'{d}/a.c', 'w').write('int v = 22222;\n'))
        check(a != b, 'the object did not change after the source was edited')
    finally: v.stop()

@test(xfail='Caches never invalidated (header created after a miss)')
def header_created_after_miss():
    v = Vccd()
    try:
        d = f'{v.dir}/w'
        src = tu(d, 'a.c', '#if __has_include("n.h")\n#include "n.h"\n#else\n#define N 1\n#endif\nint v = N;\n')
        a, b = two_builds(v, d, src, lambda: open(f'{d}/n.h', 'w').write('#define N 22222\n'))
        check(a != b, 'the new header was not found in the second build')
    finally: v.stop()

@test(xfail='Caches never invalidated (header created in an earlier -I directory)')
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
