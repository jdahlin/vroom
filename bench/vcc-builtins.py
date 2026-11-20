#!/usr/bin/env python3
"""Compare builtin feature queries and report unrecognized calls in C.

The candidate inventory is the union of vroom's checked-in reference query
tables and Clang generated builtin tables if supplied or built by TableGen.
Query results are
observations for the selected configuration. The C call probe detects
undeclared names, and provides no evidence of correct semantic checking.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TARGETS = (
    'aarch64-linux-gnu', 'arm64-apple-darwin',
    'x86_64-linux-gnu', 'x86_64-w64-mingw32',
)
SNAPSHOT_ROW = re.compile(
    r'(?:\{\s*"__has_(?:constexpr_)?builtin",\s*|'
    r'FEATURE_CHECK\((?:constexpr_)?builtin,\s*)"([A-Za-z_]\w*)"'
)
ENUMERATOR = re.compile(r'^\s*BI([A-Za-z_]\w*),\s*$', re.MULTILINE)

CLANG_TABLES = {
    'Builtins.td': '-gen-clang-builtins',
    'BuiltinsX86.td': '-gen-clang-builtins',
    'BuiltinsX86_64.td': '-gen-clang-builtins',
    'BuiltinsAArch64.td': '-gen-clang-builtins',
    'arm_neon.td': '-gen-arm-neon-sema',
    'arm_fp16.td': '-gen-arm-neon-sema',
    'arm_sve.td': '-gen-arm-sve-builtins',
    'arm_sme.td': '-gen-arm-sme-builtins',
}

def candidates(include_dirs):
    snapshots = sorted((ROOT / 'src/frontend/pp').glob('ppdata*.inc'))
    snapshots.append(ROOT / 'src/frontend/cplusplus/ext/clang/feature-checks.inc')
    names = set()
    sources = []
    for path in snapshots:
        found = SNAPSHOT_ROW.findall(path.read_text())
        if found:
            names.update(found)
            sources.append(str(path.relative_to(ROOT)))
    for directory in include_dirs:
        for path in sorted(directory.glob('Builtins*.inc')):
            found = ENUMERATOR.findall(path.read_text())
            if found:
                names.update(found)
                sources.append(str(path))
    return sorted(names), sources



def clang_candidates(source, tblgen):
    basic = source / 'clang/include/clang/Basic'
    extracted = {}
    for filename, action in CLANG_TABLES.items():
        path = basic / filename
        command = [tblgen, action, '-I', str(basic),
                   '-I', str(source / 'clang/include'),
                   '-I', str(source / 'llvm/include'), str(path)]
        proc = run(command, '')
        if proc.returncode:
            raise RuntimeError(f'{filename}: clang-tblgen failed\n{proc.stderr}')
        found = set(ENUMERATOR.findall(proc.stdout))
        if not found:
            raise RuntimeError(f'{filename}: clang-tblgen produced no recognized builtins')
        extracted[filename] = found
    return extracted


def run(command, source):
    return subprocess.run(command, input=source, capture_output=True, text=True)


GCC_CATALOG_SOURCE = """
#define DEF_BUILTIN(ENUM, NAME, CLASS, TYPE, LIBTYPE, BOTH, FALLBACK, NONANSI, ATTRS, IMPLICIT, COND) \\
    VROOM_BUILTIN(#ENUM, #NAME, #CLASS, #TYPE, #LIBTYPE, #BOTH, #FALLBACK, #NONANSI, #ATTRS, #IMPLICIT, #COND)
#include "builtins.def"
"""
GCC_FIELDS = ('id', 'name_expression', 'class', 'type', 'library_type',
              'library_condition', 'fallback_condition', 'nonansi_condition',
              'attributes', 'implicit_condition', 'registration_condition')
QUOTED = r'"(?:[^"\\]|\\.)*"'


def gcc_common_catalog(source, clang):
    options = ['-E', '-P', '-x', 'c', '-I', str(source / 'gcc'), '-']
    proc = run([clang, *options], GCC_CATALOG_SOURCE)
    if proc.returncode:
        raise RuntimeError(f'GCC builtins.def preprocessing failed\n{proc.stderr}')
    rows = re.findall(r'VROOM_BUILTIN\(((?:' + QUOTED + r'(?:,\s*|(?=\))))+)\)',
                      proc.stdout)
    if not rows or len(rows) != proc.stdout.count('VROOM_BUILTIN('):
        raise RuntimeError('Could not decode every GCC builtin record')
    catalog = []
    for row in rows:
        fields = json.loads(f'[{row}]')
        if len(fields) != len(GCC_FIELDS):
            raise RuntimeError(f'Unexpected GCC builtin record: {row}')
        record = dict(zip(GCC_FIELDS, fields))
        expression = record['name_expression']
        if re.fullmatch(rf'{QUOTED}(?:\s*{QUOTED})*', expression):
            record['spelling'] = ''.join(json.loads(piece)
                                         for piece in re.findall(QUOTED, expression))
        else:
            record['spelling'] = None
        catalog.append(record)
    return catalog


GCC_TARGET_SOURCES = {
    'config/i386/i386-builtin.def': r'"(__builtin_ia32\w+)"',
    'config/i386/i386-builtins.cc': r'"(__builtin_ia32\w+)"',
    'config/aarch64/aarch64-builtins.cc': r'"(__builtin_aarch64_\w+)"',
}


def gcc_target_candidates(source):
    return {filename: sorted(set(re.findall(pattern,
                                           (source / 'gcc' / filename).read_text())))
            for filename, pattern in GCC_TARGET_SOURCES.items()}


def source_revision(source):
    proc = run(['git', '-C', str(source), 'rev-parse', 'HEAD'], '')
    return proc.stdout.strip() if proc.returncode == 0 else None


def header_api(clang, options, headers):
    if not headers:
        return {}
    options = [clang, *options, '-ffreestanding', '-nostdlibinc']
    macro_command = [*options, '-E', '-dM', '-']
    macro_pattern = r'^#define ([A-Za-z_]\w*)\('
    baseline = run(macro_command, '')
    if baseline.returncode:
        raise RuntimeError(f'Clang predefined macro probe failed\n{baseline.stderr}')
    predefined = set(re.findall(macro_pattern, baseline.stdout, re.MULTILINE))
    result = {}
    for header in headers:
        source = f'#include <{header}>\n'
        ast = run([*options, '-fsyntax-only', '-Xclang', '-ast-dump', '-'], source)
        macros = run(macro_command, source)
        if ast.returncode or macros.returncode:
            raise RuntimeError(f'Clang header probe failed for {header}\n'
                               f'{ast.stderr}{macros.stderr}')
        functions = re.findall(r"^.*\bFunctionDecl\b[^\n]*?\b([A-Za-z_]\w*) '[^\n]*$",
                               ast.stdout, re.MULTILINE)
        result[header] = {
            'functions': sorted({name for name in functions
                                 if not name.startswith('__builtin_')}),
            'function_like_macros': sorted(set(re.findall(
                macro_pattern, macros.stdout, re.MULTILINE)) - predefined),
        }
    return result


def query(cc, options, names, kind):
    source = ''.join(f'{index} {kind}({name})\n'
                     for index, name in enumerate(names))
    proc = run([cc, *options, '-E', '-P', '-'], source)
    rows = [line.split() for line in proc.stdout.splitlines() if line.strip()]
    if (proc.returncode or len(rows) != len(names) or
            any(len(row) != 2 or row[0] != str(index) or not re.fullmatch(r'[0-9]+[uUlL]*', row[1])
                for index, row in enumerate(rows))):
        raise RuntimeError(f'{cc} {options}: invalid {kind} output\n{proc.stderr}')
    return {name: int(row[1].rstrip('uUlL'))
            for name, row in zip(names, rows) if int(row[1].rstrip('uUlL'))}


def unrecognized_calls(vpp, options, names):
    unknown = set()
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'calls.c'
        for offset in range(0, len(names), 200):
            chunk = names[offset:offset + 200]
            path.write_text(''.join(f'void f{i}(void) {{ {name}(0); }}\n'
                                    for i, name in enumerate(chunk)))
            proc = subprocess.run([vpp, *options, '-fsyntax-only',
                                   '-ferror-limit=0', '-w', str(path)],
                                  capture_output=True, text=True)
            if proc.returncode not in (0, 1):
                raise RuntimeError(f'{vpp} {options}: call probe failed\n{proc.stderr}')
            missing = re.findall(r"undeclared (?:function|identifier) '([^']+)'",
                                 proc.stderr)
            unknown.update(set(chunk).intersection(missing))
    return sorted(unknown)


def compare_queries(reference, actual):
    common = reference.keys() & actual.keys()
    return {
        'both': sorted(common),
        'reference_only': sorted(reference.keys() - actual.keys()),
        'vroom_only': sorted(actual.keys() - reference.keys()),
        'different_values': {name: [reference[name], actual[name]]
                             for name in sorted(common)
                             if reference[name] != actual[name]},
    }


def report(clang, gcc, vpp, target, language, flags, names, calls, headers):
    options = [f'--target={target}', '-x', language, *flags]
    queries = {}
    accepted = set()
    gcc_query = None
    for kind in ('__has_builtin', '__has_constexpr_builtin'):
        reference = query(clang, options, names, kind)
        actual = query(vpp, options, names, kind)
        queries[kind] = compare_queries(reference, actual)
        if kind == '__has_builtin':
            accepted = reference
            if gcc:
                gcc_reference = query(gcc, ['-x', language, *flags], names, kind)
                gcc_query = compare_queries(gcc_reference, actual)
    missing = (unrecognized_calls(vpp, options, sorted(accepted))
               if calls and language == 'c' else None)
    return {'target': target, 'language': language, 'flags': flags,
            'queries': queries, 'gcc_has_builtin': gcc_query,
            'unrecognized_c_calls': missing,
            'clang_header_apis': header_api(clang, options, headers)}


def default_vpp():
    if os.environ.get('VCC_BIN'):
        return Path(os.environ['VCC_BIN']) / 'vpp'
    for directory in ('build/linux/src', 'build/src'):
        path = ROOT / directory / 'vpp'
        if path.is_file():
            return path
    return ROOT / 'build/src/vpp'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default=os.environ.get('CLANG') or shutil.which('clang'))
    parser.add_argument('--vpp', default=default_vpp(), type=Path)
    parser.add_argument('--clang-include', type=Path, action='append', default=[],
                        help='directory containing Clang Basic/Builtins*.inc')
    parser.add_argument('--llvm-source', type=Path,
                        help='LLVM source checkout containing clang/include and llvm/include')
    parser.add_argument('--gcc', help='GCC executable for the selected target')
    parser.add_argument('--gcc-source', type=Path, help='GCC source checkout')
    parser.add_argument('--header', action='append', default=[],
                        help='inventory a Clang resource header for each selected profile')
    parser.add_argument('--clang-tblgen', default=os.environ.get('CLANG_TBLGEN'))
    parser.add_argument('--target', action='append', help='repeat for multiple targets')
    parser.add_argument('--all-targets', action='store_true')
    parser.add_argument('--language', action='append', choices=('c', 'c++'))
    parser.add_argument('--flag', action='append', default=[], help='pass as --flag=-mavx2')
    parser.add_argument('--no-calls', action='store_true')
    parser.add_argument('--report-only', action='store_true',
                        help='report differences without failing on extra vroom claims')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('-v', '--verbose', action='store_true')
    args = parser.parse_args()
    if not args.clang:
        parser.error('Clang not found. Supply --clang.')
    if args.all_targets and args.target:
        parser.error('Choose --all-targets or --target.')
    if any(not re.fullmatch(r'[A-Za-z0-9_./-]+\.h', header) for header in args.header):
        parser.error('--header requires a resource header name')
    if any(not path.is_dir() for path in args.clang_include):
        parser.error('--clang-include must name an existing directory.')
    names, sources = candidates(args.clang_include)
    extracted = {}
    llvm_revision = None
    if args.llvm_source:
        sibling = Path(args.clang).resolve().with_name('clang-tblgen')
        tblgen = args.clang_tblgen or (str(sibling) if sibling.is_file()
                                      else shutil.which('clang-tblgen'))
        if not tblgen:
            parser.error('clang-tblgen not found. Supply --clang-tblgen.')
        extracted = clang_candidates(args.llvm_source, tblgen)
        llvm_revision = source_revision(args.llvm_source)
        names = sorted(set(names).union(*extracted.values()))
        sources.extend(f'{args.llvm_source}/clang/include/clang/Basic/{name}'
                       for name in extracted)
    gcc_catalog = []
    gcc_target = {}
    gcc_revision = None
    if args.gcc_source:
        gcc_catalog = gcc_common_catalog(args.gcc_source, args.clang)
        gcc_revision = source_revision(args.gcc_source)
        gcc_target = gcc_target_candidates(args.gcc_source)
        names = sorted(set(names).union(*gcc_target.values()))
        sources.extend(str(args.gcc_source / 'gcc' / name) for name in gcc_target)
        sources.append(str(args.gcc_source / 'gcc/builtins.def'))
        names = sorted(set(names).union(entry['spelling'] for entry in gcc_catalog
                                       if re.fullmatch(r'[A-Za-z_]\w*', entry['spelling'] or '')))
    targets = TARGETS if args.all_targets else args.target or TARGETS[:1]
    clang_version = subprocess.check_output([args.clang, '--version'], text=True).splitlines()[0]
    gcc_version = None
    if args.gcc:
        machine = subprocess.check_output([args.gcc, '-dumpmachine'], text=True).strip()
        if tuple(targets) != (machine,):
            parser.error(f'--gcc requires selecting its target with --target {machine}')
        gcc_version = subprocess.check_output([args.gcc, '--version'], text=True).splitlines()[0]
    reports = [report(args.clang, args.gcc, str(args.vpp), target, language, args.flag, names,
                      not args.no_calls, args.header)
               for target in targets for language in args.language or ('c',)]
    if args.json:
        print(json.dumps({'clang': clang_version, 'sources': sources,
                          'candidate_count': len(names), 'candidates': names,
                          'llvm_revision': llvm_revision,
                          'clang_tables': {name: sorted(entries) for name, entries in extracted.items()},
                          'gcc': gcc_version, 'gcc_revision': gcc_revision,
                          'gcc_common': gcc_catalog, 'gcc_target_literals': gcc_target,
                          'profiles': reports}, indent=2))
    else:
        print(f'{len(names)} candidate spellings from {len(sources)} tables')
        print(clang_version)
        for name, entries in extracted.items():
            print(f'  {name}: {len(entries)} spellings')
        if gcc_version:
            print(gcc_version)
        if gcc_catalog:
            print(f'  GCC builtins.def: {len(gcc_catalog)} records')
            print(f'  GCC target registration sources: {len(set().union(*gcc_target.values()))} literals')
        for item in reports:
            print(f"{item['target']} {item['language']}")
            for kind, groups in item['queries'].items():
                print(f'  {kind}: ' + ', '.join(f'{len(group)} {label}'
                                               for label, group in groups.items()))
                if args.verbose:
                    for label in ('reference_only', 'vroom_only'):
                        print(f"    {label}: {' '.join(groups[label])}")
                    for name, values in groups['different_values'].items():
                        print(f'    {name}: Clang {values[0]}, vroom {values[1]}')
            if item['gcc_has_builtin'] is not None:
                groups = item['gcc_has_builtin']
                print('  GCC __has_builtin: ' + ', '.join(f'{len(group)} {label}'
                                                         for label, group in groups.items()))
            for header, api in item['clang_header_apis'].items():
                print(f'  {header}: ' + ', '.join(f'{len(group)} {label}'
                                                 for label, group in api.items()))
            if item['unrecognized_c_calls'] is not None:
                unknown = item['unrecognized_c_calls']
                print(f'  {len(unknown)} unrecognized C calls')
                if args.verbose:
                    print('    ' + ' '.join(unknown))
    if args.report_only:
        return 0
    return int(any(item['queries']['__has_builtin']['vroom_only'] for item in reports))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
