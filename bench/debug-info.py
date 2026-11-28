#!/usr/bin/env python3
"""debug-info.py: what -g describes (src/test/debug-info), against clang.

Each program is compiled by vpp and clang -g -c at -O0 for x86-64 and arm64
Linux.  llvm-dwarfdump verifies vcc's object, and every function's signature
(result, parameters, and the types under them, members and enumerators
included) rendered from its entries is the one rendered from clang's.
"""
import glob, importlib.util, os, re, subprocess, sys, tempfile

B = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ct', f'{B}/vcc-ctests.py')
ct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ct)

TESTS = f'{os.path.dirname(B)}/src/test/debug-info'
DWARFDUMP = ct.llvm('llvm-dwarfdump')


def entries(obj):
    """offset -> (tag, attributes, children), and the unit's entry"""
    out = subprocess.run([DWARFDUMP, '--debug-info', obj], capture_output=True,
                         text=True, check=True).stdout
    found, open_at, current = {}, [], None
    unit = None
    for line in out.splitlines():
        if m := re.match(r'(0x[0-9a-f]+):( +)(DW_TAG_\w+|NULL)', line):
            offset, depth, tag = int(m[1], 16), len(m[2]) // 2, m[3]
            if tag == 'NULL':
                continue
            current = (tag[7:], {}, [])
            found[offset] = current
            del open_at[depth:]
            if open_at:
                open_at[-1][2].append(current)
            else:
                unit = current
            open_at.append(current)
        elif current and (m := re.match(r'\s+DW_AT_(\w+)\s+\((.*)\)\s*$', line)):
            current[1][m[1]] = m[2]
    return found, unit


def render(found, entry, seen):
    tag, at, children = entry
    name = at.get('name', '""').strip('"')

    def under():
        if 'type' not in at:
            return 'void'
        return render(found, found[int(at['type'].split()[0], 16)], seen)

    def number(value):
        if value is None:
            return ''
        return str(int(value, 0))

    if tag == 'base_type':
        return f'{name}:{at["encoding"]}:{number(at.get("byte_size"))}'
    if tag in ('pointer_type', 'const_type', 'volatile_type', 'restrict_type', 'atomic_type'):
        return f'{tag[:-5]}({under()})'
    if tag == 'typedef':
        return f'typedef {name}={under()}'
    if tag == 'array_type':
        return under() + ''.join(f'[{number(c[1].get("count"))}]' for c in children)
    if tag == 'subroutine_type':
        parameters = [render(found, c, seen) for c in children]
        return f'({",".join(parameters)})->{under()}'
    if tag == 'formal_parameter':
        return under()
    if tag == 'unspecified_parameters':
        return '...'
    if tag in ('structure_type', 'union_type'):
        if name and name in seen:
            return f'{tag[:-5]} {name}'
        seen = seen | {name}
        members = ';'.join(
            f'{c[1].get("name", "")}:{render(found, c, seen)}@'
            f'{number(c[1].get("data_member_location"))}/{number(c[1].get("data_bit_offset"))}'
            f'/{number(c[1].get("bit_size"))}' for c in children
            if c[0] == 'member')  # (clang nests inner types here too)
        return f'{tag[:-5]} {name}{{{members}}}{number(at.get("byte_size"))}'
    if tag == 'member':
        return under()
    if tag == 'enumeration_type':
        values = ','.join(f'{c[1]["name"]}={number(c[1]["const_value"])}' for c in children)
        return f'enum {name}:{under()}{{{values}}}'
    return f'?{tag}'


def signatures(obj):
    found, unit = entries(obj)
    out = {}
    for entry in unit[2]:
        tag, at, children = entry
        if tag == 'subprogram' and 'low_pc' in at:
            parameters = [c[1].get('name', '') + ':' + render(found, c, frozenset())
                          for c in children
                          if c[0] in ('formal_parameter', 'unspecified_parameters')]
            result = 'void'
            if 'type' in at:
                result = render(found, found[int(at['type'].split()[0], 16)], frozenset())
            out[at['name'].strip('"')] = f'({", ".join(parameters)}) -> {result}'
    return out


def main():
    failed = 0
    sources = sorted(glob.glob(f'{TESTS}/*.c'))
    with tempfile.TemporaryDirectory() as tmp:
        for source in sources:
            for target in ('x86_64-linux-gnu', 'aarch64-linux-gnu'):
                made = {}
                for who, compiler in (('vcc', ct.VPP), ('clang', ct.llvm('clang'))):
                    obj = f'{tmp}/{who}.o'
                    subprocess.run([compiler, f'--target={target}', '-nostdinc', '-isystem', ct.RES,
                                    '-g', '-O0', '-w', '-c', source, '-o', obj], check=True)
                    made[who] = signatures(obj)
                label = f'{os.path.basename(source)} {target}'
                verify = subprocess.run([DWARFDUMP, '--verify', f'{tmp}/vcc.o'], capture_output=True)
                if verify.returncode:
                    print(f'FAIL {label}: llvm-dwarfdump --verify')
                    failed += 1
                for name in sorted(made['vcc'].keys() | made['clang'].keys()):
                    ours, theirs = made['vcc'].get(name), made['clang'].get(name)
                    if ours != theirs:
                        print(f'FAIL {label}: {name}\n  vcc   {ours}\n  clang {theirs}')
                        failed += 1
    print(f'debug-info: {len(sources)} programs, {failed} failures')
    sys.exit(bool(failed))


main()
