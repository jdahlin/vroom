#!/usr/bin/env python3
"""vcc-stress.py: pathological inputs must fail cleanly, never crash (TODO.md T5).

  bench/vcc-stress.py [--vccd] [--bin-dir DIR] [-v] [PAT]

Every case is generated into a temporary directory and compiled with
`vpp -c --target=aarch64-linux-gnu`.  What a case expects:
  ok     valid input clang accepts: exit 0
  error  invalid or unreasonable input: exit 1..127 and a diagnostic
         ("error" on stderr, and the case's regex if it has one)
  any    either of the two, as long as nothing crashes
A signal (SIGSEGV from a stack overflow, SIGABRT from an abort()), a
timeout, or an exit status other than these is a failure.

--vccd sends every case through the shim to one private vccd (as
bench/vccd-cc.py does), with no backend: a TU vcc rejects fails with vcc's
own diagnostics.  A case then also fails if vccd died; vccd is
restarted for the next case.  At the end a normal TU must still compile in
the surviving server.  In vccd mode one bad TU must not take the other jobs
with it, which is the point of the test.

Known failures are marked xfail with their TODO.md item; a failure there is
counted as xfail, a pass is XPASS, which is an error (remove the marker in
the commit that fixes it).  Exit status 1 on any FAIL or XPASS.

--bin-dir build/ccs-asan runs the sanitizer build (then an ASan report is a
crash too: ASan exits with status 1 but prints "ERROR: AddressSanitizer").
With a sanitizer build the timeout is 240 s and time budgets are not checked.
"""
import os, re, shutil, signal, subprocess, sys, tempfile, time, importlib.util
spec = importlib.util.spec_from_file_location('vd', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vcc-diff.py'))
vd = importlib.util.module_from_spec(spec); spec.loader.exec_module(vd)

# a sanitizer build (build/ccs-asan, ccs-tsan) is 2-20x slower: a longer timeout, and no
# time budgets (they check that vcc stays linear; the normal build checks that)
SAN = bool(re.search(r'ccs-[a-z]*san\b', vd.BIN))
TIMEOUT = 240 if SAN else 60
TARGET = ['--target=aarch64-linux-gnu', '-nostdinc', '-ffreestanding']

def nest(open_, close, n, inner):
    return open_ * n + inner + close * n

# name: (expect, regex or None, xfail or None, files {name: text | ('sparse', size)},
#        main file, extra args)
def cases(d):
    C = {}
    def case(name, expect, files, main, args=(), regex=None, xfail=None, budget=None):
        C[name] = dict(expect=expect, regex=regex, xfail=xfail, files=files, main=main, args=list(args), budget=budget)

    # front end recursion: vstack.h ends the TU before the stack runs out (clang itself crashes here)
    case('paren-200k', 'error', {'a.c': 'int x = ' + nest('(', ')', 200000, '1') + ';\n'}, 'a.c')
    case('unary-minus-300k', 'any', {'a.c': 'int x = ' + '- ' * 300000 + '1;\n'}, 'a.c')
    case('ternary-100k', 'any', {'a.c': 'int x = ' + '1 ? ' * 100000 + '1' + ' : 0' * 100000 + ';\n'}, 'a.c')
    case('cast-chain-100k', 'any', {'a.c': 'long x = ' + '(long)' * 100000 + '1;\n'}, 'a.c')
    case('block-nest-5000', 'any', {'a.c': 'void f(void) ' + nest('{', '}', 5000, '') + '\n'}, 'a.c')
    # Flat sequences use scalable storage and iterative parsing, not C recursion.
    case('pointer-10k', 'ok', {'a.c': 'int ' + '* ' * 10000 + 'p;\n'}, 'a.c')
    for order in ('forward', 'reverse'):
        values = range(100000) if order == 'forward' else reversed(range(100000))
        labels = ''.join(f'case {i}:' for i in values)
        case('case-labels-100k-' + order, 'ok',
             {'a.c': 'int f(int n) { switch(n) {' + labels + 'return 1; default:return 2;} }\n'}, 'a.c')
    labels = ''.join(f'case {i}:' for i in range(1000))
    case('case-labels-late-duplicate', 'error',
         {'a.c': 'int f(int n) { switch(n) {' + labels + 'case 10:return 1;}return 0;}\n'},
         'a.c', regex='duplicate case')
    # preprocessor recursion
    case('pp-if-parens-200k', 'any', {'a.c': '#if ' + nest('(', ')', 200000, '1') + '\nint x;\n#endif\n'}, 'a.c')
    case('pp-macro-nest-100k', 'any', {'a.c': '#define F(x) x\nint y = ' + nest('F(', ')', 100000, '1') + ';\n'}, 'a.c')
    # one function-like expansion of 2^25 tokens (the argument doubles at every level)
    case('pp-expansion-33M', 'any', {'a.c': '#define D(x) x+x\nlong big = ' + nest('D(', ')', 25, '1') + ';\n'}, 'a.c')
    case('pp-include-self', 'error', {'a.c': '#include "a.c"\n'}, 'a.c')
    # more distinct identifiers than the intern table holds (6.3M): an error, and vccd restarts
    case('idents-7M', 'error', {'a.c': ''.join('int ' + ','.join(f'v{j}' for j in range(i, i + 10)) + ';\n'
                                           for i in range(0, 7000000, 10))}, 'a.c', regex='identifier table full')
    # fixed limits that reject (or crash on) valid code
    def rnest(n):
        e = 'a[0]'
        for i in range(n):
            e = f'a[{i % 64}] + x * ({e})'
        return 'volatile long a[64];\nlong f(long x) { return ' + e + '; }\n'
    # right-nested expressions past the temporaries: spilled; far past the stack: an error or code, no crash (clang crashes)
    case('expr-right-nest-250', 'ok', {'a.c': rnest(250)}, 'a.c')
    case('expr-right-nest-10k', 'any', {'a.c': rnest(10000)}, 'a.c')	# vccd's stacks are bigger: it compiles
    case('struct-10k-members', 'ok', {'a.c': 'struct s {' + ''.join(f' int m{i};' for i in range(10000)) + ' } v;\n'}, 'a.c')
    case('declarator-100-ptrs', 'ok', {'a.c': 'int ' + '*' * 100 + 'p;\n'}, 'a.c')
    case('anon-nest-20', 'ok', {'a.c': 'struct s { ' + 'struct { ' * 20 + 'int x; ' + '}; ' * 20 + '} v;\nint f(void) { return v.x; }\n'}, 'a.c')
    case('call-300-args', 'ok', {'a.c': 'void g(' + ', '.join(['int'] * 300) + ');\nvoid f(void) { g(' + ', '.join(['1'] * 300) + '); }\n'}, 'a.c')
    case('include-dirs-5000', 'ok', {'a.c': 'int x;\n'}, 'a.c', [f'-I{d}/nodir{i}' for i in range(5000)])
    # files
    case('file-3g', 'error', {'a.c': ('sparse', 3 << 30)}, 'a.c', regex='too large')
    # assembler
    case('asm-rept-200M', 'any', {'a.S': '.rept 200000000\nnop\n.endr\n'}, 'a.S')
    case('asm-space-512G', 'any', {'a.S': '.space 0x7fffffffff\n'}, 'a.S')
    case('asm-macro-recursion', 'error', {'a.S': '.macro m\nm\n.endm\nm\n'}, 'a.S')
    # diagnostics (PLAN-diag lager 7): 40 000 errors in one TU stay linear (typo correction scans
    # every symbol: clang's -fspell-checking-limit of 50 attempts bounds it; records are in an arena)
    case('errors-40k', 'error', {'a.c': 'int counter;\n' + ''.join(f'int v{i} = countr + undeclared{i};\n' for i in range(40000))},
         'a.c', ['-ferror-limit=0'], budget=3)
    # control: a normal TU
    case('normal', 'ok', {'a.c': 'int f(int a) { return a * 2 + 1; }\n'}, 'a.c')
    return C

def write(d, files):
    os.makedirs(d, exist_ok=True)
    for n, t in files.items():
        p = f'{d}/{n}'
        if isinstance(t, tuple):
            with open(p, 'wb') as f: f.truncate(t[1])
        else:
            open(p, 'w').write(t)

def crashed(rc, err):
    return rc < 0 or rc >= 128 or 'Sanitizer' in err

class Vccd:
    def __init__(self, tmp):
        self.tmp, self.p = tmp, None
        self.sock = f'{tmp}/vccd.sock'
        self.log = open(f'{tmp}/vccd.log', 'a')
    def start(self):
        if os.path.exists(self.sock): os.unlink(self.sock)
        self.p = subprocess.Popen([f'{vd.BIN}/vccd', '--sock', self.sock, '-j', '4'],
                                  stdout=self.log, stderr=self.log)
        for _ in range(200):
            if os.path.exists(self.sock): return
            time.sleep(0.02)
        raise SystemExit('vcc-stress: vccd did not start')
    def alive(self):
        return self.p.poll() is None
    def stop(self):
        if self.p and self.alive():
            self.p.send_signal(signal.SIGTERM); self.p.wait()

def run(c, d, vccd):
    out = f'{d}/out.o'
    argv = TARGET + c['args'] + ['-c', '-o', out, c['main']]
    if vccd:
        cmd, env = [f'{vd.BIN}/vcc'] + argv, dict(os.environ, VCC_SOCK=vccd.sock)
    else:
        cmd, env = [vd.VPP] + argv, None
    t0 = time.time()
    try:
        p = subprocess.run(cmd, cwd=d, capture_output=True, timeout=TIMEOUT, env=env)
        rc, err = p.returncode, p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return 'timeout', time.time() - t0, ''
    return rc, time.time() - t0, err

def verdict(c, rc, err, server_ok):
    if rc == 'timeout': return False, f'timeout after {TIMEOUT} s'
    if not server_ok: return False, f'vccd died (client rc {rc})'
    if crashed(rc, err): return False, f'crash: rc {rc}' + (' (sanitizer report)' if 'Sanitizer' in err else '')
    if c['expect'] == 'ok':
        return (rc == 0, 'ok' if rc == 0 else f'rejected valid input: rc {rc}: {first(err)}')
    has_diag = 'error' in err
    if rc == 0:
        return (c['expect'] == 'any', 'accepted' if c['expect'] == 'any' else 'accepted, want an error')
    if not has_diag: return False, f'rc {rc} without a diagnostic'
    if c['regex'] and not re.search(c['regex'], err): return False, f'diagnostic does not match /{c["regex"]}/: {first(err)}'
    return True, f'error: {first(err)}'

def first(err):
    for l in err.splitlines():
        if 'error' in l: return l.strip()[:120]
    return err.strip().splitlines()[0][:120] if err.strip() else ''

def main():
    av = sys.argv[1:]
    use_vccd = '--vccd' in av; av = [a for a in av if a != '--vccd']
    verbose = '-v' in av; av = [a for a in av if a != '-v']
    pat = av[0] if av else None
    tmp = tempfile.mkdtemp(prefix='vcc-stress-')
    C = {k: v for k, v in cases(tmp).items() if not pat or re.search(pat, k)}
    vccd = Vccd(tmp) if use_vccd else None
    if vccd: vccd.start()
    fails = xfails = passes = 0
    try:
        for name, c in C.items():
            d = f'{tmp}/{name}'
            write(d, c['files'])
            rc, dt, err = run(c, d, vccd)
            server_ok = vccd.alive() if vccd else True
            ok, why = verdict(c, rc, err, server_ok)
            if ok and c['budget'] and not SAN and dt > c['budget']: ok, why = False, f'{dt:.1f} s, budget {c["budget"]} s'
            if c['xfail'] and not ok: st = 'xfail'; xfails += 1
            elif c['xfail'] and ok: st = 'XPASS'; fails += 1; why += f' -- remove xfail ({c["xfail"]})'
            elif ok: st = 'pass'; passes += 1
            else: st = 'FAIL'; fails += 1
            if verbose or st in ('FAIL', 'XPASS') or st == 'xfail':
                print(f'{st:5} {name:22} {dt:5.1f} s  {why}' + (f'  [{c["xfail"]}]' if st == 'xfail' and verbose else ''), flush=True)
            shutil.rmtree(d, ignore_errors=True)
            if vccd and not server_ok:
                vccd.start()
        if vccd:	# the server that survived everything above still compiles
            c = cases(tmp)['normal']; d = f'{tmp}/final'; write(d, c['files'])
            rc, _, err = run(c, d, vccd)
            ok = rc == 0 and vccd.alive()
            print(f'{"pass" if ok else "FAIL":5} {"vccd-still-serves":22}         rc {rc} {first(err)}')
            passes += ok; fails += not ok
    finally:
        if vccd: vccd.stop()
    print(f'{"vccd" if vccd else "vpp"}: {passes} pass, {xfails} xfail, {fails} fail' + (f'; vccd log {tmp}/vccd.log' if vccd else ''))
    if not vccd: shutil.rmtree(tmp, ignore_errors=True)
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
