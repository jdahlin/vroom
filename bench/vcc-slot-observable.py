#!/usr/bin/env python3
"""Check stack-cell volatile/atomic accesses by address, including unread stores.

The small straight-line decoders track frame addresses through registers or
wasm locals. They count accesses to the actual cells, excluding ordinary spills
and saved registers. --check-mutation compiles a fixture with the unread volatile
store deleted and requires the contract to reject it at every selected level.
"""
import argparse
from collections import Counter
from pathlib import Path
import re
import subprocess
import tempfile

from hostenv import BUILD, R, has_wasm


def run(cmd):
    subprocess.run([str(x) for x in cmd], check=True, capture_output=True, text=True, timeout=30)


def body(text, name, wasm):
    match = re.search(r'^' + name + r':\s*\n', text, re.M)
    if not match:
        raise AssertionError('missing function ' + name)
    tail = text[match.end():]
    end = re.search(r'^\s*end_function\b' if wasm else r'^\s*\.size\s+' + name + r'\b', tail, re.M)
    if not end:
        raise AssertionError('missing function end ' + name)
    return tail[:end.start()]


def add(a, b):
    if isinstance(a, int) and isinstance(b, int):
        return a + b
    if isinstance(a, tuple) and isinstance(b, int):
        return (a[0], a[1] + b)
    if isinstance(b, tuple) and isinstance(a, int):
        return (b[0], b[1] + a)
    return None


def native(text, arch):
    values = {'rsp': ('frame', 0), 'sp': ('frame', 0)}
    accesses = []

    def reg(s):
        s = s.strip().lstrip('%')
        if arch == 'aarch64':
            return re.sub(r'^w(?=\d)', 'x', s)
        aliases = {'eax': 'rax', 'edi': 'rdi', 'esi': 'rsi', 'edx': 'rdx',
                   'ecx': 'rcx', 'ebx': 'rbx', 'ebp': 'rbp', 'esp': 'rsp'}
        return aliases.get(s, re.sub(r'^(r\d+)d$', r'\1', s))

    def val(s):
        s = s.strip()
        if s.startswith(('$', '#')):
            try:
                return int(s[1:], 0)
            except ValueError:
                return None
        return values.get(reg(s))

    def address(s):
        s = s.strip()
        if arch == 'x86_64':
            m = re.fullmatch(r'(-?\d*)\(%(\w+)\)', s)
            if m:
                return add(values.get(reg(m[2])), int(m[1] or '0'))
            if 'observed(%rip)' in s:
                return ('observed', 0)
        else:
            if ':lo12:observed' in s:
                return ('observed', 0)
            m = re.fullmatch(r'\[(\w+)(?:, #(-?\d+))?\]', s)
            if m:
                return add(values.get(reg(m[1])), int(m[2] or '0'))
        return None

    def access(kind, location, value=None):
        if isinstance(location, tuple):
            accesses.append((kind, location, value))

    for line in text.splitlines():
        line = line.strip()
        if not line or line.endswith(':') or line.startswith('.cfi'):
            continue
        if arch == 'x86_64':
            match = re.match(r'(?:lock\s+)?(\w+)\s+(.*)', line)
            if not match:
                continue
            op, operands = match.groups()
            fields = [x.strip() for x in operands.split(',')]
            if op == 'pushq':
                values['rsp'] = add(values.get('rsp'), -8)
            if len(fields) != 2:
                continue
            src, dst = fields
            if op == 'movl':
                access('load', address(src))
                access('store', address(dst), val(src))
            if op in ('xchgl', 'xaddl'):
                # xchg is the target's atomic-store lowering; xadd is RMW.
                access('store', address(dst), val(src))
                if op == 'xaddl':
                    access('load', address(dst))
            if not dst.startswith('%'):
                continue
            key = reg(dst)
            if op == 'leaq':
                values[key] = address(src)
            elif op.startswith('mov'):
                values[key] = val(src)
            elif op in ('addq', 'subq'):
                amount = val(src)
                values[key] = add(values.get(key), -amount if op == 'subq' and isinstance(amount, int) else amount)
            elif op not in ('cmpq', 'cmpl', 'testq', 'testl'):
                values[key] = None
        else:
            if line.startswith('.inst'):
                word = int(line.split()[1], 0)
                opcode = word & 0xfffffc00
                base = 'x' + str((word >> 5) & 31)
                # 32-bit acquire/release and load/store-exclusive records.
                if opcode in (0x88dffc00, 0x885ffc00):
                    access('load', values.get(base))
                elif opcode == 0x889ffc00 or word & 0xffe0fc00 == 0x8800fc00:
                    access('store', values.get(base))
                continue
            match = re.match(r'(\w+)\s+(.*)', line)
            if not match:
                continue
            op, operands = match.groups()
            fields = re.split(r',\s*(?![^\[]*\])', operands)
            if op in ('str', 'stur', 'ldr', 'ldur') and len(fields) == 2:
                r, addr = fields
                if r.startswith('w'):
                    access('load' if op.startswith('ld') else 'store', address(addr), val(r))
                if op.startswith('ld'):
                    values[reg(r)] = None
                continue
            if op in ('mov', 'add', 'sub') and len(fields) >= 2:
                dst = reg(fields[0])
                if op == 'mov':
                    values[dst] = val(fields[1])
                elif len(fields) == 3:
                    amount = val(fields[2])
                    if op == 'sub' and isinstance(amount, int):
                        amount = -amount
                    values[dst] = add(val(fields[1]), amount)
            elif op == 'stp' and fields[0] == 'x29' and '[sp, #-16]!' in operands:
                values['sp'] = add(values.get('sp'), -16)
            elif fields and re.fullmatch('[xw][0-9]+', fields[0]):
                values[reg(fields[0])] = None
    return accesses


def wasm(text):
    stack, locals_, accesses = [], {}, []

    def pop():
        if not stack:
            raise AssertionError('unsupported wasm stack shape')
        return stack.pop()

    for line in text.splitlines():
        fields = line.strip().split()
        if not fields:
            continue
        op = fields[0]
        arg = fields[1] if len(fields) > 1 else ''
        if op == 'global.get':
            stack.append(('frame', 0) if arg == '__stack_pointer' else None)
        elif op == 'global.set':
            pop()
        elif op in ('local.get', 'local.set', 'local.tee'):
            if op == 'local.get':
                stack.append(locals_.get(arg))
            else:
                value = pop()
                locals_[arg] = value
                if op == 'local.tee':
                    stack.append(value)
        elif op in ('i32.const', 'i64.const'):
            try:
                stack.append(int(arg, 0))
            except ValueError:
                stack.append(('observed', 0) if arg == 'observed' else None)
        elif op in ('i32.add', 'i64.add', 'i32.sub', 'i64.sub'):
            right, left = pop(), pop()
            if op.endswith('.sub'):
                right = -right if isinstance(right, int) else None
            stack.append(add(left, right))
        elif re.fullmatch(r'i(?:32|64)\.(?:load|store)', op):
            store = op.endswith('.store')
            value = pop() if store else None
            location = add(pop(), int(arg or '0', 0))
            if op.startswith('i32.') and isinstance(location, tuple):
                accesses.append(('store' if store else 'load', location, value))
            if not store:
                stack.append(None)
        elif op == 'drop':
            pop()
        elif op in ('i32.wrap_i64', 'i64.extend_i32_u', 'i64.extend_i32_s'):
            pass
        elif op in ('block', 'end_block', 'br', 'return', 'end_function') or op.startswith('.'):
            pass
        else:
            raise AssertionError('unsupported wasm operation ' + line)
    return accesses


def check(text, arch):
    decode = wasm if arch.startswith('wasm') else lambda t: native(t, arch)
    volatile = decode(body(text, 'volatile_cells', arch.startswith('wasm')))
    counts = Counter((kind, location) for kind, location, _ in volatile)
    # Identify the array from its deliberately unread second element. Then
    # inspect the first element and global result separately from compiler
    # spill slots; do not assert unrelated total instruction counts.
    candidates = [location for kind, location, value in volatile
                  if kind == 'store' and location[0] == 'frame' and value == 43]
    assert len(candidates) == 1, 'missing/duplicated unread volatile store of 43'
    second = candidates[0]
    first = ('frame', second[1] - 4)
    assert counts['store', first] == 3, 'volatile cells[0] must retain three stores'
    assert counts['load', first] == 2, 'volatile cells[0] must retain two loads'
    assert counts['store', second] == 1, 'volatile cells[1] unread store must survive'
    assert counts['store', ('observed', 0)] == 1, 'missing global volatile result store'
    assert counts['load', ('observed', 0)] == 1, 'missing global volatile result load'
    atomic = decode(body(text, 'atomic_cells', arch.startswith('wasm')))
    atomic_counts = Counter((kind, location) for kind, location, _ in atomic if location[0] == 'frame')
    # Every target implements two atomic stores, two atomic loads and one RMW.
    # Native x86 exchange-store's incidental read is deliberately not counted.
    assert any(atomic_counts['store', location] >= 3 and atomic_counts['load', location] >= 3
               for _, location in atomic_counts), 'atomic cell lost store/load/RMW memory access'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(R))
    parser.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    parser.add_argument('--target', action='append', choices=['x86_64-linux-gnu', 'aarch64-linux-gnu',
                        'wasm32-unknown-emscripten', 'wasm64-unknown-emscripten'])
    parser.add_argument('--check-mutation', action='store_true')
    args = parser.parse_args()
    source = args.source_root / 'ccs/vcc/tests/vcc/stack_cells.c'
    targets = args.target or ['x86_64-linux-gnu', 'aarch64-linux-gnu',
                             'wasm32-unknown-emscripten', 'wasm64-unknown-emscripten']
    if not args.target and not has_wasm(args.vpp):
        targets = [t for t in targets if not t.startswith('wasm')]
        print('wasm targets left out: vpp is built without WebAssembly (make VCC_WASM=1)', flush=True)
    with tempfile.TemporaryDirectory(prefix='vcc-slot-observable-') as tmp:
        directory = Path(tmp)
        mutant = directory / 'mutant.c'
        if args.check_mutation:
            text = source.read_text()
            assert text.count('cells[1] = 43;') == 1, 'missing unique mutation anchor'
            mutant.write_text(text.replace('cells[1] = 43;', '/* unread volatile store deleted */'))
        for target in targets:
            arch = target.split('-')[0]
            for level in range(4):
                assembly = directory / 'probe.s'
                command = [args.vpp, '--target=' + target, '-O' + str(level), '-ffreestanding',
                           '-fno-stack-protector', '-S']
                run(command + [source, '-o', assembly])
                check(assembly.read_text(), arch)
                if args.check_mutation:
                    run(command + [mutant, '-o', assembly])
                    try:
                        check(assembly.read_text(), arch)
                    except AssertionError as error:
                        assert str(error) == 'missing/duplicated unread volatile store of 43', error
                    else:
                        raise AssertionError('deleted unread volatile store escaped contract')
                print(f'{arch} -O{level}: observable stack-cell accesses PASS', flush=True)


if __name__ == '__main__':
    main()
