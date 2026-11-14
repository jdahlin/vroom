#!/usr/bin/env python3
"""win64-native.py: run exported Windows x64 test programs and compare with their expected results (WIN9).

  python bench/win64-native.py DIR|ARCHIVE [-j N] [--allow FILE] [--wrapper CMD]
                               [--timeout-scale X] [-v] [PAT]

The `native` job of .github/workflows/win64.yml runs this on a Windows runner
with nothing but Python: no compiler, no third-party module.  DIR holds what
the `wine` job exported (bench/vcc-win64.py --export DIR, bench/vccrt-test.py
diff|chkstk --export DIR): executables under DIR/exe/ and one or more
DIR/*.manifest.json, each {"version": 1, "producer": ..., "tests": [...]} with
per test
  name     "gcc/20000112-1", unique over all manifests
  exe      path relative to DIR
  argv     list of arguments after the program
  stdin    text fed to the program, or null (an empty stdin)
  stdout   the expected output, "\\n" line ends (clang's reference build run
           under Wine: the same oracle as the Linux job)
  exit     the expected exit code
  timeout  seconds
An ARCHIVE (.tar.xz, .tar.gz, .tar.zst is not read: no module for it) is
unpacked into a directory beside it first.

Each program runs in an empty directory of its own (some tests write files),
in parallel (-j, default the CPU count).  It passes when its stdout with
"\\r\\n" read as "\\n" equals the expected text and its exit code equals the
expected one.  Wine reports exit codes modulo 256 (a Unix process status), so
a code above 255 that is not an NTSTATUS (below 0x80000000) is compared
modulo 256 too; an exception code (0xC0000005 ...) never matches.

--allow FILE (default ci/win64-native-allow.txt beside this script's
checkout, when present) lists tests that legitimately differ on real Windows:
one per line, `NAME  reason`, `#` comments.  An allowed test that fails is
reported but does not fail the run; an allowed test that passes is an error
(remove the entry), so the list cannot go stale.  An entry naming no test in
the manifests is only a warning (the export may be a subset).

--wrapper CMD runs every program as `CMD EXE ARGS` (e.g. `--wrapper wine`
on Linux, to try this runner where there is no Windows).  The summary goes to
stdout; with $GITHUB_STEP_SUMMARY set, the counts and a table of failures are
appended there too.  The exit status is 1 on any unexpected result.
"""
import argparse, concurrent.futures, glob, json, os, re, shlex, subprocess, sys, tarfile, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ALLOW = os.path.join(ROOT, 'ci', 'win64-native-allow.txt')


def load(d):
    tests, names = [], set()
    files = sorted(glob.glob(os.path.join(d, '*.manifest.json')))
    if not files:
        sys.exit(f'win64-native: no *.manifest.json in {d}')
    for f in files:
        with open(f, encoding='utf-8') as m:
            man = json.load(m)
        if man.get('version') != 1:
            sys.exit(f'win64-native: {f}: unknown manifest version {man.get("version")}')
        for t in man['tests']:
            if t['name'] in names:
                sys.exit(f'win64-native: {f}: test {t["name"]} named twice')
            names.add(t['name'])
            tests.append(t)
    return files, tests


def load_allow(path):
    allow = {}
    if not path or not os.path.exists(path):
        return allow
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            parts = line.split(None, 1)
            if len(parts) < 2:
                sys.exit(f'win64-native: {path}:{n}: an entry needs a reason: {line}')
            allow[parts[0]] = parts[1]
    return allow


def exit_matches(got, want):
    if got == want:
        return True
    return isinstance(got, int) and 255 < got < 0x80000000 and got & 0xff == want


def first_difference(want, got):
    a, b = want.split('\n'), got.split('\n')
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else '<end>'
        y = b[i] if i < len(b) else '<end>'
        if x != y:
            return f'line {i + 1}: expected {x[:80]!r}, got {y[:80]!r}'
    return 'same lines'


def run_one(d, t, wrapper, scale):
    exe = os.path.abspath(os.path.join(d, t['exe'].replace('/', os.sep)))
    cmd = wrapper + [exe] + list(t.get('argv') or [])
    stdin = t.get('stdin')
    timeout = float(t.get('timeout', 20)) * scale
    t0 = time.time()
    with tempfile.TemporaryDirectory(prefix='w64n-') as cwd:
        try:
            p = subprocess.run(cmd, input=stdin.encode('latin-1') if stdin is not None else b'',
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd, timeout=timeout)
        except subprocess.TimeoutExpired:
            return t, False, f'timed out after {timeout:.0f} s', time.time() - t0
        except OSError as e:
            return t, False, f'cannot run: {e}', time.time() - t0
    rc = p.returncode
    if rc < 0 and os.name == 'nt':
        rc &= 0xffffffff
    out = p.stdout.decode('latin-1').replace('\r\n', '\n')
    why = []
    if not exit_matches(rc, t['exit']):
        why.append(f'exit {rc:#x} ({rc}), expected {t["exit"]}' if rc > 255 or rc < 0 else f'exit {rc}, expected {t["exit"]}')
    if out != t['stdout']:
        why.append('stdout differs: ' + first_difference(t['stdout'], out))
    if why and p.stderr:
        why.append('stderr: ' + p.stderr.decode('latin-1').strip().replace('\r\n', ' | ').replace('\n', ' | ')[:200])
    return t, not why, '; '.join(why), time.time() - t0


def unpack(archive):
    d = re.sub(r'\.(tar\.(xz|gz|bz2)|txz|tgz)$', '', archive) + '.d'
    if not os.path.isdir(d):
        with tarfile.open(archive) as tf:
            tf.extractall(d + '.tmp', filter='data') if hasattr(tarfile, 'data_filter') else tf.extractall(d + '.tmp')
        os.replace(d + '.tmp', d)
    subs = [s for s in os.listdir(d) if os.path.isdir(os.path.join(d, s))]
    if not glob.glob(os.path.join(d, '*.manifest.json')) and len(subs) == 1:
        d = os.path.join(d, subs[0])
    return d


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dir')
    ap.add_argument('pattern', nargs='?')
    ap.add_argument('-j', type=int, default=os.cpu_count() or 2)
    ap.add_argument('--allow', default=DEFAULT_ALLOW)
    ap.add_argument('--wrapper', default='', help='command to run each program with (e.g. wine)')
    ap.add_argument('--timeout-scale', type=float, default=1.0)
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    d = unpack(a.dir) if os.path.isfile(a.dir) else a.dir
    files, tests = load(d)
    if a.pattern:
        tests = [t for t in tests if re.search(a.pattern, t['name'])]
    allow = load_allow(a.allow)
    wrapper = shlex.split(a.wrapper)
    t0 = time.time()
    res = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as pool:
        for t, ok, why, dt in pool.map(lambda t: run_one(d, t, wrapper, a.timeout_scale), tests):
            res.append((t, ok, why, dt))
            if a.v:
                print(f'{"ok  " if ok else "FAIL"} {t["name"]} ({dt:.2f} s){"" if ok else ": " + why}', flush=True)
    passed = [t for t, ok, _, _ in res if ok]
    failed = [(t, why) for t, ok, why, _ in res if not ok and t['name'] not in allow]
    allowed = [(t, why) for t, ok, why, _ in res if not ok and t['name'] in allow]
    stale = [t for t in passed if t['name'] in allow]
    present = {t['name'] for t in tests}
    unknown = [n for n in allow if n not in present] if not a.pattern else []
    for t, why in failed:
        print(f'FAIL {t["name"]}: {why}')
    for t, why in allowed:
        print(f'allowed {t["name"]}: {why} [{allow[t["name"]]}]')
    for t in stale:
        print(f'STALE {t["name"]}: passes now; remove it from {a.allow}')
    for n in unknown:
        print(f'warning: {n} in {a.allow} is in no manifest')
    slow = ', '.join('%s %.1f s' % (r[0]['name'], r[3]) for r in sorted(res, key=lambda r: -r[3])[:3])
    print(f'win64-native: {len(passed)}/{len(tests)} pass, {len(failed)} fail, {len(allowed)} allowed, '
          f'{len(stale)} stale allow entries; {len(files)} manifests; {time.time() - t0:.1f} s (slowest: {slow})')
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as s:
            s.write(f'### win64 native\n{len(passed)}/{len(tests)} pass, {len(failed)} fail, '
                    f'{len(allowed)} allowed, {len(stale)} stale allow entries\n\n')
            if failed or stale:
                s.write('| test | result |\n|---|---|\n')
                for t, why in failed[:200]:
                    s.write(f'| {t["name"]} | {why.replace("|", "/")[:300]} |\n')
                for t in stale:
                    s.write(f'| {t["name"]} | passes: remove from the allow-list |\n')
    return 1 if failed or stale else 0


if __name__ == '__main__':
    sys.exit(main())
