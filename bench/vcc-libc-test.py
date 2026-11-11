#!/usr/bin/env python3
"""Linux libc-test runtime and API parity using the upstream make graph.

Clang is the glibc runtime oracle. Reference build/runtime failures are reported
separately, never counted as compiler passes. vcc compiles all test/support C;
clang assembles and links. No source edits or expected-result substitutions.

Requires the pinned fetch-testsuites.sh libc-test checkout and Linux glibc
headers/libraries (including libcrypt development files). ARM additionally
needs qemu-aarch64 and the /usr/aarch64-linux-gnu cross sysroot. --config-mak
accepts upstream configuration, e.g. explicit include/link/rpath additions for
an extracted cross dependency; the full config is retained in the artifacts.
Default all maps functional, math and regression dynamic executables, preserving
per-test .mk support objects/DSOs. --linkage static or both adds only the static
variants that upstream defines (math has none); static runs retain the dynamic
prerequisites and report those incidental outcomes separately. --suite api
checks each API object with upstream strict flags and the unchanged full link
aggregate. --suite musl exposes the private-libc dependency; --suite full selects
all categories. Upstream disables musl/pleval's dynamic run, which is reported
as unsupported rather than passing its empty .err file. Glibc does not provide its
private __pleval symbol; Clang may reject the source before reaching that link. The support library is built by each compiler.

Examples:
  LLVM=/usr/lib/llvm-23 bench/vcc-libc-test.py --suite math 'math/fabs*'
  bench/vcc-libc-test.py --target aarch64-linux-gnu --config-mak cross.mak
Results and reproducible command/config/source/binary snapshots are saved under
--output (must not exist), or a fresh /tmp/vcc-libc-test-* directory. The runtime
uses upstream runtest's timeout, stack limit and exit-status protocol.
"""
import argparse
from collections import Counter
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from hostenv import BUILD, WORK, llvm


def execute(cmd, timeout, log=None):
    error = None
    try:
        p = subprocess.run(cmd, timeout=timeout, stderr=subprocess.PIPE)
        rc = p.returncode
        stderr = p.stderr.decode(errors='replace')
        sys.stderr.write(stderr)
        if rc < 0:
            error = 'signal'
        elif rc in (124, 126, 127):
            error = 'tool-error'
        elif rc and re.search(r'unable to execute command|cannot execute|unable to find library|cannot find -l|cannot find .*crt|PLEASE submit a bug report|internal compiler error', stderr, re.I):
            error = 'tool-error'
    except subprocess.TimeoutExpired:
        rc, error = 124, 'timeout'
    except OSError as exc:
        rc, error = 127, 'tool-error'
        print(str(exc), file=sys.stderr)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(json.dumps({'cwd': os.getcwd(), 'argv': cmd, 'returncode': rc,
                                   'error': error}, indent=2))
    return rc


def stage_errors(directory):
    errors = []
    for path in sorted(directory.glob('*.json')):
        record = json.loads(path.read_text())
        rc = record['returncode']
        if record.get('error') or rc < 0 or rc in (124, 126, 127):
            errors.append({'log': str(path), **record})
    return errors


def make_error(returncode, logfile, command_dir, observed):
    """make -k can fail because an ordinary rejected object blocks a target."""
    if not returncode:
        return False
    if returncode != 2:
        return True
    text = logfile.read_text(errors='replace')
    if re.search(r'No rule to make target|missing separator|unterminated.*(?:variable|function)|recipe commences before first target|Error (?:12[467]|[3-9][0-9]*)\b|command not found|Permission denied', text):
        return True
    failures = [line for line in text.splitlines() if '***' in line]
    if not failures or any(not re.match(r'^make(?:\[\d+\])?: \*\*\* \[.*\] Error [12](?:\s|$)', line) for line in failures):
        return True
    records = [json.loads(p.read_text()) for p in command_dir.glob('*.json')]
    normal_rejection = any(0 < r['returncode'] < 124 and not r.get('error') for r in records)
    # A command rejection must have an observable failed build; unexplained make
    # failures remain fatal even if another test happened to pass.
    return not (normal_rejection and 'build-fail' in observed.values() or
                'runtime-fail' in observed.values())


def failed_run(counts, errors):
    return bool(errors or not counts.get('pass') or
                any(k.startswith('vcc-') or 'harness-error' in k for k in counts))


def compiler(config, argv):
    """Compiler adapter; upstream supplies flags, objects and libraries unchanged."""
    cfg = json.loads(Path(config).read_text())
    out = argv[argv.index('-o') + 1] if '-o' in argv else 'preprocess'
    key = hashlib.sha256((' '.join(argv)).encode()).hexdigest()[:16]
    log = Path(cfg['logs']) / key
    base = [cfg['clang'], '--target=' + cfg['target']]
    if '-E' in argv:
        cmd = [cfg['vpp'], '--target=' + cfg['target'], *cfg['includes']] if cfg['mode'] == 'vcc' else base
        path = log.with_suffix('.preprocess.json')
        rc = execute([*cmd, *argv], cfg['compile_timeout'], path)
        if rc:
            record = json.loads(path.read_text())
            record['error'] = record.get('error') or 'support-preprocess-fail'
            path.write_text(json.dumps(record, indent=2))
        return rc
    if cfg['mode'] == 'vcc' and '-c' in argv:
        asm = out + '.s'
        args = list(argv)
        args[args.index('-c')] = '-S'
        args[args.index('-o') + 1] = asm
        rc = execute([cfg['vpp'], '--target=' + cfg['target'], *cfg['includes'], *args],
                     cfg['compile_timeout'], log.with_suffix('.compile.json'))
        if rc:
            return rc
        return execute([*base, '-c', asm, '-o', out], cfg['compile_timeout'],
                       log.with_suffix('.assemble.json'))
    return execute([*base, *argv], cfg['compile_timeout'], log.with_suffix('.clang.json'))


def make(source, args, log):
    cmd = ['make', '-k', '--no-print-directory', '-C', str(source), *args]
    with log.open('w') as f:
        p = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    log.with_suffix('.command.json').write_text(json.dumps(cmd, indent=2))
    return p.returncode


# This is an upstream .mk contract, not a compiler failure exemption.
UPSTREAM_DISABLED = {'musl/pleval': 'src/musl/pleval.mk disables the dynamic run: __pleval is private'}


def disabled_rules(source):
    disabled = {}
    for name, reason in UPSTREAM_DISABLED.items():
        rule = source / 'src' / (name + '.mk')
        if rule.is_file() and re.search(r'^\$\(B\)/\$\(N\)\.err:\n\ttouch \$@\s*$', rule.read_text(), re.M):
            disabled[name] = reason
    return disabled


def corpus(binaries, sources, suite, linkage, patterns, api_objects=None):
    dynamic = sorted(n for n in binaries if not n.endswith('-static'))
    static = sorted(n for n in binaries if n.endswith('-static'))
    api_objects = sorted(api_objects if api_objects is not None else (n.removesuffix('.c') + '.o' for n in sources if n.startswith('api/')))
    suites = {'all': ('functional', 'math', 'regression'),
              'full': ('functional', 'math', 'regression', 'api', 'musl')}.get(suite, (suite,))
    runtime = list(dynamic if linkage == 'dynamic' else static if linkage == 'static' else dynamic + static)
    # The API aggregate is a single link contract, independent of runtime linkage.
    if 'api' in suites and 'api/main' not in runtime:
        runtime.append('api/main')
    available = runtime + (api_objects if 'api' in suites else [])
    selected = sorted(n for n in available if n.split('/')[0] in suites and
                      any(fnmatch.fnmatchcase(n, pat) for pat in patterns))
    return selected, {'mapped_dynamic': dynamic, 'mapped_static': static,
                      'api_objects': api_objects, 'upstream_disabled': UPSTREAM_DISABLED,
                      'implicit_dynamic_prerequisites': sorted(n.removesuffix('-static') for n in selected if n.endswith('-static'))}


def build_target(name):
    return name if name.endswith('.o') else name + '.err'


def outcomes(root, names, errors=(), completed=None, disabled=UPSTREAM_DISABLED):
    results = {}
    for name in names:
        exe, err = root / (name + '.exe'), root / (name + '.err')
        if name in disabled:
            status = 'unsupported-upstream'
        elif name.endswith('.o'):
            status = 'pass' if (root / name).is_file() and (completed is None or str(root / name) in completed) else ('build-error' if errors else 'build-fail')
        elif not exe.exists():
            status = 'build-error' if errors else 'build-fail'
        elif not err.exists() or not (root / (name + '.run-status')).exists():
            status = 'harness-error'
        elif (root / (name + '.run-status')).read_text().strip() in ('125', '126', '127') or re.search(r'exec failed:|error while loading shared libraries|Could not open .*ld-linux', err.read_text(errors='replace')):
            status = 'harness-error'
        elif (root / (name + '.run-status')).read_text().strip() != '0' or err.read_bytes().strip():
            status = 'runtime-fail'
        else:
            status = 'pass'
        results[name] = status
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('patterns', nargs='*', default=['*'])
    p.add_argument('--source', type=Path, default=Path(WORK) / 'testsuites/libc-test')
    p.add_argument('--config-mak', type=Path, help='upstream config.mak override (copied into artifacts)')
    p.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    p.add_argument('--suite', choices=['functional', 'math', 'regression', 'api', 'musl', 'all', 'full'], default='all')
    p.add_argument('--linkage', choices=['dynamic', 'static', 'both'], default='dynamic')
    p.add_argument('--output', type=Path)
    p.add_argument('--bin-dir', type=Path, default=Path(os.environ.get('VCC_BIN', BUILD)))
    p.add_argument('-j', type=int, default=6)
    p.add_argument('--timeout', type=int, default=5)
    p.add_argument('--compile-timeout', type=int, default=30)
    args = p.parse_args()
    required = ['make', 'ar', 'ranlib', 'sh', 'awk', llvm('clang'), str(args.bin_dir / 'vpp')]
    if args.target.startswith('aarch64'):
        required.append('qemu-aarch64')
    missing = [tool for tool in required if not shutil.which(tool)]
    if missing:
        p.error('required tools unavailable: ' + ', '.join(missing))
    if platform.system() != 'Linux' or not 1 <= args.j <= 6:
        p.error('Linux and 1 <= -j <= 6 required')
    if args.target.startswith('x86_64') and platform.machine() not in ('x86_64', 'AMD64'):
        p.error('x86 target requires an x86 host')
    if args.timeout <= 0 or args.compile_timeout <= 0:
        p.error('timeouts must be positive')
    out = (args.output or Path(tempfile.mkdtemp(prefix='vcc-libc-test-'))).resolve()
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    src = out / 'source'
    shutil.copytree(args.source, src, ignore=shutil.ignore_patterns('.git', '*.o', '*.lo', '*.exe', '*.so', '*.err', 'REPORT', 'config.mak'))
    shutil.copyfile(args.config_mak or src / 'config.mak.def', src / 'config.mak')
    vpp = out / 'vpp'
    shutil.copy2(args.bin_dir / 'vpp', vpp)
    clang = llvm('clang')
    probe = subprocess.run([clang, '--target=' + args.target, '-E', '-x', 'c', '-v', '/dev/null'], capture_output=True, text=True, check=True)
    listing = probe.stderr.split('#include <...> search starts here:', 1)[1].split('End of search list.', 1)[0]
    includes = [arg for line in listing.splitlines() if line.strip() for arg in ('-isystem', line.strip())]
    # Ask make for its fully expanded binaries: .mk files remove support-only C.
    manifest = out / 'manifest.mk'
    manifest.write_text('include Makefile\n.PHONY: manifest\nmanifest:\n\t@echo BINS $(BINS)\n\t@echo API $(api.OBJS)\n')
    inventory = subprocess.check_output(['make', '-s', '-C', str(src), '-f', str(manifest), 'B=build', 'manifest'], text=True)
    rows = {}
    for line in inventory.splitlines():
        key, _, value = line.partition(' ')
        rows[key] = value
    bins = rows['BINS'].split()
    api_objects = [n.removeprefix('build/') for n in rows['API'].split()]
    binaries = [x.removeprefix('build/').removesuffix('.exe') for x in bins if x.endswith('.exe')]
    sources = sorted(str(x.relative_to(src / 'src')) for x in (src / 'src').glob('*/*.c'))
    names, contracts = corpus(binaries, sources, args.suite, args.linkage, args.patterns, api_objects)
    disabled = disabled_rules(src)
    contracts['upstream_disabled'] = disabled
    if not names:
        p.error('no mapped tests selected')
    source_rev = subprocess.run(['git', '-C', str(args.source), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    mapping = {'source_revision': source_rev, 'target': args.target, 'selected': names,
               'vpp_sha256': hashlib.sha256(vpp.read_bytes()).hexdigest(),
               'clang_version': subprocess.check_output([clang, '--version'], text=True),
               **contracts, 'suite': args.suite, 'linkage': args.linkage,
               'source_counts': dict(Counter(n.split('/')[0] for n in sources)),
               'outside_scope': ['other libc implementations; no musl runtime is provided'],
               'sources_without_dynamic_entry': sorted(n for n in sources if n.removesuffix('.c') not in contracts['mapped_dynamic'])}
    (out / 'mapping.json').write_text(json.dumps(mapping, indent=2))
    runner = []
    wrap = out / 'run-target'
    if args.target.startswith('aarch64'):
        runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu']
    wrap.write_text('#!/bin/sh\ncd "$(dirname "$(dirname "$1")")/run" || exit 125\nexec ' + shlex.join(runner) + (' ' if runner else '') + '"$@"\n')
    wrap.chmod(0o755)
    script = out / 'vcc-libc-test.py'
    shutil.copy2(__file__, script)
    shutil.copy2(Path(__file__).with_name('hostenv.py'), out / 'hostenv.py')
    roots = {mode: out / mode for mode in ('clang', 'vcc')}
    for mode, root in roots.items():
        (root / 'run').mkdir(parents=True)
        (root / 'run/src').symlink_to(root, target_is_directory=True)
        cfg = out / (mode + '.json')
        cfg.write_text(json.dumps({'mode': mode, 'clang': clang, 'vpp': str(vpp), 'target': args.target,
                                  'includes': includes, 'logs': str(out / (mode + '-commands')), 'compile_timeout': args.compile_timeout}))
        adapter = out / (mode + '-cc')
        adapter.write_text('#!/bin/sh\nexec ' + shlex.join([sys.executable, str(script), '--compiler', str(cfg)]) + ' "$@"\n')
        adapter.chmod(0o755)
    # Keep test supervision outside the compiler under test.
    ref = roots['clang']
    common = ['B=' + str(ref), 'CC=' + str(out / 'clang-cc')]
    make(src, [*common, str(ref / 'common/runtest.exe'), '-j', str(args.j)], out / 'supervisor-build.log')
    supervisor = ref / 'common/runtest.exe'
    if not supervisor.exists():
        raise SystemExit('reference supervisor did not build; see ' + str(out))
    supervise = out / 'supervise'
    supervise.write_text('#!/bin/sh\n' + shlex.join([*runner, str(supervisor), '-t', str(args.timeout), '-w', str(wrap)]) + ' "$@" 2>&1\nrc=$?\necho "$rc" > "${1%.exe}.run-status"\nexit "$rc"\n')
    supervise.chmod(0o755)
    run_test = shlex.quote(str(supervise))
    results, errors, make_status, incidental = [], [], [], {}
    for mode in ('clang', 'vcc'):
        selected = names if mode == 'clang' else [n for n in names if reference[n] == 'pass']
        rc = 0
        requested = [n for n in selected if n not in disabled]
        if requested:
            root = roots[mode]
            rc = make(src, ['B=' + str(root), 'CC=' + str(out / (mode + '-cc')), 'RUN_TEST=' + run_test,
                           '-j', str(args.j), *[str(root / build_target(n)) for n in requested]], out / (mode + '-make.log'))
            if rc:
                print(f'{mode} make exited {rc}; inspect artifacts', file=sys.stderr)
        build_errors = stage_errors(out / (mode + '-commands'))
        errors.extend({'mode': mode, **e} for e in build_errors)
        records = [json.loads(p.read_text()) for p in (out / (mode + '-commands')).glob('*.json')]
        completed = {r['argv'][r['argv'].index('-o') + 1] for r in records if r['returncode'] == 0 and '-o' in r['argv']}
        observed = outcomes(roots[mode], selected, build_errors, completed, disabled)
        fatal_make = bool(requested) and make_error(rc, out / (mode + '-make.log'), out / (mode + '-commands'), observed)
        make_status.append({'mode': mode, 'returncode': rc, 'fatal': fatal_make})
        if fatal_make:
            errors.append({'mode': mode, 'stage': 'make', 'returncode': rc})
        dependency_names = [n.removesuffix('-static') for n in selected if n.endswith('-static') and n.removesuffix('-static') not in names]
        incidental[mode] = outcomes(roots[mode], dependency_names, build_errors, disabled=disabled)
        if mode == 'clang':
            reference = observed
        else:
            for n in names:
                status = 'reference-' + reference[n] if reference[n] != 'pass' else ('pass' if observed[n] == 'pass' else 'vcc-' + observed[n])
                results.append({'name': n, 'status': status})
    counts = dict(Counter(r['status'] for r in results))
    (out / 'results.json').write_text(json.dumps({**mapping, 'counts': counts, 'results': results, 'stage_errors': errors, 'make_status': make_status, 'incidental_dynamic_outcomes': incidental}, indent=2))
    print(json.dumps(counts, sort_keys=True))
    if errors:
        print(f'{len(errors)} infrastructure/compiler stage errors; see results.json', file=sys.stderr)
    print('Artifacts:', out)
    return int(failed_run(counts, errors))


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--compiler':
        sys.exit(compiler(sys.argv[2], sys.argv[3:]))
    sys.exit(main())
