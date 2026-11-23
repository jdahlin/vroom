#!/usr/bin/env python3
"""Compare defined Itanium symbols from selected LLVM CodeGenCXX fixtures.

The fixtures come from the LLVM checkout managed by fetch-testsuites.sh.
Both compilers build objects for AArch64 Linux. This checks symbol names
under a common target and language mode, independent of the fixtures'
LLVM IR and FileCheck assertions.

    bench/cxx-mangle.py [PATTERN] [--llvm-dir DIR] [--bin-dir DIR]

PATTERN is a substring of the fixture name. A known failure must match
its recorded diagnostic or symbol difference. An unexpected pass fails
the run so the expectation can be removed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from hostenv import BUILD, llvm

ROOT = Path(__file__).resolve().parent.parent

# Fixture stem, language version, and expected vpp diagnostic, if any.
CASES = (
    ('abi-examples', '11', 'the rules of [class.virtual] are not implemented yet'),
    ('abi-tag', '11', 'the rules of [ext.gnu] are not implemented yet: abi_tag ( "A" , "B" )'),
    ('alias-template', '11', 'the rules of [temp.deduct.call] are not implemented yet'),
    ('alignof', '11', None),
    ('exception-spec', '11', 'the rules of [dcl.fct] are not implemented yet'),
    ('extern-local', '11', None),
    ('lambdas-cxx20', '20', 'the rules of [dcl.fct.default] are not implemented yet'),
    ('local-anonymous-unions', '11', 'the rules of [dcl.pre] are not implemented yet'),
    ('local-class-names', '11', None),
    ('local-classes-nested', '11', 'the rules of [class.local] are not implemented yet'),
    ('nullptr-arg', '11', 'the rules of [temp.param] are not implemented yet'),
    ('ref-qualifiers', '11', None),
    ('subst', '11', 'the rules of [temp.deduct.decl] are not implemented yet'),
    ('subst-std', '11', 'the rules of [class.mi] are not implemented yet'),
    ('unnamed', '98', 'the rules of [class.pre] are not implemented yet'),
    ('variadic-templates', '11', 'the rules of [temp.variadic] are not implemented yet'),
)

# mangle-alignof: vpp encodes the resolved type of each dependent decltype.
ALIGNOF_DIFFERENCE = (
    ['_Z2f1IiEvDTatT_E', '_Z2f2IiEvDTu11__alignof__T_EE',
     '_Z2f3IiEvDTazcvT_Li0EE', '_Z2f4IiEvDTu11__alignof__XcvT_Li0EEEE'],
    [f'_Z2f{i}IiEvm' for i in range(1, 5)],
)


def defined_symbols(obj):
    output = subprocess.check_output(
        [llvm('llvm-nm'), '--defined-only', '--just-symbol-name', obj],
        text=True, timeout=10)
    return {name for name in output.splitlines() if name.startswith('_Z')}


def compile_source(compiler, source, standard, obj):
    return subprocess.run(
        [compiler, '--target=aarch64-linux-gnu', '-x', 'c++',
         f'-std=c++{standard}', '-w', '-O0', '-c', source, '-o', obj],
        capture_output=True, text=True, timeout=15)


def first_error(stderr):
    for line in stderr.splitlines():
        if 'error: ' in line:
            return line.split('error: ', 1)[1]
    return stderr.strip()


def check(stem, standard, expected_error, source, vpp, output):
    reference_obj = output / 'clang.o'
    ours_obj = output / 'vpp.o'
    reference = compile_source(llvm('clang'), source, standard, reference_obj)
    if reference.returncode:
        return 'FAIL', f'Clang failed: {first_error(reference.stderr)}'
    reference_symbols = defined_symbols(reference_obj)
    if not reference_symbols:
        return 'FAIL', 'Clang defined no Itanium symbols'

    ours = compile_source(vpp, source, standard, ours_obj)
    if ours.returncode:
        error = first_error(ours.stderr)
        if ours.returncode == 1 and expected_error and error == expected_error:
            return 'XFAIL', error
        return 'FAIL', error

    actual_symbols = defined_symbols(ours_obj)
    missing = sorted(reference_symbols - actual_symbols)
    extra = sorted(actual_symbols - reference_symbols)
    if missing or extra:
        if stem == 'alignof' and (missing, extra) == ALIGNOF_DIFFERENCE:
            return 'XFAIL', 'dependent alignof expressions in decltype'
        return 'FAIL', f'missing {missing}, extra {extra}'
    if expected_error or stem == 'alignof':
        return 'XPASS', 'remove the expected failure'
    return 'PASS', ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pattern', nargs='?', default='')
    parser.add_argument('--llvm-dir', type=Path, default=ROOT / 'work/testsuites/llvm')
    parser.add_argument('--bin-dir', type=Path, default=os.environ.get('VCC_BIN', BUILD))
    args = parser.parse_args()
    fixtures = args.llvm_dir / 'clang/test/CodeGenCXX'
    vpp = args.bin_dir.resolve() / 'vpp'
    if not fixtures.is_dir():
        parser.error('LLVM fixtures are missing. Run bench/fetch-testsuites.sh llvm.')
    if not vpp.is_file():
        parser.error(f'vpp is missing at {vpp}. Build it or pass --bin-dir.')
    cases = [case for case in CASES if args.pattern in f'mangle-{case[0]}']
    if not cases:
        parser.error(f'no fixture matches {args.pattern!r}')

    counts = dict.fromkeys(('PASS', 'XFAIL', 'XPASS', 'FAIL'), 0)
    with tempfile.TemporaryDirectory(prefix='vcc-mangle-') as directory:
        for stem, standard, expected_error in cases:
            source = fixtures / f'mangle-{stem}.cpp'
            try:
                status, detail = check(stem, standard, expected_error, source,
                                       vpp, Path(directory))
            except (OSError, subprocess.SubprocessError) as error:
                status, detail = 'FAIL', str(error)
            counts[status] += 1
            print(f'{status} {source.name}' + (f': {detail}' if detail else ''))
    print('cxx-mangle: ' + ', '.join(f'{count} {status}' for status, count in counts.items()))
    return int(bool(counts['FAIL'] or counts['XPASS']))


if __name__ == '__main__':
    raise SystemExit(main())
