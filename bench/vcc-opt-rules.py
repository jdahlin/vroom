#!/usr/bin/env python3
"""Bounded optimization checks, at every optimization level.

  bench/vcc-opt-rules.py --target x86_64-linux-gnu --run
  bench/vcc-opt-rules.py --target aarch64-linux-gnu --run
  bench/vcc-opt-rules.py --target wasm64-unknown-emscripten --run

Only --run requires target tools: clang/lld and native execution or qemu-aarch64,
or vcc/vlink and node (Node 24+ for wasm64 table64). No libc or SDK is used.
The wasm path needs neither Emscripten nor LLVM; --node selects the JS engine.
The ordinary C regression suites also run the source files for correctness.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from hostenv import BUILD, R, llvm


def run(cmd):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=60)
    if p.returncode:
        raise RuntimeError(f'{cmd}: exit {p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout


def body(assembly, name, wasm):
    m = re.search(r'^' + re.escape(name) + r':[^\n]*\n', assembly, re.M)
    if not m:
        raise RuntimeError(f'missing probe {name}')
    tail = assembly[m.end():]
    end = (re.search(r'^\s*end_function\b', tail, re.M) if wasm else
           re.search(r'^\s*\.size\s+' + re.escape(name) + r'\s*,', tail, re.M))
    if not end:
        raise RuntimeError(f'missing function end for {name}')
    return tail[:end.start()]


def check_pow2(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    probes = ['opt_u32_div8', 'opt_u32_rem8', 'opt_u32_divhigh',
              'opt_u32_remhigh', 'opt_u32_div1', 'opt_u32_rem1',
              'opt_u64_div8', 'opt_u64_rem8', 'opt_u64_divhigh',
              'opt_u64_remhigh', 'opt_cast_div', 'opt_cast_rem',
              'opt_truncated_div', 'opt_signed_literal_div',
              'opt_compound_div', 'opt_compound_rem',
              'opt_compound_div64', 'opt_compound_rem64']
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: unsigned power-of-two retained divide/remainder\n{text}')
    # Both the optimized result and the discarded result must evaluate next().
    # The RHS comma controls additionally prevent treating a folded expression
    # as an effect-free literal and accidentally dropping its left operand.
    for name in ['effect_div', 'effect_rem', 'effect_zero',
                 'effect_denominator', 'effect_remdenominator']:
        text = body(assembly, name, wasm)
        calls = re.findall(r'\b(?:callq?|bl)\s+next\b', text)
        if len(calls) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to next\n{text}')
    # These are outside this rule. Keep a genuine divide in the dynamic
    # control; signed division may legitimately use another correct sequence.
    text = body(assembly, 'dynamic_div', wasm)
    if not re.search(divide, text):
        raise RuntimeError(f'dynamic_div: missing dynamic divide\n{text}')


def check_identities(assembly, arch):
    wasm = arch.startswith('wasm')
    # Reject the relevant arithmetic only within each named function. Address
    # and stack arithmetic may legitimately survive direct lowering.
    operations = {
        'mul': r'\b(?:i(?:32|64)\.mul|[i]?mul[lq]?)\b',
        'div': r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b',
        'or': r'\b(?:i(?:32|64)\.or|orr|or[lq]?)\b',
        'xor': r'\b(?:i(?:32|64)\.xor|eor|xor[lq]?)\b',
        'and': r'\b(?:i(?:32|64)\.and|and[lq]?)\b',
        'shift': r'\b(?:i(?:32|64)\.(?:shl|shr_[su])|[al]s[lr]|s[ah][lr][lq]?)\b',
    }
    probes = {
        'mul': ['id_mul', 'id_mulzero', 'id_umul64'],
        'div': ['id_div', 'id_remone', 'id_uremone64', 'id_sdiv64', 'id_sremone64'],
        'or': ['id_or', 'id_orones', 'id_uorones', 'id_uorones64'],
        'xor': ['id_xor'],
        'and': ['id_and', 'id_andzero', 'id_uand', 'id_uand64'],
        'shift': ['id_shl', 'id_shr'],
    }
    for operation, names in probes.items():
        for name in names:
            text = body(assembly, name, wasm)
            # x86 zeroing XOR is a valid implementation of a constant zero;
            # the XOR probe returns its argument and has no reason to zero it.
            if re.search(operations[operation], text):
                raise RuntimeError(f'{name}: retained identity {operation}\n{text}')
    for name, operation in [('id_add', 'add'), ('id_sub', 'sub')]:
        text = body(assembly, name, wasm)
        if wasm:
            redundant = re.search(r'i(?:32|64)\.const\s+0\s*\n\s*i(?:32|64)\.' + operation + r'\b', text)
        elif arch == 'x86_64':
            redundant = re.search(r'\b' + operation + r'[lq]?\s+\$0\s*,', text)
        else:
            redundant = re.search(r'\b' + operation + r'\s+[^\n]*,\s*#0\s*(?:\n|$)', text)
        if redundant:
            raise RuntimeError(f'{name}: retained zero {operation}\n{text}')
    for name in ['id_effect_mulzero', 'id_effect_andzero', 'id_effect_remone',
                 'id_effect_orones', 'id_effect_mulone', 'id_rhs_add',
                 'id_rhs_mulzero', 'id_rhs_andones']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl)\s+id_next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to id_next\n{text}')
    for name in ['id_volatile_zero', 'id_volatile_compound']:
        text = body(assembly, name, wasm)
        if wasm:
            read = re.search(r'\bi32\.load\b', text)
        elif arch == 'aarch64':
            read = re.search(r'\bldr\s', text)
        else:
            read = re.search(r'\bmov[lq]?\s+[^\n,]*\([^)]*\)[^\n,]*,\s*%', text)
        if not re.search(r'\bid_input\b', text) or not read:
            raise RuntimeError(f'{name}: dropped volatile operand read\n{text}')


def check_fallthrough(assembly, arch):
    # Inspect native control flow without requiring a full assembly snapshot.
    # Empty lines, comments and debug/CFI directives emit no instructions.
    # Every other directive and inline-asm block remains a barrier, just as an
    # opaque machine record does in the pass.
    # This extraction retains one bounded forward scan. Cascading deletions
    # may expose an earlier branch; ft_forward exercises those semantically,
    # while this simple positive probe already has an adjacent label run.
    probes = ['ft_adjacent']
    jump = re.compile(r'^\s*(?:jmpq?|b)\s+(\.L[\w.$]+)\s*(?:[#;].*)?$')
    label = re.compile(r'^(\.L[\w.$]+):\s*(?:[#;].*)?$')
    for name in probes:
        lines = []
        for line in body(assembly, name, False).splitlines():
            if re.match(r'^\s*(?:\.loc\b|\.cfi_\w+\b)', line):
                continue
            if re.match(r'^\s*(?:#|//)\s*(?:APP|NO_APP)\b', line):
                lines.append(line)
                continue
            if not line.strip() or re.match(r'^\s*(?:#|//)', line):
                continue
            lines.append(line)
        for i, line in enumerate(lines):
            target = jump.match(line)
            if not target:
                continue
            for following in lines[i + 1:]:
                destination = label.match(following)
                if not destination:
                    break
                if destination[1] == target[1]:
                    raise RuntimeError(f'{name}: jump to adjacent label {target[1]}')
    # The backwards goto remains a true control-flow edge; the pass must not
    # remove a branch merely because a matching label exists elsewhere.
    text = body(assembly, 'ft_backward', False)
    seen = set()
    for line in text.splitlines():
        destination = label.match(line)
        if destination:
            seen.add(destination[1])
        # Conditional or unconditional backwards edges are equally essential.
        branch = re.match(r'\s*(?:j\w+|b(?:\.\w+)?)\s+(\.L[\w.$]+)', line)
        if branch and branch[1] in seen:
            break
    else:
        raise RuntimeError('ft_backward: missing backward control-flow edge')


def check_loopback(assembly, arch):
    # Restrict the assertion to the first loop body: returning from the
    # function may use a separate outer-block branch after end_loop.
    for name, expected in [('loop_return', 0), ('loop_break', 0),
                           ('loop_continue', 1), ('loop_both', 0),
                           ('loop_normal', 1)]:
        text = body(assembly, name, True)
        loop = re.search(r'^\s*loop\s*$([\s\S]*?)^\s*end_loop\b', text, re.M)
        if not loop:
            raise RuntimeError(f'{name}: missing loop control')
        branches = re.findall(r'^\s*br\s+0\s*$', loop[1], re.M)
        if len(branches) != expected:
            raise RuntimeError(f'{name}: expected {expected} loop backedges, found {len(branches)}\n{loop[1]}')


def check_unreachable(assembly, arch):
    # These functions contain a dead call after a direct goto, return, break,
    # or continue. Inspect symbol references, rather than instruction counts:
    # register allocation and frame cleanup can independently change size.
    for name in ['dc_goto', 'dc_return', 'dc_break', 'dc_continue', 'dc_labels', 'dc_vla']:
        text = body(assembly, name, False)
        if re.search(r'\bdc_effect\b', text):
            raise RuntimeError(f'{name}: retained unreachable effect call\n{text}')
    for name in ['dc_goto', 'dc_return']:
        text = body(assembly, name, False)
        if re.search(r'\bdc_seen\b', text):
            raise RuntimeError(f'{name}: retained unreachable volatile store\n{text}')
    # Effects on both conditional paths and cleanup transfers must survive.
    text = body(assembly, 'dc_condition', False)
    if len(re.findall(r'\b(?:callq?|bl)\s+dc_effect\b', text)) != 2:
        raise RuntimeError(f'dc_condition: lost a reachable conditional effect\n{text}')
    text = body(assembly, 'dc_cleanup', False)
    if not re.search(r'\b(?:callq?|bl)\s+dc_clean\b', text):
        raise RuntimeError(f'dc_cleanup: lost reachable cleanup\n{text}')
    if arch == 'x86_64':
        # The a64 optimizer currently deletes this block before the shared
        # record pass. x86 directly exercises its conservative opaque barrier.
        text = body(assembly, 'dc_raw_barrier', False)
        if 'dc_opaque_barrier' not in text or not re.search(r'\bcallq?\s+dc_effect\b', text):
            raise RuntimeError(f'dc_raw_barrier: removed opaque asm or its fallthrough\n{text}')
    else:
        for name in ['dc_debugtrap', 'dc_builtin_debugtrap']:
            text = body(assembly, name, False)
            if not re.search(r'\bbrk\b', text) or not re.search(r'\bbl\s+dc_effect\b', text):
                raise RuntimeError(f'{name}: removed resumable trap or continuation\n{text}')


def execute(args, directory, assembly, arch, source, level):
    obj = directory / 'probe.o'
    exe = directory / ('probe.wasm' if arch.startswith('wasm') else 'probe')
    if arch.startswith('wasm'):
        run([args.vpp, f'--target={args.target}', f'-O{level}',
             '-ffreestanding', '-fno-stack-protector', '-c', source, '-o', obj])
        # vlink selects its wasm implementation through this executable name.
        # For wasm32 the input object's imported memory selects 32-bit layout.
        linker = directory / 'wasm-ld'
        if not linker.exists():
            linker.symlink_to(Path(shutil.which(args.vlink) or args.vlink).resolve())
        run([linker] + (['-mwasm64'] if arch == 'wasm64' else []) +
            ['--no-entry', '--export=main', obj, '-o', exe])
        script = directory / 'run.js'
        script.write_text('const fs=require("fs");\n'
                          'WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {})'
                          '.then(({instance})=>{process.exit(instance.exports.main(0,' +
                          ('0n' if arch == 'wasm64' else '0') + '));})'
                          '.catch(e=>{console.error(e);process.exit(99);});\n')
        run([args.node, *args.node_flags, script, exe])
    else:
        assembler = args.vpp if arch == 'aarch64' else args.clang
        run([assembler, f'--target={args.target}', '-c', assembly, '-o', obj])
        start = directory / 'start.s'
        start.write_text('.text\n.globl _start\n_start:\n' +
                         ('call main\nmov %eax, %edi\nmov $60, %eax\nsyscall\n'
                          if arch == 'x86_64' else
                          'bl main\nmov x8, #93\nsvc #0\n'))
        run([args.clang, f'--target={args.target}', '-fuse-ld=lld',
             '-nostdlib', '-static', '-Wl,-e,_start', start, obj, '-o', exe])
        run(([args.qemu] if arch == 'aarch64' else []) + [exe])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--target', default='x86_64-linux-gnu')
    ap.add_argument('--rule', choices=['pow2', 'identities', 'fallthrough', 'loopback', 'unreachable', 'all'], default='all')
    ap.add_argument('--run', action='store_true', help='also execute literal-answer correctness checks')
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--qemu', default='qemu-aarch64')
    args = ap.parse_args()
    arch = args.target.split('-')[0]
    if arch not in ['x86_64', 'aarch64', 'wasm32', 'wasm64']:
        ap.error('target must be x86_64, aarch64, wasm32 or wasm64')
    args.node_flags = []
    if args.run and arch == 'wasm64':
        # Older V8 requires a flag; current Node enables memory64 by default
        # and rejects the removed experimental option. Table64 needs Node 24+.
        if '--experimental-wasm-memory64' in run([args.node, '--v8-options']):
            args.node_flags.append('--experimental-wasm-memory64')
    rules = {'pow2': ('unsigned_pow2.c', check_pow2),
             'identities': ('integer_identities.c', check_identities),
             'fallthrough': ('branch_fallthrough.c', check_fallthrough),
             'loopback': ('while_backedge.c', check_loopback),
             'unreachable': ('unreachable_records.c', check_unreachable)}
    selected = rules if args.rule == 'all' else {args.rule: rules[args.rule]}
    with tempfile.TemporaryDirectory(prefix='vcc-opt-rules-') as tmp:
        directory = Path(tmp)
        for rule, (filename, check) in selected.items():
            source = Path(R) / 'ccs/vcc/tests/vcc' / filename
            for level in range(4):
                assembly = directory / f'{rule}-O{level}.s'
                run([args.vpp, f'--target={args.target}', f'-O{level}',
                     '-ffreestanding', '-fno-stack-protector', '-S', source, '-o', assembly])
                checked = ((not arch.startswith('wasm') and level >= 1) if rule in ['fallthrough', 'unreachable'] else
                           arch.startswith('wasm') if rule == 'loopback' else True)
                if checked:
                    check(assembly.read_text(), arch)
                if args.run:
                    execute(args, directory, assembly, arch, source, level)
                print(f'{args.target} {rule} O{level}: ' +
                      ('codegen' if checked else 'compile') +
                      (' and execution' if args.run else '') + ' OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
