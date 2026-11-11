#!/usr/bin/env python3
"""Check 25 beginner C repair stories against exact messages and semantic contracts.

LLVM=/usr/lib/llvm-23 bench/vcc-onboarding.py --target x86_64-linux-gnu -j 4
Repeat with --target aarch64-linux-gnu. Broken programs are never executed.
Missing explanations are GAPs, not passing tests. --baseline permits only
documented gaps and rejects unexpected improvements until their entry is updated.
The link story uses the same Clang/linker driver for both generated objects;
it measures the external linker, not vcc's own driver or linker diagnostics.
"""
import argparse
import difflib
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from hostenv import BUILD, llvm

ROOT = Path(__file__).resolve().parent.parent
CORPUS = ROOT / 'ccs/vcc/tests/onboarding'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def invoke(cmd, cwd):
    try:
        p = subprocess.run(list(map(str, cmd)), cwd=cwd, capture_output=True,
                           text=True, timeout=30)
        return {'command': list(map(str, cmd)), 'returncode': p.returncode,
                'stdout': p.stdout, 'stderr': p.stderr}
    except subprocess.TimeoutExpired:
        return {'command': list(map(str, cmd)), 'returncode': None,
                'stdout': '', 'stderr': 'compiler timed out'}


def flatten(records):
    for record in records:
        yield record
        yield from flatten(record.get('children', []))


def check_expectations(records, expectations, filename):
    """Match diagnostic records, never source snippets or rendered text."""
    missing = []
    for want in expectations:
        matches = []
        for record in flatten(records):
            loc = record.get('location') or {}
            if record.get('level') != want['severity']:
                continue
            if loc.get('line') != want['line']:
                continue
            if Path(loc.get('file', '')).name != filename:
                continue
            explanation = '\n'.join(str(record.get(k) or '') for k in ('message', 'label', 'help'))
            if re.search(want['pattern'], explanation, re.I):
                matches.append(record)
        if not matches:
            missing.append(want)
    return missing


def clang_records(text):
    pattern = r'^(\S[^\n]*?):(\d+):(\d+): (warning|error|fatal error|note): (.*)$'
    return [{'location': {'file': m[1], 'line': int(m[2]), 'column': int(m[3])},
             'level': m[4], 'message': m[5]}
            for m in re.finditer(pattern, text, re.M)]


def check_snapshot(path, actual, bless=False):
    """An independent, checked-in expected message; never accept a regex alone."""
    if bless:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(actual)
    if not path.is_file():
        return f'missing diagnostic snapshot {path.name}; generate with --bless and review'
    expected = path.read_text()
    if expected == actual:
        return None
    diff = '\n'.join(difflib.unified_diff(expected.splitlines(), actual.splitlines(),
                                         str(path), 'actual diagnostic', n=2))
    return 'diagnostic snapshot differs:\n' + diff


def validate_manifest(cases, corpus=CORPUS):
    if len(cases) != 25 or len({c['id'] for c in cases}) != 25:
        raise ValueError('manifest must contain exactly 25 distinct cases')
    for case in cases:
        if not case['expect']:
            raise ValueError(f"{case['id']}: missing independent diagnostic contract")
        for variant in ('broken', 'fixed', 'valid'):
            path = (corpus / case[variant]).resolve()
            if not path.is_relative_to(corpus.resolve()) or not path.is_file():
                raise ValueError(f"{case['id']}: invalid {variant} path")
        for want in case['expect']:
            re.compile(want['pattern'])
            if want['severity'] not in ('warning', 'error', 'note'):
                raise ValueError(f"{case['id']}: invalid severity")


def one(case, args, includes, vpp):
    evidence, problems = {}, []
    snapshot_dir = CORPUS / 'snapshots' / args.target

    def snapshot(style, actual):
        problem = check_snapshot(snapshot_dir / f"{case['id']}.{style}.stderr", actual, args.bless)
        if problem:
            problems.append(problem)
    base = [f'--target={args.target}', '-std=c17', '-Wall', '-Wextra', '-Wconversion',
            '-fno-color-diagnostics', *includes, *case.get('flags', [])]
    for compiler, exe in (('clang', args.clang), ('vcc', vpp)):
        evidence[compiler] = {}
        for variant in ('broken', 'fixed', 'valid'):
            src = case[variant]
            run = invoke([exe, *base, '-fsyntax-only', src], CORPUS)
            evidence[compiler][variant] = run
            if run['returncode'] is None or run['returncode'] < 0:
                problems.append(f'{compiler} {variant}: crash/timeout')
            if variant != 'broken' and (run['returncode'] != 0 or clang_records(run['stderr'])):
                problems.append(f'{compiler} {variant}: repaired/intentional example is not clean')
    broken = evidence['vcc']['broken']
    if (broken['returncode'] == 0) != (evidence['clang']['broken']['returncode'] == 0):
        problems.append('broken example acceptance differs from Clang')
    missing = []
    if case['phase'] == 'link':
        with tempfile.TemporaryDirectory(prefix='onboarding-link-') as wd:
            for compiler, exe in (('clang', args.clang), ('vcc', vpp)):
                evidence[compiler]['link'] = {}
                for variant in ('broken', 'fixed', 'valid'):
                    asm, obj, binary = [Path(wd) / (compiler + variant + ext) for ext in ('.s', '.o', '.exe')]
                    compile_run = invoke([exe, *base, '-S', case[variant], '-o', asm], CORPUS)
                    assembly = invoke([args.clang, f'--target={args.target}', '-c', asm, '-o', obj], CORPUS) if compile_run['returncode'] == 0 else compile_run
                    link = invoke([args.clang, f'--target={args.target}', '-fuse-ld=lld', '-no-pie', obj, '-o', binary], CORPUS) if assembly['returncode'] == 0 else assembly
                    evidence[compiler]['link'][variant] = {'compile': compile_run, 'assembly': assembly, 'link': link}
                    if compile_run['returncode'] != 0 or assembly['returncode'] != 0:
                        problems.append(f'{compiler} {variant}: failed before linking')
                    elif variant != 'broken' and link['returncode'] != 0:
                        problems.append(f'{compiler} {variant}: corrected link failed')
                    elif variant == 'broken' and (link['returncode'] in (None, 0) or any(not re.search(w['pattern'], link['stderr'], re.I) for w in case['expect'])):
                        problems.append(f'{compiler}: missing expected external-linker explanation')
                    if compiler == 'vcc' and variant == 'broken':
                        # The directory is generated per run; retain every other byte.
                        snapshot('link', link['stderr'].replace(wd, '<temporary>'))
    else:
        snapshot('vcc', broken['stderr'])
        narrated = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-style=narrated', case['broken']], CORPUS)
        evidence['vcc']['narrated'] = narrated
        snapshot('narrated', narrated['stderr'])
        if narrated['returncode'] != broken['returncode']:
            problems.append('narrated/text exit status differs')
        structured = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-format=json', case['broken']], CORPUS)
        evidence['vcc']['json'] = structured
        try:
            records = [json.loads(line) for line in structured['stderr'].splitlines() if line.strip()]
            missing = check_expectations(records, case['expect'], Path(case['broken']).name)
            for record in flatten(records):
                if not record.get('id') or record['id'] == 'vcc_unknown':
                    problems.append('diagnostic lacks a stable ID')
                if record.get('level') in ('error', 'fatal error') and broken['returncode'] == 0:
                    problems.append('error diagnostic accompanied by successful exit')
            if args.render.render(structured['stderr'].splitlines(), str(CORPUS)).decode() != broken['stderr']:
                problems.append('structured records do not reproduce the diagnostic text')
            clang_style = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-style=clang', case['broken']], CORPUS)
            evidence['vcc']['clang_style'] = clang_style
            if args.render.render(structured['stderr'].splitlines(), str(CORPUS), style='clang').decode() != clang_style['stderr']:
                problems.append('structured records do not reproduce clang-style text')
            if structured['returncode'] != broken['returncode']:
                problems.append('text/JSON exit status differs')
            sarif_run = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-format=sarif', case['broken']], CORPUS)
            evidence['vcc']['sarif'] = sarif_run
            if args.sarif.check(json.loads(sarif_run['stderr'])):
                problems.append('invalid SARIF diagnostic structure')
        except (ValueError, KeyError, TypeError) as e:
            problems.append(f'invalid structured diagnostic: {e}')
        evidence['clang']['missing_expectations'] = check_expectations(
            clang_records(evidence['clang']['broken']['stderr']), case['expect'], Path(case['broken']).name)
    status = 'FAIL' if problems else 'GAP' if missing else 'PASS'
    known_gap = case.get('known_gap')
    if isinstance(known_gap, dict):
        known_gap = known_gap.get(args.target)
    if args.baseline and not problems and known_gap:
        status = 'XFAIL' if missing else 'XPASS'
    return {'id': case['id'], 'title': case['title'], 'status': status,
            'missing': missing, 'problems': problems, 'known_gap': known_gap,
            'diagnostic_provider': 'external linker' if case['phase'] == 'link' else 'vcc',
            'evidence': evidence}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu'], default='x86_64-linux-gnu')
    ap.add_argument('--bin-dir', default=os.environ.get('VCC_BIN', BUILD))
    ap.add_argument('--baseline', action='store_true')
    ap.add_argument('--bless', action='store_true', help='write exact message snapshots; review changes before committing')
    ap.add_argument('--json', type=Path)
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('pattern', nargs='?', default='')
    args = ap.parse_args()
    if not 1 <= args.j <= 6:
        ap.error('jobs must be 1..6')
    cases = json.loads((CORPUS / 'manifest.json').read_text())
    validate_manifest(cases)
    cases = [c for c in cases if re.search(args.pattern, str(c['id']) + ' ' + c['title'])]
    if not cases:
        ap.error('no cases matched')
    args.clang = llvm('clang')
    args.render = module('onboarding_render', ROOT / 'bench/diag-render.py')
    args.sarif = module('onboarding_sarif', ROOT / 'bench/sarif-check.py')
    probe = invoke([args.clang, f'--target={args.target}', '-E', '-x', 'c', '-v', os.devnull], ROOT)
    if probe['returncode'] != 0:
        ap.error('cannot discover target system headers')
    listing = probe['stderr'].split('#include <...> search starts here:', 1)[1].split('End of search list.', 1)[0]
    includes = [arg for line in listing.splitlines() if line.strip() for arg in ('-isystem', line.strip())]
    with tempfile.TemporaryDirectory(prefix='onboarding-vpp-') as wd:
        vpp = Path(wd) / 'vpp'
        shutil.copy2(Path(args.bin_dir) / 'vpp', vpp)
        compiler_hash = hashlib.sha256(vpp.read_bytes()).hexdigest()
        with ThreadPoolExecutor(max_workers=args.j) as pool:
            results = list(pool.map(lambda c: one(c, args, includes, vpp), cases))
    for result in results:
        print(f"{result['status']:5} {result['id']}: {result['title']}")
        for problem in result['problems']:
            print('      ' + problem)
        for missing in result['missing']:
            print(f"      missing {missing['severity']} at line {missing['line']}: {missing['pattern']}")
    counts = Counter(r['status'] for r in results)
    print(', '.join(f'{n} {status}' for status, n in sorted(counts.items())))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({'target': args.target, 'vpp_sha256': compiler_hash,
                                       'clang': invoke([args.clang, '--version'], ROOT),
                                       'results': results}, indent=2) + '\n')
    return int(any(r['status'] in ('FAIL', 'GAP', 'XPASS') for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
