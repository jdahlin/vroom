#!/usr/bin/env python3
"""Bounded four-target O2 compiler-work gate using whole-process Callgrind Ir.

Compare --base to --candidate; optional --parent also reports the incremental
change, reusing each candidate profile. All binaries must be built with matched
host flags. This measures compiler work, not generated-program speed or timing.
The fixed corpus is twelve existing regression sources. Unsupported cases stay
in the report; candidate-only failures and incomplete measurements fail the gate.
"""
import argparse
import difflib
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

from hostenv import BUILD, R, llvm

SOURCES = ('deep_mixed', 'register_locals', 'bit_cast', 'stack_cells',
           'integer_identities', 'deep_conditional', 'unsigned_pow2',
           'signed_pow2', 'atomic_ops', 'mem_builtin_inline', 'bitint_runtime',
           'wasm_frame_cells')
TARGETS = ('x86_64-linux-gnu', 'aarch64-linux-gnu',
           'wasm32-unknown-emscripten', 'wasm64-unknown-emscripten')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def invoke(command, env):
    command = [str(value) for value in command]
    try:
        result = subprocess.run(command, cwd=R, env=env, capture_output=True,
                                text=True, timeout=90)
        return dict(command=command, returncode=result.returncode,
                    stderr=result.stderr, stdout=result.stdout)
    except subprocess.TimeoutExpired as error:
        return dict(command=command, returncode=None, error=str(error))


def save(report, output):
    output.write_text(json.dumps(report, indent=2) + '\n')


def summarize(report, budget):
    """Require complete supported comparisons; never turn a failed run into PASS."""
    failures = list(report.get('errors', []))
    comparisons = {}
    for label in report['binary']:
        if label == 'candidate':
            continue
        totals = {}
        for target in TARGETS:
            accepted, unsupported = [], []
            actual = [row['source'] for row in report['runs'] if row['target'] == target]
            if len(actual) != len(SOURCES) or set(actual) != set(SOURCES):
                failures.append(f'{label}/{target}: fixed sources must appear exactly once each')
            for row in report['runs']:
                if row['target'] != target:
                    continue
                sides = [row['sides'][side] for side in (label, 'candidate')]
                for kind in ('assembly', 'object'):
                    if sides[0][kind]['returncode'] == 0 and sides[1][kind]['returncode'] != 0:
                        failures.append(f'{label}/{target}/{row["source"]}/{kind}: candidate preflight regression')
                if any(side['assembly']['returncode'] != 0 for side in sides):
                    unsupported.append(row['source'])
                    continue
                if not all(side.get('profile', {}).get('returncode') == 0 and
                           side.get('profile', {}).get('instructions', 0) > 0
                           for side in sides):
                    failures.append(f'{label}/{target}/{row["source"]}: incomplete profile')
                    continue
                accepted.append(row)
            before = sum(row['sides'][label]['profile']['instructions'] for row in accepted)
            after = sum(row['sides']['candidate']['profile']['instructions'] for row in accepted)
            change = 100 * (after / before - 1) if before else None
            totals[target] = dict(files=len(accepted), sources=[row['source'] for row in accepted],
                                  unsupported=unsupported, base_instructions=before,
                                  candidate_instructions=after, percent=change)
            if len(accepted) + len(unsupported) != len(SOURCES) or not accepted:
                failures.append(f'{label}/{target}: incomplete or empty corpus')
            if change is not None and change > budget:
                failures.append(f'{label}/{target}: {change:+.4f}% exceeds {budget:.4f}% budget')
        comparisons[label] = totals
    report['comparisons'] = comparisons
    report['gate'] = dict(budget_percent=budget, passed=not failures, failures=failures)
    return not failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'parent', 'candidate'):
        parser.add_argument('--' + name, type=Path)
        parser.add_argument('--' + name + '-revision')
        parser.add_argument('--' + name + '-worktree', type=Path)
        parser.add_argument('--' + name + '-build-log', type=Path)
    parser.add_argument('--out', type=Path, default=Path(BUILD) / 'opt-cost')
    parser.add_argument('--valgrind', default=shutil.which('valgrind'))
    parser.add_argument('--valgrind-lib', type=Path)
    parser.add_argument('--resource-dir', type=Path)
    parser.add_argument('--build-flags', default='Clang23 -O3 -march=x86-64-v3',
                        help='recorded build assertion; build logs verify the actual flags')
    parser.add_argument('--budget', type=float, default=1.0,
                        help='maximum aggregate compiler Ir growth per target/comparison (percent)')
    parser.add_argument('--preflight-only', action='store_true')
    parser.add_argument('--check-report', type=Path,
                        help='recheck a completed report with --budget, without compiling/profiling')
    args = parser.parse_args()
    if not math.isfinite(args.budget) or args.budget < 0:
        parser.error('--budget must be finite and nonnegative')
    if args.check_report:
        report = json.loads(args.check_report.read_text())
        ok = summarize(report, args.budget)
        print(json.dumps(report['gate'], indent=2))
        return 0 if ok else 1
    if not args.base or not args.candidate or not args.valgrind:
        parser.error('--base, --candidate and --valgrind are required')
    labels = ['base'] + (['parent'] if args.parent else []) + ['candidate']
    for label in labels:
        if not getattr(args, label + '_revision') or not getattr(args, label + '_worktree'):
            parser.error(f'--{label}-revision and --{label}-worktree are required for provenance')
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for name in ('VCC_NOOPT', 'VCC_LREGS'):
        env.pop(name, None)
    if args.valgrind_lib:
        env['VALGRIND_LIB'] = str(args.valgrind_lib.resolve())
    resources = args.resource_dir
    if not resources:
        resources = Path(subprocess.check_output([llvm('clang'), '-print-resource-dir'],
                                                 text=True).strip()) / 'include'
    report = dict(schema_version=1, sources={}, binary={}, runs=[], errors=[],
                  targets=list(TARGETS), compiler_level='O2', host_build_assertion=args.build_flags,
                  harness_sha256=digest(__file__), notes=[
                      'Whole-process Callgrind user instructions; no wall-time, hardware-cycle or generated-program speed claim.',
                      'Each binary is profiled once per supported source/target; candidate counts are reused for both comparisons.',
                      'Supported source intersections are reported separately for each comparison. Unsupported cases are not PASS.',
                      'Assembly and direct-object byte equality describe artifacts, not runtime correctness.',
                      'Compiler-work sums require assembly support; object preflight support is reported independently.',
                      'Budget applies to aggregate instruction sums for this fixed small corpus, not all compiler workloads.'])
    for label in labels:
        binary = getattr(args, label).resolve()
        tree = getattr(args, label + '_worktree').resolve()
        revision = getattr(args, label + '_revision')
        head = subprocess.check_output(['git', '-C', str(tree), 'rev-parse', 'HEAD'], text=True).strip()
        resolved = subprocess.check_output(['git', '-C', str(tree), 'rev-parse', revision], text=True).strip()
        dirty = subprocess.run(['git', '-C', str(tree), 'diff', '--quiet', 'HEAD', '--', 'src'], check=False)
        untracked = subprocess.check_output(['git', '-C', str(tree), 'ls-files', '--others', '--exclude-standard', '--', 'src'], text=True)
        if dirty.returncode or untracked:
            parser.error(f'{label}: compiler/source changes must be committed before measurement')
        if head != resolved:
            parser.error(f'{label}: worktree HEAD {head} differs from asserted revision {resolved}')
        info = dict(path=str(binary), sha256=digest(binary), revision=head, worktree=str(tree),
                    ccs_tree=subprocess.check_output(['git', '-C', str(tree), 'rev-parse', 'HEAD:src'], text=True).strip(),
                    worktree_status=subprocess.check_output(['git', '-C', str(tree), 'status', '--short'], text=True))
        log = getattr(args, label + '_build_log')
        if log:
            info['build_log'] = dict(path=str(log.resolve()), sha256=digest(log))
        report['binary'][label] = info
    for name in SOURCES:
        path = Path(R) / 'src/tests/vcc' / (name + '.c')
        report['sources'][name] = dict(path=str(path), sha256=digest(path))
    report['resource_dir'] = str(resources.resolve())
    report['resource_headers_sha256'] = {str(path.relative_to(resources)): digest(path)
                                         for path in sorted(resources.rglob('*.h'))}
    report['runtime_headers_sha256'] = {str(path.relative_to(Path(R))): digest(path)
                                        for path in sorted((Path(R) / 'src/tests/rt/include').rglob('*')) if path.is_file()}
    report['versions'] = {name: invoke(command, env) for name, command in
                          [('clang', [llvm('clang'), '--version']), ('valgrind', [args.valgrind, '--version'])]}
    result_path = output / ('preflight.json' if args.preflight_only else 'result.json')
    for target in TARGETS:
        for name in SOURCES:
            row = dict(target=target, source=name, sides={})
            flags = [f'--target={target}', '-O2', '-ffreestanding', '-fno-stack-protector', '-std=c23',
                     '-isystem', str(resources), '-I' + str(Path(R) / 'src/tests/rt/include')]
            source = report['sources'][name]['path']
            for label in labels:
                stem = output / f'{target}-{name}-{label}'
                side = {}
                for kind, flag, suffix in [('assembly', '-S', '.s'), ('object', '-c', '.o')]:
                    artifact = stem.with_suffix(suffix)
                    side[kind] = invoke([report['binary'][label]['path'], *flags, flag, source, '-o', artifact], env)
                    if side[kind]['returncode'] is None or side[kind]['returncode'] < 0:
                        report['errors'].append(f'{target}/{name}/{label}/{kind}: preflight crash or timeout')
                    if side[kind]['returncode'] == 0:
                        side[kind].update(path=str(artifact), sha256=digest(artifact))
                row['sides'][label] = side
            for label in labels:
                if label == 'candidate':
                    continue
                for kind in ('assembly', 'object'):
                    before, after = row['sides'][label][kind], row['sides']['candidate'][kind]
                    if before['returncode'] == 0 and after['returncode'] != 0:
                        report['errors'].append(f'{target}/{name}/{kind}: candidate regression against {label}')
                    if before['returncode'] == after['returncode'] == 0:
                        row.setdefault('artifacts', {}).setdefault(label, {})[kind + '_equal'] = before['sha256'] == after['sha256']
                        if kind == 'assembly' and before['sha256'] != after['sha256']:
                            diff = ''.join(difflib.unified_diff(Path(before['path']).read_text().splitlines(True),
                                                              Path(after['path']).read_text().splitlines(True)))
                            (output / f'{target}-{name}-{label}.asm.diff').write_text(diff)
            report['runs'].append(row)
            save(report, result_path)
            print(target, name, 'preflight', {label: {kind: row['sides'][label][kind]['returncode']
                  for kind in ('assembly', 'object')} for label in labels}, flush=True)
    if args.preflight_only or report['errors']:
        report['preflight_complete'] = True
        save(report, result_path)
        return 1 if report['errors'] else 0
    for row in report['runs']:
        target, name = row['target'], row['source']
        for label, side in row['sides'].items():
            if side['assembly']['returncode'] != 0:
                continue
            # Do not profile a side that cannot form any supported comparison.
            peers = ['candidate'] if label != 'candidate' else [x for x in labels if x != label]
            if not any(row['sides'][peer]['assembly']['returncode'] == 0 for peer in peers):
                continue
            stem = output / f'{target}-{name}-{label}'
            command = [args.valgrind, '--tool=callgrind', '--callgrind-out-file=' + str(stem) + '.cg',
                       '--log-file=' + str(stem) + '.log', *side['assembly']['command']]
            side['profile'] = invoke(command, env)
            if side['profile']['returncode'] == 0:
                summaries = [line for line in Path(str(stem) + '.cg').read_text().splitlines() if line.startswith('summary:')]
                if len(summaries) != 1 or digest(side['assembly']['path']) != side['assembly']['sha256']:
                    report['errors'].append(f'{target}/{name}/{label}: invalid profile or changed output')
                else:
                    side['profile']['instructions'] = int(summaries[0].split()[1])
            save(report, result_path)
        print(target, name, 'profile complete', flush=True)
    ok = summarize(report, args.budget)
    save(report, result_path)
    print(json.dumps(dict(comparisons=report['comparisons'], gate=report['gate']), indent=2))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
