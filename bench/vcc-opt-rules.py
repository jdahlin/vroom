#!/usr/bin/env python3
"""Bounded optimization checks, at every optimization level.

  bench/vcc-opt-rules.py --target x86_64-linux-gnu --run
  bench/vcc-opt-rules.py --target aarch64-linux-gnu --run
  bench/vcc-opt-rules.py --target wasm64-unknown-emscripten --run

Only --run requires target tools: clang/lld and native execution or qemu-aarch64,
or qemu-x86_64 on another host; wasm uses vcc/vlink and node (Node 24+
for wasm64 table64). No libc or SDK is used.
The wasm path needs neither Emscripten nor LLVM; --node selects the JS engine.
The ordinary C regression suites also run the source files for correctness.
"""
import argparse
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from hostenv import BUILD, R, llvm


def run(cmd, env=None):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=60, env=env)
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
        # Unreachable after its goto: kept whole, or removed whole as arm64 and clang do
        # (the x86 walker drops unreachable statements); never the asm without its fallthrough.
        text = body(assembly, 'dc_raw_barrier', False)
        if ('dc_opaque_barrier' in text) != bool(re.search(r'\bcallq?\s+dc_effect\b', text)):
            raise RuntimeError(f'dc_raw_barrier: removed opaque asm or its fallthrough\n{text}')
    else:
        for name in ['dc_debugtrap', 'dc_builtin_debugtrap']:
            text = body(assembly, name, False)
            if not re.search(r'\bbrk\b', text) or not re.search(r'\bbl\s+dc_effect\b', text):
                raise RuntimeError(f'{name}: removed resumable trap or continuation\n{text}')


def check_signedpow2(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    probes = ['sp_div32_8', 'sp_rem32_8', 'sp_div32_neg8', 'sp_rem32_neg8',
                 'sp_div64_16', 'sp_rem64_16', 'sp_div64_neg16', 'sp_rem64_neg16',
                 'sp_div32_min', 'sp_rem32_min', 'sp_div64_min', 'sp_rem64_min',
                 'sp_div32_neg1', 'sp_rem32_neg1', 'sp_div64_neg1', 'sp_rem64_neg1',
                 'sp_div32_2', 'sp_rem32_2', 'sp_div32_high', 'sp_rem32_high',
                 'sp_div64_2', 'sp_rem64_2', 'sp_div64_high', 'sp_rem64_high',
                 'sp_nested', 'sp_narrow',
                 'sp_volatile_div', 'sp_compound', 'sp_char', 'sp_short']
    if wasm:
        probes = ['sp_div32_min', 'sp_div64_min', 'sp_div32_neg1',
                  'sp_rem32_neg1', 'sp_div64_neg1', 'sp_rem64_neg1']
        # LLVM also keeps these compact wasm operations: the engine lowers
        # constant division. Do not expand a bias sequence into many records.
        for name, op in [('sp_div32_8', 'div_s'), ('sp_rem32_8', 'rem_s')]:
            if not re.search(r'\bi32\.' + op + r'\b', body(assembly, name, True)):
                raise RuntimeError(f'{name}: lost compact wasm {op}')
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: retained signed power-of-two divide/remainder\n{text}')
    for name in ['sp_effect_div', 'sp_effect_rem']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl)\s+sp_next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected exactly one call to sp_next\n{text}')
    for name in ['sp_volatile_div', 'sp_compound']:
        text = body(assembly, name, wasm)
        if wasm:
            read = re.search(r'\bi32\.load\b', text)
            store = re.search(r'\bi32\.store\b', text)
        elif arch == 'aarch64':
            read = re.search(r'\bldr(?:sw)?\s', text)
            store = re.search(r'\bstr\s', text)
        else:
            read = re.search(r'\bmov(?:slq|[lq])?\s+[^\n,]*\([^)]*\)[^\n,]*,\s*%', text)
            store = re.search(r'\bmov[lq]?\s+%[^\n,]*,\s*[^\n]*\(', text)
        if not read or (name == 'sp_compound' and not store):
            raise RuntimeError(f'{name}: dropped volatile access\n{text}')


def check_extensions(assembly, arch):
    if arch != 'x86_64':
        return
    for name in ['ke_load', 'ke_add', 'ke_index']:
        text = body(assembly, name, False)
        if re.search(r'\bmovl\s+%(\w+)\s*,\s*%\1\b', text):
            raise RuntimeError(f'{name}: repeated unsigned extension\n{text}')


def check_locals(assembly, arch):
    if arch != 'x86_64':
        return
    # Selected scalar parameters bind to their homes at the ABI boundary.
    # Ignore callee-save slots and unrelated FP/variadic save areas: only a
    # store of the named incoming integer register reveals the old detour.
    probes = {
        'rl_bind': ['rdi|edi|di|dil'],
        'rl_bind_narrow': ['rdi|edi|di|dil', 'rsi|esi|si|sil',
                           'rdx|edx|dx|dl', 'rcx|ecx|cx|cl'],
        'rl_bind_mixed': ['rdi|edi|di|dil', 'rsi|esi|si|sil', 'rdx|edx|dx|dl'],
        'rl_bind_sret': ['rsi|esi|si|sil'],
        'rl_bind_variadic': ['rdi|edi|di|dil'],
    }
    homes = r'(?:[re]bx|bx|bl|r1[345][dwb]?)'
    for name, registers in probes.items():
        text = body(assembly, name, False)
        for register in registers:
            if re.search(r'\bmov\w*\s+%(?:' + register +
                         r')\s*,\s*-?[0-9]*\(%rbp\)', text):
                raise RuntimeError(f'{name}: selected parameter retained ABI spill\n{text}')
            if not re.search(r'\bmov\w*\s+%(?:' + register +
                             r')\s*,\s*%' + homes + r'\b', text):
                raise RuntimeError(f'{name}: missing direct ABI register binding\n{text}')
    text = body(assembly, 'rl_bind_stack', False)
    if not re.search(r'\bmovq\s+16\(%rbp\)\s*,\s*%' + homes + r'\b', text):
        raise RuntimeError(f'rl_bind_stack: missing direct incoming stack binding\n{text}')
    text = body(assembly, 'rl_cold', False)
    if re.search(r'%(?:rbx|ebx|r1[345][dwb]?)\b', text):
        raise RuntimeError(f'rl_cold: unprofitable preserved register binding\n{text}')
    # Confirm all four clobbered homes are actually used after the opaque asm;
    # memory-only lowering would pass the runtime checks without testing the
    # promotion/asm interaction. Snapshot stores and restores do not match.
    for name in ['rl_asm_four', 'rl_asm_goto_four']:
        text = body(assembly, name, False)
        marker = re.search(r'^.*mov \$123, %rbx;.*$', text, re.M)
        if not marker:
            raise RuntimeError(f'{name}: missing actual register clobbers')
        after = text[marker.end():]
        for register in ['rbx', 'r13', 'r14', 'r15']:
            if not re.search(r'\bmovq\s+%' + register + r'\s*,\s*%', after):
                raise RuntimeError(f'{name}: {register} not live after asm\n{after}')
    # These two straight-line-return probes need only their scalar loop state.
    # A save/restore pair (or one short-lived call spill) may touch a frame slot
    # twice. Repeated accesses to the same slot expose a stack-resident loop
    # local, regardless of register numbering or prologue layout.
    for name in ['rl_loop', 'rl_calls']:
        text = body(assembly, name, False)
        accesses = re.findall(r'(-?[0-9]+)\(%rbp\)', text)
        for slot in set(accesses):
            if accesses.count(slot) > 2:
                raise RuntimeError(f'{name}: repeated loop-local frame access at {slot}(%rbp)\n{text}')
        if not re.search(r'%(?:rbx|ebx|r1[345][dwb]?)\b', text):
            raise RuntimeError(f'{name}: missing preserved register local\n{text}')


def check_copies(assembly, arch):
    text = body(assembly, 'cp_stores', False)
    # Infer the preserved homes from ABI binding, without fixing register
    # numbers or total move counts. The first store reuses pointer/value
    # operands directly; an extra copy of either home is unnecessary.
    pointer = re.search(r'\bmovq\s+%rdi\s*,\s*%(\w+)\b', text)
    value = re.search(r'\bmovq\s+%rsi\s*,\s*%(\w+)\b', text)
    if not pointer or not value:
        raise RuntimeError(f'cp_stores: missing register homes for reused parameters\n{text}')
    first_store = re.search(r'\bmovq\s+%(\w+)\s*,\s*\(%(\w+)\)', text)
    if not first_store or (first_store[1], first_store[2]) != (value[1], pointer[1]):
        raise RuntimeError(f'cp_stores: retained copies of first-store operands\n{text}')
    # The runtime probes cover implicit CL/RAX/RDX operands, atomics, partial
    # writes, calls, labels and asm; keep those assertions semantic rather
    # than prescribing their physical register allocation.


def check_sdivconst(assembly, arch, optimized=True):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_s|sdiv|idiv[lq]?)\b'
    suffixes = ['p3', 'n3', 'p7', 'n7', 'p10', 'n10', 'p37', 'n37',
                'plarge', 'nlarge', 'pmax', 'nmax']
    for width in [32, 64]:
        selected = suffixes + (['pwide', 'nwide'] if width == 64 else [])
        for operation in ['q', 'r']:
            for suffix in selected:
                name = f'sd{width}_{operation}_{suffix}'
                text = body(assembly, name, wasm)
                retained = re.search(divide, text) is not None
                # Wasm has no economical signed high-product instruction.
                if retained != (wasm or not optimized):
                    expectation = 'retained' if wasm or not optimized else 'expanded'
                    raise RuntimeError(f'{name}: expected {expectation} signed division\n{text}')
            name = f'sd{width}_dynamic_{operation}'
            if not re.search(divide, body(assembly, name, wasm)):
                raise RuntimeError(f'{name}: lost genuine runtime division')
    for name in ['sd_effect', 'sd_effect_r', 'sd_rhs', 'sd_rhs_r', 'sd_compound_rhs']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl)\s+next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')


def check_udivconst64(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i64\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    divisors = [3,5,7,10,11,14,19,37,63,4294967295,4294967297,
                9223372036854775807,9223372036854775809,18446744073709551615]
    selected = [9223372036854775809,18446744073709551615] if wasm else divisors
    probes = ['u64_q'+str(d) for d in selected]
    if not wasm:
        probes += ['u64_r'+str(d) for d in divisors]
        probes += ['oa_div10', 'u64_compound', 'u64_compoundrem']
    for name in probes:
        text = body(assembly,name,wasm)
        if re.search(divide,text):
            raise RuntimeError(f'{name}: retained unsigned64 constant division\n{text}')
    for name in ['u64_effect','u64_effectrem','u64_rhs','u64_rhsrem']:
        text = body(assembly,name,wasm)
        if len(re.findall(r'\b(?:callq?|bl)\s+next\b',text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')
    if wasm:
        for name in ['u64_q3','u64_r3']:
            if not re.search(divide,body(assembly,name,True)):
                raise RuntimeError(f'{name}: expanded unsupported wasm high product')
        if not re.search(r'i64\.ge_u\s*\n\s*i64\.extend_i32_u',body(assembly,'u64_q18446744073709551615',True)):
            raise RuntimeError('u64 high-divisor comparison must produce an i64 quotient')
    else:
        high = r'\bumulh\b' if arch=='aarch64' else r'\bmulq\b'
        if not re.search(high,body(assembly,'u64_q10',False)):
            raise RuntimeError('u64_q10: missing genuine high64 product')
    if not re.search(divide,body(assembly,'u64_dynamic',wasm)):
        raise RuntimeError('u64_dynamic: lost genuine runtime division')


def check_udivconst(assembly, arch):
    wasm = arch.startswith('wasm')
    divide = r'\b(?:i(?:32|64)\.(?:div|rem)_[su]|[su]div|idiv[lq]?|div[lq]?)\b'
    divisors = [3, 5, 6, 7, 9, 10, 11, 14, 19, 31, 37, 1000, 65535,
                65537, 2147483647, 2147483649, 4294967295]
    # Wasm deliberately retains expensive correction/remainder sequences.
    # The simple quotient recipe uses one widened multiplication and shift.
    selected = [3, 5, 6, 9, 10, 11, 1000, 65535, 65537, 2147483649, 4294967295] if wasm else divisors
    probes = ['ud_q' + str(d) for d in selected] + ['ud_truncated', 'ud_negative_literal']
    if not wasm:
        probes += ['ud_r' + str(d) for d in divisors]
        probes += ['ud_cast', 'ud_castrem', 'ud_compound', 'ud_compoundrem']
    for name in probes:
        text = body(assembly, name, wasm)
        if re.search(divide, text):
            raise RuntimeError(f'{name}: retained constant division/remainder\n{text}')
    for name in ['ud_effect', 'ud_effectrem', 'ud_rhs', 'ud_rhsrem', 'ud_compound_rhs']:
        text = body(assembly, name, wasm)
        if len(re.findall(r'\b(?:callq?|bl)\s+next\b', text)) != 1:
            raise RuntimeError(f'{name}: expected one call to next\n{text}')
    if wasm:
        for name in ['ud_q7', 'ud_r3']:
            if not re.search(divide, body(assembly, name, True)):
                raise RuntimeError(f'{name}: expanded an expensive wasm fallback')
    if arch == 'aarch64' and not re.search(divide, body(assembly, 'ud_pressure', False)):
        raise RuntimeError('ud_pressure: missing conservative register-pressure fallback')
    if not re.search(divide, body(assembly, 'ud_dynamic', wasm)):
        raise RuntimeError('ud_dynamic: lost genuine runtime division')


def check_switches(assembly, arch):
    # The old sparse chain contains equality/range tests only. A balanced
    # dispatch must split both halves, with bounded native decision paths.
    wasm = arch.startswith('wasm')
    for name in ['sw_sparse32', 'sw_ranges32', 'sw_unsigned32', 'sw_unsigned64',
                 'sw_signed64', 'sw_broad64', 'sw_cross32']:
        text = body(assembly, name, wasm)
        split = (r'\bi(?:32|64)\.lt_[su]\b' if wasm else
                 r'\bb\.(?:lt|lo)\b' if arch == 'aarch64' else r'\bj(?:l|b)\s')
        if not re.search(split, text):
            raise RuntimeError(f'{name}: missing balanced split\n{text}')
    text = body(assembly, 'sw_sparse32', wasm)
    if wasm:
        depth = maximum = 0
        for line in text.splitlines():
            if re.match(r'^\s*if\s*$', line):
                depth += 1
                maximum = max(maximum, depth)
            elif re.match(r'^\s*end_if\b', line):
                depth -= 1
        if maximum != 4:
            raise RuntimeError(f'sw_sparse32: expected four balanced split levels, got {maximum}')
        if not re.search(r'\bbr_table\b', body(assembly, 'sw_dense', True)):
            raise RuntimeError('sw_dense: lost existing wasm branch table')
        return
    # Count the maximum conditional decisions along the actual emitted CFG.
    # The probe is acyclic and contains no calls/loops; labels/directives do
    # not count as instructions. This rejects the former 31-test chain.
    instructions, labels = [], {}
    for line in text.splitlines():
        label = re.match(r'^([.\w$]+):', line)
        if label:
            labels[label[1]] = len(instructions)
        elif line.strip() and not line.lstrip().startswith(('.', '#', '//')):
            instructions.append(line.strip())
    memo, visiting = {}, set()
    def path(i):
        if i >= len(instructions):
            return 0
        if i in memo:
            return memo[i]
        if i in visiting:
            raise RuntimeError('sw_sparse32: unexpected dispatch cycle')
        visiting.add(i)
        line = instructions[i]
        op = line.split()[0]
        branch = re.match(r'(?:j\w+|b(?:\.\w+)?)\s+([.\w$]+)', line)
        if op.startswith('ret'):
            result = 0
        elif branch and branch[1] in labels:
            target = path(labels[branch[1]])
            result = target if op in ['jmp', 'jmpq', 'b'] else 1 + max(target, path(i + 1))
        else:
            result = path(i + 1)
        visiting.remove(i)
        memo[i] = result
        return result
    maximum = path(0)
    if maximum > 10:
        raise RuntimeError(f'sw_sparse32: {maximum} decisions, expected <=10 for 31 cases')


def x64_start(check_preserved):
    if not check_preserved:
        return 'call main\nmov %eax, %edi\nmov $60, %eax\nsyscall\n'
    # External assembly caller: generated main must preserve all registers used
    # by local promotion, including through nested generated callees and asm.
    registers = [('rbx', 0x12345), ('r13', 0x23456), ('r14', 0x34567), ('r15', 0x45678)]
    text = ''.join(f'mov ${value}, %{reg}\n' for reg, value in registers)
    text += 'call main\ntest %eax, %eax\njnz 1f\n'
    for reg, value in registers:
        text += f'cmp ${value}, %{reg}\njne 2f\n'
    return text + ('1: mov %eax, %edi\nmov $60, %eax\nsyscall\n'
                   '2: mov $97, %edi\nmov $60, %eax\nsyscall\n')


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
                         (x64_start(args.rule in ['locals', 'localjmp', 'all'])
                          if arch == 'x86_64' else
                          'bl main\nmov x8, #93\nsvc #0\n'))
        run([args.clang, f'--target={args.target}', '-fuse-ld=lld',
             '-nostdlib', '-static', '-Wl,-e,_start', start, obj, '-o', exe])
        runner = [args.qemu] if arch == 'aarch64' else []
        if arch == 'x86_64' and (args.qemu_x86 or platform.machine() != 'x86_64'):
            runner = [args.qemu_x86 or 'qemu-x86_64']
        run(runner + [exe])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--vpp', default=str(Path(BUILD) / 'vpp'))
    ap.add_argument('--target', default='x86_64-linux-gnu')
    ap.add_argument('--rule', choices=['pow2', 'signedpow2', 'identities', 'fallthrough', 'loopback', 'unreachable', 'locals', 'localjmp', 'copies', 'extensions', 'switches', 'udivconst', 'udivconst64', 'sdivconst', 'all'], default='all')
    ap.add_argument('--run', action='store_true', help='also execute literal-answer correctness checks')
    ap.add_argument('--clang', default=llvm('clang'))
    ap.add_argument('--vlink', default=str(Path(BUILD) / 'vlink'))
    ap.add_argument('--node', default='node')
    ap.add_argument('--qemu', default='qemu-aarch64')
    ap.add_argument('--qemu-x86', help='force x86 emulator; auto qemu-x86_64 on non-x86 hosts')
    args = ap.parse_args()
    arch = args.target.split('-')[0]
    if arch not in ['x86_64', 'aarch64', 'wasm32', 'wasm64']:
        ap.error('target must be x86_64, aarch64, wasm32 or wasm64')
    if args.rule == 'localjmp' and arch != 'x86_64':
        ap.error('localjmp tests x86 callee-save preservation')
    args.node_flags = []
    if args.run and arch == 'wasm64':
        # Older V8 requires a flag; current Node enables memory64 by default
        # and rejects the removed experimental option. Table64 needs Node 24+.
        if '--experimental-wasm-memory64' in run([args.node, '--v8-options']):
            args.node_flags.append('--experimental-wasm-memory64')
    rules = {'pow2': ('unsigned_pow2.c', check_pow2),
             'signedpow2': ('signed_pow2.c', check_signedpow2),
             'identities': ('integer_identities.c', check_identities),
             'fallthrough': ('branch_fallthrough.c', check_fallthrough),
             'loopback': ('while_backedge.c', check_loopback),
             'unreachable': ('unreachable_records.c', check_unreachable),
             'locals': ('register_locals.c', check_locals),
             'localjmp': ('register_locals_setjmp.c', None),
             'copies': ('record_copies.c', check_copies),
             'extensions': ('known_extensions.c', check_extensions),
             'switches': ('switch_dispatch.c', check_switches),
             'udivconst': ('unsigned_divconst.c', check_udivconst),
             'udivconst64': ('unsigned_divconst64.c', check_udivconst64),
             'sdivconst': ('signed_divconst.c', check_sdivconst)}
    selected = rules if args.rule == 'all' else {args.rule: rules[args.rule]}
    with tempfile.TemporaryDirectory(prefix='vcc-opt-rules-') as tmp:
        directory = Path(tmp)
        for rule, (filename, check) in selected.items():
            if rule == 'localjmp' and arch != 'x86_64':
                continue
            source = Path(R) / 'ccs/vcc/tests/vcc' / filename
            for level in range(4):
                assembly = directory / f'{rule}-O{level}.s'
                run([args.vpp, f'--target={args.target}', f'-O{level}',
                     '-ffreestanding', '-fno-stack-protector', '-S', source, '-o', assembly])
                checked = ((arch == 'x86_64' and level >= 1) if rule in ['locals', 'copies', 'extensions'] else
                           (not arch.startswith('wasm') and level >= 1) if rule in ['fallthrough', 'unreachable'] else
                           arch.startswith('wasm') if rule == 'loopback' else True)
                checked = checked and check is not None and (rule not in ['switches', 'udivconst', 'udivconst64'] or level >= 1)
                if checked:
                    if rule == 'sdivconst':
                        check(assembly.read_text(), arch, optimized=level >= 1)
                    else:
                        check(assembly.read_text(), arch)
                if args.run:
                    execute(args, directory, assembly, arch, source, level)
                print(f'{args.target} {rule} O{level}: ' +
                      ('codegen' if checked else 'compile') +
                      (' and execution' if args.run else '') + ' OK')
            if rule == 'sdivconst':
                # Check the debugging escape hatch independently of O0.
                assembly = directory / 'sdivconst-noopt.s'
                run([args.vpp, f'--target={args.target}', '-O2',
                     '-ffreestanding', '-fno-stack-protector', '-S', source, '-o', assembly],
                    env={**os.environ, 'VCC_NOOPT': '1'})
                check_sdivconst(assembly.read_text(), arch, optimized=False)
                print(f'{args.target} sdivconst O2 VCC_NOOPT: codegen OK')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as e:
        print(e, file=sys.stderr)
        sys.exit(1)
