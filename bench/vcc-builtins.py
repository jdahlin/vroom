#!/usr/bin/env python3
"""Compare builtin feature queries and report unrecognized calls in C.

The candidate inventory is the union of vroom's checked-in reference query
tables and Clang builtin tables if supplied or read by vroom.
Query results are
observations for the selected configuration. The C call probe detects
undeclared names, and provides no evidence of correct semantic checking.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CLANG_VENDOR = ROOT / 'contrib/clang-builtins'
TARGETS = (
    'aarch64-linux-gnu', 'arm64-apple-darwin',
    'x86_64-linux-gnu', 'x86_64-w64-mingw32',
)
SNAPSHOT_ROW = re.compile(
    r'(?:\{\s*"__has_(?:constexpr_)?builtin",\s*|'
    r'FEATURE_CHECK\((?:constexpr_)?builtin,\s*)"([A-Za-z_]\w*)"'
)
ENUMERATOR = re.compile(r'^\s*BI([A-Za-z_]\w*),\s*$', re.MULTILINE)

CLANG_TABLES = (
    'Builtins.td', 'BuiltinsX86.td', 'BuiltinsX86_64.td', 'BuiltinsAArch64.td',
    'arm_neon.td', 'arm_fp16.td', 'arm_sve.td', 'arm_sme.td',
)


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



def clang_candidates(source, reader, tables=CLANG_TABLES):
    include = source / 'clang/include'
    extracted = {}
    for filename in tables:
        entry = include / 'clang/Basic' / filename
        proc = run([str(reader.resolve()), str(include), str(entry)], '')
        if proc.returncode:
            raise RuntimeError(f'{entry}: clang-builtin-reader failed\n{proc.stderr}')
        spellings = proc.stdout.splitlines()
        if (not spellings or spellings != sorted(set(spellings)) or
                any(not re.fullmatch(r'[A-Za-z_]\w*', name) for name in spellings)):
            raise RuntimeError(f'{entry}: clang-builtin-reader produced invalid spellings')
        extracted[filename] = set(spellings)
    return extracted


def clang_metadata(source, reader, tables):
    include = source / 'clang/include'
    metadata = {}
    for table, spellings in tables.items():
        entry = include / 'clang/Basic' / table
        proc = run([str(reader.resolve()), '--metadata', str(include), str(entry)], '')
        if proc.returncode:
            raise RuntimeError(f'{entry}: clang-builtin-reader metadata failed\n{proc.stderr}')
        rows = [json.loads(line) for line in proc.stdout.splitlines()]
        if {row['spelling'] for row in rows} != spellings:
            raise RuntimeError(f'{entry}: metadata and spelling inventories differ')
        for row in rows:
            metadata.setdefault(row['spelling'], []).append({'table': table, **row})
    return metadata


def check_clang_reader(reader):
    reference = {}
    for row in (CLANG_VENDOR / 'spellings.sha256').read_text().splitlines():
        filename, count, digest = row.split()
        reference[filename] = (int(count), digest)
    if reference.keys() != set(CLANG_TABLES):
        raise RuntimeError('Clang spelling reference must cover all eight input tables')
    for filename, spellings in clang_candidates(CLANG_VENDOR, reader).items():
        payload = ''.join(f'{name}\n' for name in sorted(spellings)).encode()
        actual = len(spellings), hashlib.sha256(payload).hexdigest()
        expected = reference[filename]
        if actual != expected:
            raise RuntimeError(f'{filename}: expected {expected[0]} spellings with SHA-256 '
                               f'{expected[1]}, got {actual[0]} with SHA-256 {actual[1]}')
        print(f'{filename}: {actual[0]} spellings match the pinned reference')


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


def probe_c_names(vpp, options, names):
    unknown, stubs = set(), set()
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
            stubs.update(set(chunk).intersection(re.findall(
                r"builtin '([^']+)' is not implemented", proc.stderr)))
    return sorted(unknown), sorted(stubs)


def compare_queries(reference, actual, retain_values=False):
    common = reference.keys() & actual.keys()
    result = {
        'both': sorted(common),
        'reference_only': sorted(reference.keys() - actual.keys()),
        'vroom_only': sorted(actual.keys() - reference.keys()),
        'different_values': {name: [reference[name], actual[name]]
                             for name in sorted(common)
                             if reference[name] != actual[name]},
    }
    if retain_values:
        result['non_boolean_values'] = {
            name: [reference.get(name, 0), actual.get(name, 0)]
            for name in sorted(reference.keys() | actual.keys())
            if reference.get(name, 0) > 1 or actual.get(name, 0) > 1
        }
    return result


def report(clang, gcc, vpp, target, language, flags, names, calls, headers, tables, retain_values=False):
    options = [f'--target={target}', '-x', language, *flags]
    queries = {}
    accepted = set()
    gcc_query = None
    for kind in ('__has_builtin', '__has_constexpr_builtin'):
        reference = query(clang, options, names, kind)
        actual = query(vpp, options, names, kind)
        queries[kind] = compare_queries(reference, actual, retain_values)
        if kind == '__has_builtin':
            accepted = reference
            if gcc:
                gcc_reference = query(gcc, ['-x', language, *flags], names, kind)
                gcc_query = compare_queries(gcc_reference, actual, retain_values)
    missing, stubs = (probe_c_names(vpp, options, sorted(accepted))
                      if calls and language == 'c' else (None, None))
    claims = queries['__has_builtin']
    both = set(claims['both'])
    reference_only = set(claims['reference_only'])
    vroom_only = set(claims['vroom_only'])
    unrecognized = set(missing or [])
    by_table = {}
    for filename, spellings in tables.items():
        by_table[filename] = {
            'spellings': len(spellings),
            'both': len(spellings & both),
            'reference_only': len(spellings & reference_only),
            'vroom_only': len(spellings & vroom_only),
            'neither': len(spellings - both - reference_only - vroom_only),
            'unrecognized_c_calls': (len(spellings & unrecognized)
                                     if missing is not None else None),
        }
    return {'target': target, 'language': language, 'flags': flags,
            'queries': queries, 'gcc_has_builtin': gcc_query,
            'clang_tables': by_table,
            'unrecognized_c_calls': missing,
            'stub_c_calls': stubs,
            'clang_header_apis': header_api(clang, options, headers)}


def query_observations(groups):
    """Expand a pair of sparse feature-query reports without inventing support."""
    observations = {name: [1, 1] for name in groups['both']}
    observations.update((name, [1, 0]) for name in groups['reference_only'])
    observations.update((name, [0, 1]) for name in groups['vroom_only'])
    observations.update(groups['non_boolean_values'])
    return observations


def coverage_ledger(names, tables, gcc_catalog, gcc_target, reports, samples, metadata):
    memberships = {name: [] for name in names}
    for table, spellings in tables.items():
        for name in spellings:
            memberships[name].append(table)
    gcc_common = {}
    for row in gcc_catalog:
        gcc_common.setdefault(row['spelling'], []).append(row['id'])
    gcc_files = {}
    for path, spellings in gcc_target.items():
        for name in spellings:
            gcc_files.setdefault(name, []).append(path)
    probes = []
    for report in reports:
        profile = {key: report[key] for key in ('target', 'language', 'flags')}
        queries = {key: query_observations(value)
                   for key, value in report['queries'].items()}
        if report['gcc_has_builtin'] is not None:
            queries['gcc_vs_vroom_has_builtin'] = query_observations(report['gcc_has_builtin'])
        unknown = report['unrecognized_c_calls']
        probes.append((profile, queries, None if unknown is None else set(unknown),
                       set(report['stub_c_calls'] or [])))
    typed = {}
    for sample in samples:
        typed.setdefault(sample['spelling'], []).append(sample)
    for name in names:
        observations = []
        for profile, queries, unknown, stubs in probes:
            item = dict(profile)
            for kind, values in queries.items():
                item[kind] = values.get(name, [0, 0])
            clang_claim = item['__has_builtin'][0]
            item['c_name_probe'] = (None if unknown is None or not clang_claim
                                    else 'stub' if name in stubs
                                    else 'undeclared' if name in unknown
                                    else 'no_undeclared_diagnostic')
            observations.append(item)
        yield {
            'spelling': name,
            'clang_tables': memberships[name],
            'gcc_common_ids': gcc_common.get(name, []),
            'gcc_target_sources': gcc_files.get(name, []),
            'profiles': observations,
            'reference_records': metadata.get(name, []),
            'typed_compile': typed.get(name, []),
        }


# Each row is a valid C call on the selected profile. SVE and SME use
# the ACLE declarations, which supply the public signatures for these names.
X64 = 'x86_64-linux-gnu'
A64 = 'aarch64-linux-gnu'
SAMPLE_CALLS = (
    ('Builtins.td', '__builtin_popcount', X64, (), '',
     'int f(unsigned x) { return __builtin_popcount(x); }'),
    ('Builtins.td', '__builtin_popcountl', X64, (), '',
     'int f(unsigned long x) { return __builtin_popcountl(x); }'),
    ('BuiltinsX86.td', '__builtin_ia32_crc32qi', X64, ('-msse4.2',), '',
     'unsigned f(unsigned a, unsigned char b) { return __builtin_ia32_crc32qi(a, b); }'),
    ('BuiltinsX86.td', '__builtin_ia32_crc32si', X64, ('-msse4.2',), '',
     'unsigned f(unsigned a, unsigned b) { return __builtin_ia32_crc32si(a, b); }'),
    ('BuiltinsX86_64.td', '__builtin_ia32_crc32di', X64, ('-msse4.2',), '',
     'unsigned long long f(unsigned long long a, unsigned long long b) '
     '{ return __builtin_ia32_crc32di(a, b); }'),
    ('BuiltinsX86_64.td', '__builtin_ia32_addcarryx_u64', X64, ('-madx',), '',
     'unsigned char f(unsigned char c, unsigned long long a, unsigned long long b, '
     'unsigned long long *out) { return __builtin_ia32_addcarryx_u64(c, a, b, out); }'),
    ('BuiltinsAArch64.td', '__builtin_arm_rbit', A64, (), '',
     'unsigned f(unsigned x) { return __builtin_arm_rbit(x); }'),
    ('BuiltinsAArch64.td', '__builtin_arm_clz64', A64, (), '',
     'unsigned f(unsigned long long x) { return __builtin_arm_clz64(x); }'),
    ('arm_neon.td', '__builtin_neon_vcntq_v', A64, (), 'arm_neon.h',
     'uint8x16_t f(uint8x16_t x) { return vcntq_u8(x); }'),
    ('arm_neon.td', '__builtin_neon_vminq_v', A64, (), 'arm_neon.h',
     'uint8x16_t f(uint8x16_t a, uint8x16_t b) { return vminq_u8(a, b); }'),
    ('arm_fp16.td', '__builtin_neon_vaddh_f16', A64, ('-march=armv8.2-a+fp16',), '',
     '__fp16 f(__fp16 a, __fp16 b) { return __builtin_neon_vaddh_f16(a, b); }'),
    ('arm_fp16.td', '__builtin_neon_vsubh_f16', A64, ('-march=armv8.2-a+fp16',), '',
     '__fp16 f(__fp16 a, __fp16 b) { return __builtin_neon_vsubh_f16(a, b); }'),
    ('arm_sve.td', '__builtin_sve_svcntb', A64, ('-march=armv8-a+sve',), 'arm_sve.h',
     'unsigned long long f(void) { return svcntb(); }'),
    ('arm_sve.td', '__builtin_sve_svcntd', A64, ('-march=armv8-a+sve',), 'arm_sve.h',
     'unsigned long long f(void) { return svcntd(); }'),
    ('arm_sme.td', '__builtin_sme_svcntsb', A64, ('-march=armv9-a+sme',), 'arm_sme.h',
     'unsigned long long f(void) { return svcntsb(); }'),
    ('arm_sme.td', '__builtin_sme_svcntsd', A64, ('-march=armv9-a+sme',), 'arm_sme.h',
     'unsigned long long f(void) { return svcntsd(); }'),
)


def sample_calls(clang, vpp, tables):
    results = []
    resource = subprocess.check_output([clang, '-print-resource-dir'], text=True).strip()
    vroom_includes = ['-isystem', str(ROOT / 'src/headers'),
                      '-isystem', str(Path(resource) / 'include')]
    for table, name, target, flags, header, body in SAMPLE_CALLS:
        if name not in tables.get(table, ()):
            raise RuntimeError(f'{table}: sample spelling {name} is missing')
        options = [f'--target={target}', *flags, '-ffreestanding', '-x', 'c',
                   '-Werror=implicit-function-declaration']
        source = (f'#include <{header}>\n' if header else '') + body + '\n'
        for level in range(4):
            compile_options = [*options, f'-O{level}', '-c', '-', '-o', os.devnull]
            reference = run([clang, *compile_options], source)
            if reference.returncode:
                raise RuntimeError(f'{table}: Clang rejected sample {name} at -O{level}\n'
                                   f'{reference.stderr}')
            actual = run([str(vpp), *vroom_includes, *compile_options], source)
            if actual.returncode not in (0, 1):
                raise RuntimeError(f'{table}: vpp failed on sample {name} at -O{level}\n'
                                   f'{actual.stderr}')
            results.append({'table': table, 'spelling': name, 'target': target,
                            'entry': body.split('return ', 1)[1].split('(', 1)[0],
                            'header': header or None, 'flags': list(flags),
                            'language': 'c', 'optimization': f'O{level}',
                            'object_emitted': actual.returncode == 0,
                            'diagnostic': actual.stderr.strip()})
    return results

def check_integer_arity(clang, vpp):
    generator = ROOT / 'src/tools/gen-builtin-tests.py'
    registry = vpp.resolve().parent / 'generated/include/frontend/integer-builtins.inc'
    source = subprocess.check_output([sys.executable, str(generator), '--invalid-arity',
                                     '--registry', str(registry)],
                                     text=True)
    expected = set(range(1, len(source.splitlines()) + 1))
    with tempfile.TemporaryDirectory() as directory:
        for language, extension in (('c', 'c'), ('c++', 'cpp')):
            path = Path(directory) / f'invalid.{extension}'
            path.write_text(source)
            for compiler in (clang, str(vpp)):
                proc = run([compiler, f'--target={A64}', '-fsyntax-only',
                            '-ferror-limit=0', str(path)], '')
                diagnostics = set(map(int, re.findall(
                    rf'{re.escape(str(path))}:(\d+):\d+: error:', proc.stderr)))
                if proc.returncode != 1 or diagnostics != expected:
                    missing = sorted(expected - diagnostics)
                    extra = sorted(diagnostics - expected)
                    raise RuntimeError(f'{compiler}: {language} arity check exit '
                                       f'{proc.returncode}, missed lines {missing}, '
                                       f'unexpected lines {extra}\n{proc.stderr}')
            print(f'{language}: both compilers rejected all {len(expected)} wrong-arity calls')
    return 0


def check_stubs(vpp):
    cases = (
        (A64, '__builtin_neon_vabdh_f16', 'is not implemented'),
        (A64, '__builtin_sve_svcntb', 'is not implemented'),
        (A64, '__builtin_sme_svcntsb', 'is not implemented'),
        (X64, '__builtin_ia32_addcarryx_u32', 'is not implemented'),
        (X64, '__builtin_sve_svcntb', 'undeclared function'),
        (A64, '__builtin_vroom_nonexistent', 'undeclared function'),
    )
    for target, name, diagnostic in cases:
        source = f'void test(void) {{ {name}(); }}\n'
        proc = run([str(vpp), f'--target={target}', '-x', 'c', '-fsyntax-only', '-'], source)
        if proc.returncode != 1 or diagnostic not in proc.stderr or name not in proc.stderr:
            raise RuntimeError(f'{target}: unexpected diagnostic for {name}\n{proc.stderr}')
    print(f'{len(cases)} generated builtin stub diagnostics checked')
    return 0


def default_tool(name):
    if os.environ.get('VCC_BIN'):
        return Path(os.environ['VCC_BIN']) / name
    for directory in ('build/linux/src', 'build/src'):
        path = ROOT / directory / name
        if path.is_file():
            return path
    return ROOT / 'build/src' / name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default=os.environ.get('CLANG') or shutil.which('clang'))
    parser.add_argument('--vpp', default=default_tool('vpp'), type=Path)
    parser.add_argument('--clang-include', type=Path, action='append', default=[],
                        help='directory containing Clang Basic/Builtins*.inc')
    clang_sources = parser.add_mutually_exclusive_group()
    clang_sources.add_argument('--llvm-source', type=Path,
                               help='LLVM source checkout containing clang/include and llvm/include')
    clang_sources.add_argument('--clang-vendored', action='store_true',
                               help='extract candidates from the pinned Clang TableGen import')
    clang_sources.add_argument('--clang-common-reader', type=Path,
                               help='read the pinned common Clang table with clang-builtin-reader')
    parser.add_argument('--gcc', help='GCC executable for the selected target')
    parser.add_argument('--gcc-source', type=Path, help='GCC source checkout')
    parser.add_argument('--header', action='append', default=[],
                        help='inventory a Clang resource header for each selected profile')
    parser.add_argument('--clang-reader', type=Path, default=default_tool('clang-builtin-reader'),
                        help='path to the vroom TableGen reader')
    clang_sources.add_argument('--check-clang-reader', action='store_true',
                               help='verify all vendored tables against the pinned spelling sets')
    parser.add_argument('--target', action='append', help='repeat for multiple targets')
    parser.add_argument('--all-targets', action='store_true')
    parser.add_argument('--language', action='append', choices=('c', 'c++'))
    parser.add_argument('--flag', action='append', default=[], help='pass as --flag=-mavx2')
    parser.add_argument('--no-calls', action='store_true')
    parser.add_argument('--check-stubs', action='store_true',
                        help='check generated C builtin stub diagnostics')
    parser.add_argument('--check-integer-arity', action='store_true',
                        help='check C and C++ rejection of wrong-arity fixed integer calls')
    parser.add_argument('--sample-calls', action='store_true',
                        help='compile two typed calls per vendored table through both compilers')
    parser.add_argument('--report-only', action='store_true',
                        help='report differences without failing on extra vroom claims')
    output = parser.add_mutually_exclusive_group()
    output.add_argument('--json', action='store_true')
    output.add_argument('--coverage-ledger', action='store_true',
                        help='emit JSON Lines with evidence for each inventoried spelling')
    parser.add_argument('--spelling', action='append', default=[],
                        help='limit the ledger to these exact spellings (repeatable)')
    parser.add_argument('-v', '--verbose', action='store_true')
    args = parser.parse_args()
    if args.spelling and not args.coverage_ledger:
        parser.error('--spelling requires --coverage-ledger.')
    if args.coverage_ledger and (args.sample_calls or args.check_clang_reader):
        parser.error('--coverage-ledger requires an inventory run.')
    if args.sample_calls and not args.clang_vendored:
        parser.error('--sample-calls requires --clang-vendored.')
    if args.check_clang_reader:
        check_clang_reader(args.clang_reader)
        return 0
    if args.check_stubs:
        return check_stubs(args.vpp)
    if not args.clang:
        parser.error('Clang not found. Supply --clang.')
    if args.check_integer_arity:
        return check_integer_arity(args.clang, args.vpp)
    if args.all_targets and args.target:
        parser.error('Choose --all-targets or --target.')
    if any(not re.fullmatch(r'[A-Za-z0-9_./-]+\.h', header) for header in args.header):
        parser.error('--header requires a resource header name')
    if any(not path.is_dir() for path in args.clang_include):
        parser.error('--clang-include must name an existing directory.')
    names, sources = candidates(args.clang_include)
    extracted = {}
    llvm_revision = None
    vendored = args.clang_vendored or args.clang_common_reader
    llvm_source = CLANG_VENDOR if vendored else args.llvm_source
    if llvm_source:
        reader = args.clang_common_reader or args.clang_reader
        if not reader.is_file():
            parser.error('Build clang-builtin-reader with make -C src clang-builtin-reader '
                         'or supply its path with --clang-reader.')
        tables = ('Builtins.td',) if args.clang_common_reader else CLANG_TABLES
        extracted = clang_candidates(llvm_source, reader, tables)
        llvm_revision = ((CLANG_VENDOR / 'REVISION').read_text().strip()
                         if vendored else source_revision(llvm_source))
        names = sorted(set(names).union(*extracted.values()))
        source_label = CLANG_VENDOR.relative_to(ROOT) if vendored else llvm_source
        sources.extend(str(source_label / 'clang/include/clang/Basic' / name)
                       for name in extracted)
    if args.sample_calls:
        samples = sample_calls(args.clang, args.vpp, extracted)
        if args.json:
            print(json.dumps(samples, indent=2))
        else:
            for offset in range(0, len(samples), 4):
                levels = samples[offset:offset + 4]
                sample = levels[0]
                tag = f"{sample['table']} {sample['entry']}"
                if all(item['object_emitted'] for item in levels):
                    print(f'{tag}: object emitted at -O0 through -O3')
                    continue
                for item in levels:
                    if not item['object_emitted']:
                        print(f"{tag} -{item['optimization']}: failed")
                        lines = item['diagnostic'].splitlines()
                        if lines:
                            print(next((line for line in lines if 'error:' in line), lines[0]))
        return int(not args.report_only and any(not item['object_emitted'] for item in samples))
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
    if args.spelling:
        unknown = set(args.spelling) - set(names)
        if unknown:
            parser.error(f'Unknown inventory spellings: {", ".join(sorted(unknown))}')
    reports = [report(args.clang, args.gcc, str(args.vpp), target, language, args.flag, names,
                      not args.no_calls, args.header, extracted, args.coverage_ledger)
               for target in targets for language in args.language or ('c',)]
    if args.coverage_ledger:
        samples = sample_calls(args.clang, args.vpp, extracted) if args.clang_vendored else []
        metadata = clang_metadata(llvm_source, reader, extracted) if llvm_source else {}
        ledger = coverage_ledger(names, extracted, gcc_catalog, gcc_target, reports,
                                 samples, metadata)
        selected = set(args.spelling)
        print(json.dumps({
            'schema': 'vroom-builtin-coverage-v1', 'clang': clang_version,
            'llvm_revision': llvm_revision, 'gcc': gcc_version,
            'gcc_revision': gcc_revision, 'vpp': str(args.vpp.resolve()),
            'sources': sources, 'query_compiler_order': ['reference', 'vroom'],
        }))
        for item in ledger:
            if not selected or item['spelling'] in selected:
                print(json.dumps(item, separators=(',', ':')))
        return 0
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
            for name, coverage in item['clang_tables'].items():
                print(f"  {name}: {coverage['both']} both, "
                      f"{coverage['reference_only']} reference_only, "
                      f"{coverage['vroom_only']} vroom_only, {coverage['neither']} neither")
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
                print(f"  {len(item['stub_c_calls'])} C calls diagnosed as stubs")
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
