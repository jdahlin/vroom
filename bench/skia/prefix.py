#!/usr/bin/env python3
"""-fcxx-prefix= on Skia: the files of the largest flag set of an existing GN
build (work/skia-src/skia/out/vcc, read only), compiled with and without a
prefix of the standard headers Skia names.  Outputs go to build/skia-prefix.

  bench/skia/prefix.py [-j N] [--stride N] [--project N]

--project N adds the N of Skia's own public headers its .cpp files include
most.

own: one vpp per file.  prefix: one vpp --cxx-prefix-batch forking per file.
Per side: failures, instructions, CPU, wall; then the defined symbols of the
files both compiled.
"""
import argparse, collections, os, re, shlex, subprocess, time

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAIN = R.split('/.claude/')[0]
SKIA = os.path.join(MAIN, 'work/skia-src/skia')
LIBCXX = '/usr/lib/llvm-23/include/c++/v1'


def commands(out):
    p = subprocess.run([SKIA + '/third_party/ninja/ninja', '-t', 'commands'],
                       cwd=out, capture_output=True, text=True, check=True)
    sets = collections.defaultdict(list)
    for line in p.stdout.splitlines():
        a = shlex.split(line)
        if not a or not a[0].endswith('vcc-cxx') or '-c' not in a:
            continue
        flags, src, i = [], None, 1
        while i < len(a):
            if a[i] in ('-o', '-MF', '-MT'):
                i += 2
                continue
            if a[i] == '-c':
                src = a[i + 1]
                i += 2
                continue
            if a[i] not in ('-MD', '-MMD'):
                flags.append(a[i])
            i += 1
        sets[tuple(flags)].append(src)
    return max(sets.items(), key=lambda x: len(x[1]))


def standard_headers():
    names = set()
    for top in ('src', 'include', 'modules'):
        for d, _, files in os.walk(os.path.join(SKIA, top)):
            for f in files:
                if f.endswith(('.cpp', '.h')):
                    with open(os.path.join(d, f), errors='replace') as h:
                        names.update(re.findall(
                            r'^\s*#\s*include\s*<([a-z_0-9]+(?:\.h)?)>',
                            h.read(), re.M))
    return sorted(n for n in names if os.path.exists(os.path.join(LIBCXX, n)))


def symbols(obj):
    out = subprocess.run(['llvm-nm-23', '--defined-only', obj],
                         capture_output=True, text=True).stdout
    return {w[-1] for w in (x.split() for x in out.splitlines())
            if w and not w[-1].startswith(('.L', 'GCC_except'))}


def measured(command, cwd, stat):
    start = time.time()
    p = subprocess.run(['perf', 'stat', '-x,', '-o', stat, '-e',
                        'instructions:u,task-clock'] + command, cwd=cwd,
                       capture_output=True, text=True)
    wall = time.time() - start
    got = {}
    with open(stat) as f:
        for line in f:
            w = line.split(',')
            if len(w) > 2:
                got[w[2].split(':')[0]] = float(w[0])
    return (p, got['instructions'] / 1e9, got['task-clock'] / 1e3, wall)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--stride', type=int, default=1)
    ap.add_argument('--project', type=int, default=0)
    ap.add_argument('--bin-dir', default=os.path.join(R, 'build/linux/src'))
    a = ap.parse_args()
    vpp = os.path.abspath(os.path.join(a.bin_dir, 'vpp'))
    build = os.path.join(SKIA, 'out/vcc')
    flags, sources = commands(build)
    included = collections.Counter()
    for s in sources:
        with open(os.path.join(build, s), errors='replace') as f:
            included.update(set(re.findall(
                r'^\s*#include\s+"((?:include|src)/[^"]+\.h)"', f.read(),
                re.M)))
    own_headers = [h for h, _ in included.most_common(a.project)]
    sources = sources[::a.stride]
    out = os.path.join(R, 'build/skia-prefix')
    for side in ('own', 'prefix'):
        os.makedirs(os.path.join(out, side), exist_ok=True)

    def obj(side, s):
        return os.path.join(out, side, s.replace('../', '').replace('/', '_') +
                            '.o')

    for s in sources:
        for side in ('own', 'prefix'):
            if os.path.exists(obj(side, s)):
                os.unlink(obj(side, s))
    prefix = os.path.join(out, 'prefix.h')

    def write(headers):
        with open(prefix, 'w') as f:
            for h in headers:
                if h in own_headers:
                    f.write('#include "%s"\n' % h)
                else:
                    f.write('#include <%s>\n' % h)

    def compiles(headers):
        write(headers)
        with open(os.path.join(out, 'empty.cpp'), 'w') as f:
            f.write('')
        return not subprocess.run(
            [vpp] + list(flags) + ['-fsyntax-only', '-include', prefix,
                                   os.path.join(out, 'empty.cpp')],
            cwd=build, capture_output=True).returncode

    # a header the front end cannot compile yet stays out of the prefix
    named = standard_headers() + own_headers
    refused = [h for h in named if not compiles([h])]
    headers = [h for h in named if h not in refused]
    if not compiles(headers):
        raise SystemExit('skia: the headers do not compile together')
    print('%d files, %d headers in the prefix; refused alone: %s' %
          (len(sources), len(headers), ' '.join(refused) or 'none'))
    flags = list(flags) + ['-c']
    stat = os.path.join(out, 'stat')
    run = os.path.join(out, 'own.sh')
    with open(run, 'w') as f:
        for s in sources:
            f.write(shlex.join([vpp] + flags + [s, '-o', obj('own', s)]) +
                    ' 2>%s\0' % shlex.quote(obj('own', s) + '.err'))
    _, ins, cpu, wall = measured(
        ['sh', '-c', 'xargs -0 -P %d -n 1 sh -c < %s' % (a.j, shlex.quote(run))],
        build, stat)
    failed = [s for s in sources if not os.path.exists(obj('own', s))]
    print('own     %4d failed, %7.1fG instructions, %7.1fs CPU, %6.1fs wall' %
          (len(failed), ins, cpu, wall))
    jobs = os.path.join(out, 'jobs')
    with open(jobs, 'w') as f:
        for s in sources:
            f.write('%s\x1f%s\n' % (s, obj('prefix', s)))
    p, ins, cpu, wall = measured(
        [vpp, '--cxx-prefix-batch', jobs, '-j', str(a.j)] + flags +
        ['-fcxx-prefix=' + prefix, sources[0], '-o', obj('prefix', sources[0])],
        build, stat)
    with open(os.path.join(out, 'prefix.err'), 'w') as f:
        f.write(p.stderr)
    failed2 = [s for s in sources if not os.path.exists(obj('prefix', s))]
    print('prefix  %4d failed, %7.1fG instructions, %7.1fs CPU, %6.1fs wall' %
          (len(failed2), ins, cpu, wall))
    only = sorted(set(failed2) - set(failed))
    gained = sorted(set(failed) - set(failed2))
    both = [s for s in sources if s not in failed and s not in failed2]
    differ = [s for s in both
              if symbols(obj('own', s)) != symbols(obj('prefix', s))]
    print('fail only with the prefix: %d, only without: %d; both compile %d, '
          'defined symbols differ in %d' %
          (len(only), len(gained), len(both), len(differ)))
    for title, group in (('only with prefix', only), ('differ', differ)):
        for s in group[:5]:
            print('  %s: %s' % (title, s))


if __name__ == '__main__':
    main()
