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
import tomllib

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


TARGETS = ('x86_64-linux-gnu', 'aarch64-linux-gnu')
SECTION = re.compile(r'^=== ([a-zA-Z0-9_.-]+) ===\n', re.M)


def compare_message(expected, actual, label):
    if expected is None:
        return f'missing expected message {label}; generate with --bless and review'
    if expected == actual:
        return None
    diff = '\n'.join(difflib.unified_diff(expected.splitlines(), actual.splitlines(),
                                         label, 'actual diagnostic', n=2))
    return 'diagnostic snapshot differs:\n' + diff


def parse_case(text, path):
    markers = list(SECTION.finditer(text))
    if not markers:
        raise ValueError(f'{path}: no fixture sections')
    case = tomllib.loads(text[:markers[0].start()])
    sections = {}
    allowed = {'broken.c', 'fixed.c', 'valid.c', 'stderr', 'narrated'} | {
        f'{style}.{target}' for style in ('stderr', 'narrated') for target in TARGETS}
    for i, marker in enumerate(markers):
        name = marker[1]
        if name not in allowed or name in sections:
            raise ValueError(f'{path}: unknown or duplicate section {name}')
        end = markers[i + 1].start() if i + 1 < len(markers) else len(text)
        sections[name] = text[marker.end():end]
    case.update(_path=str(path), _sections=sections)
    case.setdefault('phase', 'syntax')
    for variant in ('broken', 'fixed', 'valid'):
        case[variant] = variant + '.c'
    return case


def expected_message(case, target, style):
    name = 'stderr' if style in ('vcc', 'link') else style
    sections = case['_sections']
    return sections.get(f'{name}.{target}', sections.get(name))


def save_case(case):
    # Share identical messages between targets; keep only genuine differences.
    sections = dict(case['_sections'])
    styles = ('stderr',) if case['phase'] == 'link' else ('stderr', 'narrated')
    for style in styles:
        values = {t: sections.get(f'{style}.{t}', sections.get(style)) for t in TARGETS}
        if all(value is not None for value in values.values()):
            sections[style] = values[TARGETS[0]]
            for target, value in values.items():
                sections.pop(f'{style}.{target}', None)
                if value != sections[style]:
                    sections[f'{style}.{target}'] = value
    quote = lambda value: json.dumps(value, ensure_ascii=False)
    lines = [f'id = {quote(case["id"])}', f'title = {quote(case["title"])}']
    if case['phase'] != 'syntax':
        lines.append(f'phase = {quote(case["phase"])}')
    if case.get('flags'):
        lines.append('flags = [' + ', '.join(map(quote, case['flags'])) + ']')
    lines.append('expect = [')
    for want in case['expect']:
        fields = ', '.join(f'{key} = {quote(value)}' for key, value in want.items())
        lines.append('  { ' + fields + ' },')
    lines.append(']')
    gap = case.get('known_gap')
    if gap:
        value = ('{ ' + ', '.join(f'{quote(k)} = {quote(v)}' for k, v in gap.items()) + ' }'
                 if isinstance(gap, dict) else quote(gap))
        lines.append('known_gap = ' + value)
    text = '\n'.join(lines) + '\n\n'
    names = ['broken.c', 'fixed.c', 'valid.c']
    for style in styles:
        names += [style] + [f'{style}.{t}' for t in TARGETS]
    for name in names:
        if name in sections:
            text += f'=== {name} ===\n' + sections[name]
    Path(case['_path']).write_text(text)
    case['_sections'] = sections


def load_cases(corpus=CORPUS):
    return [parse_case(path.read_text(), path) for path in sorted(corpus.glob('*.test'))]


def validate_cases(cases, corpus=CORPUS):
    if len(cases) != 25 or len({c['id'] for c in cases}) != 25:
        raise ValueError('suite must contain exactly 25 distinct cases')
    for case in cases:
        if not case['expect']:
            raise ValueError(f"{case['id']}: missing independent diagnostic contract")
        for variant in ('broken', 'fixed', 'valid'):
            if case[variant] not in case['_sections']:
                raise ValueError(f"{case['id']}: missing {variant} source section")
        for want in case['expect']:
            re.compile(want['pattern'])
            if want['severity'] not in ('warning', 'error', 'note'):
                raise ValueError(f"{case['id']}: invalid severity")


def one(case, args, includes, vpp):
    with tempfile.TemporaryDirectory(prefix='onboarding-case-') as wd:
        directory = Path(wd)
        for variant in ('broken', 'fixed', 'valid'):
            (directory / case[variant]).write_text(case['_sections'][case[variant]])
        return evaluate(case, args, includes, vpp, directory)


def evaluate(case, args, includes, vpp, case_dir):
    evidence, problems = {}, []

    def snapshot(style, actual):
        name = 'stderr' if style in ('vcc', 'link') else style
        if args.bless:
            case['_sections'][f'{name}.{args.target}'] = actual
        problem = compare_message(expected_message(case, args.target, style), actual,
                                  f"{case['_path']} [{name}, {args.target}]")
        if problem:
            problems.append(problem)
    base = [f'--target={args.target}', '-std=c17', '-Wall', '-Wextra', '-Wconversion',
            '-fno-color-diagnostics', *includes, *case.get('flags', [])]
    for compiler, exe in (('clang', args.clang), ('vcc', vpp)):
        evidence[compiler] = {}
        for variant in ('broken', 'fixed', 'valid'):
            src = case[variant]
            run = invoke([exe, *base, '-fsyntax-only', src], case_dir)
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
                    compile_run = invoke([exe, *base, '-S', case[variant], '-o', asm], case_dir)
                    assembly = invoke([args.clang, f'--target={args.target}', '-c', asm, '-o', obj], case_dir) if compile_run['returncode'] == 0 else compile_run
                    link = invoke([args.clang, f'--target={args.target}', '-fuse-ld=lld', '-no-pie', obj, '-o', binary], case_dir) if assembly['returncode'] == 0 else assembly
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
        narrated = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-style=narrated', case['broken']], case_dir)
        evidence['vcc']['narrated'] = narrated
        snapshot('narrated', narrated['stderr'])
        if narrated['returncode'] != broken['returncode']:
            problems.append('narrated/text exit status differs')
        structured = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-format=json', case['broken']], case_dir)
        evidence['vcc']['json'] = structured
        try:
            records = [json.loads(line) for line in structured['stderr'].splitlines() if line.strip()]
            missing = check_expectations(records, case['expect'], Path(case['broken']).name)
            for record in flatten(records):
                if not record.get('id') or record['id'] == 'vcc_unknown':
                    problems.append('diagnostic lacks a stable ID')
                if record.get('level') in ('error', 'fatal error') and broken['returncode'] == 0:
                    problems.append('error diagnostic accompanied by successful exit')
            if args.render.render(structured['stderr'].splitlines(), str(case_dir)).decode() != broken['stderr']:
                problems.append('structured records do not reproduce the diagnostic text')
            clang_style = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-style=clang', case['broken']], case_dir)
            evidence['vcc']['clang_style'] = clang_style
            if args.render.render(structured['stderr'].splitlines(), str(case_dir), style='clang').decode() != clang_style['stderr']:
                problems.append('structured records do not reproduce clang-style text')
            if structured['returncode'] != broken['returncode']:
                problems.append('text/JSON exit status differs')
            sarif_run = invoke([vpp, *base, '-fsyntax-only', '-fdiagnostics-format=sarif', case['broken']], case_dir)
            evidence['vcc']['sarif'] = sarif_run
            if args.sarif.check(json.loads(sarif_run['stderr'])):
                problems.append('invalid SARIF diagnostic structure')
        except (ValueError, KeyError, TypeError) as e:
            problems.append(f'invalid structured diagnostic: {e}')
        evidence['clang']['missing_expectations'] = check_expectations(
            clang_records(evidence['clang']['broken']['stderr']), case['expect'], Path(case['broken']).name)
    if args.bless:
        save_case(case)
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
    cases = load_cases()
    validate_cases(cases)
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
