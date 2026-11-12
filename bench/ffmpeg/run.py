#!/usr/bin/env python3
"""ffmpeg (macOS, no assembly) as a test corpus and a loop-heavy benchmark.

  bench/ffmpeg/run.py fetch                     # ffmpeg TAG -> work/ffmpeg/src (once)
  bench/ffmpeg/run.py build [clang vcc vir]     # configure + gmake -j 6 -> work/ffmpeg/b-NAME
  bench/ffmpeg/run.py fate  [clang vcc vir]     # FATE, the tests that need no samples (~2 min each)
  bench/ffmpeg/run.py compile [-n 2] [MODE..]   # compile time of the 2019 C files, no make
  bench/ffmpeg/run.py speed [-n 3] [clang vcc vir] [-k PAT]   # codec/filter CPU time, outputs vs clang's
  bench/ffmpeg/run.py all

Every tree is configured by clang with --disable-asm --disable-autodetect
(config.h is the same file in all of them), so the C fallbacks of the DSP
code are what runs; make then gets CC=bench/pg/cc, which hands every C file
to vpp and lets clang link.  `vcc` is the direct emitter (VCC_VIR=0), `vir`
is VCC_VIR=bl.  A file vcc fails on is logged in b-NAME/fail.jsonl and
compiled by clang (the build reports the count; it must be 0).

`compile` takes clang's command lines from `gmake -n -B` and runs them
without make or the wrapper: one process per file on -j workers (clang, vcc,
vir) or one `vpp --batch` process (vcc-batch, vir-batch).  Wall and CPU, best
of -n rounds.

`speed` runs each build's ffmpeg with -threads 1 on inputs clang's build
generated (work/ffmpeg/bench: 100 frames of 720p, 60 s of stereo audio), CPU
time, best of -n interleaved rounds, and compares each workload's output MD5
with clang's build.

FF_CFLAGS=-ffp-contract=off FF_TAG=-nc ... builds, tests and measures a second
set of trees with extra C flags (work/ffmpeg/b-NAME-nc, bench-nc): without
contraction the floating-point workloads must give clang's bytes too.
Override with CLANG, VPP, VROOM_WORK, JOBS.
"""
import concurrent.futures, hashlib, os, resource, shlex, shutil, subprocess, sys, time

TAG = 'n8.1.3'
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W = os.path.join(os.environ.get('VROOM_WORK') or os.path.join(ROOT, 'work'), 'ffmpeg')
SRC = os.path.join(W, 'src')
EXTRA = os.environ.get('FF_CFLAGS', '')       # extra C flags for every build; trees and data get the suffix FF_TAG
SFX = os.environ.get('FF_TAG', '')
DATA = os.path.join(W, 'bench' + SFX)
CLANG = os.environ.get('CLANG') or ('/opt/homebrew/opt/llvm/bin/clang' if os.path.exists('/opt/homebrew/opt/llvm/bin/clang') else 'clang')
VPP = os.environ.get('VPP') or os.path.join(ROOT, 'build', 'ccs', 'vpp')
WRAP = os.path.join(ROOT, 'bench', 'pg', 'cc')
JOBS = os.environ.get('JOBS', '6')
VIR = {'clang': None, 'vcc': '0', 'vir': 'bl'}
CONFIGURE = ['--disable-asm', '--disable-autodetect', '--disable-doc', '--disable-debug']


def bdir(name):
    return os.path.join(W, 'b-' + name + SFX)


def benv(name):
    env = dict(os.environ, VCC_PG_CLANG=CLANG, VPP=VPP)
    if VIR[name] is not None:
        env.update(VCC_VIR=VIR[name], VCC_PG_LOG=os.path.join(bdir(name), 'fail.jsonl'), VCC_PG_FALLBACK='1')
    return env


def make(name, args, log):
    cmd = ['gmake', '-j', JOBS] + args + (['CC=' + WRAP] if VIR[name] is not None else [])
    t = time.time()
    with open(os.path.join(bdir(name), log), 'w') as f:
        rc = subprocess.run(cmd, cwd=bdir(name), env=benv(name), stdout=f, stderr=subprocess.STDOUT).returncode
    return rc, time.time() - t


def fetch():
    if not os.path.isdir(SRC):
        os.makedirs(W, exist_ok=True)
        subprocess.run(['git', 'clone', '-q', '--depth', '1', '--branch', TAG, 'https://git.ffmpeg.org/ffmpeg.git', SRC], check=True)


def fallbacks(name):
    p = os.path.join(bdir(name), 'fail.jsonl')
    return sum(1 for _ in open(p)) if os.path.exists(p) else 0


def build(name):
    fetch()
    b = bdir(name)
    if not os.path.exists(os.path.join(b, 'config.h')):
        os.makedirs(b, exist_ok=True)
        with open(os.path.join(b, 'configure.out'), 'w') as f:
            subprocess.run([os.path.join(SRC, 'configure'), '--cc=' + CLANG] + CONFIGURE + (['--extra-cflags=' + EXTRA] if EXTRA else []), cwd=b, stdout=f, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['gmake', 'clean'], cwd=b, capture_output=True)      # make does not track the compiler
    if os.path.exists(os.path.join(b, 'fail.jsonl')):
        os.remove(os.path.join(b, 'fail.jsonl'))
    rc, t = make(name, [], 'build.log')
    if rc:
        sys.exit(f'{name}: build failed, see {b}/build.log')
    text = int(subprocess.run(['size', 'ffmpeg'], cwd=b, capture_output=True, text=True).stdout.splitlines()[1].split()[0])
    print(f'{name:6} build {t:5.0f} s   __TEXT {text / 1e6:.1f} MB   compiled by clang instead: {fallbacks(name)}')
    return fallbacks(name)


def fate(name):
    rc, t = make(name, ['-k', 'fate'], 'fate.log')
    log = open(os.path.join(bdir(name), 'fate.log'), errors='replace').read().splitlines()
    ran = sum(1 for l in log if l.startswith('TEST'))
    bad = sorted(l.split()[1] for l in log if l.startswith('Test ') and l.rstrip().endswith('for details.'))
    print(f'{name:6} FATE {ran - len(bad)}/{ran} pass ({t:.0f} s), compiled by clang instead: {fallbacks(name)}')
    for x in bad:
        print('   FAIL', x)
    return 1 if rc or bad or not ran else 0


# ---- compile time ----

def commands():
    """clang's -c command lines for the whole build, as argv lists without -o and the depfile flags."""
    out = subprocess.run(['gmake', '-n', '-B', 'V=1'], cwd=bdir('clang'), capture_output=True, text=True).stdout
    jobs = []
    for line in out.splitlines():
        if not line.startswith(CLANG + ' ') or ' -c ' not in line:
            continue
        a, args, i = shlex.split(line)[1:], [], 0
        while i < len(a):
            if a[i] in ('-MF', '-MT', '-o'):
                i += 2
                continue
            if a[i] != '-MMD':
                args.append(a[i])
            i += 1
        if args[-1].endswith('.c'):
            jobs.append(args)
    return jobs


def cpu():
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime


def compile_mode(mode, jobs, out):
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    cwd = bdir('clang')
    name = mode.split('-')[0]
    env = dict(os.environ)
    if VIR[name] is not None:
        env['VCC_VIR'] = VIR[name]
    t, c = time.perf_counter(), cpu()
    if mode.endswith('-batch'):
        jf = os.path.join(out, 'jobs')
        with open(jf, 'w') as f:
            for k, a in enumerate(jobs):
                f.write('\x1f'.join(['-'] + a + ['-o', os.path.join(out, f'{k}.o')]) + '\n')
        subprocess.run([VPP, '--batch', jf, '-j', JOBS, '-C', cwd], cwd=cwd, env=env, capture_output=True)
    else:
        cc = CLANG if name == 'clang' else VPP

        def one(k):
            return subprocess.run([cc] + jobs[k] + ['-o', os.path.join(out, f'{k}.o')], cwd=cwd, env=env, capture_output=True).returncode
        with concurrent.futures.ThreadPoolExecutor(int(JOBS)) as pool:
            list(pool.map(one, range(len(jobs))))
    wall, used = time.perf_counter() - t, cpu() - c
    ok = sum(1 for k in range(len(jobs)) if os.path.exists(os.path.join(out, f'{k}.o')) and os.path.getsize(os.path.join(out, f'{k}.o')))
    return wall, used, ok


def compile_time(modes, n):
    jobs = commands()
    if not jobs:
        sys.exit('no compile commands: build clang first')
    best = {}
    for _ in range(n):
        for m in modes:
            wall, used, ok = compile_mode(m, jobs, os.path.join(W, 'ct-' + m))
            if ok != len(jobs):
                print(f'{m}: {len(jobs) - ok} of {len(jobs)} files gave no object')
            b = best.get(m)
            best[m] = (min(wall, b[0]), min(used, b[1])) if b else (wall, used)
    print(f'\n{len(jobs)} C files, -j {JOBS}, best of {n}; ratio: {modes[0]} / mode')
    print(f'  {"":10} {"wall s":>8} {"":>7} {"CPU s":>8}')
    for m in modes:
        print(f'  {m:10} {best[m][0]:8.2f} {best[modes[0]][0] / best[m][0]:6.1f}x {best[m][1]:8.2f} {best[modes[0]][1] / best[m][1]:6.1f}x')
    return 0


# ---- run time ----

VIDEO = 'testsrc2=s=1280x720:r=25,noise=alls=12:allf=t'
AUDIO = 'anoisesrc=color=pink:sample_rate=44100:duration=60:seed=1,aecho=0.8:0.9:40:0.4'
INPUTS = [      # file, arguments after the global ones (clang's build makes them)
    ('v.y4m', ['-f', 'lavfi', '-i', VIDEO, '-frames:v', '100', '-pix_fmt', 'yuv420p']),
    ('a.wav', ['-f', 'lavfi', '-i', AUDIO, '-ac', '2', '-c:a', 'pcm_s16le']),
    ('v.mpeg4.avi', ['-i', 'v.y4m', '-c:v', 'mpeg4', '-q:v', '3']),
    ('v.mpeg2.ts', ['-i', 'v.y4m', '-c:v', 'mpeg2video', '-q:v', '3']),
    ('v.mjpeg.avi', ['-i', 'v.y4m', '-c:v', 'mjpeg', '-q:v', '3']),
    ('v.ffv1.nut', ['-i', 'v.y4m', '-c:v', 'ffv1', '-level', '3']),
    ('v.prores.mov', ['-i', 'v.y4m', '-frames:v', '25', '-c:v', 'prores_ks']),
    ('a.aac', ['-i', 'a.wav', '-c:a', 'aac', '-b:a', '128k']),
    ('a.flac', ['-i', 'a.wav', '-c:a', 'flac']),
    ('a.mp2', ['-i', 'a.wav', '-c:a', 'mp2', '-b:a', '192k']),
]
WORK = [        # name, arguments between the global ones and the output
    ('enc-mpeg4', ['-i', 'v.y4m', '-c:v', 'mpeg4', '-q:v', '3', '-f', 'avi']),
    ('dec-mpeg4', ['-i', 'v.mpeg4.avi', '-f', 'rawvideo']),
    ('enc-mpeg2', ['-i', 'v.y4m', '-c:v', 'mpeg2video', '-q:v', '3', '-f', 'mpegts']),
    ('dec-mpeg2', ['-i', 'v.mpeg2.ts', '-f', 'rawvideo']),
    ('enc-mjpeg', ['-i', 'v.y4m', '-c:v', 'mjpeg', '-q:v', '3', '-f', 'avi']),
    ('dec-mjpeg', ['-i', 'v.mjpeg.avi', '-f', 'rawvideo']),
    ('enc-ffv1', ['-i', 'v.y4m', '-c:v', 'ffv1', '-level', '3', '-f', 'nut']),
    ('dec-ffv1', ['-i', 'v.ffv1.nut', '-f', 'rawvideo']),
    ('enc-prores', ['-i', 'v.y4m', '-frames:v', '25', '-c:v', 'prores_ks', '-f', 'mov']),
    ('dec-prores', ['-i', 'v.prores.mov', '-f', 'rawvideo']),
    ('scale', ['-i', 'v.y4m', '-vf', 'scale=1920:1080:flags=bicubic', '-pix_fmt', 'rgb24', '-f', 'rawvideo']),
    ('gblur', ['-i', 'v.y4m', '-vf', 'gblur=sigma=4', '-f', 'rawvideo']),
    ('convolve', ['-i', 'v.y4m', '-vf', 'convolution=0 -1 0 -1 5 -1 0 -1 0', '-f', 'rawvideo']),
    ('unsharp', ['-i', 'v.y4m', '-vf', 'unsharp=7:7:1.5', '-f', 'rawvideo']),
    ('enc-aac', ['-i', 'a.wav', '-c:a', 'aac', '-b:a', '128k', '-f', 'adts']),
    ('dec-aac', ['-i', 'a.aac', '-f', 's16le']),
    ('enc-flac', ['-i', 'a.wav', '-c:a', 'flac', '-compression_level', '8', '-f', 'flac']),
    ('dec-flac', ['-i', 'a.flac', '-f', 's16le']),
    ('enc-mp2', ['-i', 'a.wav', '-c:a', 'mp2', '-b:a', '192k', '-f', 'mp2']),
    ('dec-mp2', ['-i', 'a.mp2', '-f', 's16le']),
    ('resample', ['-i', 'a.wav', '-ar', '48000', '-f', 's16le']),
]
GLOBAL = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-threads', '1', '-y']
TAIL = ['-threads', '1', '-bitexact', '-map_metadata', '-1']


def inputs():
    os.makedirs(DATA, exist_ok=True)
    ff = os.path.join(bdir('clang'), 'ffmpeg')
    for f, a in INPUTS:
        if not os.path.exists(os.path.join(DATA, f)):
            subprocess.run([ff] + GLOBAL + a + TAIL + [f], cwd=DATA, check=True)


def run1(name, args, out):
    """One workload, output to `out`: CPU seconds, or None when it fails."""
    p = subprocess.Popen([os.path.join(bdir(name), 'ffmpeg')] + GLOBAL + args + TAIL + [out], cwd=DATA, stderr=subprocess.PIPE)
    _, st, ru = os.wait4(p.pid, 0)
    err = p.stderr.read().decode(errors='replace').strip()
    if st:
        print(f'{name}: {" ".join(args)}: status {st}: {err[-200:]}')
        return None
    return ru.ru_utime + ru.ru_stime


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for blk in iter(lambda: f.read(1 << 20), b''):
            h.update(blk)
    return h.hexdigest()


def speed(names, n, pat):
    inputs()
    work = [w for w in WORK if not pat or pat in w[0]]
    best, sums, rc = {}, {}, 0
    for r in range(n):
        for w, args in work:
            for c in names:
                out = os.path.join(DATA, f'out-{c}')
                t = run1(c, args, out)
                if t is None:
                    rc = 1
                    continue
                best[c, w] = min(best.get((c, w), 1e9), t)
                if r == 0:
                    sums[c, w] = md5(out)
                os.remove(out)
    print(f'\nffmpeg -threads 1, CPU s, best of {n}; ratio to {names[0]}; * = output differs from {names[0]}\'s')
    print(f'  {"":10}' + ''.join(f'{c:>16}' for c in names))
    tot = dict.fromkeys(names, 0.0)
    for w, _ in work:
        if any((c, w) not in best for c in names):
            continue
        row = f'  {w:10}'
        for c in names:
            d = '*' if sums[c, w] != sums[names[0], w] else ' '
            row += f'{best[c, w]:8.2f} {best[c, w] / best[names[0], w]:5.2f}x{d}'
            tot[c] += best[c, w]
        print(row)
    print(f'  {"total":10}' + ''.join(f'{tot[c]:8.2f} {tot[c] / tot[names[0]]:5.2f}x ' for c in names))
    return rc


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd, a = a[0], a[1:]
    n, pat = None, None
    if '-n' in a:
        i = a.index('-n'); n = int(a[i + 1]); del a[i:i + 2]
    if '-k' in a:
        i = a.index('-k'); pat = a[i + 1]; del a[i:i + 2]
    dflt = ['clang', 'vcc', 'vir']
    if cmd == 'fetch':
        fetch()
    elif cmd == 'build':
        return 1 if sum(build(x) for x in a or dflt) else 0
    elif cmd == 'fate':
        return max(fate(x) for x in a or dflt)
    elif cmd == 'compile':
        return compile_time(a or ['clang', 'vcc', 'vir', 'vcc-batch', 'vir-batch'], n or 2)
    elif cmd == 'speed':
        return speed(a or dflt, n or 3, pat)
    elif cmd == 'all':
        rc = 1 if sum(build(x) for x in dflt) else 0
        return rc | max(fate(x) for x in dflt) | compile_time(['clang', 'vcc', 'vir', 'vcc-batch', 'vir-batch'], 2) | speed(dflt, 3, None)
    else:
        sys.exit(__doc__)
    return 0


if __name__ == '__main__':
    sys.exit(main())
