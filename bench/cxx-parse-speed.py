#!/usr/bin/env python3
"""The new C++ front end's speed against clang (doc/PLAN-cxx-codegen.md S0).

For each input it reports, as the best of N runs (default 3, one at a time:
the machine is shared):

  pp ns/tok      cxxparse -stats -tokens-only: phases 1-7, every token made
  parse ns/tok   cxxparse -stats, minus the above: the parser alone
  clang -E       clang -cc1 -E, wall seconds (its phases 1-4)
  clang sema     clang -cc1 -fsyntax-only, wall seconds (parse and semantics)
  ratio          clang -fsyntax-only / cxxparse: above 1 is faster than clang

and the parser's counters: tentative parses, rewound tokens, lookups.  The
cxxparse times are its own (-stats nanoseconds: from the start of phase 1 to
the end of the parse, process start-up excluded); clang's are the process's
wall time.

Inputs: json.hpp (work/cxx/json) with libstdc++ 14's headers
(work/cxx-sysroot, PLAN-cxx-parse P9), and with --headers each libstdc++
header alone (or those matching PAT).  Target aarch64-linux-gnu, gnu++17; the
compiler's own headers (stddef.h, ...) are clang's resource directory.

  bench/cxx-parse-speed.py [-n N] [--headers [PAT]] [--bin-dir DIR] [--no-clang]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get('VROOM_WORK', os.path.join(R, 'work'))
SYSROOT = os.path.join(WORK, 'cxx-sysroot', 'usr', 'include')
JSON = os.path.join(WORK, 'cxx', 'json', 'include')
TRIPLE = 'aarch64-linux-gnu'


def find_clang():
    for c in ('clang-23', 'clang'):
        if shutil.which(c):
            return shutil.which(c)
    sys.exit('cxx-parse-speed: no clang')


def find_cxxparse(bin_dir):
    for d in ([bin_dir] if bin_dir else []) + [os.path.join(R, 'build', 'linux', 'src'), os.path.join(R, 'build', 'src')]:
        p = os.path.join(d, 'cxxparse')
        if os.path.exists(p):
            return p
    sys.exit('cxx-parse-speed: no cxxparse; make -C src cxxparse')


def flags(clang):
    resource = subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()
    s = SYSROOT
    return ['-std=gnu++17', '-nostdsysteminc',
            '-internal-isystem', os.path.join(s, 'c++', '14'),
            '-internal-isystem', os.path.join(s, TRIPLE, 'c++', '14'),
            '-internal-isystem', os.path.join(s, 'c++', '14', 'backward'),
            '-internal-isystem', os.path.join(resource, 'include'),
            '-internal-externc-isystem', os.path.join(s, TRIPLE),
            '-internal-externc-isystem', s,
            '-I' + JSON]


def stats(cmd):
    """cxxparse -stats' counters, or None if it failed."""
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode not in (0, 1):
        return None
    out = {}
    for line in p.stdout.splitlines():
        m = re.match(r'^([a-z-]+) (\d+)$', line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out if 'nanoseconds' in out else None


def wall(cmd):
    t = time.perf_counter()
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return time.perf_counter() - t


def measure(cxxparse, clang, common, path, runs, with_clang):
    best = {}
    for _ in range(runs):
        t = stats([cxxparse, '-stats', '-tokens-only'] + common + [path])
        p = stats([cxxparse, '-stats'] + common + [path])
        if not t or not p:
            return None
        if 'pp' not in best or t['nanoseconds'] < best['pp']['nanoseconds']:
            best['pp'] = t
        if 'parse' not in best or p['nanoseconds'] < best['parse']['nanoseconds']:
            best['parse'] = p
        if with_clang:
            cc1 = [clang, '-cc1', '-triple', TRIPLE] + common
            best['clang_e'] = min(best.get('clang_e', 1e9), wall(cc1 + ['-E', path, '-o', '/dev/null']))
            best['clang_sema'] = min(best.get('clang_sema', 1e9), wall(cc1 + ['-fsyntax-only', path]))
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-n', type=int, default=3, help='runs per input (best is kept)')
    ap.add_argument('--headers', nargs='?', const='', default=None, metavar='PAT',
                    help="each libstdc++ header alone (those whose name contains PAT)")
    ap.add_argument('--bin-dir')
    ap.add_argument('--no-clang', action='store_true')
    a = ap.parse_args()
    if not os.path.isdir(SYSROOT) or not os.path.isdir(JSON):
        sys.exit('cxx-parse-speed: needs %s and %s (PLAN-cxx-parse P9)' % (SYSROOT, JSON))
    clang = find_clang()
    cxxparse = find_cxxparse(a.bin_dir)
    common = flags(clang)
    tmp = tempfile.mkdtemp(prefix='cxx-parse-speed-')

    inputs = []
    j = os.path.join(tmp, 'json.cpp')
    open(j, 'w').write('#include <nlohmann/json.hpp>\nint main() {}\n')
    inputs.append(('json.hpp', j))
    if a.headers is not None:
        d = os.path.join(SYSROOT, 'c++', '14')
        for h in sorted(os.listdir(d)):
            if os.path.isfile(os.path.join(d, h)) and '.' not in h and a.headers in h:
                f = os.path.join(tmp, 'h-%s.cpp' % h)
                open(f, 'w').write('#include <%s>\n' % h)
                inputs.append(('<%s>' % h, f))

    print('%-22s %9s %8s %8s %8s %8s %8s %6s %9s %9s' % (
        'input', 'tokens', 'pp ns', 'parse ns', 'vcc s', 'clang -E', 'clang s', 'ratio',
        'tentative', 'rewound'))
    total = {'tokens': 0, 'pp': 0, 'parse': 0, 'clang': 0.0}
    failed = 0
    for name, path in inputs:
        b = measure(cxxparse, clang, common, path, a.n, not a.no_clang)
        if not b:
            print('%-22s failed' % name)
            failed += 1
            continue
        tokens = b['parse']['tokens']
        pp_ns = b['pp']['nanoseconds']
        all_ns = b['parse']['nanoseconds']
        parse_ns = max(all_ns - pp_ns, 0)
        vcc_s = all_ns / 1e9
        ce = b.get('clang_e')
        cs = b.get('clang_sema')
        print('%-22s %9d %8.0f %8.0f %8.3f %8s %8s %6s %9d %9d' % (
            name, tokens, pp_ns / tokens, parse_ns / tokens, vcc_s,
            '%.3f' % ce if ce else '-', '%.3f' % cs if cs else '-',
            '%.2f' % (cs / vcc_s) if cs else '-',
            b['parse']['tentative-parses'], b['parse']['rewound-tokens']))
        total['tokens'] += tokens
        total['pp'] += pp_ns
        total['parse'] += parse_ns
        total['clang'] += cs or 0
    if len(inputs) > 1 and total['tokens']:
        vcc_s = (total['pp'] + total['parse']) / 1e9
        print('%-22s %9d %8.0f %8.0f %8.3f %8s %8s %6s' % (
            'all', total['tokens'], total['pp'] / total['tokens'], total['parse'] / total['tokens'],
            vcc_s, '', '%.3f' % total['clang'] if total['clang'] else '-',
            '%.2f' % (total['clang'] / vcc_s) if total['clang'] else '-'))
    shutil.rmtree(tmp)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
