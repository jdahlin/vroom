#!/usr/bin/env python3
"""Linux gcc.dg execution/compilation parity, with clang as oracle and filter.

Covers direct gcc.dg run tests and direct gcc.dg/torture C tests. Subdirectories
with their own .exp drivers remain outside this harness. --suite errors runs
S4 error-row parity on direct gcc.dg files carrying dg-error: clang and vcc must
reject exactly the same source rows, or both accept. GCC message text and
warning/note parity are outside that deliberately narrower contract. The default
all suite retains the execution/torture selection; errors is a separate mode. Unknown directives are explicit skips. Default torture configuration is
-O0; --matrix selects the six upstream non-LTO configurations. A single selected
configuration does not constitute the whole upstream suite. gcc.dg root tests
use dg.exp's -ansi -pedantic-errors unless dg-options replaces those defaults.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import fnmatch
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location('torture_compile', Path(__file__).with_name('vcc-torture-compile.py'))
tc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tc)
from torture_directives import directives, flags_for, replaces_default_options, selector

ROOT = tc.ROOT.parents[1] / 'gcc.dg'
# gcc-dg.exp DG_TORTURE_OPTIONS (LTO configurations deliberately not modeled).
VARIANTS = {
    '0': ['-O0'], '1': ['-O1'], '2': ['-O2'],
    '3-loop': ['-O3', '-fomit-frame-pointer', '-funroll-loops', '-fpeel-loops', '-ftracer', '-finline-functions'],
    '3-debug': ['-O3', '-g'], 's': ['-Os'],
}


def configuration(src, target, torture=False):
    """Return action, flags, companions, reason; never ignore a contract."""
    action, companions, other = 'compile', [], []
    try:
        ds = list(directives(src))
        actions = [p for p in ds if p[0] == 'dg-do']
        if len(actions) > 1:
            return None, [], [], 'multiple dg-do directives'
        if actions:
            action = actions[0][1] if len(actions[0]) > 1 else None
        if action not in ('compile', 'assemble', 'link', 'run'):
            return None, [], [], 'unsupported dg-do action'
        for parts in ds:
            if parts[0] == 'dg-additional-sources':
                if not 2 <= len(parts) <= 3 or not isinstance(parts[1], str):
                    return None, [], [], 'malformed dg-additional-sources'
                applies = selector(parts[2], target) if len(parts) == 3 else True
                if applies is None:
                    return None, [], [], 'unknown additional-sources selector'
                if applies:
                    companions.extend(shlex.split(parts[1]))
            else:
                other.append(parts)
        # Serialize parsed directives as Tcl lists for the shared conservative
        # evaluator, preserving braced selectors and quoted option strings.
        def tcl(item):
            if isinstance(item, list):
                return '{ ' + ' '.join(tcl(x) for x in item) + ' }'
            return '"' + item + '"'
        text = '\n'.join(tcl(p) for p in other)
        # The scanner expects an unquoted command name.
        text = re.sub(r'\{ "(dg-[\w-]+)"', r'{ \1', text)
        flags, reason = flags_for(text, target, action)
        if flags is None:
            return None, [], [], reason
        defaults = [] if torture or replaces_default_options(src, target) else ['-ansi', '-pedantic-errors']
        return action, defaults + flags, companions, ''
    except (ValueError, TypeError, IndexError):
        return None, [], [], 'malformed or unsupported directive syntax'


def invoke(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout,
                           env={**os.environ, 'LC_ALL': 'C', 'LANG': 'C'})
        return p.returncode, p.stdout, p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return None, b'', 'timeout'


def error_configuration(src, target):
    """Prepare the deliberately narrower S4 error-row contract, not GCC text.

    Warning/note expectations are parsed but not asserted in this mode. Every
    emitted error is compared with clang, including errors at unannotated rows.
    The reference is usable only when its errors are at active dg-error rows.
    """
    if re.search(r'^\s*#\s*(?:line\b|[0-9])', src, re.M):
        return None, set(), 'source location remapping is unsupported'
    lines = src.splitlines()
    items, labels, retained, expected = [], {}, [], set()
    try:
        for line, text in enumerate(lines, 1):
            ds = list(directives(text))
            items.extend((line, p) for p in ds)
            if ds and ds[0][0] == 'dg-line':
                if len(ds[0]) != 2 or not isinstance(ds[0][1], str) or ds[0][1] in labels:
                    return None, set(), 'malformed/duplicate dg-line'
                labels[ds[0][1]] = line
            if not ds or ds[0][0] not in ('dg-line', 'dg-error', 'dg-warning', 'dg-message', 'dg-bogus'):
                retained.append(text)
        for line, parts in items:
            name, *args = parts
            if name not in ('dg-error', 'dg-warning', 'dg-message', 'dg-bogus'):
                continue
            if not 1 <= len(args) <= 4 or not isinstance(args[0], str):
                return None, set(), 'malformed diagnostic directive'
            if len(args) > 1 and not isinstance(args[1], str):
                return None, set(), 'malformed diagnostic comment'
            applies = True
            if len(args) > 2:
                if not isinstance(args[2], list) or not args[2] or args[2][0] != 'target':
                    return None, set(), 'unsupported diagnostic selector'
                applies = selector(args[2], target)
                if applies is None:
                    return None, set(), 'unknown diagnostic selector'
            loc = args[3] if len(args) > 3 else '.'
            if not isinstance(loc, str):
                return None, set(), 'malformed diagnostic line'
            if loc == '.':
                row = line
            elif re.fullmatch(r'\.[+-][0-9]+', loc):
                row = line + int(loc[1:])
            elif loc.isdigit():
                row = int(loc)
            elif loc in labels:
                row = labels[loc]
            else:
                return None, set(), 'unsupported diagnostic line ' + loc
            if not applies:
                continue
            if row < 1 or row > len(lines):
                return None, set(), 'unlocated/out-of-range diagnostic line'
            if name == 'dg-error':
                expected.add(row)
        action, flags, companions, reason = configuration('\n'.join(retained), target)
        if action != 'compile' or companions:
            return None, set(), reason or 'error parity supports single-source dg-do compile only'
        return flags, expected, ''
    except (ValueError, TypeError):
        return None, set(), 'malformed or unsupported diagnostic directive syntax'


def error_locations(stderr, cwd):
    """Read only diagnostic headers, never notes, source excerpts or totals."""
    rows, unlocated = set(), []
    for line in stderr.splitlines():
        m = re.match(r'^(.+?):([0-9]+):(?:[0-9]+:)?\s+(?:fatal )?error:\s*(.*)', line)
        if m:
            file = m[1]
            if not file.startswith('<'):
                file = str((Path(cwd) / file).resolve())
            rows.add((file, int(m[2])))
        elif re.match(r'^(?:[^\s:][^:]*:\s+)?(?:fatal )?error:', line):
            unlocated.append(line)
    return rows, unlocated


def compare_error_rows(reference, actual):
    """A row is the unit; repeated messages there neither add nor hide a row."""
    return sorted(reference - actual), sorted(actual - reference)


def one_errors(path, args, includes, vpp):
    src = path.read_text(errors='replace')
    flags, expected, reason = error_configuration(src, args.target)
    result = {'name': str(path.relative_to(ROOT)), 'variant': 'errors', 'action': 'error-row-parity',
              'status': 'skip', 'reason': reason}
    def finish(status, reason=''):
        return {**result, 'status': status, 'reason': reason}
    if flags is None:
        return result
    opts = [f'--target={args.target}', '-std=gnu23', *includes, *flags,
            '-ferror-limit=0', '-fno-color-diagnostics', '-fno-caret-diagnostics']
    result['options'] = opts
    result['annotated_error_rows'] = sorted(expected)
    with tempfile.TemporaryDirectory(prefix='vcc-gcc-dg-errors-') as wd:
        reference = None
        reference_rc = None
        for compiler, binary in [('clang', tc.CLANG), ('vcc', vpp)]:
            style = ['-fdiagnostics-style=clang'] if compiler == 'vcc' else []
            rc, _, stderr = invoke([binary, *opts, *style, '-S', str(path), '-o', f'{wd}/{compiler}.s'], wd, args.compile_timeout)
            if rc is None or rc < 0:
                return finish('error' if compiler == 'clang' else 'crash', f'{compiler} compile {rc}: {tc.detail(stderr)}')
            rows, unlocated = error_locations(stderr, wd)
            result[compiler + '_errors'] = sorted(rows)
            result[compiler + '_unlocated_errors'] = unlocated
            if compiler == 'clang':
                if unlocated or (rc != 0 and not rows):
                    return finish('skip', 'clang unlocated/driver error: ' + tc.detail(stderr))
                if (rc == 0) != (not rows):
                    return finish('error', 'clang exit status contradicts error diagnostics')
                outside = rows - {(str(path.resolve()), line) for line in expected}
                if outside:
                    result['clang_errors_outside_annotations'] = sorted(outside)
                    return finish('skip', 'clang errors outside active dg-error rows')
                reference, reference_rc = rows, rc
            else:
                missing, extra = compare_error_rows(reference, rows)
                result['missing_error_rows'] = missing
                result['extra_error_rows'] = extra
                if unlocated or (rc != 0 and not rows):
                    return finish('fail', 'vcc unlocated error: ' + tc.detail(stderr))
                if (rc == 0) != (reference_rc == 0):
                    return finish('fail', 'vcc accepts clang-rejected source' if rc == 0 else 'vcc rejects clang-accepted source')
                if missing or extra:
                    return finish('fail', f'error rows: {len(missing)} missing, {len(extra)} extra')
    return finish('pass')


def one(job, args, includes, vpp):
    path, variant = job
    name = str(path.relative_to(ROOT))
    src = path.read_text(errors='replace')
    torture = path.parent == ROOT / 'torture'
    action, flags, companions, reason = configuration(src, args.target, torture)
    result = {'name': name, 'variant': variant, 'action': action, 'status': 'skip', 'reason': reason}
    def finish(status, reason=''):
        return {**result, 'status': status, 'reason': reason}
    if action is None:
        return result
    sources = [path]
    for companion in companions:
        source = (path.parent / companion).resolve()
        if not source.is_relative_to(ROOT) or not source.is_file():
            return finish('error', 'invalid/missing companion ' + companion)
        if source.suffix != '.c':
            return finish('skip', 'non-C companion ' + companion)
        sources.append(source)
    if variant == '3-loop' and not any(fnmatch.fnmatchcase(line, pattern) for line in src.splitlines() for pattern in ('*for*(*', '*while*(*')):
        return finish('skip', 'upstream omits loop configuration for source without loops')
    # dg-options are appended after torture options, just as dg-test does.
    opts = [f'--target={args.target}', '-std=gnu23', *includes, *VARIANTS.get(variant, []), *flags]
    result['options'] = opts
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.target.startswith('aarch64') else []
    with tempfile.TemporaryDirectory(prefix='vcc-gcc-dg-') as wd:
        for compiler, binary in [('clang', tc.CLANG), ('vcc', vpp)]:
            objects = []
            for i, source in enumerate(sources):
                obj, asm = f'{wd}/{compiler}-{i}.o', f'{wd}/{compiler}-{i}.s'
                cmd = [binary, *opts, '-c' if compiler == 'clang' else '-S', str(source), '-o', obj if compiler == 'clang' else asm]
                rc, _, err = invoke(cmd, wd, args.compile_timeout)
                if rc is None or rc < 0:
                    return finish('error' if compiler == 'clang' else 'crash', f'{compiler} compile {rc}: {tc.detail(err)}')
                if rc:
                    return finish('skip' if compiler == 'clang' else 'fail', f'{compiler} compile: {tc.detail(err)}')
                if compiler == 'vcc':
                    # Carry target ISA options through to the assembler too.
                    rc, _, err = invoke([tc.CLANG, *opts, '-c', asm, '-o', obj], wd, args.compile_timeout)
                    if rc != 0:
                        return finish('fail', 'vcc assembly: ' + tc.detail(err))
                objects.append(obj)
            if action not in ('link', 'run'):
                continue
            exe = f'{wd}/{compiler}.exe'
            rc, _, err = invoke([tc.CLANG, *opts, '-fuse-ld=lld', '-no-pie', *objects, '-o', exe], wd, args.compile_timeout)
            if rc != 0:
                return finish('skip' if compiler == 'clang' else 'fail', f'{compiler} link: {tc.detail(err)}')
            if action == 'run':
                rc, _, err = invoke([*runner, exe], wd, args.timeout)
                if rc != 0:
                    return finish('skip' if compiler == 'clang' else 'fail', f'{compiler} execution {rc}: {tc.detail(err)}')
        # dg-do run checks successful exit. Output is unconstrained unless
        # dg-output appears (currently an explicit unsupported-contract skip).
    return finish('pass')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    ap.add_argument('--suite', choices=['all', 'run', 'torture', 'errors'], default='all')
    group = ap.add_mutually_exclusive_group()
    group.add_argument('--opt', choices=list(VARIANTS), default='0')
    group.add_argument('--matrix', action='store_true')
    ap.add_argument('--limit', type=int, help='limit source files (explicit sample, not full suite)')
    ap.add_argument('--timeout', type=int, default=10)
    ap.add_argument('--compile-timeout', type=int, default=30)
    ap.add_argument('--json', type=Path)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-v', action='store_true')
    ap.add_argument('pattern', nargs='?', default='')
    args = ap.parse_args()
    if min(args.j, args.timeout, args.compile_timeout, args.limit if args.limit is not None else 1) < 1:
        ap.error('jobs, timeouts and limit must be positive')
    paths = []
    if args.suite in ('all', 'run'):
        paths.extend(p for p in sorted(ROOT.glob('*.c')) if re.search(r'\{\s*dg-do\s+run\b', p.read_text(errors='replace')))
    if args.suite == 'errors':
        paths.extend(p for p in sorted(ROOT.glob('*.c')) if re.search(r'\{\s*dg-error\b', p.read_text(errors='replace')))
    if args.suite in ('all', 'torture'):
        paths.extend(sorted((ROOT / 'torture').glob('*.c')))
    paths = [p for p in paths if re.search(args.pattern, str(p.relative_to(ROOT)))]
    selected_count = len(paths)
    if args.limit:
        paths = paths[:args.limit]
    if not paths:
        ap.error('no matching tests; fetch GCC with bench/fetch-testsuites.sh gcc')
    jobs = [(p, variant) for p in paths for variant in
            (list(VARIANTS) if args.matrix else [args.opt]) if p.parent == ROOT / 'torture']
    jobs += [(p, 'errors' if args.suite == 'errors' else 'default') for p in paths if p.parent == ROOT]
    includes = tc.system_includes(args.target)
    # Other agents may relink the build while this harness is executing it.
    # Snapshot the selected compiler once; all jobs use the same binary.
    with tempfile.TemporaryDirectory(prefix='vcc-gcc-dg-bin-') as wd:
        vpp = str(Path(wd) / 'vpp')
        shutil.copy2(tc.VPP, vpp)
        with ThreadPoolExecutor(max_workers=args.j) as pool:
            results = list(pool.map(lambda job: one_errors(job[0], args, includes, vpp) if args.suite == 'errors' else one(job, args, includes, vpp), jobs))
    report = {'target': args.target, 'suite': args.suite,
              'scope': ('direct gcc.dg error-row parity (warning/note text is not checked)' if args.suite == 'errors' else 'direct gcc.dg run tests and/or direct gcc.dg/torture C files'),
              'selected_sources': len(paths), 'matching_sources': selected_count,
              'torture_variants': [] if args.suite == 'errors' else list(VARIANTS) if args.matrix else [args.opt], 'results': results}
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + '\n')
    counts = Counter(r['status'] for r in results)
    if args.v:
        for r in results:
            if r['status'] != 'pass':
                print(f"{r['status']} {r['name']} [{r['variant']}]: {r['reason']}")
    for (status, reason), n in Counter((r['status'], r['reason']) for r in results if r['status'] != 'pass').most_common(20):
        print(f'{n:4d} {status}: {reason}')
    print(f'{len(paths)}/{selected_count} matching sources; {len(results)} configurations')
    print(' '.join(f'{s}={counts[s]}' for s in ('pass', 'fail', 'crash', 'skip', 'error')))
    return bool(counts['fail'] or counts['crash'] or counts['error'])


if __name__ == '__main__':
    sys.exit(main())
