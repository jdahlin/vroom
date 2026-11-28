#!/usr/bin/env python3
"""cxx-debug-lines.py: -g line tables of C++ (src/test/cxx/debug-lines).

Each program is compiled by vpp and clang++ -g -c at -O0: every line marked
"// row", in the program and the headers beside it, has a line-table row in
both, llvm-dwarfdump verifies vcc's object, and the object's functions are
the ones its DW_TAG_subprogram entries name.
"""
import glob, importlib.util, os, re, subprocess, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

TESTS = f'{os.path.dirname(B)}/src/test/cxx/debug-lines'
DWARFDUMP = ct.llvm('llvm-dwarfdump')


def rows(obj):
    """(file name, line) of every line-table row"""
    out = subprocess.run([DWARFDUMP, '--debug-line', obj], capture_output=True,
                         text=True, check=True).stdout
    names, found = {}, set()
    index = None
    for line in out.splitlines():
        if m := re.match(r'file_names\[\s*(\d+)\]:', line):
            index = int(m[1])
        elif m := re.match(r'\s+name: "(.*)"', line):
            names[index] = os.path.basename(m[1])
        elif line.startswith('0x'):
            parts = line.split()
            found.add((names.get(int(parts[3])), int(parts[1])))
    return found


def undescribed(obj):
    """the functions obj defines and the ones its subprograms name, where
    they differ"""
    out = subprocess.run([DWARFDUMP, '--debug-info', obj], capture_output=True,
                         text=True, check=True).stdout
    described = set()
    for entry in re.split(r'\n(?=0x[0-9a-f]+: )', out):
        if 'DW_TAG_subprogram' in entry:
            names = dict(re.findall(r'DW_AT_(name|linkage_name)\s+\("(.*?)"\)', entry))
            described.add(names.get('linkage_name', names.get('name')))
    nm = subprocess.run([ct.llvm('llvm-nm'), '--defined-only', obj],
                        capture_output=True, text=True, check=True).stdout
    defined = {p[2] for p in map(str.split, nm.splitlines())
               if len(p) == 3 and p[1] in 'tTW' and not p[2].startswith('.')}
    return sorted(defined ^ described)


def marked():
    for path in glob.glob(f'{TESTS}/*'):
        with open(path) as f:
            for number, text in enumerate(f, 1):
                if text.rstrip().endswith('// row'):
                    yield os.path.basename(path), number


def main():
    wanted = set(marked())
    failed = 0
    sources = sorted(glob.glob(f'{TESTS}/*.cpp'))
    with tempfile.TemporaryDirectory() as tmp:
        for source in sources:
            for who, compiler in (('vcc', ct.VPP), ('clang', ct.llvm('clang++'))):
                obj = f'{tmp}/{who}.o'
                subprocess.run([compiler, '-g', '-O0', '-c', source, '-o', obj], check=True)
                if missing := wanted - rows(obj):
                    print(f'FAIL {who} {os.path.basename(source)}: no row for {sorted(missing)}')
                    failed += 1
            if odd := undescribed(f'{tmp}/vcc.o'):
                print(f'FAIL {os.path.basename(source)}: functions and subprograms differ: {odd}')
                failed += 1
            if subprocess.run([DWARFDUMP, '--verify', f'{tmp}/vcc.o'], capture_output=True).returncode:
                print(f'FAIL {os.path.basename(source)}: llvm-dwarfdump --verify')
                failed += 1
    print(f'cxx-debug-lines: {len(sources)} programs, {len(wanted)} lines, {failed} failures')
    sys.exit(bool(failed))


main()
