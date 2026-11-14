#!/usr/bin/env python3
"""vlink-macho.py: vld's Mach-O linker (platform/darwin/macho-linker.c) against a
base build, on programs of src/test/vcc (macOS only).

  bench/vlink-macho.py --base DIR [--bin-dir DIR] [-n N]

Compiles N programs of src/test/vcc (default 80, a fixed sample) for the host
with the new vpp, links each with `vlink -o a.out X.o -lc` from both
directories, and adds error cases (more undefined symbols than the error
limit, an undefined symbol without -lc, a missing input, a duplicate
definition).  Every link must give the same status, the same stderr and the
same bytes from both, and every executable must exit the same way.  Prints
the outcomes (link status, run status) of the new side; exit 1 on any
difference.  Nothing else in bench/ links Mach-O with vld: the self build
links with the host cc.
"""
import argparse, collections, glob, os, platform, random, subprocess, sys, tempfile, shutil

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument('--base', required=True, help='directory with the base vlink')
ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', f'{R}/build/src'))
ap.add_argument('-n', type=int, default=80)
a = ap.parse_args()
if platform.system() != 'Darwin':
    sys.exit('vlink-macho: macOS only')
base, new = os.path.abspath(a.base), os.path.abspath(a.bin_dir)
T = tempfile.mkdtemp(prefix='vlink-macho-')
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

files = sorted(glob.glob(f'{R}/src/test/vcc/*.c'))
random.Random(2).shuffle(files)
objs = []
for f in files:
    if len(objs) >= a.n:
        break
    o = f'{T}/{os.path.basename(f)[:-2]}.o'
    if subprocess.run([f'{new}/vpp', '-c', '-O1', '-w', f, '-o', o], capture_output=True, timeout=60, env=env).returncode == 0:
        objs.append(o)
open(f'{T}/many.c', 'w').write(''.join(f'extern int u{i}(void);\n' for i in range(30)) +
                               'int main(void){return ' + '+'.join(f'u{i}()' for i in range(30)) + ';}\n')
open(f'{T}/one.c', 'w').write('extern int nope(void); int main(void){return nope();}\n')
for n in ('many', 'one'):
    subprocess.run([f'{new}/vpp', '-c', f'{T}/{n}.c', '-o', f'{T}/{n}.o'], check=True, env=env)
links = [([o, '-lc'], True) for o in objs]
links += [([f'{T}/many.o', '-lc'], False), ([f'{T}/one.o'], False), ([f'{T}/one.o', f'{T}/missing.o'], False),
          ([objs[0], objs[0], '-lc'], False)]

def link(d, lab, args, run):
    os.makedirs(f'{T}/{lab}', exist_ok=True)
    out = f'{T}/{lab}/a.out'	# the same name on both sides: the signature holds it
    if os.path.exists(out):
        os.unlink(out)
    p = subprocess.run([f'{d}/vlink', '-o', out] + args, capture_output=True, timeout=120, env=env)
    img = open(out, 'rb').read() if os.path.exists(out) else None
    status = None
    if run and img is not None and p.returncode == 0:
        try:
            status = subprocess.run([out], capture_output=True, timeout=20, cwd=T).returncode
        except subprocess.TimeoutExpired:
            status = 'timeout'
        except OSError as e:
            status = f'exec errno {e.errno}'
    return p.returncode, p.stderr.replace(T.encode(), b'T').replace(lab.encode() + b'/', b'X/'), img, status

bad = 0
outcomes = collections.Counter()
for args, run in links:
    b, n = link(base, 'base', args, run), link(new, 'new', args, run)
    outcomes[(n[0], n[3])] += 1
    if b != n:
        bad += 1
        print(f'DIFF {" ".join(os.path.basename(x) for x in args)}: link {b[0]}/{n[0]}, run {b[3]}/{n[3]}, '
              f'same bytes {b[2] == n[2]}\n  base: {b[1][:300]!r}\n  new:  {n[1][:300]!r}')
print('outcomes of the new side (link status, run status):', dict(outcomes))
print(f'vlink-macho: {len(links) - bad}/{len(links)} identical ({len(objs)} programs, 4 error cases)')
shutil.rmtree(T)
sys.exit(1 if bad else 0)
