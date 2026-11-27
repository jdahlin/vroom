#!/usr/bin/env python3
"""Skia's dm with the C++ objects compiled through -fcxx-prefix=: a private
mirror of work/skia-src/skia (symlinks, and a copy of out/vcc-test, the vcc
build with dm that must exist), every C++ file of dm recompiled by
vpp --cxx-prefix-batch per flag set (a file the prefix fails is compiled
without it, as vccd would), dm relinked.  Run build/skia-dm/skia/out/dm/dm.

  bench/skia/dm.py [-j N]
"""
import argparse, collections, importlib.util, os, shlex, shutil, subprocess
import time

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location(
    'skia_prefix', os.path.join(R, 'bench/skia/prefix.py'))
sp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=8)
    a = ap.parse_args()
    vpp = os.path.join(R, 'build/linux/src/vpp')
    vlink = os.path.join(R, 'build/linux/src/vlink')
    top = os.path.join(R, 'build/skia-dm')
    tree = os.path.join(top, 'skia')
    out = os.path.join(tree, 'out/dm')
    original = os.path.join(sp.SKIA, 'out/vcc-test')
    shutil.rmtree(top, ignore_errors=True)
    os.makedirs(os.path.join(tree, 'out'))
    for name in os.listdir(sp.SKIA):
        if name != 'out':
            os.symlink(os.path.join(sp.SKIA, name), os.path.join(tree, name))
    shutil.copytree(original, out, symlinks=True)
    lines = subprocess.run(
        [sp.SKIA + '/third_party/ninja/ninja', '-t', 'commands', 'dm'],
        cwd=original, capture_output=True, text=True, check=True
    ).stdout.splitlines()
    sets = collections.defaultdict(list)
    link = None
    for line in lines:
        w = shlex.split(line)
        if not w or not w[0].endswith('vcc-cxx'):
            continue
        if '-c' not in w:
            link = line
            continue
        flags, src, obj, i = [], None, None, 1
        while i < len(w):
            if w[i] == '-o':
                obj = w[i + 1]
                i += 2
                continue
            if w[i] == '-c':
                src = w[i + 1]
                i += 2
                continue
            if w[i] in ('-MF', '-MT'):
                i += 2
                continue
            if w[i] not in ('-MD', '-MMD'):
                flags.append(w[i])
            i += 1
        sets[tuple(flags)].append((src, obj))
    prefix = os.path.join(top, 'prefix.h')
    with open(prefix, 'w') as f:
        f.write(''.join('#include <%s>\n' % h for h in sp.standard_headers()
                        if h != 'future'))
    for flags, units in sets.items():
        for _, obj in units:
            os.unlink(os.path.join(out, obj))
    start = time.time()
    shared = alone = 0
    broken = []
    for n, (flags, units) in enumerate(sorted(sets.items(),
                                              key=lambda x: -len(x[1]))):
        common = list(flags) + ['-c']
        if len(units) >= 4:
            jobs = os.path.join(top, 'jobs%d' % n)
            with open(jobs, 'w') as f:
                f.write(''.join('%s\x1f%s\n' % u for u in units))
            subprocess.run(
                [vpp, '--cxx-prefix-batch', jobs, '-j', str(a.j)] + common +
                ['-fcxx-prefix=' + prefix, units[0][0], '-o', units[0][1]],
                cwd=out, capture_output=True)
        left = [u for u in units if not os.path.exists(os.path.join(out, u[1]))]
        shared += len(units) - len(left)
        running = []
        for src, obj in left:
            running.append((src, subprocess.Popen(
                [vpp] + common + [src, '-o', obj], cwd=out,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)))
            if len(running) >= a.j:
                s, p = running.pop(0)
                if p.wait():
                    broken.append(s)
        for s, p in running:
            if p.wait():
                broken.append(s)
        alone += len(left)
    print('dm: %d files through the prefix, %d without it, %d failed, %.0fs' %
          (shared, alone - len(broken), len(broken), time.time() - start))
    if broken:
        raise SystemExit('dm: ' + ' '.join(broken[:5]))
    # ninja's response file: the link statement's inputs
    with open(os.path.join(out, 'obj/dm.ninja')) as f:
        statement = next(x for x in f if x.startswith('build ./dm: link '))
    inputs = statement[len('build ./dm: link '):].split(' |')[0].split()
    with open(os.path.join(out, 'dm.rsp'), 'w') as f:
        f.write(' '.join(inputs))
    clang = '/usr/bin/clang++-23'
    w = shlex.split(link.replace('\\$', '$'))[1:]
    line = subprocess.run([clang, '-###'] + w, cwd=out, capture_output=True,
                          text=True, check=True).stderr.strip().splitlines()[-1]
    subprocess.run([vlink] + shlex.split(line)[1:], cwd=out, check=True)
    print('dm: linked %s/dm' % out)


if __name__ == '__main__':
    main()
